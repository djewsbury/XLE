// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "XPak.h"
#include "XPak_Internal.h"
#include "IFileSystem.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/HeapUtils.h"
#include "../Foreign/FastLZ/fastlz.h"

namespace Assets
{

	namespace ArchiveUtility
	{
		class ArchiveDanglingFileMonitor
		{
		public:
			#if defined(_DEBUG)
				Threading::Mutex _closingProtectionLock;
				int _openFileCount = 0;
				bool _closingArchive = false;
			#endif

			ArchiveDanglingFileMonitor();
			~ArchiveDanglingFileMonitor();
		};

		ArchiveDanglingFileMonitor::ArchiveDanglingFileMonitor() = default;
		ArchiveDanglingFileMonitor::~ArchiveDanglingFileMonitor()
		{
			#if defined(_DEBUG)
				{
					ScopedLock(_closingProtectionLock);
					assert(!_closingArchive);
					assert(_openFileCount == 0);		// if you hit this is means that there are files opened from this archive that haven't been closed. This is dangerous because any reads on those files will crash from this point on 
					_closingArchive = true;
				}
			#endif
		}

		class FileCache
		{
		public:
			struct File
			{
				IteratorRange<void*> _data;
				std::atomic<bool> _internalUtility = false;
			};

			using InitializationFn = void(IteratorRange<void*>, IteratorRange<const void*>);
			std::shared_ptr<File> Reserve(uint64_t resourceGuid, size_t size, InitializationFn* initFn = nullptr, IteratorRange<const void*> usrData = {})
			{
				std::unique_lock<decltype(_lock)> lk(_lock);
				assert(size < 0xffffffffull);
				assert(size > 0);

				// If we've already got this file, return it as is
				for (auto f=_files.begin(); f!=_files.end(); ++f)
					if (f->_resourceGuid == resourceGuid && f->_clientFile) {
						FileEntry t { std::move(*f) };
						_files.erase(f);
						_files.push_back(std::move(t));
						auto res = _files.back()._clientFile;
						lk = {};

						// wait for initialization completion outside of the lock
						while (!res->_internalUtility.load())
							std::this_thread::sleep_for(std::chrono::milliseconds(10));
						return res;
					}

				IteratorRange<void*> foundSpace;
				unsigned foundPageId = ~0u;
				// look for space in an existing page we can use
				for (auto& p:_pages) {
					auto a = p._spanningHeap.Allocate((unsigned)size);
					if (a != ~0u) {
						foundSpace = { PtrAdd(p._data.get(), a), PtrAdd(p._data.get(), a+size) };
						foundPageId = p._id;
						break;
					}
				}

				if (foundPageId == ~0u)
					std::tie(foundSpace, foundPageId) = FreeUpSpaceFor(size);

				if (foundPageId == ~0u) {
					auto pageSize = std::max(size, _defaultPageSize);
					Page newPage;
					newPage._data = std::make_unique<uint8_t[]>(pageSize);
					foundPageId = newPage._id = _nextPageId++;
					newPage._spanningHeap = SpanningHeap<uint32_t>((unsigned)pageSize);
					newPage._pageSize = pageSize;

					auto a = newPage._spanningHeap.Allocate((unsigned)size);
					foundSpace = { PtrAdd(newPage._data.get(), a), PtrAdd(newPage._data.get(), a+size) };

					_pages.emplace_back(std::move(newPage));
					_currentAllocatedInPages += pageSize;
				}

				FileEntry e;
				e._clientFile = std::make_shared<File>();
				e._clientFile->_data = foundSpace;
				e._clientFile->_internalUtility.store(false);
				e._pageId = foundPageId;
				e._resourceGuid = resourceGuid;
				auto res = e._clientFile;
				_files.push_back(std::move(e));
				lk = {}; // unlock

				// Run the initialization operation outside of the main loop, with just a simple race condition protection scheme
				if (initFn) (*initFn)(res->_data, usrData);
				res->_internalUtility.store(true);
				return res;
			}

			FileCache::FileCache(size_t maxCachedBytes)
			: _maxCachedBytes(maxCachedBytes), _defaultPageSize(1024*1024) 
			, _nextPageId(1)
			{
				_files.reserve(32);
				_currentAllocatedInPages = 0;
			}

			FileCache::~FileCache() {}

		private:
			Threading::Mutex _lock;
			struct FileEntry
			{
				std::shared_ptr<File> _clientFile;
				unsigned _pageId = ~0u;
				uint64_t _resourceGuid = ~0ull;
			};
			std::vector<FileEntry> _files;

			size_t _maxCachedBytes, _defaultPageSize;
			unsigned _nextPageId;
			size_t _currentAllocatedInPages;

			struct Page
			{
				std::unique_ptr<uint8_t[]> _data;
				unsigned _id;
				SpanningHeap<uint32_t> _spanningHeap;
				size_t _pageSize;
			};
			std::vector<Page> _pages;

			std::pair<IteratorRange<void*>, unsigned> FreeUpSpaceFor(size_t size)
			{
				// Keep destroying files until we have enough free space in a page, or we're ok to allocate a new page
				for (auto f=_files.begin(); f!=_files.end();) {
					if (f->_clientFile.use_count() != 1) {
						++f;
						continue;
					}

					auto p = std::find_if(_pages.begin(), _pages.end(), [i=f->_pageId](const auto& q) { return q._id == i; });
					assert(p != _pages.end());
					assert(f->_clientFile->_data.begin() >= p->_data.get() && f->_clientFile->_data.end() <= PtrAdd(p->_data.get(), p->_pageSize));
					p->_spanningHeap.Deallocate((unsigned)PtrDiff(f->_clientFile->_data.begin(), (void*)p->_data.get()), (unsigned)f->_clientFile->_data.size());
					f = _files.erase(f);

					// re-attempt the allocation
					auto a = p->_spanningHeap.Allocate((unsigned)size);
					if (a != ~0u)
						return {
							{ PtrAdd(p->_data.get(), a), PtrAdd(p->_data.get(), a+size) },
							p->_id
						};

					// freed the last black from the page, then we'll actually destroy the page
					if (p->_spanningHeap.IsEmpty()) {
						_currentAllocatedInPages -= p->_pageSize;
						_pages.erase(p);
						if (_currentAllocatedInPages >= size)
							break;		// early out
					}
				}

				return { {}, ~0u };
			}
		};


		class ArchiveFileUncompressed : public IFileInterface
		{
		public:
			size_t      Read(void *buffer, size_t size, size_t count) const never_throws override
			{
				if (!(size*count)) return 0;
				auto remainingSpace = ptrdiff_t(_uncompressedData.end()) - ptrdiff_t(_tellp);
				assert(remainingSpace >= 0);
				auto objectsToRead = (size_t)std::max(ptrdiff_t(0), std::min(remainingSpace / ptrdiff_t(size), ptrdiff_t(count)));
				std::memcpy(buffer, _tellp, objectsToRead*size);
				_tellp = PtrAdd(_tellp, objectsToRead*size);
				return objectsToRead;
			}

			size_t      Write(const void *buffer, size_t size, size_t count) never_throws override
			{ 
				Throw(::Exceptions::BasicLabel("BSAFile::Write() unimplemented"));
			}

			ptrdiff_t	Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor anchor) never_throws override
			{
				ptrdiff_t result = ptrdiff_t(_tellp) - ptrdiff_t(_uncompressedData.begin());
				switch (anchor) {
				case OSServices::FileSeekAnchor::Start: _tellp = PtrAdd(_uncompressedData.begin(), seekOffset); break;
				case OSServices::FileSeekAnchor::Current: _tellp = PtrAdd(_tellp, seekOffset); break;
				case OSServices::FileSeekAnchor::End: _tellp = PtrAdd(_uncompressedData.end(), -ptrdiff_t(seekOffset)); break;
				default:
					Throw(::Exceptions::BasicLabel("Unknown seek anchor in BSAFile::Seek(). Only Start/Current/End supported"));
				}
				return result;
			}

			size_t      TellP() const never_throws override
			{
				return ptrdiff_t(_tellp) - ptrdiff_t(_uncompressedData.begin());
			}

			size_t				GetSize() const never_throws override
			{
				return _uncompressedData.size();
			}

			FileSnapshot	GetSnapshot() const never_throws override
			{
				return { FileSnapshot::State::Normal, _archiveModificationTime };
			}

			ArchiveFileUncompressed(ArchiveDanglingFileMonitor& fs, IteratorRange<const void*> uncompressedData, uint64_t archiveModificationTime)
			: _uncompressedData(uncompressedData)
			, _fs(&fs)
			, _archiveModificationTime(archiveModificationTime)
			{
				#if defined(_DEBUG)
					ScopedLock(_fs->_closingProtectionLock);
					if (_fs->_closingArchive)
						Throw(std::runtime_error("Cannot open file because archive is begin closed"));
					++_fs->_openFileCount;
				#endif

				_tellp = _uncompressedData.begin();
			}

			~ArchiveFileUncompressed()
			{
				#if defined(_DEBUG)
					// protection because we hold a raw pointer to the archive
					ScopedLock(_fs->_closingProtectionLock);
					--_fs->_openFileCount;
					assert(_fs->_openFileCount >= 0);
				#endif
			}
		protected:
			IteratorRange<const void*> _uncompressedData;
			mutable const void*	_tellp;
			ArchiveDanglingFileMonitor* _fs = nullptr;		// raw pointer
			uint64_t _archiveModificationTime;
		};

		std::unique_ptr<IFileInterface> CreateArchiveFileUncompressed(ArchiveDanglingFileMonitor& fs, IteratorRange<const void*> uncompressedData, uint64_t archiveModificationTime)
		{
			return std::make_unique<ArchiveFileUncompressed>(fs, uncompressedData, archiveModificationTime);
		}

		class ArchiveFileBufferedDecompress : public ArchiveFileUncompressed
		{
		public:
			ArchiveFileBufferedDecompress(std::shared_ptr<FileCache::File> file, ArchiveDanglingFileMonitor& fs, uint64_t archiveModificationTime)
			: ArchiveFileUncompressed(fs, file->_data, archiveModificationTime)
			{
				_file = std::move(file);
			}
			~ArchiveFileBufferedDecompress() {}
		private:
			std::shared_ptr<FileCache::File> _file;
		};

		OSServices::MemoryMappedFile CreateTrackedMemoryMappedFile(
			ArchiveDanglingFileMonitor& fs,
			IteratorRange<void*> data)
		{
			#if defined(_DEBUG)
				{
					ScopedLock(fs._closingProtectionLock);
					if (fs._closingArchive)
						Throw(std::runtime_error("Cannot open file because archive is begin closed"));
					++fs._openFileCount;
				}
			#endif

			return OSServices::MemoryMappedFile(
				data,
				[fs=&fs](auto) {
					#if defined(_DEBUG)
						// protection because we hold a raw pointer to the archive
						ScopedLock(fs->_closingProtectionLock);
						--fs->_openFileCount;
						assert(fs->_openFileCount >= 0);
					#endif
				});
		}

		OSServices::MemoryMappedFile CreateTrackedMemoryMappedFile(
			ArchiveDanglingFileMonitor& fs,
			std::shared_ptr<FileCache::File> file)
		{
			#if defined(_DEBUG)
				{
					ScopedLock(fs._closingProtectionLock);
					if (fs._closingArchive)
						Throw(std::runtime_error("Cannot open file because archive is begin closed"));
					++fs._openFileCount;
				}
			#endif

			return OSServices::MemoryMappedFile(
				file->_data,
				[fs=&fs, file](auto) {
					(void)file;	// keep alive
					#if defined(_DEBUG)
						// protection because we hold a raw pointer to the archive
						ScopedLock(fs->_closingProtectionLock);
						--fs->_openFileCount;
						assert(fs->_openFileCount >= 0);
					#endif
				});
		}
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const FilenameRules s_filenameRules{'/', true};

	class XPakFileSystem : public IFileSystem // , public ISearchableFileSystem
	{
	public:
		TranslateResult		TryTranslate(Marker& result, StringSection<utf8> filename) override;
		TranslateResult		TryTranslate(Marker& result, StringSection<utf16> filename) override;

		IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;
		IOReason	TryOpen(OSServices::BasicFile& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;
		IOReason	TryOpen(OSServices::MemoryMappedFile& result, const Marker& marker, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;

		IOReason	TryMonitor(/* out */ FileSnapshot&, const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) override;
		IOReason	TryFakeFileChange(const Marker& marker) override;
		FileDesc	TryGetDesc(const Marker& marker) override;

		/*
		ISearchableFileSystem not implemented yet

		auto FindFiles(
			StringSection<> baseDirectory,
			StringSection<> matchPattern) -> std::vector<IFileSystem::Marker> override;
		auto FindSubDirectories(StringSection<> baseDirectory) -> std::vector<std::string> override;
		*/

		XPakFileSystem(StringSection<> archive, std::shared_ptr<ArchiveUtility::FileCache> fileCache);
		~XPakFileSystem();
	private:
		OSServices::MemoryMappedFile _archive;
		void Initialize();

		IteratorRange<const Internal::XPakStructures::FileEntry*> _fileEntries;
		IteratorRange<const uint64_t*> _hashTable;
		const char* _stringTable;

		struct MarkerContents
		{
			uint32_t _fileIndex;
		};
		Marker AsMarker(uint32_t fileIndex);

		std::string _archiveName;
		FileDesc _archiveDesc;
		ArchiveUtility::ArchiveDanglingFileMonitor _danglingFileMonitor;

		std::shared_ptr<ArchiveUtility::FileCache> _fileCache;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	using IOReason = IFileSystem::IOReason;

	static void XPakDecompressBlob(IteratorRange<void*> decompressionDst, IteratorRange<const void*> compressedData)
	{
		int res = fastlz_decompress(compressedData.data(), (int)compressedData.size(), decompressionDst.begin(), (int)decompressionDst.size());
		assert(res == decompressionDst.size()); (void)res;
	}

	auto XPakFileSystem::TryTranslate(Marker& result, StringSection<utf8> filename) -> TranslateResult
	{
		auto hash = HashFilenameAndPath<utf8>(filename, s_filenameRules);
		auto i = std::lower_bound(_hashTable.begin(), _hashTable.end(), hash);
		if (i == _hashTable.end() || *i != hash) return TranslateResult::Invalid;

		result = AsMarker(unsigned(i-_hashTable.begin()));
		return TranslateResult::Success;
	}

	auto XPakFileSystem::TryTranslate(Marker& result, StringSection<utf16> filename) -> TranslateResult
	{ 
		auto hash = HashFilenameAndPath<utf16>(filename, s_filenameRules);
		auto i = std::lower_bound(_hashTable.begin(), _hashTable.end(), hash);
		if (i == _hashTable.end() || *i != hash) return TranslateResult::Invalid;

		result = AsMarker(unsigned(i-_hashTable.begin()));
		return TranslateResult::Success;
	}

	IOReason	XPakFileSystem::TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		if (marker.size() < sizeof(MarkerContents)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerContents*)marker.data();
		assert(m._fileIndex <= _fileEntries.size());
		const auto& entry = _fileEntries[m._fileIndex];

		if ((entry._flags + entry._compressedSize) > _archive.GetSize())
			Throw(std::runtime_error("File entry corrupted in archive lookup table"));

		auto srcData = MakeIteratorRange(
			PtrAdd(_archive.GetData().begin(), entry._offset),
			PtrAdd(_archive.GetData().begin(), entry._offset+entry._compressedSize));

		if (entry._compressedSize < entry._decompressedSize) {

			auto resourceGuid = HashCombine(_hashTable[m._fileIndex], entry._contentsHash);
			auto file = _fileCache->Reserve(resourceGuid, entry._decompressedSize, &XPakDecompressBlob, srcData);
			result = std::make_unique<ArchiveUtility::ArchiveFileBufferedDecompress>(std::move(file), _danglingFileMonitor, _archiveDesc._snapshot._modificationTime);
			
		} else {

			result = CreateArchiveFileUncompressed(_danglingFileMonitor, srcData, _archiveDesc._snapshot._modificationTime);

		}
		return IOReason::Success;
	}

	IOReason	XPakFileSystem::TryOpen(OSServices::BasicFile& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		return IOReason::Invalid;
	}

	IOReason	XPakFileSystem::TryOpen(OSServices::MemoryMappedFile& result, const Marker& marker, uint64_t size, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		if (marker.size() < sizeof(MarkerContents)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerContents*)marker.data();
		assert(m._fileIndex <= _fileEntries.size());
		const auto& entry = _fileEntries[m._fileIndex];

		auto srcData = MakeIteratorRange(
			PtrAdd(_archive.GetData().begin(), entry._offset),
			PtrAdd(_archive.GetData().begin(), entry._offset+entry._compressedSize));

		if (entry._compressedSize < entry._decompressedSize) {

			auto resourceGuid = HashCombine(_hashTable[m._fileIndex], entry._contentsHash);
			auto file = _fileCache->Reserve(resourceGuid, entry._decompressedSize, &XPakDecompressBlob, srcData);
			result = ArchiveUtility::CreateTrackedMemoryMappedFile(_danglingFileMonitor, std::move(file));

		} else {

			result = ArchiveUtility::CreateTrackedMemoryMappedFile(_danglingFileMonitor, srcData);

		}

		return IOReason::Success;
	}

	IOReason	XPakFileSystem::TryMonitor(/* out */ FileSnapshot& snapshot, const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt)
	{
		if (marker.size() < sizeof(MarkerContents)) {
			snapshot = { FileSnapshot::State::DoesNotExist, 0 };
			return IOReason::Invalid;
		}

		snapshot = { FileSnapshot::State::Normal, _archiveDesc._snapshot._modificationTime };
		return IOReason::Invalid;
	}

	IOReason	XPakFileSystem::TryFakeFileChange(const Marker& marker)
	{
		return IOReason::Invalid;
	}

	FileDesc XPakFileSystem::TryGetDesc(const Marker& marker)
	{
		if (marker.size() < sizeof(MarkerContents)) 
			return FileDesc{
				{}, {},
				{FileSnapshot::State::DoesNotExist, 0}, 0 };
		const auto& m = *(const MarkerContents*)marker.data();
		assert(m._fileIndex < _fileEntries.size());
		const auto& entry = _fileEntries[m._fileIndex];

		std::string fn = &_stringTable[entry._stringTableOffset];
		return FileDesc{
			fn, fn,
			{ FileSnapshot::State::Normal, _archiveDesc._snapshot._modificationTime },
			entry._decompressedSize};
	}

	auto XPakFileSystem::AsMarker(uint32_t fileIndex) -> Marker
	{
		auto result = std::vector<uint8_t>(sizeof(Marker), 0);
		auto& m = *(MarkerContents*)result.data();
		m._fileIndex = fileIndex;
		return result;
	}

	/*
	auto XPakFileSystem::FindFiles(
		StringSection<> baseDirectory,
		StringSection<> matchPattern) -> std::vector<IFileSystem::Marker>
	{
		assert(0);
		return {};
	}

	auto XPakFileSystem::FindSubDirectories(StringSection<> baseDirectory) -> std::vector<std::string>
	{
		assert(0);
		return {};
	}
	*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void XPakFileSystem::Initialize()
	{
		_archiveDesc = MainFileSystem::TryGetDesc(_archiveName);		// only using stats of the first archive with the file table in it (in practice, they multi-part archives should all have the same modification date)
		_archive = MainFileSystem::OpenMemoryMappedFile(_archiveName, 0u, "r");

		auto& hdr = *(const Internal::XPakStructures::Header*)_archive.GetData().begin();
		if (hdr._majik != 'KAPX')
			Throw(std::runtime_error("Archive does not appear to be a XPAK file, or file corrupted (initial bytes don't contain magic number)"));

		if (hdr._version != 0)
			Throw(std::runtime_error("Archive incorrect version (only version 0 supported)"));

		if ((hdr._fileEntriesOffset + hdr._fileCount) > _archive.GetData().size())
			Throw(std::runtime_error("Bad file list in XPAK file (header appears to be corrupted)"));

		_fileEntries = MakeIteratorRange(
			(const Internal::XPakStructures::FileEntry*)PtrAdd(_archive.GetData().begin(), hdr._fileEntriesOffset),
			(const Internal::XPakStructures::FileEntry*)PtrAdd(_archive.GetData().begin(), hdr._fileEntriesOffset + sizeof(Internal::XPakStructures::FileEntry)*hdr._fileCount));

		_hashTable = MakeIteratorRange(
			(const uint64_t*)PtrAdd(_archive.GetData().begin(), hdr._hashTableOffset),
			(const uint64_t*)PtrAdd(_archive.GetData().begin(), hdr._hashTableOffset + sizeof(uint64_t)*hdr._fileCount));

		_stringTable = (const char*)PtrAdd(_archive.GetData().begin(), hdr._stringTableOffset);
	}

	XPakFileSystem::XPakFileSystem(StringSection<> archive, std::shared_ptr<ArchiveUtility::FileCache> fileCache)
	: _archiveName(archive.AsString())
	, _fileCache(std::move(fileCache))
	{
		const size_t fileCacheSize = 4*1024*1024;
		_fileCache = std::make_shared<ArchiveUtility::FileCache>(fileCacheSize);
		Initialize();
	}

	XPakFileSystem::~XPakFileSystem()
	{
	}

	std::shared_ptr<IFileSystem> CreateXPakFileSystem(StringSection<> archive, std::shared_ptr<ArchiveUtility::FileCache> fileCache)
	{
		return std::make_shared<XPakFileSystem>(archive, std::move(fileCache));
	}

	std::shared_ptr<ArchiveUtility::FileCache> CreateFileCache(size_t sizeInBytes)
	{
		return std::make_shared<ArchiveUtility::FileCache>(sizeInBytes);
	}

}

