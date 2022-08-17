// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MemoryFile.h"
#include "DepVal.h"
#include <algorithm>
#include "../OSServices/FileSystemMonitor.h"
#if !defined(EXCLUDE_Z_LIB)
    #include "../Foreign/zlib/zlib.h"
#endif
#include "../Utility/Conversion.h"
#include "../Utility/Threading/Mutex.h"
#include <stdexcept>

namespace Assets
{
	class MemoryFile : public IFileInterface
	{
	public:
		size_t			Write(const void * source, size_t size, size_t count) never_throws override;
		size_t			Read(void * destination, size_t size, size_t count) const never_throws override;
		ptrdiff_t		Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor) never_throws override;
		size_t			TellP() const never_throws override;

		size_t			GetSize() const never_throws override;
		FileDesc		GetDesc() const never_throws override;

		MemoryFile(const Blob& blob, OSServices::FileTime modificationTime = 0);
		~MemoryFile();
	private:
		Blob			_blob;
		mutable size_t	_ptr;
		OSServices::FileTime _modificationTime;
	};

	size_t			MemoryFile::Write(const void * source, size_t size, size_t count) never_throws
	{
		size_t finalSize = size * count;
		if (!_blob) _blob = std::make_shared<std::vector<uint8_t>>();
		_blob->insert(_blob->begin() + _ptr, (const uint8_t*)source, (const uint8_t*)PtrAdd(source, finalSize));
		_ptr += finalSize;
		return finalSize;
	}

	size_t			MemoryFile::Read(void * destination, size_t size, size_t count) const never_throws
	{
		if (!size || !count) return 0;
		if (!_blob) return 0;

		ptrdiff_t spaceLeft = _blob->size() - _ptr;
		ptrdiff_t maxCount = spaceLeft / size;
		ptrdiff_t finalCount = std::min(ptrdiff_t(count), maxCount);

		std::memcpy(
			destination,
			PtrAdd(AsPointer(_blob->begin()), _ptr),
			finalCount * size);
		_ptr += finalCount * size;
		return finalCount;
	}

	ptrdiff_t		MemoryFile::Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor anchor) never_throws
	{
		ptrdiff_t newPtr = 0;
		switch (anchor) {
		case OSServices::FileSeekAnchor::Start:		newPtr = seekOffset; break;
		case OSServices::FileSeekAnchor::Current:	newPtr = _ptr + seekOffset; break;
		case OSServices::FileSeekAnchor::End:		newPtr = (_blob ? _blob->size() : 0) + seekOffset; break;
		default:
			assert(0);
		}
		newPtr = std::max(ptrdiff_t(0), newPtr);
		newPtr = std::min(ptrdiff_t(_blob->size()), newPtr);
		_ptr = newPtr;
		return _ptr;
	}

	size_t			MemoryFile::TellP() const never_throws
	{
		return _ptr;
	}

	size_t			MemoryFile::GetSize() const never_throws
	{
		return _blob->size();
	}

	FileDesc		MemoryFile::GetDesc() const never_throws
	{
		return FileDesc
			{
				"<<in memory>>", {},
				FileSnapshot::State::Normal,
				_modificationTime, uint64_t(_blob ? _blob->size() : 0)
			};
	}

	MemoryFile::MemoryFile(const Blob& blob, OSServices::FileTime modificationTime)
	: _blob(blob)
	, _ptr(0)
	, _modificationTime(modificationTime)
	{}

	MemoryFile::~MemoryFile()
	{
	}

	std::unique_ptr<IFileInterface> CreateMemoryFile(const Blob& blob)
	{
		return std::make_unique<MemoryFile>(blob);
	}

////////////////////////////////////////////////////////////////////////////////////////////////

	class MemoryFileStatic : public IFileInterface
	{
	public:
		size_t			Write(const void * source, size_t size, size_t count) never_throws override;
		size_t			Read(void * destination, size_t size, size_t count) const never_throws override;
		ptrdiff_t		Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor) never_throws override;
		size_t			TellP() const never_throws override;

		size_t			GetSize() const never_throws override;
		FileDesc		GetDesc() const never_throws override;

		MemoryFileStatic(IteratorRange<const void*> data, OSServices::FileTime modificationTime = 0);
		~MemoryFileStatic();
	private:
		IteratorRange<const void*> _data;
		mutable size_t	_ptr;
		OSServices::FileTime _modificationTime;
	};

	size_t			MemoryFileStatic::Write(const void * source, size_t size, size_t count) never_throws
	{
		Throw(std::runtime_error("Attempting to write to a write protected MemoryFileStatic"));
	}

	size_t			MemoryFileStatic::Read(void * destination, size_t size, size_t count) const never_throws
	{
		if (!size || !count) return 0;

		ptrdiff_t spaceLeft = _data.size() - _ptr;
		ptrdiff_t maxCount = spaceLeft / size;
		ptrdiff_t finalCount = std::min(ptrdiff_t(count), maxCount);

		std::memcpy(
			destination,
			PtrAdd(AsPointer(_data.begin()), _ptr),
			finalCount * size);
		_ptr += finalCount * size;
		return finalCount;
	}

	ptrdiff_t		MemoryFileStatic::Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor anchor) never_throws
	{
		ptrdiff_t newPtr = 0;
		switch (anchor) {
		case OSServices::FileSeekAnchor::Start:		newPtr = seekOffset; break;
		case OSServices::FileSeekAnchor::Current:	newPtr = _ptr + seekOffset; break;
		case OSServices::FileSeekAnchor::End:		newPtr = _data.size() + seekOffset; break;
		default:
			assert(0);
		}
		newPtr = std::max(ptrdiff_t(0), newPtr);
		newPtr = std::min(ptrdiff_t(_data.size()), newPtr);
		_ptr = newPtr;
		return _ptr;
	}

	size_t			MemoryFileStatic::TellP() const never_throws
	{
		return _ptr;
	}

	size_t			MemoryFileStatic::GetSize() const never_throws
	{
		return _data.size();
	}

	FileDesc		MemoryFileStatic::GetDesc() const never_throws
	{
		return FileDesc
			{
				"<<in memory>>", {},
				FileSnapshot::State::Normal,
				_modificationTime, uint64_t(_data.size())
			};
	}

	MemoryFileStatic::MemoryFileStatic(IteratorRange<const void*> data, OSServices::FileTime modificationTime)
	: _data(data)
	, _ptr(0)
	, _modificationTime(modificationTime)
	{}

	MemoryFileStatic::~MemoryFileStatic()
	{
	}

	std::unique_ptr<IFileInterface> CreateMemoryFile(IteratorRange<const void*> blob)
	{
		return std::make_unique<MemoryFileStatic>(blob);
	}

////////////////////////////////////////////////////////////////////////////////////////////////

	class ArchiveSubFile : public ::Assets::IFileInterface
	{
	public:
		size_t      Read(void *buffer, size_t size, size_t count) const never_throws override;
		size_t      Write(const void *buffer, size_t size, size_t count) never_throws override;
		ptrdiff_t	Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor) never_throws override;
		size_t      TellP() const never_throws override;

		size_t				GetSize() const never_throws override;
		::Assets::FileDesc	GetDesc() const never_throws override;

		ArchiveSubFile(
			const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile,
			const IteratorRange<const void*> memoryRange);
		~ArchiveSubFile();
	protected:
		std::shared_ptr<OSServices::MemoryMappedFile> _archiveFile;
		const void*			_dataStart;
		const void*			_dataEnd;
		mutable const void*	_tellp;
	};

	size_t      ArchiveSubFile::Read(void *buffer, size_t size, size_t count) const never_throws 
	{ 
		if (!(size*count)) return 0;
		auto remainingSpace = ptrdiff_t(_dataEnd) - ptrdiff_t(_tellp);
		auto objectsToRead = (size_t)std::max(ptrdiff_t(0), std::min(remainingSpace / ptrdiff_t(size), ptrdiff_t(count)));
		std::memcpy(buffer, _tellp, objectsToRead*size);
		_tellp = PtrAdd(_tellp, objectsToRead*size);
		return objectsToRead;
	}

	size_t      ArchiveSubFile::Write(const void *buffer, size_t size, size_t count) never_throws 
	{ 
		Throw(::Exceptions::BasicLabel("BSAFile::Write() unimplemented"));
	}

	ptrdiff_t	ArchiveSubFile::Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor anchor) never_throws 
	{
		ptrdiff_t result = ptrdiff_t(_tellp) - ptrdiff_t(_dataStart);
		switch (anchor) {
		case OSServices::FileSeekAnchor::Start: _tellp = PtrAdd(_dataStart, seekOffset); break;
		case OSServices::FileSeekAnchor::Current: _tellp = PtrAdd(_tellp, seekOffset); break;
		case OSServices::FileSeekAnchor::End: _tellp = PtrAdd(_dataEnd, -ptrdiff_t(seekOffset)); break;
		default:
			Throw(::Exceptions::BasicLabel("Unknown seek anchor in BSAFile::Seek(). Only Start/Current/End supported"));
		}
		return result;
	}

	size_t      ArchiveSubFile::TellP() const never_throws 
	{ 
		return ptrdiff_t(_tellp) - ptrdiff_t(_dataStart);
	}

	size_t		ArchiveSubFile::GetSize() const never_throws
	{
		return PtrDiff(_dataEnd, _dataStart);
	}

	::Assets::FileDesc ArchiveSubFile::GetDesc() const never_throws
	{
		Throw(::Exceptions::BasicLabel("BSAFile::GetDesc() unimplemented"));
	}

	ArchiveSubFile::ArchiveSubFile(const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile, const IteratorRange<const void*> memoryRange)
	: _archiveFile(archiveFile)
	{
		_dataStart = memoryRange.begin();
		_dataEnd = memoryRange.end();
		_tellp = _dataStart;
	}

	ArchiveSubFile::~ArchiveSubFile() {}

	std::unique_ptr<IFileInterface> CreateSubFile(
		const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange)
	{
		return std::make_unique<ArchiveSubFile>(archiveFile, memoryRange);
	}

////////////////////////////////////////////////////////////////////////////////////////////////

	class FileDecompressOnRead : public ::Assets::IFileInterface
	{
	public:
		size_t      Read(void *buffer, size_t size, size_t count) const never_throws override;
		size_t      Write(const void *buffer, size_t size, size_t count) never_throws override;
		ptrdiff_t	Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor) never_throws override;
		size_t      TellP() const never_throws override;

		size_t				GetSize() const never_throws override;
		::Assets::FileDesc	GetDesc() const never_throws override;

		FileDecompressOnRead(
			const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile,
			const IteratorRange<const void*> memoryRange,
			size_t decompressedSize,
			unsigned fixedWindowSize);
		~FileDecompressOnRead();
	protected:
		std::shared_ptr<OSServices::MemoryMappedFile> _archiveFile;
		#if !defined(EXCLUDE_Z_LIB)
            mutable z_stream	_stream;
        #endif
		size_t				_decompressedSize;
		mutable size_t		_tellp = 0;
	};

	size_t      FileDecompressOnRead::Write(const void *buffer, size_t size, size_t count) never_throws
	{
		Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::Seek() unimplemented"));
	}

	ptrdiff_t	FileDecompressOnRead::Seek(ptrdiff_t seekOffset, OSServices::FileSeekAnchor anchor) never_throws
	{
		// We can't easily seek, because the underlying stream is compressed. Seeking would require
		// decompressing the buffer as we go along.
		auto offset = seekOffset;
		if (anchor == OSServices::FileSeekAnchor::Current) {
			offset += _tellp;
		} else if (anchor == OSServices::FileSeekAnchor::End) {
			offset = _decompressedSize - seekOffset;
		} else {
			assert(anchor == OSServices::FileSeekAnchor::Start);
		}
		if (offset == (ptrdiff_t)_tellp)
			return _tellp;

		if (size_t(offset) < _tellp)	//(we could reset and restart from the top here, but that would be inefficient)
			Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::Seek() unimplemented"));

		// Move the pointer forward by just reading in dummy bytes
		auto dummyBuffer = std::make_unique<uint8_t[]>(offset-_tellp);
		Read(dummyBuffer.get(), 1, offset-_tellp);
		return _tellp;
	}

	size_t      FileDecompressOnRead::TellP() const never_throws
	{
		return _tellp;
	}

	size_t		FileDecompressOnRead::GetSize() const never_throws 
	{
		return _decompressedSize;
	}

	::Assets::FileDesc	FileDecompressOnRead::GetDesc() const never_throws
	{
		// Throw(::Exceptions::BasicLabel("BSAFileDecompressOnRead::GetDesc() unimplemented"));
		return {
			{}, {}, FileSnapshot::State::Normal, 0,
			_decompressedSize
		};
	}

	size_t      FileDecompressOnRead::Read(void *buffer, size_t size, size_t count) const never_throws
	{
        #if !defined(EXCLUDE_Z_LIB)
            _stream.next_out = (Bytef*)buffer;
            _stream.avail_out = (uInt)(size*count);
            auto err = inflate(&_stream, Z_SYNC_FLUSH);
            assert(err >= 0); (void) err;
			auto readSize = _stream.total_out - _tellp;
			_tellp = _stream.total_out;
            return readSize / size;
        #else
            return 0;
        #endif
	}

	FileDecompressOnRead::FileDecompressOnRead(
		const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange,
		size_t decompressedSize,
		unsigned fixedWindowSize)
	: _archiveFile(archiveFile)
	, _decompressedSize(decompressedSize)
	{
        #if !defined(EXCLUDE_Z_LIB)
            _stream.next_in = (z_const Bytef *)memoryRange.begin();
            _stream.avail_in = (uInt)memoryRange.size();

            _stream.next_out = nullptr;
            _stream.avail_out = 0;

            _stream.zalloc = (alloc_func)0;
            _stream.zfree = (free_func)0;

			int err;
			if (fixedWindowSize) {
				err = inflateInit2(&_stream, -(signed)fixedWindowSize);
			} else {
				err = inflateInit(&_stream);
			}
            assert(err == Z_OK); (void) err;

			_tellp = 0;
        #else
            assert(0);
        #endif
	}

	FileDecompressOnRead::~FileDecompressOnRead() 
	{
        #if !defined(EXCLUDE_Z_LIB)
		    auto err = inflateEnd(&_stream);
		    assert(err == Z_OK); (void) err;
        #endif
	}

	std::unique_ptr<IFileInterface> CreateDecompressOnReadFile(
		const std::shared_ptr<OSServices::MemoryMappedFile>& archiveFile,
		const IteratorRange<const void*> memoryRange,
		size_t decompressedSize,
		unsigned fixedWindowSize)
	{
		return std::make_unique<FileDecompressOnRead>(archiveFile, memoryRange, decompressedSize, fixedWindowSize);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FileSystem_Memory : public IFileSystem
	{
	public:
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf8> filename) override;
		virtual TranslateResult		TryTranslate(Marker& result, StringSection<utf16> filename) override;

		virtual IOReason	TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& uri, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;
		virtual IOReason	TryOpen(OSServices::BasicFile& result, const Marker& uri, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;
		virtual IOReason	TryOpen(OSServices::MemoryMappedFile& result, const Marker& uri, uint64 size, const char openMode[], OSServices::FileShareMode::BitField shareMode) override;
		virtual IOReason	TryMonitor(FileSnapshot&, const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) override;
		virtual IOReason	TryFakeFileChange(const Marker& marker) override;
		virtual	FileDesc	TryGetDesc(const Marker& marker) override;

		FileSystem_Memory(
			const std::unordered_map<std::string, Blob>& filesAndContents, 
			const std::unordered_map<std::string, IteratorRange<const void*>>& staticFilesAndContents, 
			const FilenameRules& filenameRules, FileSystemMemoryFlags::BitField flags);
		~FileSystem_Memory();

	protected:
		std::vector<std::pair<uint64_t, Blob>> _filesAndContents;
		std::vector<std::pair<uint64_t, IteratorRange<const void*>>> _staticFilesAndContents;
		Threading::Mutex _attachedMonitorsLock;
		std::vector<std::pair<unsigned, std::weak_ptr<IFileMonitor>>> _attachedMonitors;
		std::vector<std::pair<unsigned, std::weak_ptr<IFileMonitor>>> _staticAttachedMonitors;
		FilenameRules _filenameRules;
		FileSystemMemoryFlags::BitField _flags;

		struct MarkerStruct
		{
			size_t _fileIdx;
		};

		OSServices::FileTime _modificationTime;
	};

	auto FileSystem_Memory::TryTranslate(Marker& result, StringSection<utf8> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		auto hash = HashFilenameAndPath(filename, _filenameRules);
		auto i = LowerBound(_filesAndContents, hash);
		if (i != _filesAndContents.end() && i->first == hash) {
			result.resize(sizeof(MarkerStruct));
			auto* out = (MarkerStruct*)AsPointer(result.begin());
			out->_fileIdx = std::distance(_filesAndContents.begin(), i) << size_t(1);
			return TranslateResult::Success;
		}

		auto i2 = LowerBound(_staticFilesAndContents, hash);
		if (i2 != _staticFilesAndContents.end() && i2->first == hash) {
			result.resize(sizeof(MarkerStruct));
			auto* out = (MarkerStruct*)AsPointer(result.begin());
			out->_fileIdx = (std::distance(_staticFilesAndContents.begin(), i2) << size_t(1)) | size_t(1);
			return TranslateResult::Success;
		}

		return TranslateResult::Invalid;
	}

	auto FileSystem_Memory::TryTranslate(Marker& result, StringSection<utf16> filename) -> TranslateResult
	{
		if (filename.IsEmpty())
			return TranslateResult::Invalid;

		auto hash = HashFilenameAndPath(filename, _filenameRules);
		auto i = LowerBound(_filesAndContents, hash);
		if (i != _filesAndContents.end() && i->first == hash) {
			result.resize(sizeof(MarkerStruct));
			auto* out = (MarkerStruct*)AsPointer(result.begin());
			out->_fileIdx = std::distance(_filesAndContents.begin(), i) << size_t(1);
			return TranslateResult::Success;
		}

		auto i2 = LowerBound(_staticFilesAndContents, hash);
		if (i2 != _staticFilesAndContents.end() && i2->first == hash) {
			result.resize(sizeof(MarkerStruct));
			auto* out = (MarkerStruct*)AsPointer(result.begin());
			out->_fileIdx = (std::distance(_staticFilesAndContents.begin(), i2) << size_t(1)) | size_t(1);
			return TranslateResult::Success;
		}

		return TranslateResult::Invalid;
	}

	auto FileSystem_Memory::TryOpen(std::unique_ptr<IFileInterface>& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx & 1) {
			if (strchr(openMode, 'w'))
				return IOReason::WriteProtect;

			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _staticFilesAndContents.size())
				return IOReason::FileNotFound;

			auto i = _staticFilesAndContents.begin();
			std::advance(i, idx);
			result = std::make_unique<MemoryFileStatic>(i->second, _modificationTime);
			return IOReason::Success;
		} else {
			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _filesAndContents.size())
				return IOReason::FileNotFound;

			auto i = _filesAndContents.begin();
			std::advance(i, idx);
			result = std::make_unique<MemoryFile>(i->second, _modificationTime);
			return IOReason::Success;
		}
	}

	auto FileSystem_Memory::TryOpen(OSServices::BasicFile& result, const Marker& marker, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		// Cannot open memory files in this way
		return IOReason::Invalid;
	}

	auto FileSystem_Memory::TryOpen(OSServices::MemoryMappedFile& result, const Marker& marker, uint64 size, const char openMode[], OSServices::FileShareMode::BitField shareMode) -> IOReason
	{
		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx & 1) {
			if (strchr(openMode, 'w'))
				return IOReason::WriteProtect;

			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _staticFilesAndContents.size())
				return IOReason::FileNotFound;

			auto i = _staticFilesAndContents.begin();
			std::advance(i, idx);
			result = OSServices::MemoryMappedFile({(void*)i->second.begin(), (void*)i->second.end()}, OSServices::MemoryMappedFile::CloseFn{});
			return IOReason::Success;
		} else {
			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _filesAndContents.size())
				return IOReason::FileNotFound;

			auto i = _filesAndContents.begin();
			std::advance(i, idx);
			result = OSServices::MemoryMappedFile(MakeIteratorRange(*i->second), OSServices::MemoryMappedFile::CloseFn{});
			return IOReason::Success;
		}
	}

	auto FileSystem_Memory::TryMonitor(FileSnapshot& snapshot, const Marker& marker, const std::shared_ptr<IFileMonitor>& evnt) -> IOReason
	{
		snapshot._state = FileSnapshot::State::DoesNotExist;

		// Monitors are only really needed for unit tests and debugging purposes
		// when not needed, we should disable them
		if (!(_flags & FileSystemMemoryFlags::EnableChangeMonitoring))
			return IOReason::Invalid;

		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		auto idx = m._fileIdx >> size_t(1);
		if (m._fileIdx & 1) {
			if (idx >= _staticFilesAndContents.size())
				return IOReason::FileNotFound;

			ScopedLock(_attachedMonitorsLock);
			auto range = EqualRange(_staticAttachedMonitors, (unsigned)idx);
			for (auto r=range.first; r!=range.second; ++r)
				// weak_ptr to shared_ptr comparison without lock -- https://stackoverflow.com/questions/12301916/equality-compare-stdweak-ptr
				// compares the control block, rather than the object pointer itself
				if (!r->second.owner_before(evnt) && !evnt.owner_before(r->second))
					return IOReason::Invalid;

			_staticAttachedMonitors.insert(range.second, std::make_pair(idx, evnt));
			snapshot._state = FileSnapshot::State::Normal;
			snapshot._modificationTime = _modificationTime;
			return IOReason::Success;
		} else {
			if (idx >= _filesAndContents.size())
				return IOReason::FileNotFound;

			ScopedLock(_attachedMonitorsLock);
			auto range = EqualRange(_attachedMonitors, (unsigned)idx);
			for (auto r=range.first; r!=range.second; ++r)
				// weak_ptr to shared_ptr comparison without lock -- https://stackoverflow.com/questions/12301916/equality-compare-stdweak-ptr
				// compares the control block, rather than the object pointer itself
				if (!r->second.owner_before(evnt) && !evnt.owner_before(r->second))
					return IOReason::Invalid;

			_attachedMonitors.insert(range.second, std::make_pair(idx, evnt));
			snapshot._state = FileSnapshot::State::Normal;
			snapshot._modificationTime = _modificationTime;
			return IOReason::Success;
		}
	}

	auto FileSystem_Memory::TryFakeFileChange(const Marker& marker) -> IOReason
	{
		if (!(_flags & FileSystemMemoryFlags::EnableChangeMonitoring))
			return IOReason::Invalid;

		if (marker.size() < sizeof(MarkerStruct)) return IOReason::FileNotFound;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		auto idx = m._fileIdx >> size_t(1);
		if (m._fileIdx & 1) {
			if (idx >= _staticFilesAndContents.size())
				return IOReason::FileNotFound;

			ScopedLock(_attachedMonitorsLock);
			auto range = EqualRange(_staticAttachedMonitors, (unsigned)idx);
			for (auto r=range.first; r!=range.second; ++r) {
				auto m = r->second.lock();
				if (m)
					m->OnChange();
			}
			return IOReason::Success;
		} else {
			if (idx >= _filesAndContents.size())
				return IOReason::FileNotFound;

			ScopedLock(_attachedMonitorsLock);
			auto range = EqualRange(_attachedMonitors, (unsigned)idx);
			for (auto r=range.first; r!=range.second; ++r) {
				auto m = r->second.lock();
				if (m)
					m->OnChange();
			}
			return IOReason::Success;
		}
	}

	FileDesc FileSystem_Memory::TryGetDesc(const Marker& marker)
	{
		if (marker.size() < sizeof(MarkerStruct)) return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileSnapshot::State::DoesNotExist };;

		const auto& m = *(const MarkerStruct*)AsPointer(marker.begin());
		if (m._fileIdx & 1) {
			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _staticFilesAndContents.size())
				return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileSnapshot::State::DoesNotExist };;

			auto i = _staticFilesAndContents.begin();
			std::advance(i, idx);

			auto name = Conversion::Convert<std::basic_string<utf8>>(i->first);
			return FileDesc
				{
					name, name,
					FileSnapshot::State::Normal,
					_modificationTime, (uint64_t)i->second.size()
				};
		} else {
			auto idx = m._fileIdx >> size_t(1);
			if (idx >= _filesAndContents.size())
				return FileDesc{ std::basic_string<utf8>(), std::basic_string<utf8>(), FileSnapshot::State::DoesNotExist };;

			auto i = _filesAndContents.begin();
			std::advance(i, idx);

			auto name = Conversion::Convert<std::basic_string<utf8>>(i->first);
			return FileDesc
				{
					name, name,
					FileSnapshot::State::Normal,
					_modificationTime, (uint64_t)i->second->size()
				};
		}
	}

	FileSystem_Memory::FileSystem_Memory(
		const std::unordered_map<std::string, Blob>& filesAndContents, 
		const std::unordered_map<std::string, IteratorRange<const void*>>& staticFilesAndContents, 
		const FilenameRules& filenameRules, FileSystemMemoryFlags::BitField flags)
	: _filenameRules(filenameRules)
	, _flags(flags)
	, _modificationTime(0)
	{
		_filesAndContents.reserve(filesAndContents.size());
		for (const auto&i:filesAndContents) {
			auto fnHash = HashFilename(MakeStringSection(i.first), _filenameRules);
			auto i2 = LowerBound(_filesAndContents, fnHash);
			assert(i2 == _filesAndContents.end() || i2->first != fnHash);
			_filesAndContents.insert(i2, {fnHash, i.second});
		}

		_staticFilesAndContents.reserve(staticFilesAndContents.size());
		for (const auto&i:staticFilesAndContents) {
			auto fnHash = HashFilename(MakeStringSection(i.first), _filenameRules);
			auto i2 = LowerBound(_staticFilesAndContents, fnHash);
			assert(i2 == _staticFilesAndContents.end() || i2->first != fnHash);
			_staticFilesAndContents.insert(i2, {fnHash, i.second});
		}

		if (flags & FileSystemMemoryFlags::UseModuleModificationTime)
			_modificationTime = OSServices::GetModuleFileTime();
	}

	FileSystem_Memory::~FileSystem_Memory() {}


	std::shared_ptr<IFileSystem> CreateFileSystem_Memory(
		const std::unordered_map<std::string, Blob>& filesAndContents,
		const FilenameRules& filenameRules,
		FileSystemMemoryFlags::BitField flags)
	{
		return std::make_shared<FileSystem_Memory>(filesAndContents, std::unordered_map<std::string, IteratorRange<const void*>>{}, filenameRules, flags);
	}

	std::shared_ptr<IFileSystem> CreateFileSystem_Memory(
		const std::unordered_map<std::string, IteratorRange<const void*>>& filesAndContents,
		const FilenameRules& filenameRules,
		FileSystemMemoryFlags::BitField flags)
	{
		return std::make_shared<FileSystem_Memory>(std::unordered_map<std::string, Blob>{}, filesAndContents, filenameRules, flags);
	}
}

