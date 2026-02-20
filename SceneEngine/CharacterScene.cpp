// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CharacterScene.h"
#include "IScene.h"
#include "../RenderCore/Techniques/DeformerConstruction.h"
#include "../RenderCore/Techniques/ResourceConstructionContext.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/DeformAccelerator.h"
#include "../RenderCore/Techniques/DrawableConstructor.h"
#include "../RenderCore/Techniques/SimpleModelRenderer.h"		// for RendererSkeletonInterface
#include "../RenderCore/Techniques/DeformGeometryInfrastructure.h"
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/LightWeightBuildDrawables.h"
#include "../RenderCore/Techniques/SkinDeformer.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/BufferUploads/IBufferUploads.h"
#include "../RenderCore/BufferUploads/BatchedResources.h"
#include "../RenderCore/Assets/ModelRendererConstruction.h"
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../RenderCore/Assets/CompiledMaterialSet.h"
#include "../RenderCore/Assets/AnimationScaffoldInternal.h"
#include "../RenderCore/Assets/CompoundObject.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Assets/Continuation.h"
#include "../Assets/IArtifact.h"
#include "../Assets/CompoundAsset.h"
#include "../Math/ProjectionMath.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/BitUtils.h"
#include <future>

namespace SceneEngine
{
	namespace CharacterSceneInternal
	{
		struct ModelEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> _completedConstruction;

			// Construction direct from ModelRendererConstruction
			std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> _referenceHolder;
		};

		struct DeformerEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> _completedConstruction;
			std::shared_ptr<RenderCore::Techniques::DeformerConstruction> _referenceHolder;
		};

		struct AnimSetEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Assets::AnimationSetScaffold>> _animSetFuture;
		};

		struct Renderer
		{
			std::shared_ptr<RenderCore::Techniques::DrawableConstructor> _drawableConstructor;
			std::shared_ptr<RenderCore::Techniques::DeformAccelerator> _deformAccelerator;
			std::shared_ptr<RenderCore::Assets::SkeletonScaffold> _skeletonScaffold;
			std::shared_ptr<RenderCore::Assets::ModelScaffold> _firstModelScaffold;
			RenderCore::BufferUploads::CommandListID _completionCmdList;
			std::pair<Float3, Float3> _aabb;

			const RenderCore::Assets::SkeletonMachine& GetSkeletonMachine() const
			{
				if (_skeletonScaffold) {
					return _skeletonScaffold->GetSkeletonMachine();
				} else {
					assert(_firstModelScaffold->EmbeddedSkeleton());
					return *_firstModelScaffold->EmbeddedSkeleton();
				}
			}
		};

		struct Animator
		{
			std::shared_ptr<RenderCore::Techniques::RendererSkeletonInterface> _deformerSkeletonInterface;
			std::shared_ptr<RenderCore::Assets::AnimationSetScaffold> _animSet;
			RenderCore::Assets::AnimationSetBinding _animSetBinding;
			RenderCore::Techniques::ModelConstructionSkeletonBinding _modelToSkeletonBinding;
			std::vector<Float4x4> _skeletonMachineOutput;
		};

		struct RendererEntry
		{
			std::shared_ptr<ModelEntry> _model;
			std::shared_ptr<DeformerEntry> _deformer;
			std::shared_ptr<AnimSetEntry> _animSet;
			Renderer _renderer;
			Animator _animator;
			BitHeap _allocatedInstances;
			std::shared_future<CharacterSceneInternal::Renderer> _pendingRenderer;
			::Assets::DependencyValidation _depVal;
		};

		struct PendingUpdate
		{
			std::weak_ptr<RendererEntry> _dst;
			Renderer _renderer;
			Animator _animator;
		};

		struct PendingExceptionUpdate
		{
			std::weak_ptr<RendererEntry> _dst;
			::Assets::Blob _log;
			::Assets::DependencyValidation _depVal;
		};
	}

	class CharacterScene : public ICharacterScene, public std::enable_shared_from_this<CharacterScene>
	{
	public:
		OpaquePtr CreateModel(std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>) override;
		OpaquePtr CreateModel(StringSection<> compoundObjectSrc) override;
		OpaquePtr CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction>) override;
		OpaquePtr CreateDeformers(StringSection<> compoundObjectSrc, const OpaquePtr& model) override;
		OpaquePtr CreateAnimationSet(StringSection<>) override;
		OpaquePtr CreateRenderer(OpaquePtr model, OpaquePtr deformers, OpaquePtr animationSet) override;

		void OnFrameBarrier() override;
		void CancelConstructions() override;

		std::shared_future<RenderCore::Assets::SkeletonBinding> CreateSkeletonBinding(OpaquePtr renderer, IteratorRange<const uint64_t*> inputInterface) override;
		std::shared_future<SkeletonMachine> GetSkeletonMachine(OpaquePtr renderer) override;
		std::shared_future<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> GetModelRendererConstruction(OpaquePtr model) override;
		std::shared_future<std::shared_ptr<RenderCore::Techniques::DeformAccelerator>> GetDeformAccelerator(OpaquePtr renderer) override;
		RenderCore::BufferUploads::CommandListID GetCompletionCommandList(void* renderer) override;

		std::shared_ptr<Assets::OperationContext> GetLoadingContext() override;

		CharacterScene(
			std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
			std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
			std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
			std::shared_ptr<Assets::OperationContext> loadingContext);
		~CharacterScene();
	private:
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::ResourceConstructionContext> _constructionContext;
		std::shared_ptr<Assets::OperationContext> _loadingContext;
		std::shared_ptr<::AssetsNew::AssetHeap> _utilityHeap;

		Threading::Mutex _poolLock;
		
		std::vector<std::pair<uint64_t, std::weak_ptr<CharacterSceneInternal::ModelEntry>>> _modelEntries;
		std::vector<std::weak_ptr<CharacterSceneInternal::DeformerEntry>> _deformerEntries;
		std::vector<std::pair<uint64_t, std::weak_ptr<CharacterSceneInternal::AnimSetEntry>>> _animSetEntries;
		std::vector<std::weak_ptr<CharacterSceneInternal::RendererEntry>> _renderers;
		std::vector<CharacterSceneInternal::PendingUpdate> _pendingUpdates;
		std::vector<CharacterSceneInternal::PendingExceptionUpdate> _pendingExceptionUpdates;
	};


	std::shared_ptr<void> CharacterScene::CreateModel(std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> construction)
	{
		auto hash = construction->GetHash();	// todo -- what to do if the hash is disabled within ModelRendererConstruction?
		ScopedLock(_poolLock);
		auto i = LowerBound(_modelEntries, hash);
		if (i != _modelEntries.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l) return std::move(l);
		}

		auto newEntry = std::make_shared<CharacterSceneInternal::ModelEntry>();
		std::promise<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> promise;
		newEntry->_completedConstruction = promise.get_future();
		construction->FulfillWhenNotPending(std::move(promise));
		newEntry->_referenceHolder = std::move(construction);

		if (i != _modelEntries.end() && i->first == hash) {
			i->second = newEntry;		// rebuilding after previously expiring
		} else {
			_modelEntries.insert(i, {hash, newEntry});
		}
		return std::move(newEntry);
	}

	std::shared_ptr<void> CharacterScene::CreateModel(StringSection<> compoundObjectSrc)
	{
		auto hash = Hash64(compoundObjectSrc);
		ScopedLock(_poolLock);
		auto i = LowerBound(_modelEntries, hash);
		if (i != _modelEntries.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l) return std::move(l);
		}

		auto newEntry = std::make_shared<CharacterSceneInternal::ModelEntry>();
		auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>(_utilityHeap);
		util->_opContext = _loadingContext;
		auto compoundObjectScaffold = RenderCore::Assets::GetResolvedCompoundObjectScaffoldFuture(util, compoundObjectSrc);

		std::promise<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> promise;
		newEntry->_completedConstruction = promise.get_future();
		::Assets::WhenAll(compoundObjectScaffold).ThenConstructToPromise(
			std::move(promise),
			[](auto&& promise, const auto& compoundObjectScaffold) {
				// todo -- we can get the dep val at this point: std::get<::Assets::DependencyValidation()>(compoundObjectScaffold)
				compoundObjectScaffold.get()->FulfillWhenNotPending(std::move(promise));
			});

		if (i != _modelEntries.end() && i->first == hash) {
			i->second = newEntry;		// rebuilding after previously expiring
		} else {
			_modelEntries.insert(i, {hash, newEntry});
		}

		return std::move(newEntry);
	}

	std::shared_ptr<void> CharacterScene::CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction> construction)
	{
		// we can't hash this, so we always allocate a new one

		auto newEntry = std::make_shared<CharacterSceneInternal::DeformerEntry>();
		std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
		newEntry->_completedConstruction = promise.get_future();
		construction->FulfillWhenNotPending(std::move(promise), std::move(_deformAcceleratorPool->GetDevice()));
		newEntry->_referenceHolder = std::move(construction);
		
		ScopedLock(_poolLock);
		_deformerEntries.emplace_back(newEntry);
		return std::move(newEntry);
	}

	std::shared_ptr<void> CharacterScene::CreateDeformers(StringSection<> compoundObjectSrc, const OpaquePtr& opaqueModel)
	{
		// we can't hash this, so we always allocate a new one
		auto model = std::static_pointer_cast<CharacterSceneInternal::ModelEntry>(opaqueModel);

		auto newEntry = std::make_shared<CharacterSceneInternal::DeformerEntry>();
		auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>(_utilityHeap);
		util->_opContext = _loadingContext;
	
		std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
		newEntry->_completedConstruction = promise.get_future();
		::Assets::WhenAll(model->_completedConstruction).ThenConstructToPromise(
			std::move(promise),
			[device=_deformAcceleratorPool->GetDevice(), util, src=compoundObjectSrc.AsString()](auto&& promise, const auto& completedMRC) mutable {
				TRY {
					auto futureDeformer = RenderCore::Techniques::GetResolvedDeformerConfigurationFuture(util, completedMRC, src);
					YieldToPool(futureDeformer);
					auto construction = futureDeformer.get();

					// todo -- we can get the dep val at this point: std::get<::Assets::DependencyValidation()>(compoundObjectScaffold)
					construction.get()->FulfillWhenNotPending(std::move(promise), std::move(device));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		
		ScopedLock(_poolLock);
		_deformerEntries.emplace_back(newEntry);
		return std::move(newEntry);
	}

	std::shared_ptr<void> CharacterScene::CreateAnimationSet(StringSection<> str)
	{
		auto hash = Hash64(str);

		ScopedLock(_poolLock);
		auto i = LowerBound(_animSetEntries, hash);
		if (i != _animSetEntries.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l) return std::move(l);
		}

		auto newEntry = std::make_shared<CharacterSceneInternal::AnimSetEntry>();
		std::promise<std::shared_ptr<RenderCore::Assets::AnimationSetScaffold>> promise;
		newEntry->_animSetFuture = promise.get_future();
		::Assets::AutoConstructToPromise(std::move(promise), str);

		if (i != _animSetEntries.end() && i->first == hash) {
			i->second = newEntry;		// rebuilding after previously expiring
		} else {
			_animSetEntries.insert(i, {hash, newEntry});
		}
		return std::move(newEntry);
	}

	static std::future<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> CreateDefaultDeformerConstruction(
		std::shared_ptr<RenderCore::IDevice> device,
		std::shared_future<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> rendererConstruction)
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
		auto result = promise.get_future();
		::Assets::WhenAll(std::move(rendererConstruction)).ThenConstructToPromise(
			std::move(promise),
			[device=std::move(device)](auto&& promise, const auto& completedRendererConstruction) {
				TRY {
					auto deformerConstruction = std::make_shared<RenderCore::Techniques::DeformerConstruction>(completedRendererConstruction);
					if (auto* skinConfigure = RenderCore::Techniques::Services::GetInstance().FindDeformConfigure("gpu_skin"))
						skinConfigure->Configure(*deformerConstruction);
					if (!deformerConstruction->IsEmpty()) {
						// fulfill directly into the original promise
						deformerConstruction->FulfillWhenNotPending(std::move(promise), device);
					} else {
						promise.set_value(nullptr);
					}
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
	}

	std::shared_ptr<void> CharacterScene::CreateRenderer(
		std::shared_ptr<void> model,
		std::shared_ptr<void> deformers,
		std::shared_ptr<void> animationSet)
	{
		// We don't create many of the final types until we're ready to bind everything together in a renderer
		//		- DrawableConstructor
		// 		- AnimSetBinding
		//		- RendererSkeletonInterface
		// we could bind the deformers and model before hand, so that pair can be reused by a different animation set...?

		ScopedLock(_poolLock);
		for (const auto& renderer:_renderers) {
			auto l = renderer.lock();
			if (!l) continue;
			bool compatibleModel = l->_model == model && l->_deformer == deformers;
			bool compatibleAnimSet = l->_animSet == animationSet;
			// todo -- check invalidations
			if (compatibleModel && compatibleAnimSet)
				return l;		// can potentially decide to just share the Renderer part here
		}

		auto newEntry = std::make_shared<CharacterSceneInternal::RendererEntry>();
		newEntry->_model = std::static_pointer_cast<CharacterSceneInternal::ModelEntry>(model);
		newEntry->_animSet = std::static_pointer_cast<CharacterSceneInternal::AnimSetEntry>(animationSet);

		// todo -- check invalidations of compound object version of ModelEntry

		std::shared_future<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> deformerConstructionFuture;
		if (deformers) {
			// custom deformers given by caller
			newEntry->_deformer = std::static_pointer_cast<CharacterSceneInternal::DeformerEntry>(deformers);
			deformerConstructionFuture = newEntry->_deformer->_completedConstruction;
		} else {
			// no explicit deformers -- we must use the defaults
			deformerConstructionFuture = CreateDefaultDeformerConstruction(_pipelineAcceleratorPool->GetDevice(), newEntry->_model->_completedConstruction);
		}

		std::promise<CharacterSceneInternal::Renderer> rendererPromise;
		newEntry->_pendingRenderer = rendererPromise.get_future();

		::Assets::WhenAll(newEntry->_model->_completedConstruction, deformerConstructionFuture).ThenConstructToPromise(
			std::move(rendererPromise),
			[drawablesPool=_drawablesPool, pipelineAcceleratorPool=_pipelineAcceleratorPool, constructionContext=_constructionContext, deformAcceleratorPool=_deformAcceleratorPool](
				auto&& promise, 
				auto completedConstruction, auto completedDeformerConstruction) mutable {

				TRY {

					std::shared_ptr<RenderCore::Techniques::DeformAccelerator> deformAccelerator;
					std::shared_ptr<RenderCore::Techniques::IGeoDeformerConductor> geoAttachment;
					if (completedDeformerConstruction && !completedDeformerConstruction->IsEmpty()) {
						deformAccelerator = deformAcceleratorPool->CreateDeformAccelerator();
						geoAttachment = completedDeformerConstruction->GetGeoAttachment();
						if (geoAttachment)
							deformAcceleratorPool->Attach(*deformAccelerator, geoAttachment);
						if (auto uniformsAttachment = completedDeformerConstruction->GetUniformsAttachment())
							deformAcceleratorPool->Attach(*deformAccelerator, std::move(uniformsAttachment));
					}

					auto drawableConstructor = std::make_shared<RenderCore::Techniques::DrawableConstructor>(
						drawablesPool, std::move(pipelineAcceleratorPool), std::move(constructionContext),
						*completedConstruction, deformAcceleratorPool, deformAccelerator);

					if (geoAttachment) {
						::Assets::WhenAll(ToFuture(*drawableConstructor), geoAttachment->GetInitializationFuture()).ThenConstructToPromiseWithFutures(
							std::move(promise),
							[geoAttachment, deformAccelerator, completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture, std::shared_future<void>&& deformerInitFuture) mutable {
								deformerInitFuture.get();	// propagate exceptions

								CharacterSceneInternal::Renderer renderer;
								renderer._drawableConstructor = drawableConstructorFuture.get();
								renderer._completionCmdList = std::max(renderer._drawableConstructor->_completionCommandList, geoAttachment->GetCompletionCommandList());
								renderer._deformAccelerator = deformAccelerator;
								renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
								if (completedConstruction->GetElementCount() != 0) {
									renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModel();
									renderer._aabb = renderer._firstModelScaffold->GetStaticBoundingBox();
								} else {
									renderer._aabb = {Zero<Float3>(), Zero<Float3>()};
								}
								return renderer;
							});
					} else {
						::Assets::WhenAll(ToFuture(*drawableConstructor)).ThenConstructToPromiseWithFutures(
							std::move(promise),
							[completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture) mutable {
								CharacterSceneInternal::Renderer renderer;
								renderer._drawableConstructor = drawableConstructorFuture.get();
								renderer._completionCmdList = renderer._drawableConstructor->_completionCommandList;
								renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
								if (completedConstruction->GetElementCount() != 0)
									renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModel();
								return renderer;
							});
					}

				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END

			});

		std::promise<CharacterSceneInternal::Animator> animatorPromise;
		auto animatorFuture = animatorPromise.get_future();

		::Assets::WhenAll(newEntry->_pendingRenderer, newEntry->_animSet->_animSetFuture, newEntry->_model->_completedConstruction).ThenConstructToPromise(
			std::move(animatorPromise),
			[deformAcceleratorPool=_deformAcceleratorPool](const auto& renderer, auto animSet, auto modelConstruction) mutable {
				CharacterSceneInternal::Animator result;

				if (renderer._deformAccelerator) {
					auto* geoDeformers = deformAcceleratorPool->GetGeoDeformerConductor(*renderer._deformAccelerator).get();
					if (geoDeformers) {
						result._deformerSkeletonInterface = std::make_shared<RenderCore::Techniques::RendererSkeletonInterface>(
							renderer.GetSkeletonMachine().GetOutputInterface(),
							*geoDeformers);
					}
				}

				auto& animImmData = animSet->ImmutableData();
				result._animSetBinding = { animImmData._animationSet.GetOutputInterface(), renderer.GetSkeletonMachine() };
				result._animSet = std::move(animSet);

				// setup skeleton binding & initial pose for rigid parts
				result._modelToSkeletonBinding = RenderCore::Techniques::ModelConstructionSkeletonBinding { *modelConstruction };
				result._skeletonMachineOutput.resize(renderer.GetSkeletonMachine().GetOutputMatrixCount());
				renderer.GetSkeletonMachine().GenerateOutputTransforms(MakeIteratorRange(result._skeletonMachineOutput));
				return result;
			});

		::Assets::WhenAll(newEntry->_pendingRenderer, std::move(animatorFuture)).Then(
			[dstEntryWeak=std::weak_ptr<CharacterSceneInternal::RendererEntry>(newEntry), sceneWeak=weak_from_this()](auto rendererFuture, auto animatorFuture) {
				auto scene = sceneWeak.lock();
				if (!scene) return;

				ScopedLock(scene->_poolLock);
				TRY {
					auto renderer = rendererFuture.get();
					auto animator = animatorFuture.get();
					scene->_pendingUpdates.emplace_back(CharacterSceneInternal::PendingUpdate { dstEntryWeak, std::move(renderer), std::move(animator) });
				} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
					scene->_pendingExceptionUpdates.emplace_back(CharacterSceneInternal::PendingExceptionUpdate { dstEntryWeak, e.GetActualizationLog(), e.GetDependencyValidation() });
				} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
					scene->_pendingExceptionUpdates.emplace_back(CharacterSceneInternal::PendingExceptionUpdate { dstEntryWeak, e.GetActualizationLog(), e.GetDependencyValidation() });
				} CATCH(const std::exception& e) {
					scene->_pendingExceptionUpdates.emplace_back(CharacterSceneInternal::PendingExceptionUpdate { dstEntryWeak, ::Assets::AsBlob(e.what()) });
				} CATCH_END
			});

		_renderers.emplace_back(newEntry);
		return newEntry;
	}

	std::shared_future<RenderCore::Assets::SkeletonBinding> CharacterScene::CreateSkeletonBinding(OpaquePtr renderer, IteratorRange<const uint64_t*> inputInterface)
	{
		assert(renderer);
		ScopedLock(_poolLock);

		std::promise<RenderCore::Assets::SkeletonBinding> promise;
		auto result = promise.get_future();

		auto& rendererEntry = *((const CharacterSceneInternal::RendererEntry*)renderer.get());
		if (rendererEntry._pendingRenderer.valid()) {
			::Assets::WhenAll(rendererEntry._pendingRenderer).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[ii = std::vector<uint64_t>{inputInterface.begin(), inputInterface.end()}](const auto& renderer) {
					return RenderCore::Assets::SkeletonBinding { renderer.GetSkeletonMachine().GetOutputInterface(), ii };
				});
		} else {
			promise.set_value(
				RenderCore::Assets::SkeletonBinding {
					rendererEntry._renderer.GetSkeletonMachine().GetOutputInterface(),
					inputInterface });
		}

		return result;
	}

	static CharacterScene::SkeletonMachine MakeCharacterSceneSkeletonMachine(const RenderCore::Assets::SkeletonMachine& skm)
	{
		CharacterScene::SkeletonMachine result;
		result._outputInterface.insert(result._outputInterface.end(), skm.GetOutputInterface()._outputMatrixNames, skm.GetOutputInterface()._outputMatrixNames+skm.GetOutputInterface()._outputMatrixNameCount);
		result._cmdStream.insert(result._cmdStream.end(), skm.GetCommandStream().begin(),skm.GetCommandStream().end());
		return result;
	}

	auto CharacterScene::GetSkeletonMachine(OpaquePtr renderer) -> std::shared_future<SkeletonMachine>
	{
		assert(renderer);
		ScopedLock(_poolLock);

		std::promise<SkeletonMachine> promise;
		auto result = promise.get_future();

		auto& rendererEntry = *((const CharacterSceneInternal::RendererEntry*)renderer.get());
		if (rendererEntry._pendingRenderer.valid()) {
			::Assets::WhenAll(rendererEntry._pendingRenderer).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[](const auto& renderer) -> SkeletonMachine {
					return MakeCharacterSceneSkeletonMachine(renderer.GetSkeletonMachine());
				});
		} else
			promise.set_value(MakeCharacterSceneSkeletonMachine(rendererEntry._renderer.GetSkeletonMachine()));
		return result;
	}

	auto CharacterScene::GetModelRendererConstruction(OpaquePtr model) -> std::shared_future<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>>
	{
		assert(model);
		ScopedLock(_poolLock);

		std::promise<SkeletonMachine> promise;
		auto result = promise.get_future();

		auto& modelEntry = *((const CharacterSceneInternal::ModelEntry*)model.get());
		return modelEntry._completedConstruction;
	}

	auto CharacterScene::GetDeformAccelerator(OpaquePtr renderer) -> std::shared_future<std::shared_ptr<RenderCore::Techniques::DeformAccelerator>>
	{
		assert(renderer);
		ScopedLock(_poolLock);

		std::promise<std::shared_ptr<RenderCore::Techniques::DeformAccelerator>> promise;
		auto result = promise.get_future();

		auto& rendererEntry = *((const CharacterSceneInternal::RendererEntry*)renderer.get());
		if (rendererEntry._pendingRenderer.valid()) {
			::Assets::WhenAll(rendererEntry._pendingRenderer).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[](const auto& renderer) {
					return renderer._deformAccelerator;
				});
		} else
			promise.set_value(rendererEntry._renderer._deformAccelerator);
		return result;
	}

	RenderCore::BufferUploads::CommandListID CharacterScene::GetCompletionCommandList(void* renderer)
	{
		return ((const CharacterSceneInternal::RendererEntry*)renderer)->_renderer._completionCmdList;
	}

	unsigned CharacterInstanceAllocate(void* renderer)
	{
		auto* realRenderer = (CharacterSceneInternal::RendererEntry*)renderer;
		return realRenderer->_allocatedInstances.Allocate();
	}

	void CharacterInstanceRelease(void* renderer, unsigned instanceIdx)
	{
		auto* realRenderer = (CharacterSceneInternal::RendererEntry*)renderer;
		realRenderer->_allocatedInstances.Deallocate(instanceIdx);
	}

	void ICharacterScene::BuildDrawablesHelper::BuildDrawables(
		unsigned instanceIdx,
		const Float3x4& localToWorld, uint32_t viewMask, uint64_t cmdStream)
	{
		assert(cmdStream == 0);
		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			localToWorld, instanceIdx, viewMask);
		_deformersPacket->Queue(*_activeRenderer->_deformAccelerator, instanceIdx);
	}

	void ICharacterScene::BuildDrawablesHelper::CullAndBuildDrawables(
		unsigned instanceIdx, const Float3x4& localToWorld)
	{
		if (_complexCullingVolume && _complexCullingVolume->TestAABB(localToWorld, _activeRenderer->_aabb.first, _activeRenderer->_aabb.second) == CullTestResult::Culled)
			return;
		uint32_t viewMask = 0;
		for (unsigned v=0; v<_views.size(); ++v) {
			auto localToClip = Combine(localToWorld, _views[v]._worldToProjection);
			viewMask |= (!CullAABB(localToClip, _activeRenderer->_aabb.first, _activeRenderer->_aabb.second, RenderCore::Techniques::GetDefaultClipSpaceType())) << v;
		}
		if (!viewMask) return;

		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			localToWorld, instanceIdx, viewMask);
		_deformersPacket->Queue(*_activeRenderer->_deformAccelerator, instanceIdx);
	}

	void ICharacterScene::BuildDrawablesHelper::CullAndBuildDrawables(
		unsigned instanceIdx, const Float3& translation, const Float3x3& rotation, float uniformScale)
	{
		auto composedTransform = Expand3x4(rotation, translation);
		if (_complexCullingVolume && _complexCullingVolume->TestAABB(composedTransform, uniformScale*_activeRenderer->_aabb.first, uniformScale*_activeRenderer->_aabb.second) == CullTestResult::Culled)
			return;

		Combine_IntoRHS(UniformScale{uniformScale}, composedTransform);
		uint32_t viewMask = 0;
		for (unsigned v=0; v<_views.size(); ++v) {
			auto localToClip = Combine(composedTransform, _views[v]._worldToProjection);
			viewMask |= (!CullAABB(localToClip, _activeRenderer->_aabb.first, _activeRenderer->_aabb.second, RenderCore::Techniques::GetDefaultClipSpaceType())) << v;
		}
		if (!viewMask) return;

		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			composedTransform, instanceIdx, viewMask);
		_deformersPacket->Queue(*_activeRenderer->_deformAccelerator, instanceIdx);
	}

	bool ICharacterScene::BuildDrawablesHelper::SetRenderer(void* renderer)
	{
		auto* rendererEntry = (CharacterSceneInternal::RendererEntry*)renderer;
		_activeRenderer = &rendererEntry->_renderer;
		return _activeRenderer->_drawableConstructor != nullptr;
	}

	ICharacterScene::BuildDrawablesHelper::BuildDrawablesHelper(
		ICharacterScene& scene,
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
		RenderCore::Techniques::DeformersPacket* deformersPacket,
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> views,
		const XLEMath::ArbitraryConvexVolumeTester* complexCullingVolume)
	: _pkts(pkts), _deformersPacket(deformersPacket)
	, _activeRenderer(nullptr)
	, _views(views), _complexCullingVolume(complexCullingVolume)
	{}

	ICharacterScene::BuildDrawablesHelper::BuildDrawablesHelper(
		ICharacterScene& scene,
		SceneEngine::ExecuteSceneContext& executeContext)
	: _pkts(executeContext._destinationPkts), _deformersPacket(executeContext._deformersPacket)
	, _activeRenderer(nullptr)
	, _views(executeContext._views), _complexCullingVolume(executeContext._complexCullingVolume)
	{
		
	}

	bool ICharacterScene::AnimationConfigureHelper::SetRenderer(void* renderer)
	{
		auto* realRenderer = (CharacterSceneInternal::RendererEntry*)renderer;
		if (realRenderer->_renderer._drawableConstructor) {
			_activeAnimator = &realRenderer->_animator;
			_activeSkeletonMachine = &realRenderer->_renderer.GetSkeletonMachine();
			return true;
		} else {
			_activeAnimator = nullptr;
			_activeSkeletonMachine = nullptr;
			return false;
		}
	}

	void ICharacterScene::AnimationConfigureHelper::ApplySingleAnimation(unsigned instanceIdx, uint64_t id, float time)
	{
		assert(_activeAnimator);
		assert(_activeAnimator->_deformerSkeletonInterface);

		// Get the animation parameter set for this anim state, and run the skeleton machine with those parameters
		auto parameterBlockSize = _activeAnimator->_animSetBinding.GetParameterDefaultsBlock().size();
		VLA(uint8_t, parameterBlock, parameterBlockSize);
		std::memcpy(parameterBlock, _activeAnimator->_animSetBinding.GetParameterDefaultsBlock().begin(), parameterBlockSize);

		// calculate animated parameters
		_activeAnimator->_animSet->ImmutableData()._animationSet.CalculateOutput(
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]),
			RenderCore::Assets::AnimationState{time, id},
			_activeAnimator->_animSetBinding.GetParameterBindingRules());

		// generate the joint transforms based on the animation parameters
		assert(_activeAnimator->_skeletonMachineOutput.size() == _activeSkeletonMachine->GetOutputMatrixCount());
		_activeAnimator->_animSetBinding.GenerateOutputTransforms(
			MakeIteratorRange(_activeAnimator->_skeletonMachineOutput),
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]));

		// set the skeleton machine output to the deformer
		_activeAnimator->_deformerSkeletonInterface->FeedInSkeletonMachineResults(
			instanceIdx, MakeIteratorRange(_activeAnimator->_skeletonMachineOutput));
	}

	void ICharacterScene::AnimationConfigureHelper::ApplyAnimation(unsigned instanceIdx, const uint64_t ids[], const float times[], const float weights[], unsigned animationCount)
	{
		assert(_activeAnimator);
		assert(_activeAnimator->_deformerSkeletonInterface);

		assert(animationCount >= 1);

		// Get the animation parameter set for this anim state, and run the skeleton machine with those parameters
		auto& bind = _activeAnimator->_animSetBinding;
		auto paramDefaults = bind.GetParameterDefaultsBlock();
		auto parameterBlockSize = paramDefaults.size();
		VLA(uint8_t, parameterBlock, parameterBlockSize);
		std::memcpy(parameterBlock, paramDefaults.begin(), parameterBlockSize);
		
		// calculate animated parameters
		_activeAnimator->_animSet->ImmutableData()._animationSet.CalculateOutput(
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]),
			RenderCore::Assets::AnimationState{times[0], ids[0]},
			bind.GetParameterBindingRules());

		for (auto o:bind._float1ParameterOffsets) *(float*)PtrAdd(parameterBlock, o) = LinearInterpolate(*(float*)PtrAdd(paramDefaults.begin(), o), *(float*)PtrAdd(parameterBlock, o), weights[0]);
		for (auto o:bind._float3ParameterOffsets) *(Float3*)PtrAdd(parameterBlock, o) = LinearInterpolate(*(Float3*)PtrAdd(paramDefaults.begin(), o), *(Float3*)PtrAdd(parameterBlock, o), weights[0]);
		for (auto o:bind._float4ParameterOffsets) *(Float4*)PtrAdd(parameterBlock, o) = LinearInterpolate(*(Float4*)PtrAdd(paramDefaults.begin(), o), *(Float4*)PtrAdd(parameterBlock, o), weights[0]);
		for (auto o:bind._float4x4ParameterOffsets) *(Float4x4*)PtrAdd(parameterBlock, o) = LinearInterpolate(*(Float4x4*)PtrAdd(paramDefaults.begin(), o), *(Float4x4*)PtrAdd(parameterBlock, o), weights[0]);
		for (auto o:bind._quaternionParameterOffsets) *(Quaternion*)PtrAdd(parameterBlock, o) = SphericalInterpolate(*(Quaternion*)PtrAdd(paramDefaults.begin(), o), *(Quaternion*)PtrAdd(parameterBlock, o), weights[0]);

		VLA(uint8_t, parameterBlockTemp, parameterBlockSize);
		for (unsigned a=1; a<animationCount; ++a) {
			_activeAnimator->_animSet->ImmutableData()._animationSet.CalculateOutput(
				MakeIteratorRange(parameterBlockTemp, &parameterBlockTemp[parameterBlockSize]),
				RenderCore::Assets::AnimationState{times[a], ids[a]},
				bind.GetParameterBindingRules());

			float w = weights[a];
			for (auto o:bind._float1ParameterOffsets) *(float*)PtrAdd(parameterBlock, o) += (*(float*)PtrAdd(parameterBlockTemp, o) - *(float*)PtrAdd(paramDefaults.begin(), o)) * w;
			for (auto o:bind._float3ParameterOffsets) *(Float3*)PtrAdd(parameterBlock, o) += (*(Float3*)PtrAdd(parameterBlockTemp, o) - *(Float3*)PtrAdd(paramDefaults.begin(), o)) * w;
			for (auto o:bind._float4ParameterOffsets) *(Float4*)PtrAdd(parameterBlock, o) += (*(Float4*)PtrAdd(parameterBlockTemp, o) - *(Float4*)PtrAdd(paramDefaults.begin(), o)) * w;
			for (auto o:bind._float4x4ParameterOffsets) *(Float4x4*)PtrAdd(parameterBlock, o) += (*(Float4x4*)PtrAdd(parameterBlockTemp, o) - *(Float4x4*)PtrAdd(paramDefaults.begin(), o)) * w;
			for (auto o:bind._quaternionParameterOffsets) {
				// simple multi-slerp. Imperfect blending, but we can afford a little roughness
				auto existingWeight = cml::dot(*(Quaternion*)PtrAdd(parameterBlock, o), *(Quaternion*)PtrAdd(paramDefaults.begin(), o));
				existingWeight = 0.5f - 0.5f * existingWeight; assert(existingWeight >= -1e-3f && existingWeight <= (1.f+1e-3f));
				*(Quaternion*)PtrAdd(parameterBlock, o) = SphericalInterpolate(*(Quaternion*)PtrAdd(parameterBlock, o), *(Quaternion*)PtrAdd(parameterBlockTemp, o), w / (existingWeight+w));
			}
		}

		// generate the joint transforms based on the animation parameters
		assert(_activeAnimator->_skeletonMachineOutput.size() == _activeSkeletonMachine->GetOutputMatrixCount());
		bind.GenerateOutputTransforms(
			MakeIteratorRange(_activeAnimator->_skeletonMachineOutput),
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]));

		// set the skeleton machine output to the deformer
		_activeAnimator->_deformerSkeletonInterface->FeedInSkeletonMachineResults(
			instanceIdx, MakeIteratorRange(_activeAnimator->_skeletonMachineOutput));
	}

	void ICharacterScene::AnimationConfigureHelper::ApplyAnimation(unsigned instanceIdx, IteratorRange<const Float4x4*> skeletonMachineOutput)
	{
		assert(_activeAnimator);
		assert(_activeAnimator->_deformerSkeletonInterface);

		// set the skeleton machine output to the deformer
		_activeAnimator->_deformerSkeletonInterface->FeedInSkeletonMachineResults(
			instanceIdx, skeletonMachineOutput);
	}

	IteratorRange<const Float4x4*> ICharacterScene::AnimationConfigureHelper::GetSkeletonMachineOutput()
	{
		assert(_activeAnimator);
		return _activeAnimator->_skeletonMachineOutput;
	}

	ICharacterScene::AnimationConfigureHelper::AnimationConfigureHelper(ICharacterScene& scene)
	: _scene(&scene), _activeAnimator(nullptr), _activeSkeletonMachine(nullptr)
	{}

	void CharacterScene::OnFrameBarrier()
	{
		// flush out any pending updates
		ScopedLock(_poolLock);
		for (auto&u:_pendingUpdates) {
			auto l = u._dst.lock();
			if (!l) continue;
			l->_renderer = std::move(u._renderer);
			l->_animator = std::move(u._animator);
			l->_pendingRenderer = {};
			// todo -- set dep val
		}
		_pendingUpdates.clear();
		for (auto&u:_pendingExceptionUpdates) {
			auto l = u._dst.lock();
			if (!l) continue;
			l->_depVal = std::move(u._depVal);
			l->_pendingRenderer = {};
			// todo -- record exception msg
		}
		_pendingExceptionUpdates.clear();
		// todo -- check invalidations
	}

	void CharacterScene::CancelConstructions()
	{
		if (_constructionContext)
			_constructionContext->Cancel();
	}

	std::shared_ptr<Assets::OperationContext> CharacterScene::GetLoadingContext()
	{
		return _loadingContext;
	}

	CharacterScene::CharacterScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<Assets::OperationContext> loadingContext)
	: _drawablesPool(std::move(drawablesPool))
	, _pipelineAcceleratorPool(std::move(pipelineAcceleratorPool))
	, _deformAcceleratorPool(std::move(deformAcceleratorPool))
	, _loadingContext(std::move(loadingContext))
	{
		using namespace RenderCore;
		if (bufferUploads) {
			auto repositionableGeometry = std::make_shared<Techniques::RepositionableGeometryConduit>(
				BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, BindFlag::VertexBuffer, 1024*1024, RenderCore::BufferUploads::s_batchedResultsDefaultAlignment),
				BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, BindFlag::IndexBuffer, 1024*1024, RenderCore::BufferUploads::s_batchedResultsIndexAlignment));
			_constructionContext = std::make_shared<Techniques::ResourceConstructionContext>(bufferUploads, std::move(repositionableGeometry));
		}
		_utilityHeap = std::make_shared<::AssetsNew::AssetHeap>();
	}
	CharacterScene::~CharacterScene() = default;
	ICharacterScene::~ICharacterScene() = default;

	std::shared_ptr<ICharacterScene> CreateCharacterScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<Assets::OperationContext> loadingContext)
	{
		return std::make_shared<CharacterScene>(std::move(drawablesPool), std::move(pipelineAcceleratorPool), std::move(deformAcceleratorPool), std::move(bufferUploads), std::move(loadingContext));
	}

}

