// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ChunkFileContainer.h"
#include "ChunkFile.h"
#include "BlockSerializer.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "MemoryFile.h"
#include "../Utility/StringFormat.h"
#include "../Core/Exceptions.h"

namespace Assets
{
    std::vector<ArtifactRequestResult> ChunkFileContainer::ResolveRequests(
        IteratorRange<const ArtifactRequest*> requests) const
    {
		auto file = OpenFile();
        return ResolveRequests(*file, requests);
    }

	std::shared_ptr<IFileInterface> ChunkFileContainer::OpenFile() const
	{
		std::shared_ptr<IFileInterface> result;
		if (_blob)
			return CreateMemoryFile(_blob);
		return MainFileSystem::OpenFileInterface(_filename.c_str(), "rb");
	}

    std::vector<ArtifactRequestResult> ChunkFileContainer::ResolveRequests(
        IFileInterface& file, IteratorRange<const ArtifactRequest*> requests) const
    {
        auto initialOffset = file.TellP();
        auto chunks = ChunkFile::LoadChunkTable(file);
        
        std::vector<ArtifactRequestResult> result;
        result.reserve(requests.size());

            // First scan through and check to see if we
            // have all of the chunks we need
        using ChunkHeader = ChunkFile::ChunkHeader;
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
                uint8* mem = (uint8*)XlMemAlign(i->_size, sizeof(uint64_t));
                chunkResult._buffer = std::unique_ptr<uint8[], PODAlignedDeletor>(mem);
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
				chunkResult._reopenFunction = [offset, blobCopy, filenameCopy, depValCopy, initialOffset]() -> std::shared_ptr<IFileInterface> {
					TRY {
						std::shared_ptr<IFileInterface> result;
						if (blobCopy) {
							result = CreateMemoryFile(blobCopy);
						} else 
							result = MainFileSystem::OpenFileInterface(filenameCopy.c_str(), "rb");
						result->Seek(initialOffset + offset);
						return result;
					} CATCH (const std::exception& e) {
						Throw(Exceptions::ConstructionError(e, depValCopy));
					} CATCH_END
				};
			} else if (r._dataType == ArtifactRequest::DataType::SharedBlob) {
                chunkResult._sharedBlob = std::make_shared<std::vector<uint8_t>>();
                chunkResult._sharedBlob->resize(i->_size);
                file.Seek(initialOffset + i->_fileOffset);
                file.Read(chunkResult._sharedBlob->data(), i->_size);
            } else {
                assert(0);
            }

            result.emplace_back(std::move(chunkResult));
        }

        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    ChunkFileContainer::ChunkFileContainer(std::string assetTypeName, DependencyValidation depVal)
    : _filename(std::move(assetTypeName))
    , _validationCallback(std::move(depVal))
    {}
    ChunkFileContainer::ChunkFileContainer(StringSection<> assetTypeName)
    : _filename(assetTypeName.AsString())
    {
		_validationCallback = GetDepValSys().Make(_filename);
    }

	ChunkFileContainer::ChunkFileContainer(const Blob& blob, const DependencyValidation& depVal, StringSection<ResChar>)
	: _filename("<<in memory>>")
	, _blob(blob), _validationCallback(depVal)
	{			
	}

	ChunkFileContainer::ChunkFileContainer() {}
    ChunkFileContainer::~ChunkFileContainer() {}

}

