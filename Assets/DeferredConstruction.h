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
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(uint64_t typeCode, InitializerPack&&);
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
			struct HasDirectAutoConstructAsset : decltype(HasDirectAutoConstructAsset_Helper<AssetType, Params...>(0)) {};


		template<typename AssetType, typename... Params>
			static auto HasConstructToFutureOverride_Helper(int) -> decltype(
				AssetType::ConstructToFuture(std::declval<::Assets::AssetFuture<AssetType>&>(), std::declval<Params>()...), 
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
		typename AssetType, typename... Params, 
		typename std::enable_if<Internal::HasDirectAutoConstructAsset<AssetType, Params...>::value>::type* = nullptr>
		void AutoConstructToFutureDirect(AssetFuture<AssetType>& future, Params... initialisers)
	{
		Internal::FutureResolutionMoment<AssetType> moment(future);
		TRY {
			auto asset = AutoConstructAsset<AssetType>(std::forward<Params>(initialisers)...);
			future.SetAsset(std::move(asset), {});
		} CATCH (const Exceptions::ConstructionError& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const Exceptions::InvalidAsset& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const std::exception& e) {
			Log(Warning) << "No dependency validation associated with asset after construction failure. Hot reloading will not function for this asset." << std::endl;
			future.SetInvalidAsset(std::make_shared<DependencyValidation>(), AsBlob(e));
		} CATCH_END
	}

	template<typename AssetType>
		void AutoConstructToFuture(AssetFuture<AssetType>& future, const std::shared_ptr<ArtifactCollectionFuture>& pendingCompile, uint64_t compileTypeCode = AssetType::CompileProcessType)
	{
		// We must poll the compile operation every frame, and construct the asset when it is ready. Note that we're
		// still going to end up constructing the asset in the main thread.
		future.SetPollingFunction(
			[pendingCompile, compileTypeCode](AssetFuture<AssetType>& thatFuture) -> bool {
				auto state = pendingCompile->GetAssetState();
				if (state == AssetState::Pending) return true;
				
				const auto& artifactCollection = pendingCompile->GetArtifactCollection();
				if (state == AssetState::Invalid || !artifactCollection) {
					if (artifactCollection) {
						thatFuture.SetInvalidAsset(artifactCollection->GetDependencyValidation(), pendingCompile->GetErrorMessage());
					} else {
						thatFuture.SetInvalidAsset(nullptr, nullptr);
					}
					return false;
				}

				assert(state == AssetState::Ready);
				ArtifactRequest request { "", compileTypeCode, ~0u, ArtifactRequest::DataType::SharedBlob };
				auto reqRes = artifactCollection->ResolveRequests(MakeIteratorRange(&request, &request+1));
				if (!reqRes.empty()) {
					AutoConstructToFutureDirect(
						thatFuture,
						reqRes[0]._sharedBlob, 
						artifactCollection->GetDependencyValidation(),
						artifactCollection->GetRequestParameters());
				} else {
					thatFuture.SetInvalidAsset(artifactCollection->GetDependencyValidation(), {});
				}
				return false;
			});
	}

	template<typename AssetType, typename... Args>
		static void DefaultCompilerConstruction(
			AssetFuture<AssetType>& future,
			uint64_t compileTypeCode, 		// typically AssetType::CompileProcessType,
			Args... args)
	{
		// Begin a compilation operation via the registered compilers for this type.
		// Our deferred constructor will wait for the completion of that compilation operation,
		// and then construct the final asset from the result

		#if defined(_DEBUG)
			std::string debugLabel = InitializerPack{args...}.ArchivableName();		// (note no forward here, because we reuse args below)
		#endif

		TRY { 
			auto marker = Internal::BeginCompileOperation(compileTypeCode, InitializerPack{std::forward<Args>(args)...});
			if (!marker) {
				future.SetInvalidAsset(nullptr, AsBlob("No compiler found for asset " + debugLabel));
				return;
			}
			// std::basic_string<ResChar> init0 = initializers[0].AsString();

			// Attempt to load the existing asset immediately. In some cases we should fall back to a recompile (such as, if the
			// version number is bad). We could attempt to push this into a background thread, also

			auto existingArtifact = marker->GetExistingAsset();
			if (existingArtifact && existingArtifact->GetDependencyValidation() && existingArtifact->GetDependencyValidation()->GetValidationIndex()==0) {
				bool doRecompile = false;
				ArtifactRequest request { "", compileTypeCode, ~0u, ArtifactRequest::DataType::SharedBlob };
				auto reqRes = existingArtifact->ResolveRequests(MakeIteratorRange(&request, &request+1));
				if (!reqRes.empty()) {
					auto asset = AutoConstructAsset<AssetType>(reqRes[0]._sharedBlob, existingArtifact->GetDependencyValidation(), existingArtifact->GetRequestParameters());
					future.SetAsset(std::move(asset), {});
				} else {
					future.SetInvalidAsset(existingArtifact->GetDependencyValidation(), {});
				}
				if (!doRecompile) return;
			}
		
			auto pendingCompile = marker->InvokeCompile();
			AutoConstructToFuture(future, pendingCompile, compileTypeCode);
			
		} CATCH(const Exceptions::ConstructionError& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
		} CATCH (const Exceptions::InvalidAsset& e) {
			future.SetInvalidAsset(e.GetDependencyValidation(), e.GetActualizationLog());
			throw;	// Have to rethrow InvalidAsset, otherwise we loose our dependency validation. This can occur when the AutoConstructAsset function itself loads some other asset
		} CATCH(const std::exception& e) {
			#if defined(_DEBUG)
				Log(Warning) << "No dependency validation associated with asset (" << debugLabel << ") after construction failure. Hot reloading will not function for this asset." << std::endl;
			#endif
			future.SetInvalidAsset(std::make_shared<DependencyValidation>(), AsBlob(e));
		} CATCH_END
	}

	template<
		typename AssetType, typename... Params, 
		typename std::enable_if<Internal::HasConstructToFutureOverride<AssetType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(AssetFuture<AssetType>& future, Params... initialisers)
	{
		AssetType::ConstructToFuture(future, std::forward<Params>(initialisers)...);
	}

	template<
		typename AssetType, typename... Params, 
		typename std::enable_if<Internal::AssetTraits<AssetType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<AssetType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(AssetFuture<AssetType>& future, Params... initialisers)
	{
		DefaultCompilerConstruction<AssetType>(future, AssetType::CompileProcessType, std::forward<Params>(initialisers)...);
	}

	template<
		typename AssetType, typename... Params, 
		typename std::enable_if<!Internal::AssetTraits<AssetType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<AssetType, Params...>::value>::type* = nullptr>
		void AutoConstructToFuture(AssetFuture<AssetType>& future, Params... initialisers)
	{
		AutoConstructToFutureDirect(future, std::forward<Params>(initialisers)...);
	}

	template<
		typename AssetType, 
		typename std::enable_if_t<Internal::AssetTraits<AssetType>::Constructor_Formatter && !Internal::AssetTraits<AssetType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<AssetType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructToFuture(AssetFuture<AssetType>& future, StringSection<ResChar> initializer)
	{
		const char* p = XlFindChar(initializer, ':');
		if (p) {
			std::string containerName = MakeStringSection(initializer.begin(), p).AsString();
			std::string sectionName = MakeStringSection((const utf8*)(p+1), (const utf8*)initializer.end()).AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToFuture<AssetType>(
				future,
				[containerName, sectionName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetFormatter(sectionName);
					return std::make_unique<AssetType>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
		} else {
			std::string containerName = initializer.AsString();
			auto containerFuture = Internal::GetConfigFileContainerFuture(MakeStringSection(containerName));
			WhenAll(containerFuture).ThenConstructToFuture<AssetType>(
				future,
				[containerName](const std::shared_ptr<ConfigFileContainer<>>& container) {
					auto fmttr = container->GetRootFormatter();
					return std::make_unique<AssetType>(
						fmttr, 
						DefaultDirectorySearchRules(containerName),
						container->GetDependencyValidation());
				});
		}
	}

	template<
		typename AssetType, typename... Params, 
		typename std::enable_if_t<Internal::AssetTraits<AssetType>::Constructor_ChunkFileContainer && !Internal::AssetTraits<AssetType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<AssetType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructAssetToFuture(AssetFuture<AssetType>& future, StringSection<ResChar> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToFuture<AssetType>(
			future,
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				return std::make_unique<AssetType>(*container);
			});
	}

	template<
		typename AssetType, typename... Params, 
		typename std::enable_if_t<Internal::AssetTraits<AssetType>::HasChunkRequests && !Internal::AssetTraits<AssetType>::HasCompileProcessType && !Internal::HasConstructToFutureOverride<AssetType, StringSection<ResChar>>::value>* =nullptr>
		void AutoConstructAssetToFuture(AssetFuture<AssetType>& future, StringSection<ResChar> initializer)
	{
		auto containerFuture = Internal::GetChunkFileContainerFuture(initializer);
		WhenAll(containerFuture).ThenConstructToFuture<AssetType>(
			future,
			[](const std::shared_ptr<ChunkFileContainer>& container) {
				auto chunks = container->ResolveRequests(MakeIteratorRange(AssetType::ChunkRequests));
				return std::make_unique<AssetType>(MakeIteratorRange(chunks), container->GetDependencyValidation());
			});
	}
}

