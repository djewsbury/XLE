// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CharacterScene.h"
#include "../../RenderCore/Techniques/ModelRendererConstruction.h"
#include "../../RenderCore/Techniques/DeformerConstruction.h"
#include "../../RenderCore/Techniques/ResourceConstructionContext.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../RenderCore/Techniques/DrawableConstructor.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"		// for RendererSkeletonInterface
#include "../../RenderCore/Techniques/DeformGeometryInfrastructure.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderCore/Techniques/LightWeightBuildDrawables.h"
#include "../../RenderCore/BufferUploads/IBufferUploads.h"
#include "../../RenderCore/BufferUploads/BatchedResources.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/AnimationScaffoldInternal.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Utility/Threading/Mutex.h"
#include <future>

namespace SceneEngine
{
	namespace Internal
	{
		struct ModelEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>> _completedConstruction;
			std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction> _referenceHolder;
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
			std::shared_ptr<RenderCore::Techniques::RendererSkeletonInterface> _skeletonInterface;
			std::shared_ptr<RenderCore::Assets::AnimationSetScaffold> _animSet;
			RenderCore::Assets::AnimationSetBinding _animSetBinding;
		};

		struct RendererEntry
		{
			std::shared_ptr<ModelEntry> _model;
			std::shared_ptr<DeformerEntry> _deformer;
			std::shared_ptr<AnimSetEntry> _animSet;
			::Assets::Marker<Renderer> _rendererMarker;
			::Assets::Marker<Animator> _animatorMarker;
		};
	}

	class CharacterScene : public ICharacterScene
	{
	public:
		OpaquePtr CreateModel(std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>) override;
		OpaquePtr CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction>) override;
		OpaquePtr CreateAnimationSet(StringSection<>) override;
		OpaquePtr CreateRenderer(OpaquePtr model, OpaquePtr deformers, OpaquePtr animationSet) override;

		std::unique_ptr<CmdListBuilder, void(*)(CmdListBuilder*)> BeginCmdList(
			RenderCore::Techniques::ParsingContext& parsingContext,
			IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts) override;
		void OnFrameBarrier() override;
		void CancelConstructions() override;

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

		Threading::Mutex _poolLock;
		
		std::vector<std::pair<uint64_t, std::weak_ptr<Internal::ModelEntry>>> _modelEntries;
		std::vector<std::weak_ptr<Internal::DeformerEntry>> _deformerEntries;
		std::vector<std::pair<uint64_t, std::weak_ptr<Internal::AnimSetEntry>>> _animSetEntries;
		std::vector<std::weak_ptr<Internal::RendererEntry>> _renderers;
	};


	std::shared_ptr<void> CharacterScene::CreateModel(std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction> construction)
	{
		auto hash = construction->GetHash();	// todo -- what to do if the hash is disabled within ModelRendererConstruction?
		ScopedLock(_poolLock);
		auto i = LowerBound(_modelEntries, hash);
		if (i != _modelEntries.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l) return std::move(l);
		}

		auto newEntry = std::make_shared<Internal::ModelEntry>();
		std::promise<std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>> promise;
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

	std::shared_ptr<void> CharacterScene::CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction> construction)
	{
		// we can't hash this, so we always allocate a new one

		auto newEntry = std::make_shared<Internal::DeformerEntry>();
		std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
		newEntry->_completedConstruction = promise.get_future();
		construction->FulfillWhenNotPending(std::move(promise));
		newEntry->_referenceHolder = std::move(construction);
		
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

		auto newEntry = std::make_shared<Internal::AnimSetEntry>();
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

	static std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>> ToFuture(RenderCore::Techniques::DrawableConstructor& construction)
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
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
			if (compatibleModel && compatibleAnimSet)
				return l;		// can potentially decide to just share the Renderer part here
		}

		auto newEntry = std::make_shared<Internal::RendererEntry>();
		newEntry->_model = std::static_pointer_cast<Internal::ModelEntry>(model);
		newEntry->_deformer = std::static_pointer_cast<Internal::DeformerEntry>(deformers);
		newEntry->_animSet = std::static_pointer_cast<Internal::AnimSetEntry>(animationSet);

		::Assets::WhenAll(newEntry->_model->_completedConstruction, newEntry->_deformer->_completedConstruction).ThenConstructToPromise(
			newEntry->_rendererMarker.AdoptPromise(),
			[drawablesPool=_drawablesPool, pipelineAcceleratorPool=_pipelineAcceleratorPool, constructionContext=_constructionContext, deformAcceleratorPool=_deformAcceleratorPool](
				auto&& promise, 
				auto completedConstruction, auto completedDeformerConstruction) mutable {

				auto geoDeformer = RenderCore::Techniques::CreateDeformGeoAttachment(
					*pipelineAcceleratorPool->GetDevice(), *completedConstruction, *completedDeformerConstruction);
				auto deformAccelerator = deformAcceleratorPool->CreateDeformAccelerator();
				deformAcceleratorPool->Attach(*deformAccelerator, geoDeformer);

				auto drawableConstructor = std::make_shared<RenderCore::Techniques::DrawableConstructor>(
					drawablesPool, std::move(pipelineAcceleratorPool), std::move(constructionContext),
					*completedConstruction, deformAcceleratorPool, deformAccelerator);

				::Assets::WhenAll(ToFuture(*drawableConstructor), geoDeformer->GetInitializationFuture()).ThenConstructToPromiseWithFutures(
					std::move(promise),
					[geoDeformer, deformAccelerator, completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture, std::shared_future<void>&& deformerInitFuture) mutable {
						deformerInitFuture.get();	// propagate exceptions

						Internal::Renderer renderer;
						renderer._drawableConstructor = drawableConstructorFuture.get();
						renderer._completionCmdList = std::max(renderer._drawableConstructor->_completionCommandList, geoDeformer->GetCompletionCommandList());
						renderer._deformAccelerator = deformAccelerator;
						renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
						if (completedConstruction->GetElementCount() != 0)
							renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModelScaffold();
						return renderer;
					});
			});

		::Assets::WhenAll(newEntry->_rendererMarker.ShareFuture(), newEntry->_animSet->_animSetFuture).ThenConstructToPromise(
			newEntry->_animatorMarker.AdoptPromise(),
			[deformAcceleratorPool=_deformAcceleratorPool](const auto& renderer, auto animSet) mutable {
				Internal::Animator result;

				auto* geoDeformers = deformAcceleratorPool->GetDeformGeoAttachment(*renderer._deformAccelerator).get();
				if (geoDeformers) {
					result._skeletonInterface = std::make_shared<RenderCore::Techniques::RendererSkeletonInterface>(
						renderer.GetSkeletonMachine().GetOutputInterface(),
						*geoDeformers);
				}

				auto& animImmData = animSet->ImmutableData();
				result._animSetBinding = { animImmData._animationSet.GetOutputInterface(), renderer.GetSkeletonMachine() };
				result._animSet = std::move(animSet);
				return result;
			});

		return newEntry;
	}

	namespace Internal
	{
		class CharacterSceneRealCmdListBuilder
		{
		public:
			const Renderer* _activeRenderer = nullptr;
			const Animator* _activeAnimator = nullptr;
			unsigned _currentInstanceIdx = 0;
			CharacterSceneRealCmdListBuilder(RenderCore::Techniques::ParsingContext& parsingContext, IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts) 
			: _parsingContext(&parsingContext), _pkts(pkts.begin(), pkts.end()) {}
			~CharacterSceneRealCmdListBuilder() = default;
			RenderCore::Techniques::ParsingContext* _parsingContext = nullptr;
			std::vector<RenderCore::Techniques::DrawablesPacket*> _pkts;

			#if defined(_DEBUG)
				std::vector<const Renderer*> _alreadyVisitedRenderers;
			#endif
		};
	}

	void CharacterScene::CmdListBuilder::BeginRenderer(void* renderer)
	{
		auto* that = (Internal::CharacterSceneRealCmdListBuilder*)this;
		auto* rendererEntry = (Internal::RendererEntry*)renderer;
		that->_activeRenderer = rendererEntry->_rendererMarker.TryActualize();
		that->_activeAnimator = rendererEntry->_animatorMarker.TryActualize();
		that->_currentInstanceIdx = 0;

		#if defined(_DEBUG)
			// ensure that we haven't used this renderer before in this cmd list. This isn't supported because the instance idx
			// is reset to 0 each time
			assert(std::find(that->_alreadyVisitedRenderers.begin(), that->_alreadyVisitedRenderers.end(), that->_activeRenderer) == that->_alreadyVisitedRenderers.end());
			that->_alreadyVisitedRenderers.push_back(that->_activeRenderer);
		#endif
	}

	void CharacterScene::CmdListBuilder::ApplyAnimation(uint64_t id, float time)
	{
		auto* that = (Internal::CharacterSceneRealCmdListBuilder*)this;
		auto* animator = that->_activeAnimator;
		if (!animator || !that->_activeRenderer) return;

		// Get the animation parameter set for this anim state, and run the skeleton machine with those parameters
		const auto& skelMachine = that->_activeRenderer->GetSkeletonMachine();

		auto parameterBlockSize = animator->_animSetBinding.GetParameterDefaultsBlock().size();
		uint8_t parameterBlock[parameterBlockSize];
		std::memcpy(parameterBlock, animator->_animSetBinding.GetParameterDefaultsBlock().begin(), parameterBlockSize);

		// calculate animated parameters
		animator->_animSet->ImmutableData()._animationSet.CalculateOutput(
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]),
			RenderCore::Assets::AnimationState{time, id},
			animator->_animSetBinding.GetParameterBindingRules());

		// generate the joint transforms based on the animation parameters
		Float4x4 skeletonMachineOutput[skelMachine.GetOutputMatrixCount()];
		animator->_animSetBinding.GenerateOutputTransforms(
			MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[skelMachine.GetOutputMatrixCount()]),
			MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]));

		// set the skeleton machine output to the deformer
		animator->_skeletonInterface->FeedInSkeletonMachineResults(
			that->_currentInstanceIdx, MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[skelMachine.GetOutputMatrixCount()]));
	}

	void CharacterScene::CmdListBuilder::RenderInstance(const Float3x4& localToWorld, uint32_t viewMask, uint64_t cmdStream)
	{
		auto* that = (Internal::CharacterSceneRealCmdListBuilder*)this;
		if (!that->_activeRenderer) return;

		assert(cmdStream == 0);
		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*that->_activeRenderer->_drawableConstructor,
			MakeIteratorRange(that->_pkts),
			localToWorld, that->_currentInstanceIdx, viewMask);
	}

	void CharacterScene::CmdListBuilder::NextInstance()
	{
		auto* that = (Internal::CharacterSceneRealCmdListBuilder*)this;
		that->_currentInstanceIdx = 0;
	}

	auto CharacterScene::BeginCmdList(RenderCore::Techniques::ParsingContext& parsingContext, IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts) -> std::unique_ptr<CmdListBuilder, void(*)(CmdListBuilder*)>
	{
		return std::unique_ptr<CmdListBuilder, void(*)(CmdListBuilder*)>(
			(CmdListBuilder*)new Internal::CharacterSceneRealCmdListBuilder(parsingContext, pkts),
			[](CmdListBuilder* ptr) { delete (Internal::CharacterSceneRealCmdListBuilder*)ptr; });
	}

	void CharacterScene::OnFrameBarrier()
	{}

	void CharacterScene::CancelConstructions()
	{
		if (_constructionContext)
			_constructionContext->Cancel();
	}

	CharacterScene::CharacterScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<Assets::OperationContext> loadingContext)
	{
		using namespace RenderCore;
		if (bufferUploads) {
			auto repositionableGeometry = std::make_shared<Techniques::RepositionableGeometryConduit>(
				BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, BindFlag::VertexBuffer, 1024*1024),
				BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, BindFlag::IndexBuffer, 1024*1024));
			_constructionContext = std::make_shared<Techniques::ResourceConstructionContext>(bufferUploads, std::move(repositionableGeometry));
		}
	}
	CharacterScene::~CharacterScene() = default;

}

