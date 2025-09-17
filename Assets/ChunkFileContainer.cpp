// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ChunkFileContainer.h"
#include "ChunkFileWriter.h"
#include "BlockSerializer.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "MemoryFile.h"
#include "../Utility/StringFormat.h"
#include "../Core/Exceptions.h"

using namespace Utility::Literals;

namespace Assets
{
    std::vector<ArtifactRequestResult> ArtifactChunkContainer::ResolveRequests(
        IteratorRange<const ArtifactRequest*> requests) const
    {
		auto file = OpenFile();
        return ResolveRequests(*file, requests);
    }

	static std::unique_ptr<IFileInterface> OpenFileInterface(IFileSystem& filesystem, StringSection<> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		std::unique_ptr<IFileInterface> result;
		auto ioResult = TryOpen(result, filesystem, fn, openMode, shareMode);
		if (ioResult != MainFileSystem::IOReason::Success || !result)
			Throw(std::runtime_error("Failed to open file in loose files cache: " + fn.AsString()));
		return result;
	}

	std::shared_ptr<IFileInterface> ArtifactChunkContainer::OpenFile() const
	{
		std::shared_ptr<IFileInterface> result;
		if (_blob)
			return CreateMemoryFile(_blob);
        if (_fs)
		    return OpenFileInterface(*_fs, _filename.c_str(), "rb", OSServices::FileShareMode::Read);
        return MainFileSystem::OpenFileInterface(_filename.c_str(), "rb", OSServices::FileShareMode::Read);
	}

    std::vector<ArtifactRequestResult> ArtifactChunkContainer::ResolveRequests(
        IFileInterface& file, IteratorRange<const ArtifactRequest*> requests) const
    {
        auto initialOffset = file.TellP();
        auto chunks = LoadChunkTable(file);
        
        std::vector<ArtifactRequestResult> result;
        result.reserve(requests.size());

            // First scan through and check to see if we
            // have all of the chunks we need
        for (auto r=requests.begin(); r!=requests.end(); ++r) {
            auto prevWithSameCode = std::find_if(requests.begin(), r, [r](const auto& t) { return t._chunkTypeCode == r->_chunkTypeCode; });
            if (prevWithSameCode != r)
                Throw(std::runtime_error("Type code is repeated multiple times in call to ResolveRequests"));

            auto i = std::find_if(
                chunks.begin(), chunks.end(), 
                [r](const ChunkHeader& c) { return c._chunkTypeCode == r->_chunkTypeCode; });
            if (i == chunks.end())
                Throw(Exceptions::ConstructionError(
					Exceptions::ConstructionError::Reason::MissingFile,
					_validationCallback,
                    StringMeld<128>() << "Missing chunk (" << r->_name << ") in (" << _filename << ")"));

            if (r->_expectedVersion != ~0u && (i->_chunkVersion != r->_expectedVersion))
                Throw(::Assets::Exceptions::ConstructionError(
					Exceptions::ConstructionError::Reason::UnsupportedVersion,
					_validationCallback,
                    StringMeld<256>() 
                        << "Data chunk is incorrect version for chunk (" 
                        << r->_name << ") expected: " << r->_expectedVersion << ", got: " << i->_chunkVersion << " in (" << _filename << ")"));
        }

        for (const auto& r:requests) {
            auto i = std::find_if(
                chunks.begin(), chunks.end(), 
                [&r](const ChunkHeader& c) { return c._chunkTypeCode == r._chunkTypeCode; });
            assert(i != chunks.end());

            ArtifactRequestResult chunkResult;
            if (	r._dataType == ArtifactRequest::DataType::BlockSerializer
				||	r._dataType == ArtifactRequest::DataType::Raw) {
                uint8_t* mem = (uint8_t*)XlMemAlign(i->_size, sizeof(uint64_t));
                chunkResult._buffer = std::unique_ptr<uint8_t[], PODAlignedDeletor>(mem);
                chunkResult._bufferSize = i->_size;
                file.Seek(initialOffset + i->_fileOffset);
                file.Read(chunkResult._buffer.get(), i->_size);

                // initialize with the block serializer (if requested)
                if (r._dataType == ArtifactRequest::DataType::BlockSerializer)
                    Block_Initialize(chunkResult._buffer.get());
            } else if (r._dataType == ArtifactRequest::DataType::ReopenFunction) {
                assert(!_filename.empty());
				auto offset = i->_fileOffset;
				auto blobCopy = _blob;
				auto filenameCopy = _filename;
				auto depValCopy = _validationCallback;
                if (_fs) {
                    chunkResult._reopenFunction = [offset, blobCopy, fs=std::weak_ptr<IFileSystem>(_fs), filenameCopy, depValCopy, initialOffset]() -> std::shared_ptr<IFileInterface> {
                        TRY {
                            auto l = fs.lock();
                            if (!l) Throw(std::runtime_error("Artifact filesystem expired in reopen function"));

                            std::shared_ptr<IFileInterface> result;
                            if (blobCopy) {
                                result = CreateMemoryFile(blobCopy);
                            } else 
                                result = OpenFileInterface(*l, filenameCopy.c_str(), "rb", OSServices::FileShareMode::Read);
                            result->Seek(initialOffset + offset);
                            return result;
                        } CATCH (const std::exception& e) {
                            Throw(Exceptions::ConstructionError(e, depValCopy));
                        } CATCH_END
                    };
                } else {
                    chunkResult._reopenFunction = [offset, blobCopy, filenameCopy, depValCopy, initialOffset]() -> std::shared_ptr<IFileInterface> {
                        TRY {
                            std::shared_ptr<IFileInterface> result;
                            if (blobCopy) {
                                result = CreateMemoryFile(blobCopy);
                            } else 
                                result = MainFileSystem::OpenFileInterface(filenameCopy.c_str(), "rb", OSServices::FileShareMode::Read);
                            result->Seek(initialOffset + offset);
                            return result;
                        } CATCH (const std::exception& e) {
                            Throw(Exceptions::ConstructionError(e, depValCopy));
                        } CATCH_END
                    };
                }
			} else if (r._dataType == ArtifactRequest::DataType::SharedBlob || r._dataType == ArtifactRequest::DataType::OptionalSharedBlob) {
                chunkResult._sharedBlob = std::make_shared<std::vector<uint8_t>>();
                chunkResult._sharedBlob->resize(i->_size);
                file.Seek(initialOffset + i->_fileOffset);
                file.Read(chunkResult._sharedBlob->data(), i->_size);
            } else {
                UNREACHABLE();
            }

            result.emplace_back(std::move(chunkResult));
        }

        file.Seek(initialOffset);

        return result;
    }

    const DirectorySearchRules& ArtifactChunkContainer::GetDirectorySearchRules(IFileInterface& file) const
    {
        if (!_cachedDirectorySearchRules) {
            auto initialOffset = file.TellP();
            auto chunks = LoadChunkTable(file);

            auto i = std::find_if(
                chunks.begin(), chunks.end(), 
                [](const ChunkHeader& c) { return c._chunkTypeCode == "DirectorySearchRules"_h; });
            if (i!=chunks.end()) {
                uint8_t* mem = (uint8_t*)XlMemAlign(i->_size, sizeof(uint64_t));
                auto buffer = std::unique_ptr<uint8_t[], PODAlignedDeletor>(mem);
                file.Seek(initialOffset + i->_fileOffset);
                file.Read(buffer.get(), i->_size);
                *_cachedDirectorySearchRules = DirectorySearchRules::Deserialize(MakeIteratorRange(buffer.get(), PtrAdd(buffer.get(), i->_size)));
            } else {
                *_cachedDirectorySearchRules = {};
            }

            file.Seek(initialOffset);
        }

        return *_cachedDirectorySearchRules;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    ArtifactChunkContainer::ArtifactChunkContainer(std::shared_ptr<IFileSystem> fs, std::string assetTypeName, DependencyValidation depVal)
    : _fs(std::move(fs))
    , _filename(std::move(assetTypeName))
    , _validationCallback(std::move(depVal))
    {}
    ArtifactChunkContainer::ArtifactChunkContainer(std::shared_ptr<IFileSystem> fs, StringSection<> assetTypeName)
    : _fs(std::move(fs))
    , _filename(assetTypeName.AsString())
    {
		_validationCallback = GetDepValSys().Make(_filename);
    }

	ArtifactChunkContainer::ArtifactChunkContainer(const Blob& blob, const DirectorySearchRules& searchRules, const DependencyValidation& depVal, StringSection<ResChar>)
	: _filename("<<in memory>>")
	, _blob(blob), _validationCallback(depVal)
    , _cachedDirectorySearchRules(searchRules)
	{			
	}

	ArtifactChunkContainer::ArtifactChunkContainer() {}
    ArtifactChunkContainer::~ArtifactChunkContainer() {}

}

