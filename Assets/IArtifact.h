// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#if defined(__CLR_VER)
	#error This file cannot be included in CLR builds
#endif

#include "IAsyncMarker.h"
#include "ICompileOperation.h"
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

///////////////////////////////////////////////////////////////////////////////////////////////////

	#define ENABLE_IF(...) typename std::enable_if_t<__VA_ARGS__>* = nullptr

	namespace Internal 
	{
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(CompileRequestCode targetCode, InitializerPack&&);

		template <typename AssetType>
			class AssetTraits2_
		{
		private:
			struct HasCompileProcessTypeHelper
			{
				struct FakeBase { static const uint64_t CompileProcessType; };
				struct TestSubject : public FakeBase, public AssetType {};

				template <typename C, C> struct Check;

				// This technique is based on an implementation from StackOverflow. Here, taking the address
				// of the static member variable in TestSubject would be ambiguous, iff CompileProcessType 
				// is actually a member of AssetType (otherwise, the member in FakeBase is found)
				template <typename C> static std::false_type Test(Check<const uint64_t*, &C::CompileProcessType> *);
				template <typename> static std::true_type Test(...);

				static const bool value = decltype(Test<TestSubject>(0))::value;
			};

			template<typename T> static auto HasChunkRequestsHelper(int) -> decltype(&T::ChunkRequests[0], std::true_type{});
			template<typename...> static auto HasChunkRequestsHelper(...) -> std::false_type;

		public:
			static const bool Constructor_Blob = std::is_constructible<AssetType, ::Assets::Blob&&, DependencyValidation&&, StringSection<>>::value;
			static const bool Constructor_ArtifactRequestResult = std::is_constructible<AssetType, IteratorRange<ArtifactRequestResult*>, DependencyValidation&&>::value;

			static const bool HasCompileProcessType = HasCompileProcessTypeHelper::value;
			static const bool HasChunkRequests = decltype(HasChunkRequestsHelper<AssetType>(0))::value;
		};

		template<typename AssetType>
			using AssetTraits2 = AssetTraits2_<std::decay_t<RemoveSmartPtrType<AssetType>>>;

		// Note -- here's a useful pattern that can turn any expression in a SFINAE condition
		// Taken from stack overflow -- https://stackoverflow.com/questions/257288/is-it-possible-to-write-a-template-to-check-for-a-functions-existence
		// If the expression in the first decltype() is invalid, we will trigger SFINAE and fall back to std::false_type
		template<typename PromiseType>
			static auto HasConstructToPromiseFromBlob_Helper(int) -> decltype(PromisedTypeRemPtr<PromiseType>::ConstructToPromise(std::declval<PromiseType&&>(), std::declval<Blob&&>(), std::declval<DependencyValidation&&>(), std::declval<StringSection<>>()), std::true_type{});

		template<typename...>
			static auto HasConstructToPromiseFromBlob_Helper(...) -> std::false_type;

		template<typename PromiseType>
			struct HasConstructToPromiseFromBlob_ : decltype(HasConstructToPromiseFromBlob_Helper<PromiseType>(0)) {};

		template<typename PromiseType>
			using HasConstructToPromiseFromBlob = HasConstructToPromiseFromBlob_<PromiseType>;
	}

}

#include "ChunkFileContainer.h"	// todo -- hack

namespace Assets
{

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
			auto chunks = ChunkFileContainer(blob, depVal, requestParameters).ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<AssetType>::ChunkRequests));
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
			CompileRequestCode targetCode, 		// typically Internal::RemoveSmartPtrType<AssetType>::CompileProcessType,
			InitializerPack&& initializerPack,
			OperationContext* operationContext = nullptr)
	{
		// Begin a compilation operation via the registered compilers for this type.
		// Our deferred constructor will wait for the completion of that compilation operation,
		// and then construct the final asset from the result
		// We use the "short" task pool here, because we're assuming that construction of the asset
		// from a precompiled result is quick, but actual compilation would take much longer

		TRY {
			std::string initializerLabel;
			#if defined(_DEBUG)
				initializerLabel = initializerPack.ArchivableName();
			#else
				if (operationContext) initializerLabel = initializerPack.ArchivableName();
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

			auto artifactQuery = marker->GetArtifact(targetCode);
			if (artifactQuery.first) {
				AutoConstructToPromiseSynchronously(std::move(promise), *artifactQuery.first, targetCode);
			} else {
				assert(artifactQuery.second.Valid());
				AutoConstructToPromiseFromPendingCompile(std::move(promise), artifactQuery.second, targetCode);

				if (operationContext) {
					auto operation = operationContext->Begin(Concatenate("Compiling (", initializerLabel, ") with compiler (", marker->GetCompilerDescription(), ")"));
					operation.EndWithFuture(artifactQuery.second.ShareFuture());
				}
			}
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise>
		static void DefaultCompilerConstructionSynchronously(
			Promise&& promise,
			CompileRequestCode targetCode, 		// typically Internal::RemoveSmartPtrType<AssetType>::CompileProcessType,
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
			std::string initializerLabel;
			#if defined(_DEBUG)
				initializerLabel = initializerPack.ArchivableName();
			#else
				if (operationContext) initializerLabel = initializerPack.ArchivableName();
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

			auto artifactQuery = marker->GetArtifact(targetCode);
			if (artifactQuery.first) {
				AutoConstructToPromiseSynchronously(std::move(promise), *artifactQuery.first, targetCode);
			} else {
				assert(artifactQuery.second.Valid());
				AutoConstructToPromiseFromPendingCompile(std::move(promise), artifactQuery.second, targetCode);

				if (operationContext) {
					auto operation = operationContext->Begin(Concatenate("Compiling (", initializerLabel, ") with compiler (", marker->GetCompilerDescription(), ")"));
					operation.EndWithFuture(artifactQuery.second.ShareFuture());
				}
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

	template<
		typename Promise,
		ENABLE_IF(	Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasChunkRequests 
				&&  !Internal::AssetTraits2<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(Promise&& promise, StringSection<> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToPromise(
			std::move(promise),
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				auto chunks = container->ResolveRequests(MakeIteratorRange(Internal::PromisedTypeRemPtr<Promise>::ChunkRequests));
				return Internal::InvokeAssetConstructor<Internal::PromisedType<Promise>>(MakeIteratorRange(chunks), container->GetDependencyValidation());
			});
	}

}

#if 0
namespace Utility
{
	template<
		typename Promise, typename... Params,
		ENABLE_IF(::Assets::Internal::AssetTraits2<::Assets::Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(Promise&& promise, StringSection<> p0, StringSection<> p1)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=::Assets::InitializerPack{p0, p1}, promise=std::move(promise)]() mutable {
				::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), ::Assets::Internal::PromisedTypeRemPtr<Promise>::CompileProcessType, std::move(initPack));
			});
	}
}
#endif

#if 1	// hack -- for testing
namespace std
{
	template<
		typename PromisedType, typename... Params,
		ENABLE_IF(::Assets::Internal::AssetTraits2<PromisedType>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(std::promise<PromisedType>&& promise, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=::Assets::InitializerPack{std::forward<Params>(initialisers)...}, promise=std::move(promise)]() mutable {
				::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), ::Assets::Internal::RemoveSmartPtrType<PromisedType>::CompileProcessType, std::move(initPack));
			});
	}

	template<
		typename PromisedType, typename... Params,
		ENABLE_IF(::Assets::Internal::AssetTraits2<PromisedType>::HasCompileProcessType)>
		void AutoConstructToPromiseOverride(std::promise<PromisedType>&& promise, std::shared_ptr<::Assets::OperationContext> opContext, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=::Assets::InitializerPack{std::forward<Params>(initialisers)...}, promise=std::move(promise), opContext=std::move(opContext)]() mutable {
				::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), ::Assets::Internal::RemoveSmartPtrType<PromisedType>::CompileProcessType, std::move(initPack), opContext.get());
			});
	}
}
#endif

#undef ENABLE_IF

