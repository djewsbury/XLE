// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InitializerPack.h"
#include "Marker.h"
#include "AssetTraits.h"
#include "Continuation.h"
#include "ContinuationUtil.h"
#include "IArtifact.h"
#include "IntermediateCompilers.h"
#include "OperationContext.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../OSServices/Log.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/StringUtils.h"		// required for implied StringSection<> constructor
#include <memory>

// todo -- ConfigFileContainer.h is required early, because the overrides that can be called by
// ApplyAutoConstructToPromise_impl must come before that function.
//
// In the C++ standard, this is 14.6.4.2 -- only lookups using ADl can use the context from template
// instantiation.
//
// However it would be preferable to break that dependency, because it would given us more flexibility 
// for expanding the range of AutoConstructAsset() overrides
// hm... it's difficult though because ADL isn't exactly what we want here
#include "ConfigFileContainer.h"

namespace Assets
{
	class IIntermediateCompileMarker;
	class OperationContext;

	namespace Internal 
	{
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(CompileRequestCode targetCode, InitializerPack&&);
	}

	namespace Internal
	{
		// Note -- here's a useful pattern that can turn any expression in a SFINAE condition
		// Taken from stack overflow -- https://stackoverflow.com/questions/257288/is-it-possible-to-write-a-template-to-check-for-a-functions-existence
		// If the expression in the first decltype() is invalid, we will trigger SFINAE and fall back to std::false_type
		template<typename AssetType, typename... Params>
			static auto HasDirectAutoConstructAsset_Helper(int) -> decltype(AutoConstructAsset<AssetType>(std::declval<Params>()...), std::true_type{});

		template<typename...>
			static auto HasDirectAutoConstructAsset_Helper(...) -> std::false_type;

		template<typename AssetType, typename... Params>
			struct HasDirectAutoConstructAsset_ : decltype(HasDirectAutoConstructAsset_Helper<AssetType, Params&&...>(0)) {};

		template<typename AssetType, typename... Params>
			using HasDirectAutoConstructAsset = HasDirectAutoConstructAsset_<AssetType, Params&&...>;
	}
	
	// If we can construct an AssetType directly from the given parameters, then enable an implementation of
	// AutoConstructToPromise to do exactly that.
	// The compile operation version can work for any given initializer arguments, but the direct construction
	// version will only work when the arguments match one of the asset type's constructors. So, we need to avoid 
	// ambiguities between these implementations when they overlap.
	// To achieve this, we either need to use namespace tricks, or to use SFINAE to disable the implementation 
	// we don't need.
	template<
		typename Promise, typename... Params, 
		typename std::enable_if<Internal::HasDirectAutoConstructAsset<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromiseSynchronously(Promise&& promise, Params&&... initialisers)
	{
		TRY {
			promise.set_value(AutoConstructAsset<Internal::PromisedType<Promise>>(std::forward<Params>(initialisers)...));
		} CATCH (...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise, typename std::enable_if_t<!Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasChunkRequests>* =nullptr>
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

	template<typename Promise, typename std::enable_if_t<Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasChunkRequests>* =nullptr>
		void AutoConstructToPromiseSynchronously(Promise&& promise, const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = Internal::PromisedTypeRemPtr<Promise>::CompileProcessType)
	{
		if (artifactCollection.GetAssetState() == ::Assets::AssetState::Invalid) {
			promise.set_exception(std::make_exception_ptr(Exceptions::InvalidAsset{{}, artifactCollection.GetDependencyValidation(), GetErrorMessage(artifactCollection)}));
			return;
		}

		TRY {
			auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(Internal::PromisedTypeRemPtr<Promise>::ChunkRequests));
			AutoConstructToPromiseSynchronously(promise, MakeIteratorRange(chunks), artifactCollection.GetDependencyValidation());
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<
		typename Promise, typename... Params, 
		typename std::enable_if<Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromiseSynchronously(Promise&& promise, Params&&... initialisers)
	{
		// Note that there are identical overrides for AutoConstructToPromise & AutoConstructToPromiseSynchronously
		TRY {
			Internal::PromisedTypeRemPtr<Promise>::ConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		} CATCH(const std::exception& e) {
			Log(Error) << "Suppressing exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
			Log(Error) << e.what() << std::endl
		} CATCH(...) {
			Log(Error) << "Suppressing unknown exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
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
		typename std::enable_if<	Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType 
								&& !Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromise(Promise&& promise, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=InitializerPack{initialisers...}, promise=std::move(promise)]() mutable {
				DefaultCompilerConstructionSynchronously(std::move(promise), Internal::PromisedTypeRemPtr<Promise>::CompileProcessType, std::move(initPack));
			});
	}

	template<
		typename Promise, typename... Params, 
		typename std::enable_if<	Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType 
								&& !Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromise(Promise&& promise, std::shared_ptr<OperationContext> opContext, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initPack=InitializerPack{initialisers...}, promise=std::move(promise), opContext=std::move(opContext)]() mutable {
				DefaultCompilerConstructionSynchronously(std::move(promise), Internal::PromisedTypeRemPtr<Promise>::CompileProcessType, std::move(initPack), opContext.get());
			});
	}

	namespace Internal
	{
		template<typename Promise, typename Tuple, std::size_t ... I>
			auto ApplyAutoConstructToPromise_impl(Promise&& promise, Tuple&& t, std::index_sequence<I...>)
		{
			if constexpr (Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, std::tuple_element_t<I, Tuple>&&...>::value) {
				return AutoConstructToPromiseSynchronously(std::move(promise), std::get<I>(std::forward<Tuple>(t))...);
			} else {
				// same as above, but since we're not required to go through AutoConstructToPromiseSynchronously, we can
				// make this just slightly simpler
				// this can sometimes produce clearer compiler error messages if something does wrong
				promise.set_value(AutoConstructAsset<Internal::PromisedType<Promise>>(std::get<I>(std::forward<Tuple>(t))...));
			}
		}

		template<typename Ty, typename Tuple>
			auto ApplyAutoConstructToPromise(Ty&& promise, Tuple&& t)
		{
			using Indices = std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>;
			return ApplyAutoConstructToPromise_impl(std::move(promise), std::move(t), Indices{});
		}
	}

	template<
		typename Promise, typename... Params,
		typename std::enable_if<	!Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType 
								&&  !Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromise(Promise&& promise, Params&&... initialisers)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[initializersTuple=std::tuple{MakeStoreableInAny(initialisers)...}, promise=std::move(promise)]() mutable {
				TRY {

					#if 0
						// We can use std::apply to achieve AutoConstructToPromiseSynchronously(promise, initialisers...)
						// however, this approach seems brittle; particularly as the types in the tuple are often not the
						// same as the AutoConstructToPromiseSynchronously override uses
						// This approach also does not produce clear compile error messages when a variation can't be found
						using OverrideType = void(*)(Promise&&, decltype(MakeStoreableInAny(std::declval<Params>()))&&...);
						auto* ptr = (OverrideType)&AutoConstructToPromiseSynchronously;
						std::apply(ptr, std::tuple_cat(std::make_tuple(std::move(promise)), std::move(initializersTuple)));
					#else
						Internal::ApplyAutoConstructToPromise(std::move(promise), std::move(initializersTuple));
					#endif

				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	template<
		typename Promise, typename... Params, 
		typename std::enable_if<Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, Params&&...>::value>::type* = nullptr>
		void AutoConstructToPromise(Promise&& promise, Params&&... initialisers)
	{
		// Note that there are identical overrides for AutoConstructToPromise & AutoConstructToPromiseSynchronously
		TRY {
			Internal::PromisedTypeRemPtr<Promise>::ConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		} CATCH(const std::exception& e) {
			Log(Error) << "Suppressing exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise. Details follow:" << std::endl;
			Log(Error) << e.what() << std::endl;
		} CATCH(...) {
			Log(Error) << "Suppressing unknown exception thrown from ConstructToPromise override. Overrides should not throw exceptions, and instead store them in the promise." << std::endl;
		} CATCH_END
	}

	template<
		typename Promise,
		typename std::enable_if_t<	Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasChunkRequests 
								&&  !Internal::AssetTraits<Internal::PromisedTypeRemPtr<Promise>>::HasCompileProcessType 
								&&  !Internal::HasConstructToPromiseOverride<Internal::PromisedType<Promise>, StringSection<>>::value
								>* =nullptr>
		void AutoConstructToPromise(Promise&& promise, StringSection<> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToPromise(
			std::move(promise),
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				auto chunks = container->ResolveRequests(MakeIteratorRange(Internal::PromisedTypeRemPtr<Promise>::ChunkRequests));
				return Internal::InvokeAssetConstructor<Internal::PromisedType<Promise>>(MakeIteratorRange(chunks), container->GetDependencyValidation());
			});
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Marker<AssetType>> ConstructToMarker(Params&&... initialisers)
	{
		auto future = std::make_shared<Marker<AssetType>>(Internal::AsString(initialisers...));
		AutoConstructToPromise(future->AdoptPromise(), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<MarkerPtr<AssetType>> ConstructToMarkerPtr(Params&&... initialisers)
	{
		auto future = std::make_shared<MarkerPtr<AssetType>>(Internal::AsString(initialisers...));
		AutoConstructToPromise(future->AdoptPromise(), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::future<AssetType> ConstructToFuture(Params&&... initialisers)
	{
		std::promise<AssetType> promise;
		auto future = promise.get_future();
		AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future;
	}

	template<typename AssetType, typename... Params>
		std::future<std::shared_ptr<AssetType>> ConstructToFuturePtr(Params&&... initialisers)
	{
		std::promise<std::shared_ptr<AssetType>> promise;
		auto future = promise.get_future();
		AutoConstructToPromise(std::move(promise), std::forward<Params>(initialisers)...);
		return future;
	}
}

