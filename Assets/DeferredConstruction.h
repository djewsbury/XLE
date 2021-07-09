// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "InitializerPack.h"
#include "AssetFuture.h"
#include "AssetTraits.h"
#include "AssetFutureContinuation.h"
#include "IArtifact.h"
#include "IntermediateCompilers.h"
#include "InitializerPack.h"
#include "../OSServices/Log.h"
#include <memory>

namespace Assets
{
	class IIntermediateCompileMarker;

	namespace Internal 
	{
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(TargetCode targetCode, InitializerPack&&);
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
			struct HasDirectAutoConstructAsset_ : decltype(HasDirectAutoConstructAsset_Helper<AssetType, Params...>(0)) {};

		template<typename AssetType, typename... Params>
			using HasDirectAutoConstructAsset = HasDirectAutoConstructAsset_<std::decay_t<RemoveSmartPtrType<AssetType>>, Params...>;

		template<typename AssetType, typename... Params>
			static auto HasConstructToFutureOverride_Helper(int) -> decltype(
				Internal::RemoveSmartPtrType<AssetType>::ConstructToFuture(std::declval<::Assets::Future<AssetType>&>(), std::declval<Params>()...), 
				std::true_type{});

		template<typename...>
			static auto HasConstructToFutureOverride_Helper(...) -> std::false_type;

		template<typename AssetType, typename... Params>
			struct HasConstructToFutureOverride : decltype(HasConstructToFutureOverride_Helper<AssetType, Params...>(0)) {};
	}
	
	// If we can construct an AssetType directly from the given parameters, then enable an implementation of
	// AutoConstructToFuture to do exactly that.
	// The compile operation version can work for any given initializer arguments, but the direct construction
	// version will only work when the arguments match one of the asset type's constructors. So, we need to avoid 
	// ambiguities between these implementations when they overlap.
	// To achieve this, we either need to use namespace tricks, or to use SFINAE to disable the implementation 
	// we don't need.
	template<
		typename Future, typename... Params, 
		typename std::enable_if<Internal::HasDirectAutoConstructAsset<typename Future::PromisedType, Params...>::value>::type* = nullptr>
		void AutoConstructToFutureDirect(Future& future, Params... initialisers)
	{
		Internal::FutureResolutionMoment<typename Future::PromisedType> moment(future);
		TRY {
			auto asset = AutoConstructAsset<typename Future::PromisedType>(std::forward<Params>(initialisers)...);
			future.SetAsset(std::move(asset), {});
		} CATCH (const Exceptions::ConstructionError& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const Exceptions::InvalidAsset& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const std::exception& e) {
			Log(Warning) << "No dependency validation associated with asset after construction failure. Hot reloading will not function for this asset." << std::endl;
			future.SetInvalidAsset({}, AsBlob(e));
		} CATCH_END
	}

	template<typename Future, typename std::enable_if_t<!Internal::AssetTraits<typename Future::PromisedType>::HasChunkRequests>* =nullptr>
		void AutoConstructToFuture(Future& future, const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = Internal::RemoveSmartPtrType<typename Future::PromisedType>::CompileProcessType)
	{
		if (artifactCollection.GetAssetState() == ::Assets::AssetState::Invalid) {
			future.SetInvalidAsset(artifactCollection.GetDependencyValidation(), GetErrorMessage(artifactCollection));
			return;
		}

		ArtifactRequest request { "default-blob", defaultChunkRequestCode, ~0u, ArtifactRequest::DataType::SharedBlob };
		auto reqRes = artifactCollection.ResolveRequests(MakeIteratorRange(&request, &request+1));
		if (!reqRes.empty()) {
			AutoConstructToFutureDirect(
				future,
				reqRes[0]._sharedBlob, 
				artifactCollection.GetDependencyValidation(),
				artifactCollection.GetRequestParameters());
		} else {
			future.SetInvalidAsset(artifactCollection.GetDependencyValidation(), AsBlob("Default compilation result chunk not found"));
		}
	}

	template<typename Future, typename std::enable_if_t<Internal::AssetTraits<typename Future::PromisedType>::HasChunkRequests>* =nullptr>
		void AutoConstructToFuture(Future& future, const IArtifactCollection& artifactCollection, uint64_t defaultChunkRequestCode = Internal::RemoveSmartPtrType<typename Future::PromisedType>::CompileProcessType)
	{
		if (artifactCollection.GetAssetState() == ::Assets::AssetState::Invalid) {
			future.SetInvalidAsset(artifactCollection.GetDependencyValidation(), GetErrorMessage(artifactCollection));
			return;
		}

		auto chunks = artifactCollection.ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<typename Future::PromisedType>::ChunkRequests));
		AutoConstructToFutureDirect(future, MakeIteratorRange(chunks), artifactCollection.GetDependencyValidation());
	}

	template<typename Future>
		void AutoConstructToFuture(Future& future, const std::shared_ptr<ArtifactCollectionFuture>& pendingCompile, TargetCode targetCode = Internal::RemoveSmartPtrType<typename Future::PromisedType>::CompileProcessType)
	{
		// We must poll the compile operation every frame, and construct the asset when it is ready. Note that we're
		// still going to end up constructing the asset in the main thread.
		future.SetPollingFunction(
			[pendingCompile, targetCode](Future& thatFuture) -> bool {
				auto state = pendingCompile->GetAssetState();
				if (state == AssetState::Pending) return true;
				
				const auto& artifactCollection = pendingCompile->GetArtifactCollection(targetCode);
				if (state == AssetState::Invalid || !artifactCollection) {
					if (artifactCollection) {
						thatFuture.SetInvalidAsset(artifactCollection->GetDependencyValidation(), GetErrorMessage(*artifactCollection));
					} else {
						thatFuture.SetInvalidAsset({}, AsBlob("No artifact collection of the requested type was found"));
					}
					return false;
				}

				assert(state == AssetState::Ready);
				AutoConstructToFuture(thatFuture, *artifactCollection, targetCode);
				return false;
			});
	}

	template<typename Future, typename... Args>
		static void DefaultCompilerConstruction(
			Future& future,
			TargetCode targetCode, 		// typically Internal::RemoveSmartPtrType<AssetType>::CompileProcessType,
			Args... args)
	{
		// Begin a compilation operation via the registered compilers for this type.
		// Our deferred constructor will wait for the completion of that compilation operation,
		// and then construct the final asset from the result

		#if defined(_DEBUG)
			std::string debugLabel = InitializerPack{args...}.ArchivableName();		// (note no forward here, because we reuse args below)
		#endif

		TRY { 
			auto marker = Internal::BeginCompileOperation(targetCode, InitializerPack{std::forward<Args>(args)...});
			if (!marker) {
				#if defined(_DEBUG)
					future.SetInvalidAsset({}, AsBlob("No compiler found for asset " + debugLabel));
				#else
					future.SetInvalidAsset({}, AsBlob("No compiler found for asset"));
				#endif
				return;
			}

			// Attempt to load the existing asset immediately. In some cases we should fall back to a recompile (such as, if the
			// version number is bad). We could attempt to push this into a background thread, also

			auto existingArtifact = marker->GetExistingAsset(targetCode);
			if (existingArtifact && existingArtifact->GetDependencyValidation() && existingArtifact->GetDependencyValidation().GetValidationIndex()==0) {
				bool doRecompile = false;
				AutoConstructToFuture(future, *existingArtifact, targetCode);
				if (!doRecompile) return;
			}
		
			auto pendingCompile = marker->InvokeCompile();
			AutoConstructToFuture(future, pendingCompile, targetCode);
			
		} CATCH(const Exceptions::ConstructionError& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const Exceptions::InvalidAsset& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
			throw;	// Have to rethrow InvalidAsset, otherwise we loose our dependency validation. This can occur when the AutoConstructAsset function itself loads some other asset
		} CATCH(const std::exception& e) {
			#if defined(_DEBUG)
				Log(Warning) << "No dependency validation associated with asset (" << debugLabel << ") after construction failure. Hot reloading will not function for this asset." << std::endl;
			#endif
			future.SetInvalidAsset({}, AsBlob(e));
		} CATCH_END
	}

	template<
		typename Future, typename... Params, 
		typename std::enable_if<Internal::HasConstructToFutureOverride<typename Future::PromisedType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(Future& future, Params... initialisers)
	{
		Internal::RemoveSmartPtrType<typename Future::PromisedType>::ConstructToFuture(future, std::forward<Params>(initialisers)...);
	}

	template<
		typename Future, typename... Params, 
		typename std::enable_if<Internal::AssetTraits<typename Future::PromisedType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<typename Future::PromisedType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(Future& future, Params... initialisers)
	{
		DefaultCompilerConstruction(future, Internal::RemoveSmartPtrType<typename Future::PromisedType>::CompileProcessType, std::forward<Params>(initialisers)...);
	}

	template<
		typename Future, typename... Params, 
		typename std::enable_if<!Internal::AssetTraits<typename Future::PromisedType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<typename Future::PromisedType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(Future& future, Params... initialisers)
	{
		AutoConstructToFutureDirect(future, std::forward<Params>(initialisers)...);
	}

	template<
		typename Future, 
		typename std::enable_if_t<Internal::AssetTraits<typename Future::PromisedType>::Constructor_Formatter && !Internal::AssetTraits<typename Future::PromisedType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<typename Future::PromisedType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructToFuture(Future& future, StringSection<ResChar> initializer)
	{
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			std::string containerName = MakeStringSection(initializer.begin(), p).AsString();
			std::string sectionName = MakeStringSection((const utf8*)(p+1), (const utf8*)initializer.end()).AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToFuture(
				future,
				[containerName, sectionName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetFormatter(sectionName);
					return Internal::ConstructFinalAssetObject<typename Future::PromisedType>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
		} else {
			std::string containerName = initializer.AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToFuture(
				future,
				[containerName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetRootFormatter();
					return Internal::ConstructFinalAssetObject<typename Future::PromisedType>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
		}
	}

	template<
		typename Future,
		typename std::enable_if_t<Internal::AssetTraits<typename Future::PromisedType>::Constructor_ChunkFileContainer && !Internal::AssetTraits<typename Future::PromisedType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<typename Future::PromisedType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructToFuture(Future& future, StringSection<ResChar> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToFuture(
			future,
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				return Internal::ConstructFinalAssetObject<typename Future::PromisedType>(*container);
			});
	}

	template<
		typename Future,
		typename std::enable_if_t<Internal::AssetTraits<typename Future::PromisedType>::HasChunkRequests && !Internal::AssetTraits<typename Future::PromisedType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<typename Future::PromisedType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructToFuture(Future& future, StringSection<ResChar> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToFuture(
			future,
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				auto chunks = container->ResolveRequests(MakeIteratorRange(Internal::RemoveSmartPtrType<typename Future::PromisedType>::ChunkRequests));
				return Internal::ConstructFinalAssetObject<typename Future::PromisedType>(MakeIteratorRange(chunks), container->GetDependencyValidation());
			});
	}

	template<typename AssetType, typename... Params>
		std::shared_ptr<Future<AssetType>> MakeFuture(Params... initialisers)
	{
		auto future = std::make_shared<Future<AssetType>>(Internal::AsString(initialisers...));
		AutoConstructToFuture(*future, std::forward<Params>(initialisers)...);
		return future;
	}
}

