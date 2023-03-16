// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#if defined(__CLR_VER)
	#error This file cannot be included in CLR builds
#endif

#include "ChunkFileContainer.h"
#include "IAsyncMarker.h"
#include "AssetsCore.h"
#include "Continuation.h"
#include "ContinuationUtil.h"
#include "OperationContext.h"
#include "AssetTraits.h"
#include "InitializerPack.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include <memory>
#include <functional>
#include <future>

namespace Assets
{

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
		const IArtifactCollection& GetArtifactCollection() const;
		std::shared_ptr<IArtifactCollection> GetArtifactCollectionPtr() const;

		using ArtifactCollectionSet = std::vector<std::pair<ArtifactTargetCode, std::shared_ptr<IArtifactCollection>>>;
		bool Valid() const { return _rootSharedFuture != nullptr; }
		std::shared_future<ArtifactCollectionSet> ShareFuture() { return *_rootSharedFuture; }

        ArtifactCollectionFuture(std::shared_ptr<std::shared_future<ArtifactCollectionSet>> rootSharedFuture, ArtifactTargetCode targetCode);
		ArtifactCollectionFuture();
        ~ArtifactCollectionFuture();

		ArtifactCollectionFuture(ArtifactCollectionFuture&&) = default;
		ArtifactCollectionFuture& operator=(ArtifactCollectionFuture&&) = default;
		ArtifactCollectionFuture(const ArtifactCollectionFuture&) = default;
		ArtifactCollectionFuture& operator=(const ArtifactCollectionFuture&) = default;

		const char* GetDebugLabel() const;  // GetDebugLabel only provided in debug builds, and only intended for debugging
		void SetDebugLabel(StringSection<> initializer);

		virtual AssetState		            GetAssetState() const override;
		virtual std::optional<AssetState>   StallWhilePending(std::chrono::microseconds timeout = std::chrono::microseconds(0)) const override;

	private:
		// awkwardly we use shared ptrs to a shared future, because we need to track the reference counts
		// with a weak ptr in the compiler infrastructure
		std::shared_ptr<std::shared_future<ArtifactCollectionSet>> _rootSharedFuture;
		ArtifactTargetCode _targetCode;
		DEBUG_ONLY(std::string _initializer;)
    };

	/// <summary>Returned from a IAssetCompiler on response to a compile request</summary>
	/// After receiving a compile marker, the caller can choose to either attempt to 
	/// retrieve an existing artifact from a previous compile, or begin a new 
	/// asynchronous compile operation.
	/// GetArtifact() will retrieve and existing, but if it can't be found, or is out of
	///		date, will start a new compile
	/// InvokeCompile() will always begin a new compile, even if there's a valid completed
	///		artifact. If the same compile has been begun by another caller during this same
	///		session, then there is a chance that the compile isn't begun again and we return
	///		a future to the same result
	class IIntermediateCompileMarker
	{
	public:
		virtual std::pair<std::shared_ptr<IArtifactCollection>, ArtifactCollectionFuture> GetArtifact(ArtifactTargetCode, OperationContext* opContext = nullptr) = 0;
		virtual ArtifactCollectionFuture InvokeCompile(CompileRequestCode targetCode, OperationContext* opContext = nullptr) = 0;
		virtual std::string GetCompilerDescription() const = 0;
		virtual void AttachConduit(VariantFunctions&&) = 0;
		virtual ~IIntermediateCompileMarker();
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

	struct SerializedArtifact;

	class BlobArtifactCollection : public IArtifactCollection
	{
	public:
		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const override;
		DependencyValidation GetDependencyValidation() const override;
		StringSection<ResChar> GetRequestParameters() const override;
		AssetState GetAssetState() const override;
		BlobArtifactCollection(
			IteratorRange<const SerializedArtifact*> chunks, 
			AssetState state,
			const DependencyValidation& depVal, 
			const std::string& collectionName = {},
			const std::string& requestParams = {});
		~BlobArtifactCollection();
	private:
		std::vector<SerializedArtifact> _chunks;
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

///////////////////////////////////////////////////////////////////////////////////////////////////

	class IIntermediateCompileMarker;
	namespace Internal
	{
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(CompileRequestCode targetCode, InitializerPack&&);
	}

	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	//
	//		Auto construct to:
	//			(IteratorRange<ArtifactRequestResult*>, DependencyValidation&&)
	//
	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits2<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(StringSection<> initializer)
	{
		// See also AutoConstructToPromise<> variation of this function
		const auto& container = Internal::GetChunkFileContainer(initializer);
		TRY {
			auto chunks = container.ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), container.GetDependencyValidation());
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, container.GetDependencyValidation()));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits2<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(const Blob& blob, const DependencyValidation& depVal, StringSection<> requestParameters = {})
	{
		TRY {
			auto chunks = ArtifactChunkContainer(blob, depVal, requestParameters).ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), depVal);
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	}

	template<typename AssetType, typename... Params, ENABLE_IF(Internal::AssetTraits2<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = 0)
	{
		TRY {
			auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
			return Internal::InvokeAssetConstructor<AssetType>(MakeIteratorRange(chunks), artifactCollection.GetDependencyValidation());
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
		} CATCH_END
	}

	//
	//		Auto construct to:
	//			(::Assets::Blob&&, DependencyValidation&&, StringSection<>)
	//
	template<typename AssetType, typename... Params, ENABLE_IF(!Internal::AssetTraits2<AssetType>::HasChunkRequests)>
		AssetType AutoConstructAsset(const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = Internal::RemoveSmartPtrType<AssetType>::CompileProcessType)
	{
		if (artifactCollection.GetAssetState() == AssetState::Invalid)
			Throw(Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), GetErrorMessage(artifactCollection)});

		TRY {
			ArtifactRequest request { "default-blob", defaultChunkRequestCode, ~0u, ArtifactRequest::DataType::SharedBlob };
			auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(&request, &request+1));
			if (chunks.empty() || !chunks[0]._sharedBlob)
				Throw(Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), AsBlob("Default compilation result chunk not found")});
			return AutoConstructAsset<AssetType>(std::move(chunks[0]._sharedBlob), artifactCollection.GetDependencyValidation(), StringSection<>{});
		} CATCH (const Exceptions::ExceptionWithDepVal& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
		} CATCH (const std::exception& e) {
			Throw(Exceptions::ConstructionError(e, artifactCollection.GetDependencyValidation()));
		} CATCH_END
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Promise, ENABLE_IF(Internal::HasConstructToPromiseFromBlob<Promise>::value)>
		void AutoConstructToPromiseSynchronously(Promise&& promise, const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = Internal::PromisedTypeRemPtr<Promise>::CompileProcessType)
	{
		if (artifactCollection.GetAssetState() == ::Assets::AssetState::Invalid) {
			promise.set_exception(std::make_exception_ptr(Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), GetErrorMessage(artifactCollection)}));
			return;
		}

		TRY {
			ArtifactRequest request { "default-blob", defaultChunkRequestCode, ~0u, ArtifactRequest::DataType::SharedBlob };
			auto reqRes = artifactCollection.ResolveRequests(MakeIteratorRange(&request, &request+1));
			if (!reqRes.empty()) {
				AutoConstructToPromiseSynchronously(
					promise,
					std::move(reqRes[0]._sharedBlob),
					artifactCollection.GetDependencyValidation(),
					artifactCollection.GetRequestParameters());
			} else {
				promise.set_exception(std::make_exception_ptr(Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), AsBlob("Default compilation result chunk not found")}));
			}
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise>
		void AutoConstructToPromiseFromPendingCompile(Promise&& promise, const ArtifactCollectionFuture& pendingCompile, CompileRequestCode targetCode = Internal::RemoveSmartPtrType<Internal::PromisedType<Promise>>::CompileProcessType)
	{
		::Assets::PollToPromise(
			std::move(promise),
			[pendingCompile](auto timeout) {
				auto stallResult = pendingCompile.StallWhilePending(timeout);
				if (stallResult.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
					return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[pendingCompile, targetCode](Promise&& promise) {
				TRY {
					AutoConstructToPromiseSynchronously(std::move(promise), pendingCompile.GetArtifactCollection(), targetCode);
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	template<typename Promise>
		static void DefaultCompilerConstructionSynchronously(
			Promise&& promise,
			CompileRequestCode targetCode,		// typically Internal::RemoveSmartPtrType<AssetType>::CompileProcessType,
			InitializerPack&& initializerPack,
			OperationContext* operationContext = nullptr)
	{
		// Begin a compilation operation via the registered compilers for this type.
		// Our deferred constructor will wait for the completion of that compilation operation,
		// and then construct the final asset from the result
		// We use the "short" task pool here, because we're assuming that construction of the asset
		// from a precompiled result is quick, but actual compilation would take much longer

		TRY {
			#if defined(_DEBUG)
				auto initializerLabel = initializerPack.ArchivableName();
			#endif

			auto marker = Internal::BeginCompileOperation(targetCode, std::move(initializerPack));
			if (!marker) {
				#if defined(_DEBUG)
					Throw(std::runtime_error("No compiler found for asset (" + initializerLabel + ")"));
				#else
					Throw(std::runtime_error("No compiler found for asset"));
				#endif
			}

			// Attempt to load the existing asset immediately. In some cases we should fall back to a recompile (such as, if the
			// version number is bad). We could attempt to push this into a background thread, also

			auto artifactQuery = marker->GetArtifact(targetCode, operationContext);
			if (artifactQuery.first) {
				AutoConstructToPromiseSynchronously(std::move(promise), *artifactQuery.first, targetCode);
			} else {
				assert(artifactQuery.second.Valid());
				AutoConstructToPromiseFromPendingCompile(std::move(promise), artifactQuery.second, targetCode);
			}
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise>
		static void DefaultCompilerConstructionSynchronously(
			Promise&& promise,
			CompileRequestCode targetCode,		// typically Internal::RemoveSmartPtrType<AssetType>::CompileProcessType,
			InitializerPack&& initializerPack,
			VariantFunctions&& progressiveResultConduit,
			OperationContext* operationContext = nullptr)
	{
		// Begin a compilation operation via the registered compilers for this type.
		// Our deferred constructor will wait for the completion of that compilation operation,
		// and then construct the final asset from the result
		// We use the "short" task pool here, because we're assuming that construction of the asset
		// from a precompiled result is quick, but actual compilation would take much longer

		TRY {
			#if defined(_DEBUG)
				auto initializerLabel = initializerPack.ArchivableName();
			#endif

			auto marker = Internal::BeginCompileOperation(targetCode, std::move(initializerPack));
			if (!marker) {
				#if defined(_DEBUG)
					Throw(std::runtime_error("No compiler found for asset (" + initializerLabel + ")"));
				#else
					Throw(std::runtime_error("No compiler found for asset"));
				#endif
			}

			marker->AttachConduit(std::move(progressiveResultConduit));

			// Attempt to load the existing asset immediately. In some cases we should fall back to a recompile (such as, if the
			// version number is bad). We could attempt to push this into a background thread, also

			auto artifactQuery = marker->GetArtifact(targetCode, operationContext);
			if (artifactQuery.first) {
				AutoConstructToPromiseSynchronously(std::move(promise), *artifactQuery.first, targetCode);
			} else {
				assert(artifactQuery.second.Valid());
				AutoConstructToPromiseFromPendingCompile(std::move(promise), artifactQuery.second, targetCode);
			}
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<
		typename Promise, typename... Params,
		ENABLE_IF(Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(Promise&& promise, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=InitializerPack{std::forward<Params>(initialisers)...}, promise=std::move(promise)]() mutable {
				DefaultCompilerConstructionSynchronously(std::move(promise), Internal::PromisedTypeRemPtr<Promise>::CompileProcessType, std::move(initPack));
			});
	}

	template<
		typename Promise, typename... Params,
		ENABLE_IF(Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(Promise&& promise, std::shared_ptr<OperationContext> opContext, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=InitializerPack{std::forward<Params>(initialisers)...}, promise=std::move(promise), opContext=std::move(opContext)]() mutable {
				DefaultCompilerConstructionSynchronously(std::move(promise), Internal::PromisedTypeRemPtr<Promise>::CompileProcessType, std::move(initPack), opContext.get());
			});
	}

	#undef ENABLE_IF
}
