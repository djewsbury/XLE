// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ArchiveCache.h"
#include "ChunkFile.h"
#include "ChunkFileContainer.h"
#include "IFileSystem.h"
#include "AssetUtils.h"
#include "DepVal.h"
#include "BlockSerializer.h"
#include "../OSServices/Log.h"
#include "../OSServices/RawFS.h"
#include "../OSServices/LegacyFileStreams.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/OutputStreamFormatter.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/HeapUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/BitUtils.h"
#include "../Utility/FastParseValue.h"
#include "../Utility/StringFormat.h"
#include <algorithm>

#pragma GCC diagnostic ignored "-Wmultichar"
namespace Assets
{
	static const uint64_t ChunkType_ArchiveDirectory = ConstHash64<'Arch', 'ive', 'Dir'>::Value;
	static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	bool TryRegisterDependency(
		const std::shared_ptr<DependencyValidation>& target,
		const DependentFileState& fileState,
		const StringSection<> assetName);
	ArtifactRequestResult MakeArtifactRequestResult(ArtifactRequest::DataType dataType, const ::Assets::Blob& blob);

	class ArchiveDirectoryBlock 
	{
	public:
		uint64_t _objectId;
		uint64_t _artifactTypeCode;
		unsigned _version;
		unsigned _start, _size;
	};

	class CompareArchiveDirectoryBlock
	{
	public:
		bool operator()(const ArchiveDirectoryBlock& lhs, uint64_t rhs) { return lhs._objectId < rhs; }
		bool operator()(uint64_t lhs, const ArchiveDirectoryBlock& rhs) { return lhs < rhs._objectId; }
		bool operator()(const ArchiveDirectoryBlock& lhs, const ArchiveDirectoryBlock& rhs) { return lhs._objectId < rhs._objectId; }
	};
	
	class DirectoryChunk
	{
	public:
		unsigned _blockCount = 0;
		unsigned _spanningHeapSize = 0;
	};

	class ArchiveCache::PendingCommit
	{
	public:
		uint64_t          			_objectId = 0;
		std::vector<ICompileOperation::SerializedArtifact>    _data;
		std::vector<DependentFileState> _deps;
		unsigned        			_pendingCommitPtr = 0;      // (only used during FlushToDisk)
		std::function<void()> 		_onFlush;
		std::string					_attachedStringName;
		size_t						_totalBinarySize = 0;
	};

	class ArchiveCache::ComparePendingCommit
	{
	public:
		bool operator()(const PendingCommit& lhs, uint64_t rhs) { return lhs._objectId < rhs; }
		bool operator()(uint64_t lhs, const PendingCommit& rhs) { return lhs < rhs._objectId; }
		bool operator()(const PendingCommit& lhs, const PendingCommit& rhs) { return lhs._objectId < rhs._objectId; }
	};

	static bool IsBinaryBlock(uint64_t typeCode)
	{
		return typeCode != ChunkType_Metrics && typeCode != ChunkType_Log;
	}

	void ArchiveCache::Commit(
		uint64_t objectId,
		const std::string& attachedStringName,
		IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
		IteratorRange<const DependentFileState*> dependentFiles,
		std::function<void()>&& onFlush)
	{
		for (const auto&a:artifacts)
			if (!a._data || a._data->empty())
				Throw(std::runtime_error("One or more artifacts contain no data"));

			// for for an existing pending commit, and replace it if it exists
		ScopedLock(_pendingCommitsLock);
		auto i = std::lower_bound(_pendingCommits.begin(), _pendingCommits.end(), objectId, ComparePendingCommit());
		if (i == _pendingCommits.end() || i->_objectId != objectId)
			i = _pendingCommits.insert(i, PendingCommit {objectId} );

		i->_data = std::vector<ICompileOperation::SerializedArtifact> { artifacts.begin(), artifacts.end() };
		i->_deps = std::vector<DependentFileState> { dependentFiles.begin(), dependentFiles.end() };
		i->_attachedStringName = attachedStringName;
		i->_onFlush = std::move(onFlush);
		i->_totalBinarySize = 0;
		for (const auto&a:i->_data)
			if (IsBinaryBlock(a._type) && a._data)
				i->_totalBinarySize += CeilToMultiplePow2(a._data->size(), 8);

		auto changeI = LowerBound(_changeIds, objectId);
		if (changeI != _changeIds.end() && changeI->first == objectId) {
			++changeI->second;
		} else {
			_changeIds.insert(changeI, {objectId, 1});
		}
	}

	static bool LoadBlockList(const utf8 filename[], std::vector<ArchiveDirectoryBlock>& blocks)
	{
		using namespace Assets::ChunkFile;
		std::unique_ptr<IFileInterface> directoryFile;
		if (MainFileSystem::TryOpen(directoryFile, filename, "rb") != MainFileSystem::IOReason::Success)
			return false;

		auto chunkTable = LoadChunkTable(*directoryFile);
		auto chunk = FindChunk(filename, chunkTable, ChunkType_ArchiveDirectory, 0);

		DirectoryChunk dirHdr;
		directoryFile->Seek(chunk._fileOffset);
		directoryFile->Read(&dirHdr, sizeof(dirHdr), 1);

		// we're going to remove any previous contents of "blocks"
		blocks.clear();
		blocks.resize(dirHdr._blockCount);
		directoryFile->Read(AsPointer(blocks.begin()), sizeof(ArchiveDirectoryBlock), dirHdr._blockCount);
		return true;
	}

	auto ArchiveCache::GetBlockList() const -> const std::vector<ArchiveDirectoryBlock>*
	{
		if (!_cachedBlockListValid) {
			// note that on failure, we will continue to attempt to open the file each time
			if (!LoadBlockList(_directoryFileName.c_str(), _cachedBlockList))
				return nullptr;

			_cachedBlockListValid = true;
		}
		return &_cachedBlockList;
	}

	static std::vector<std::pair<std::string, std::string>> TryParseStringTable(IteratorRange<const void*> data)
	{
		std::vector<std::pair<std::string, std::string>> result;
		InputStreamFormatter<char> formatter(data);

		for (;;) {
			auto next = formatter.PeekNext();
			if (next == FormatterBlob::KeyedItem) {
				StringSection<> name, value;
				if (!formatter.TryKeyedItem(name) || !formatter.TryValue(value))
					break;
				result.push_back({name.AsString(), value.AsString()});
			} else {
				break;	// break on any error
			}
		}

		return result;
	}

	static std::vector<std::pair<uint64_t, DependentFileState>> TryParseDependenciesTable(IteratorRange<const void*> data)
	{
		std::vector<std::pair<uint64_t, DependentFileState>> result;
		InputStreamFormatter<char> formatter(data);

		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			StringSection<> eleName;
			if (!formatter.TryKeyedItem(eleName))
				return result;	// break on any error
				
			if (!formatter.TryBeginElement())
				return result;	// break on any error

			uint64_t objectId = 0;
			auto end = FastParseValue(eleName, objectId, 16);
			if (end != eleName.end())
				return result;	// break on any error

			while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
				StringSection<> name, value;
				if (!formatter.TryKeyedItem(name) || !formatter.TryValue(value))
					return result;

				uint64_t timeCode = 0;
				auto end = FastParseValue(value, timeCode, 16);
				if (end != value.end())
					return result;	// break on any error

				result.push_back({objectId, DependentFileState{name, timeCode}});
			}

			if (!formatter.TryEndElement())
				return result;
		}

		return result;

	}

	auto ArchiveCache::GetDependencyTable() const -> const DependencyTable*
	{
		if (!_cachedDependencyTableValid) {
			utf8 depsFilename[MaxPath];
			XlCopyString(depsFilename, _mainFileName);
			XlCatString(depsFilename, ".deps");
			
			size_t existingFileSize = 0;
			auto existingFile = ::Assets::TryLoadFileAsMemoryBlock(depsFilename, &existingFileSize);
			_cachedDependencyTable = TryParseDependenciesTable(MakeIteratorRange(existingFile.get(), PtrAdd(existingFile.get(), existingFileSize)));
			_cachedDependencyTableValid = true;
		}
		return &_cachedDependencyTable;
	}

	void ArchiveCache::FlushToDisk()
	{
		ScopedLock(_pendingCommitsLock);
		if (_pendingCommits.empty()) { return; }

		_cachedBlockListValid = false;
		_cachedDependencyTableValid = false;

			// 1.   Open the directory and initialize our heap
			//      representation
			// 2.   Find older versions of the same blocks we
			//      want to write
			// 3.   Deallocate all blocks that have changed size
			// 4.   Allocate new blocks as required
			// 5.   Open the data file and write all the new blocks
			//      to disk
			// 6.   Flush out the new directory file
			//
			//  Note that the table of blocks is stored in order of id (for fast
			//  searches) not in the order that they appear in the file.

		using namespace Assets::ChunkFile;
		{
			DirectoryChunk dirHdr;
			std::vector<ArchiveDirectoryBlock> blocks;
			std::unique_ptr<uint8[]> flattenedSpanningHeap;
		
			std::unique_ptr<IFileInterface> directoryFile;
			bool directoryFileOpened = false;

			// using a soft "TryOpen" to prevent annoying exception messages when the file is being created for the first time
			if (MainFileSystem::TryOpen(directoryFile, _directoryFileName.c_str(), "r+b") == MainFileSystem::IOReason::Success) {
				TRY {
					auto chunkTable = LoadChunkTable(*directoryFile);
					auto chunk = FindChunk(_directoryFileName.c_str(), chunkTable, ChunkType_ArchiveDirectory, 0);

					directoryFile->Seek(chunk._fileOffset);
					directoryFile->Read(&dirHdr, sizeof(dirHdr), 1);

					blocks.resize(dirHdr._blockCount);
					directoryFile->Read(AsPointer(blocks.begin()), sizeof(ArchiveDirectoryBlock), dirHdr._blockCount);
					flattenedSpanningHeap = std::make_unique<uint8[]>(dirHdr._spanningHeapSize);
					directoryFile->Read(flattenedSpanningHeap.get(), 1, dirHdr._spanningHeapSize);
					directoryFileOpened = true;
				} CATCH (...) {
					// We can get format errors while reading. In this case, will just overwrite the file
				} CATCH_END
			}

			if (!directoryFileOpened) {
				directoryFile.reset();
				directoryFile = MainFileSystem::OpenFileInterface(_directoryFileName.c_str(), "wb");
			}

			SpanningHeap<uint32> spanningHeap(flattenedSpanningHeap.get(), dirHdr._spanningHeapSize);
			for (auto i=_pendingCommits.begin(); i!=_pendingCommits.end(); ++i) {
				i->_pendingCommitPtr = ~unsigned(0x0);

				// find an existing blocks with the same id. We'll deallocate all of these from the 
				// spanning heap, to free up some space
				auto range = std::equal_range(blocks.cbegin(), blocks.cend(), i->_objectId, CompareArchiveDirectoryBlock{});
				for (auto b=range.first; b!=range.second; ++b)
						// todo -- Is it useful to just reuse the same block here?
					spanningHeap.Deallocate(b->_start, b->_size);
				blocks.erase(range.first, range.second);
			}

			// Allocate space for new blocks. Allocate from largest to smallest
			// We always allocate blocks for every artifact from the same object contiguously
			std::sort(_pendingCommits.begin(), _pendingCommits.end(), 
				[](const PendingCommit& lhs, const PendingCommit& rhs) { return lhs._totalBinarySize > rhs._totalBinarySize; });
			for (auto i=_pendingCommits.begin(); i!=_pendingCommits.end(); ++i) {
				if (i->_pendingCommitPtr == ~unsigned(0x0)) {

						// we need to allocate a new block
					auto newBlockSize = (unsigned)i->_totalBinarySize;
					
					#if !defined(NDEBUG)
						auto originalHeapSize = spanningHeap.CalculateHeapSize();
						auto originalAllocatedSize = spanningHeap.CalculateAllocatedSpace();
					#endif

					i->_pendingCommitPtr = spanningHeap.Allocate(newBlockSize);
					if (i->_pendingCommitPtr == ~unsigned(0x0)) {
						i->_pendingCommitPtr = spanningHeap.AppendNewBlock(newBlockSize);
					}

					assert(spanningHeap.CalculateAllocatedSpace() >= (originalAllocatedSize + newBlockSize));
					assert(spanningHeap.CalculateHeapSize() >= originalHeapSize);

						// make sure we're not overlapping another block (just to make sure the allocators are working)
					#if !defined(NDEBUG)
						for (auto b=blocks.cbegin(); b!=blocks.cend(); ++b) {
							assert((b->_start + b->_size) <= originalHeapSize);
							assert(
								((i->_pendingCommitPtr + newBlockSize) <= b->_start)
								|| (i->_pendingCommitPtr >= (b->_start + b->_size)));
						}
					#endif

					auto b = std::lower_bound(blocks.begin(), blocks.end(), i->_objectId, CompareArchiveDirectoryBlock{});
					assert(b==blocks.cend() || b->_objectId != i->_objectId);
					unsigned artifactIterator = i->_pendingCommitPtr;
					for (const auto&a:i->_data)
						if (IsBinaryBlock(a._type) && a._data) {
							ArchiveDirectoryBlock newBlock = { i->_objectId, a._type, a._version, artifactIterator, (unsigned)a._data->size() };
							b=blocks.insert(b, newBlock);
							artifactIterator += CeilToMultiplePow2(a._data->size(), 8);
						}
				}
			}

				//  everything is allocated... we need to write the blocks to the data file
				//  sort by pending commit ptr for convenience
			std::sort(_pendingCommits.begin(), _pendingCommits.end(), 
				[](const PendingCommit& lhs, const PendingCommit& rhs) { return lhs._pendingCommitPtr < rhs._pendingCommitPtr; });
			{
				OSServices::BasicFile dataFile;
				bool good = MainFileSystem::TryOpen(dataFile, _mainFileName.c_str(), "r+b") == MainFileSystem::IOReason::Success;
				if (!good)
					dataFile = MainFileSystem::OpenBasicFile(_mainFileName.c_str(), "wb");
				for (auto i=_pendingCommits.begin(); i!=_pendingCommits.end(); ++i) {
					dataFile.Seek(i->_pendingCommitPtr);

					unsigned artifactIterator = i->_pendingCommitPtr;
					for (const auto&a:i->_data)
						if (IsBinaryBlock(a._type) && a._data) {
							dataFile.Write(a._data->data(), 1, a._data->size());
							auto sizeWithPadding = CeilToMultiplePow2(a._data->size(), 8);
							auto padding = sizeWithPadding - a._data->size();
							if (padding) {
								uint8_t filler[8] = { 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd };
								dataFile.Write(filler, 1, padding);
							}
							artifactIterator += sizeWithPadding;
						}
				}
			}

				// write the new directory file (including the blocks list and spanning heap)
			{
				ChunkFileHeader fileHeader;
				XlZeroMemory(fileHeader);
				fileHeader._magic = MagicHeader;
				fileHeader._fileVersionNumber = 0;
				XlCopyString(fileHeader._buildVersion, dimof(fileHeader._buildVersion), MakeStringSection(_buildVersionString));
				XlCopyString(fileHeader._buildDate, dimof(fileHeader._buildDate), MakeStringSection(_buildDateString));
				fileHeader._chunkCount = 1;

				auto flattenedHeap = spanningHeap.Flatten();
			
				ChunkHeader chunkHeader(
					ChunkType_ArchiveDirectory, 0, "ArchiveCache", unsigned(sizeof(DirectoryChunk) + blocks.size() * sizeof(ArchiveDirectoryBlock) + flattenedHeap.second));
				chunkHeader._fileOffset = sizeof(ChunkFileHeader) + sizeof(ChunkHeader);

				DirectoryChunk chunkData;
				chunkData._blockCount = (unsigned)blocks.size();
				chunkData._spanningHeapSize = (unsigned)flattenedHeap.second;

					// Write blank header data to the file
					//  note that there's a potential problem here, because
					//  we don't truncate the file before writing this. So if there is
					//  some data in there (but invalid data), then it will remain in the file
				directoryFile->Seek(0);
				directoryFile->Write(&fileHeader, sizeof(fileHeader), 1);
				directoryFile->Write(&chunkHeader, sizeof(chunkHeader), 1);
				directoryFile->Write(&chunkData, sizeof(chunkData), 1);
				directoryFile->Write(AsPointer(blocks.begin()), sizeof(ArchiveDirectoryBlock), blocks.size());
				directoryFile->Write(flattenedHeap.first.get(), 1, flattenedHeap.second);
			}
		}

		{
					//  read the old string table, and then merge it
					//  with the new. This will destroy and re-write the entire debugging file in one go.
			utf8 debugFilename[MaxPath];
			XlCopyString(debugFilename, _mainFileName);
			XlCatString(debugFilename, ".debug");
				
			// try to open an existing file -- but if there are any errors, we can just discard the
			// old contents
			size_t existingFileSize = 0;
			auto existingFile = ::Assets::TryLoadFileAsMemoryBlock(debugFilename, &existingFileSize);
			auto attachedStrings = TryParseStringTable(MakeIteratorRange(existingFile.get(), PtrAdd(existingFile.get(), existingFileSize)));

					// merge in the new strings
			for (auto i=_pendingCommits.begin(); i!=_pendingCommits.end(); ++i) {
				for (const auto&a:i->_data) {
					std::string attachedStringName;
					if (a._type == ChunkType_Metrics) attachedStringName = i->_attachedStringName + "-metrics";
					else if (a._type == ChunkType_Log) attachedStringName = i->_attachedStringName + "-log";
					else continue;

					std::string dataAsString {
						(const char*)AsPointer(a._data->begin()),
						(const char*)AsPointer(a._data->end())};

					auto c = LowerBound(attachedStrings, attachedStringName);
					if (c!=attachedStrings.end() && c->first == attachedStringName) {
						c->second = dataAsString;
					} else {
						attachedStrings.insert(c, std::make_pair(attachedStringName, dataAsString));
					}
				}
			}

					// write the new debugging file
			TRY {
				std::unique_ptr<IFileInterface> outputFile;
				if (MainFileSystem::TryOpen(outputFile, debugFilename, "wb") == MainFileSystem::IOReason::Success) {
					FileOutputStream stream(std::move(outputFile));
					OutputStreamFormatter formatter(stream);
					for (const auto&i:attachedStrings)
						formatter.WriteKeyedValue(i.first, i.second);
				}
			} CATCH (...) {
			} CATCH_END
		}

		{
					// Write the dep-val information. Like the process above,
					// we will read in the previous data before replacing it with the new dependency data
			utf8 depsFilename[MaxPath];
			XlCopyString(depsFilename, _mainFileName);
			XlCatString(depsFilename, ".deps");
			
			size_t existingFileSize = 0;
			auto existingFile = ::Assets::TryLoadFileAsMemoryBlock(depsFilename, &existingFileSize);
			auto depsData = TryParseDependenciesTable(MakeIteratorRange(existingFile.get(), PtrAdd(existingFile.get(), existingFileSize)));

			for (auto i=_pendingCommits.begin(); i!=_pendingCommits.end(); ++i) {
				auto existingRange = EqualRange(depsData, i->_objectId);

				// overwrite what we can, then insert / erase the rest
				auto i2 = i->_deps.begin();
				auto e=existingRange.first;
				for (; e!=existingRange.second && i2!=i->_deps.end(); ++e, ++i2)
					e->second = *i2;
						
				if (i2 != i->_deps.end()) {
					std::vector<std::pair<uint64_t, DependentFileState>> insertables;
					insertables.reserve(i->_deps.end() - i2);
					for (; i2!=i->_deps.end(); ++i2) insertables.push_back({i->_objectId, std::move(*i2)});
					depsData.insert(existingRange.second, insertables.begin(), insertables.end());
				} else {
					depsData.erase(e, existingRange.second);
				}
			}

					// Write the new .deps file
			TRY {
				std::unique_ptr<IFileInterface> outputFile;
				if (MainFileSystem::TryOpen(outputFile, depsFilename, "wb") == MainFileSystem::IOReason::Success) {
					FileOutputStream stream(std::move(outputFile));
					OutputStreamFormatter formatter(stream);
					for (auto i=depsData.begin(); i!=depsData.end();) {
						auto objEnd = i+1;
						while (objEnd != depsData.end() && objEnd->first == i->first) ++objEnd;

						char buffer[64];
						XlUI64toA(i->first, buffer, dimof(buffer), 16);
						auto ele = formatter.BeginKeyedElement(buffer);
						for (auto i2=i; i2!=objEnd; ++i2) {
							XlUI64toA(i2->second._timeMarker, buffer, dimof(buffer), 16);
							formatter.WriteKeyedValue(MakeStringSection(i2->second._filename), MakeStringSection(buffer));
						}
						formatter.EndElement(ele);
						i=objEnd;
					}
				}
			} CATCH (...) {
			} CATCH_END
		}

		for (const auto& i:_pendingCommits)
			if (i._onFlush)
				i._onFlush();

			// clear all pending block (now that they're flushed to disk)
		_pendingCommits.clear();
	}
	
	auto ArchiveCache::GetMetrics() const -> Metrics
	{
		using namespace Assets::ChunkFile;

			// We need to open the file and get metrics information
			// for the blocks contained within
		////////////////////////////////////////////////////////////////////////////////////
		std::vector<ArchiveDirectoryBlock> fileBlocks;
		TRY {
			auto directoryFile = MainFileSystem::OpenFileInterface(_directoryFileName.c_str(), "rb");

			auto chunkTable = LoadChunkTable(*directoryFile);
			auto chunk = FindChunk(_directoryFileName.c_str(), chunkTable, ChunkType_ArchiveDirectory, 0);

			directoryFile->Seek(chunk._fileOffset);
			DirectoryChunk dirHdr;
			directoryFile->Read(&dirHdr, sizeof(dirHdr), 1);

			fileBlocks.resize(dirHdr._blockCount);
			directoryFile->Read(AsPointer(fileBlocks.begin()), sizeof(ArchiveDirectoryBlock), dirHdr._blockCount);
		} CATCH (...) {
		} CATCH_END

		////////////////////////////////////////////////////////////////////////////////////
		std::vector<std::pair<std::string, std::string>> attachedStrings;
		TRY {
			utf8 debugFilename[MaxPath];
			XlCopyString(debugFilename, _mainFileName.c_str());
			XlCatString(debugFilename, ".debug");

			size_t debugFileSize = 0;
			auto debugFile = ::Assets::TryLoadFileAsMemoryBlock((char*)debugFilename, &debugFileSize);
			if (debugFile)
				attachedStrings = TryParseStringTable({debugFile.get(), PtrAdd(debugFile.get(), debugFileSize)});
		} CATCH (...) {
		} CATCH_END

		////////////////////////////////////////////////////////////////////////////////////
		std::vector<BlockMetrics> blocks;
		unsigned usedSpace = 0;
		for (auto b=fileBlocks.cbegin(); b!=fileBlocks.cend();) {
			auto e = b+1;
			while (e!=fileBlocks.cend() && e->_objectId == b->_objectId) ++b;

			BlockMetrics metrics;
			metrics._objectId = b->_objectId;
			metrics._offset = b->_start;

			for (const auto&i:MakeIteratorRange(b, e))
				metrics._size = i._size;

			auto s = std::find_if(
				attachedStrings.cbegin(), attachedStrings.cend(), 
				[b](const std::pair<std::string, std::string>& v) {
					return Hash64(v.first, b->_objectId);
				});
			if (s != attachedStrings.cend())
				metrics._attachedString = s->second;

			blocks.push_back(metrics);
			usedSpace += metrics._size;
			b = e;
		}

		////////////////////////////////////////////////////////////////////////////////////
		for (auto p=_pendingCommits.cbegin(); p!=_pendingCommits.cend(); ++p) {

			BlockMetrics newMetrics;
			newMetrics._objectId = p->_objectId;
			newMetrics._size = (unsigned)p->_totalBinarySize;
			newMetrics._offset = ~unsigned(0x0);

			auto b = std::find_if(blocks.begin(), blocks.end(),
				[=](const BlockMetrics& t) { return t._objectId == newMetrics._objectId; });
			if (b != blocks.end()) {
				*b = newMetrics;
			} else {
				blocks.push_back(newMetrics);
			}
		}

		////////////////////////////////////////////////////////////////////////////////////
		Metrics result;
		result._blocks = std::move(blocks);
		result._usedSpace = usedSpace;
		result._allocatedFileSize = unsigned(MainFileSystem::TryGetDesc(_mainFileName.c_str())._size);
		return result;
	}

	class ArchiveCache::ArchivedFileArtifactCollection : public ::Assets::IArtifactCollection
	{
	public:
		static void VerifyChangeId_AlreadyLocked(ArchiveCache& archiveCache, uint64_t objectId, unsigned expectedChangeId)
		{
			unsigned currentChangeId = 0;
			auto changeI = LowerBound(archiveCache._changeIds, objectId);
			if (changeI != archiveCache._changeIds.end() && changeI->first == objectId) {
				currentChangeId = changeI->second;
			} else {
				currentChangeId = 0;
			}
			if (currentChangeId != expectedChangeId)
				Throw(std::runtime_error("Object in ArchiveCache changed while attempting to read it at the same time"));
		}

		std::vector<ArtifactRequestResult> 	ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const
		{
			ScopedLock(_archiveCache->_pendingCommitsLock);
			VerifyChangeId_AlreadyLocked(*_archiveCache, _objectId, _changeId);
			auto i = std::lower_bound(_archiveCache->_pendingCommits.begin(), _archiveCache->_pendingCommits.end(), _objectId, ComparePendingCommit());
			if (i!=_archiveCache->_pendingCommits.end() && i->_objectId == _objectId)
				return ResolveViaPendingCommit(*i, requests);
			
			// There's no pending block, so just try to read it from the file on disk
			return ResolveViaArchiveFile(requests);
		}

		std::vector<ArtifactRequestResult> ResolveViaPendingCommit(const PendingCommit& pendingCommit, IteratorRange<const ArtifactRequest*> requests) const
		{
			std::vector<ArtifactRequestResult> result;
			result.reserve(requests.size());

				// First scan through and check to see if we
				// have all of the chunks we need
			for (auto r=requests.begin(); r!=requests.end(); ++r) {
				auto prevWithSameCode = std::find_if(requests.begin(), r, [r](const auto& t) { return t._type == r->_type; });
				if (prevWithSameCode != r)
					Throw(std::runtime_error("Type code is repeated multiple times in call to ResolveRequests"));

				auto i = std::find_if(
					pendingCommit._data.begin(), pendingCommit._data.end(), 
					[&r](const auto& c) { return c._type == r->_type; });
				if (i == pendingCommit._data.end())
					Throw(Exceptions::ConstructionError(
						Exceptions::ConstructionError::Reason::MissingFile,
						GetDependencyValidation(),
						StringMeld<128>() << "Missing chunk (" << r->_name << ")"));

				if (r->_expectedVersion != ~0u && (i->_version != r->_expectedVersion))
					Throw(::Assets::Exceptions::ConstructionError(
						Exceptions::ConstructionError::Reason::UnsupportedVersion,
						GetDependencyValidation(),
						StringMeld<256>() 
							<< "Data chunk is incorrect version for chunk (" 
							<< r->_name << ") expected: " << r->_expectedVersion << ", got: " << i->_version));
			}

			for (const auto& r:requests) {
				auto i = std::find_if(
					pendingCommit._data.begin(), pendingCommit._data.end(), 
					[&r](const auto& c) { return c._type == r._type; });
				assert(i != pendingCommit._data.end());
				result.emplace_back(MakeArtifactRequestResult(r._dataType, i->_data));
			}

			return result;
		}

		std::vector<ArtifactRequestResult> ResolveViaArchiveFile(IteratorRange<const ArtifactRequest*> requests) const
		{
			std::vector<ArtifactRequestResult> result;
			result.reserve(requests.size());

			const auto* blocks = _archiveCache->GetBlockList();
			if (!blocks)
				Throw(std::runtime_error("Resolve failed because the archive block list could not be generated"));

			auto range = std::equal_range(blocks->begin(), blocks->end(), _objectId, CompareArchiveDirectoryBlock{});
			if (range.first == range.second)
				Throw(std::runtime_error("Could not find any blocks associated with the given request"));

				// First scan through and check to see if we
				// have all of the chunks we need
			for (auto r=requests.begin(); r!=requests.end(); ++r) {
				auto prevWithSameCode = std::find_if(requests.begin(), r, [r](const auto& t) { return t._type == r->_type; });
				if (prevWithSameCode != r)
					Throw(std::runtime_error("Type code is repeated multiple times in call to ResolveRequests"));

				auto i = std::find_if(
					range.first, range.second, 
					[&r](const auto& c) { return c._artifactTypeCode == r->_type; });
				if (i == range.second)
					Throw(Exceptions::ConstructionError(
						Exceptions::ConstructionError::Reason::MissingFile,
						GetDependencyValidation(),
						StringMeld<128>() << "Missing chunk (" << r->_name << ")"));

				if (r->_expectedVersion != ~0u && (i->_version != r->_expectedVersion))
					Throw(::Assets::Exceptions::ConstructionError(
						Exceptions::ConstructionError::Reason::UnsupportedVersion,
						GetDependencyValidation(),
						StringMeld<256>() 
							<< "Data chunk is incorrect version for chunk (" 
							<< r->_name << ") expected: " << r->_expectedVersion << ", got: " << i->_version));
			}

			auto archiveFile = MainFileSystem::OpenFileInterface(_archiveCache->_mainFileName, "rb");

			for (const auto& r:requests) {
				auto i = std::find_if(range.first, range.second, [&r](const auto& c) { return c._artifactTypeCode == r._type; });
				assert(i != range.second);

				ArtifactRequestResult chunkResult;
				if (	r._dataType == ArtifactRequest::DataType::BlockSerializer
					||	r._dataType == ArtifactRequest::DataType::Raw) {
					uint8* mem = (uint8*)XlMemAlign(i->_size, sizeof(uint64_t));
					chunkResult._buffer = std::unique_ptr<uint8[], PODAlignedDeletor>(mem);
					chunkResult._bufferSize = i->_size;
					archiveFile->Seek(i->_start);
					archiveFile->Read(chunkResult._buffer.get(), i->_size);

					// initialize with the block serializer (if requested)
					if (r._dataType == ArtifactRequest::DataType::BlockSerializer)
						Block_Initialize(chunkResult._buffer.get());
				} else if (r._dataType == ArtifactRequest::DataType::ReopenFunction) {
					// note -- captured raw pointer
					chunkResult._reopenFunction = [offset=i->_start, archiveCache=_archiveCache, objectId=_objectId, changeId=_changeId]() -> std::shared_ptr<IFileInterface> {
						ScopedLock(archiveCache->_pendingCommitsLock);
						VerifyChangeId_AlreadyLocked(*archiveCache, objectId, changeId);
						auto archiveFile = MainFileSystem::OpenFileInterface(archiveCache->_mainFileName, "rb");
						archiveFile->Seek(offset);
						return archiveFile;
					};
				} else if (r._dataType == ArtifactRequest::DataType::SharedBlob) {
					chunkResult._sharedBlob = std::make_shared<std::vector<uint8_t>>();
					chunkResult._sharedBlob->resize(i->_size);
					archiveFile->Seek(i->_start);
					archiveFile->Read(chunkResult._sharedBlob->data(), i->_size);
				} else {
					assert(0);
				}

				result.emplace_back(std::move(chunkResult));
			}

			return result;
		}

		DepValPtr 							GetDependencyValidation() const
		{
			ScopedLock(_archiveCache->_pendingCommitsLock);
			VerifyChangeId_AlreadyLocked(*_archiveCache, _objectId, _changeId);

			auto i = std::lower_bound(_archiveCache->_pendingCommits.begin(), _archiveCache->_pendingCommits.end(), _objectId, ComparePendingCommit());
			if (i!=_archiveCache->_pendingCommits.end() && i->_objectId == _objectId)
				return AsDepVal(MakeIteratorRange(i->_deps));

			auto* depTable = _archiveCache->GetDependencyTable();
			if (!depTable) return nullptr;

			auto result = std::make_shared<::Assets::DependencyValidation>();
			auto range = EqualRange(*depTable, _objectId);
			for (auto r=range.first; r!=range.second; ++r)
				if (!TryRegisterDependency(result, r->second, "ArchivedAsset"))
					return nullptr;

			return result;
		}

		StringSection<ResChar>				GetRequestParameters() const
		{
			return {};
		}

		AssetState							GetAssetState() const
		{
			return AssetState::Ready;
		}
			
		ArchiveCache* _archiveCache = nullptr;
		uint64_t _objectId = 0;
		unsigned _changeId = 0;
	};

	auto ArchiveCache::TryOpenFromCache(uint64_t id) -> std::shared_ptr<IArtifactCollection>
	{
		auto result = std::make_shared<ArchivedFileArtifactCollection>();
		result->_archiveCache = this;
		result->_objectId = id;

		ScopedLock(_pendingCommitsLock);
		auto changeI = LowerBound(_changeIds, id);
		if (changeI != _changeIds.end() && changeI->first == id) {
			result->_changeId = changeI->second;
		} else {
			result->_changeId = 0;
		}

		return result;
	}
	
	ArchiveCache::ArchiveCache(
		StringSection<> archiveName, 
		const ConsoleRig::LibVersionDesc& versionDesc) 
	: _mainFileName(archiveName.AsString())
	, _buildVersionString(versionDesc._versionString)
	, _buildDateString(versionDesc._buildDateString)
	, _cachedBlockListValid(false)
	, _cachedDependencyTableValid(false)
	{
		_directoryFileName = _mainFileName + ".dir";

			// (make sure the directory provided exists)
		OSServices::CreateDirectoryRecursive(MakeFileNameSplitter(_mainFileName).DriveAndPath());
	}

	ArchiveCache::~ArchiveCache() 
	{
		TRY {
			FlushToDisk();
		} CATCH (const std::exception& e) {
			Log(Warning) << "Suppressing exception in ArchiveCache::~ArchiveCache: " << e.what() << std::endl;
			(void)e;
		} CATCH (...) {
			Log(Warning) << "Suppressing unknown exception in ArchiveCache::~ArchiveCache." << std::endl;
		} CATCH_END
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<::Assets::ArchiveCache> ArchiveCacheSet::GetArchive(StringSection<> archiveFilename)
	{
		auto hashedName = HashFilenameAndPath(archiveFilename);

		ScopedLock(_archivesLock);
		auto existing = LowerBound(_archives, hashedName);
		if (existing != _archives.cend() && existing->first == hashedName)
			return existing->second;

		auto newArchive = std::make_shared<::Assets::ArchiveCache>(archiveFilename, _versionDesc);
		_archives.insert(existing, std::make_pair(hashedName, newArchive));
		return newArchive;
	}

	void ArchiveCacheSet::FlushToDisk()
	{
		ScopedLock(_archivesLock);
		for (const auto&a:_archives)
			a.second->FlushToDisk();
	}

	ArchiveCacheSet::ArchiveCacheSet(const ConsoleRig::LibVersionDesc& versionDesc) : _versionDesc(versionDesc) {}
	ArchiveCacheSet::~ArchiveCacheSet() {}
}

