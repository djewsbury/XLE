// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IAsyncMarker.h"
#include "ICompileOperation.h"
#include "AssetsCore.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include <memory>
#include <functional>
#include <future>

namespace Assets
{
	class IFileInterface;
	using ArtifactReopenFunction = std::function<std::shared_ptr<IFileInterface>()>;

    class ArtifactRequest
    {
    public:
		const char*		_name;		// for debugging purposes, to make it easier to track requests
        uint64_t 		_chunkTypeCode;
        unsigned        _expectedVersion;
        
        enum class DataType
        {
            ReopenFunction, 
			Raw, BlockSerializer,
			SharedBlob,
			Filename
        };
        DataType        _dataType;
    };

    class ArtifactRequestResult
    {
    public:
        std::unique_ptr<uint8[], PODAlignedDeletor> _buffer;
        size_t                                      _bufferSize = 0;
		Blob										_sharedBlob;
		ArtifactReopenFunction						_reopenFunction;
		std::string 								_artifactFilename;
    };

	class IArtifactCollection
	{
	public:
		virtual std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const = 0;
		virtual DependencyValidation 	GetDependencyValidation() const = 0;
		virtual StringSection<ResChar>	GetRequestParameters() const = 0;		// these are parameters that should be passed through to the asset when it's actually loaded from the blob
		virtual AssetState				GetAssetState() const = 0;
		virtual ~IArtifactCollection();
	};

	Blob GetErrorMessage(const IArtifactCollection&);

    /// <summary>Records the state of a resource being compiled</summary>
    /// When a resource compile operation begins, we need some generic way
    /// to test it's state. We also need some breadcrumbs to find the final 
    /// result when the compile is finished.
    ///
    /// This class acts as a bridge between the compile operation and
    /// the final resource class. Therefore, we can interchangeable mix
    /// and match different resource implementations and different processing
    /// solutions.
    ///
    /// Sometimes just a filename to the processed resource will be enough.
    /// Other times, objects are stored in a "ArchiveCache" object. For example,
    /// shader compiles are typically combined together into archives of a few
    /// different configurations. So a pointer to an optional ArchiveCache is provided.
    class ArtifactCollectionFuture : public IAsyncMarker
    {
    public:
		const std::shared_ptr<IArtifactCollection>& GetArtifactCollection(ArtifactTargetCode);

		using ArtifactCollectionSet = std::vector<std::pair<ArtifactTargetCode, std::shared_ptr<IArtifactCollection>>>;
		auto ShareFuture() -> std::shared_future<ArtifactCollectionSet>;

        ArtifactCollectionFuture();
        ~ArtifactCollectionFuture();

		ArtifactCollectionFuture(ArtifactCollectionFuture&&) = delete;
		ArtifactCollectionFuture& operator=(ArtifactCollectionFuture&&) = delete;
		ArtifactCollectionFuture(const ArtifactCollectionFuture&) = delete;
		ArtifactCollectionFuture& operator=(const ArtifactCollectionFuture&) = delete;

		void SetArtifactCollections(IteratorRange<const std::pair<ArtifactTargetCode, std::shared_ptr<IArtifactCollection>>*> artifacts);
		void StoreException(std::exception_ptr);
		const char* GetDebugLabel() const;  // GetDebugLabel only provided in debug builds, and only intended for debugging
		void SetDebugLabel(StringSection<char> initializer);

		virtual AssetState		            GetAssetState() const override;
		virtual std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const override;

	private:
		std::promise<ArtifactCollectionSet> _promise;
		std::shared_future<ArtifactCollectionSet> _rootSharedFuture;
		DEBUG_ONLY(ResChar _initializer[MaxPath];)
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

	class ChunkFileArtifactCollection : public IArtifactCollection
	{
	public:
		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const override;
		DependencyValidation GetDependencyValidation() const override;
		StringSection<ResChar> GetRequestParameters() const override;
		AssetState GetAssetState() const override;
		ChunkFileArtifactCollection(
			const std::shared_ptr<IFileInterface>& file,
			const DependencyValidation& depVal,
			const std::string& requestParameters = {});
		~ChunkFileArtifactCollection();
	private:
		std::shared_ptr<IFileInterface> _file;
		DependencyValidation _depVal;
		std::string _requestParameters;
	};

	class BlobArtifactCollection : public IArtifactCollection
	{
	public:
		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const override;
		DependencyValidation GetDependencyValidation() const override;
		StringSection<ResChar> GetRequestParameters() const override;
		AssetState GetAssetState() const override;
		BlobArtifactCollection(
			IteratorRange<const ICompileOperation::SerializedArtifact*> chunks, 
			AssetState state,
			const DependencyValidation& depVal, 
			const std::string& collectionName = {},
			const std::string& requestParams = {});
		~BlobArtifactCollection();
	private:
		std::vector<ICompileOperation::SerializedArtifact> _chunks;
		AssetState _state;
		DependencyValidation _depVal;
		std::string _collectionName;
		std::string _requestParams;
	};

	class CompilerExceptionArtifact : public ::Assets::IArtifactCollection
	{
	public:
		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const override;
		DependencyValidation GetDependencyValidation() const override;
		StringSection<::Assets::ResChar> GetRequestParameters() const override;
		AssetState GetAssetState() const override;
		CompilerExceptionArtifact(
			const ::Assets::Blob& log,
			const ::Assets::DependencyValidation& depVal);
		~CompilerExceptionArtifact();
	private:
		::Assets::Blob _log;
		::Assets::DependencyValidation _depVal;
	};

}


