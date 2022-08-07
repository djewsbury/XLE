// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IArtifact.h"
#include "AssetsCore.h"
#include "IArtifact.h"
#include "BlockSerializer.h"
#include "ChunkFileContainer.h"
#include "MemoryFile.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Core/Exceptions.h"

namespace Assets 
{
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	Blob GetErrorMessage(const IArtifactCollection& collection)
	{
		// Try to find an artifact named with the type "ChunkType_Log"
		TRY {
			ArtifactRequest requests[] = {
				ArtifactRequest { "log", ChunkType_Log, 0, ArtifactRequest::DataType::SharedBlob }
			};
			auto resRequests = collection.ResolveRequests(MakeIteratorRange(requests));
			if (resRequests.empty())
				return nullptr;
			return resRequests[0]._sharedBlob;
		} CATCH (...) {
			return nullptr;
		} CATCH_END
	}
	
	IArtifactCollection::~IArtifactCollection() {}

	const IArtifactCollection& ArtifactCollectionFuture::GetArtifactCollection() const
	{
		const auto& collections = _rootSharedFuture->get();
		for (const auto&col:collections)
			if (col.first == _targetCode)
				return *col.second;
		Throw(std::runtime_error("No artifact collection of the requested type was found"));
	}

	std::shared_ptr<IArtifactCollection> ArtifactCollectionFuture::GetArtifactCollectionPtr() const
	{
		const auto& collections = _rootSharedFuture->get();
		for (const auto&col:collections)
			if (col.first == _targetCode)
				return col.second;
		Throw(std::runtime_error("No artifact collection of the requested type was found"));
	}

	AssetState ArtifactCollectionFuture::GetAssetState() const
	{
		// Unfortunately we can't implement this safely and efficiently without a lot of extra
		// infrastructure in this class
		auto s = _rootSharedFuture->wait_for(std::chrono::seconds(0));
		if (s == std::future_status::timeout)
			return AssetState::Pending;
		TRY {
			const auto& collections = _rootSharedFuture->get();
			for (const auto&col:collections)
				if (col.first == _targetCode)
					return AssetState::Ready;
			return AssetState::Invalid;	// didn't find the artifact requested, considered invalid
		} CATCH(...) {
			return AssetState::Invalid;
		} CATCH_END
	}

	std::optional<AssetState> ArtifactCollectionFuture::StallWhilePending(std::chrono::microseconds timeout) const
	{
		if (timeout.count() == 0) {
			_rootSharedFuture->wait();
			return AssetState::Ready;		// we don't know if it's invalid or ready at this point
		} else {
			auto s = _rootSharedFuture->wait_for(timeout);
			if (s == std::future_status::ready) return AssetState::Ready;
			return {};
		}
	}

    const char* ArtifactCollectionFuture::GetDebugLabel() const
    {
        #if defined(_DEBUG)
            return _initializer.c_str();
        #else
            return "";
        #endif
    }

    void ArtifactCollectionFuture::SetDebugLabel(StringSection<> initializer)
    {
        DEBUG_ONLY(_initializer = initializer.AsString());
    }

	ArtifactCollectionFuture::ArtifactCollectionFuture(std::shared_ptr<std::shared_future<ArtifactCollectionSet>> rootSharedFuture, ArtifactTargetCode targetCode)
	: _rootSharedFuture(std::move(rootSharedFuture)), _targetCode(targetCode)
	{
	}
	ArtifactCollectionFuture::ArtifactCollectionFuture()
	: _targetCode(0) {}
	ArtifactCollectionFuture::~ArtifactCollectionFuture()  {}

			////////////////////////////////////////////////////////////

	DependencyValidation ChunkFileArtifactCollection::GetDependencyValidation() const { return _depVal; }
	StringSection<ResChar>	ChunkFileArtifactCollection::GetRequestParameters() const { return MakeStringSection(_requestParameters); }
	std::vector<ArtifactRequestResult> ChunkFileArtifactCollection::ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const
	{
		ChunkFileContainer chunkFile;
		return chunkFile.ResolveRequests(*_file, requests);
	}
	AssetState ChunkFileArtifactCollection::GetAssetState() const { return AssetState::Ready; }
	ChunkFileArtifactCollection::ChunkFileArtifactCollection(
		const std::shared_ptr<IFileInterface>& file, const DependencyValidation& depVal, const std::string& requestParameters)
	: _file(file), _depVal(depVal), _requestParameters(requestParameters) {}
	ChunkFileArtifactCollection::~ChunkFileArtifactCollection() {}

	ArtifactRequestResult MakeArtifactRequestResult(ArtifactRequest::DataType dataType, const ::Assets::Blob& blob)
	{
		ArtifactRequestResult chunkResult;
		if (	dataType == ArtifactRequest::DataType::BlockSerializer
			||	dataType == ArtifactRequest::DataType::Raw) {
			uint8_t* mem = (uint8*)XlMemAlign(blob->size(), sizeof(uint64_t));
			chunkResult._buffer = std::unique_ptr<uint8_t[], PODAlignedDeletor>(mem);
			chunkResult._bufferSize = blob->size();
			std::memcpy(mem, blob->data(), blob->size());

			// initialize with the block serializer (if requested)
			if (dataType == ArtifactRequest::DataType::BlockSerializer)
				Block_Initialize(chunkResult._buffer.get());
		} else if (dataType == ArtifactRequest::DataType::ReopenFunction) {
			chunkResult._reopenFunction = [blobCopy=blob]() -> std::shared_ptr<IFileInterface> {
				return CreateMemoryFile(blobCopy);
			};
		} else if (dataType == ArtifactRequest::DataType::SharedBlob) {
			chunkResult._sharedBlob = blob;
		} else {
			assert(0);
		}
		return chunkResult;
	}

	std::vector<ArtifactRequestResult> BlobArtifactCollection::ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const
	{
		// We need to look through the list of chunks and try to match the given requests
		// This is very similar to ChunkFileContainer::ResolveRequests

		std::vector<ArtifactRequestResult> result;
		result.reserve(requests.size());

			// First scan through and check to see if we
			// have all of the chunks we need
		for (auto r=requests.begin(); r!=requests.end(); ++r) {
			auto prevWithSameCode = std::find_if(requests.begin(), r, [r](const auto& t) { return t._chunkTypeCode == r->_chunkTypeCode; });
			if (prevWithSameCode != r)
				Throw(std::runtime_error("Type code is repeated multiple times in call to ResolveRequests"));

			auto i = std::find_if(
				_chunks.begin(), _chunks.end(), 
				[&r](const auto& c) { return c._chunkTypeCode == r->_chunkTypeCode; });
			if (i == _chunks.end())
				Throw(Exceptions::ConstructionError(
					Exceptions::ConstructionError::Reason::MissingFile,
					_depVal,
					StringMeld<128>() << "Missing chunk (" << r->_name << ") in collection " << _collectionName));

			if (r->_expectedVersion != ~0u && (i->_version != r->_expectedVersion))
				Throw(::Assets::Exceptions::ConstructionError(
					Exceptions::ConstructionError::Reason::UnsupportedVersion,
					_depVal,
					StringMeld<256>() 
						<< "Data chunk is incorrect version for chunk (" 
						<< r->_name << ") expected: " << r->_expectedVersion << ", got: " << i->_version
						<< " in collection " << _collectionName));
		}

		for (const auto& r:requests) {
			auto i = std::find_if(
				_chunks.begin(), _chunks.end(), 
				[&r](const auto& c) { return c._chunkTypeCode == r._chunkTypeCode; });
			assert(i != _chunks.end());
			result.emplace_back(MakeArtifactRequestResult(r._dataType, i->_data));
		}

		return result;
	}
	DependencyValidation BlobArtifactCollection::GetDependencyValidation() const { return _depVal; }
	StringSection<ResChar>	BlobArtifactCollection::GetRequestParameters() const { return MakeStringSection(_requestParams); }
	AssetState BlobArtifactCollection::GetAssetState() const 
	{
		return _state; 
	}
	BlobArtifactCollection::BlobArtifactCollection(
		IteratorRange<const ICompileOperation::SerializedArtifact*> chunks, 
		AssetState state,
		const DependencyValidation& depVal, const std::string& collectionName, const rstring& requestParams)
	: _chunks(chunks.begin(), chunks.end()), _state(state), _depVal(depVal), _collectionName(collectionName), _requestParams(requestParams) {}
	BlobArtifactCollection::~BlobArtifactCollection() {}

	std::vector<ArtifactRequestResult> CompilerExceptionArtifact::ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const
	{
		if (requests.size() == 1 && requests[0]._chunkTypeCode == ChunkType_Log && requests[0]._dataType == ArtifactRequest::DataType::SharedBlob) {
			ArtifactRequestResult res;
			res._sharedBlob = _log;
			std::vector<ArtifactRequestResult> result;
			result.push_back(std::move(res));
			return result;
		}
		Throw(std::runtime_error("Compile operation failed with error: " + AsString(_log)));
	}
	DependencyValidation CompilerExceptionArtifact::GetDependencyValidation() const { return _depVal; }
	StringSection<::Assets::ResChar>	CompilerExceptionArtifact::GetRequestParameters() const { return {}; }
	AssetState CompilerExceptionArtifact::GetAssetState() const { return AssetState::Invalid; }
	CompilerExceptionArtifact::CompilerExceptionArtifact(const ::Assets::Blob& log, const DependencyValidation& depVal) : _log(log), _depVal(depVal) {}
	CompilerExceptionArtifact::~CompilerExceptionArtifact() {}

}

