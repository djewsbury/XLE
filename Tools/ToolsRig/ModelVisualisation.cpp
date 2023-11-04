// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelVisualisation.h"
#include "VisualisationUtils.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/AnimationScaffoldInternal.h"
#include "../../RenderCore/Techniques/SkinDeformer.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../RenderCore/Techniques/DeformGeometryInfrastructure.h"
#include "../../RenderCore/Techniques/DeformerConstruction.h"
#include "../../RenderCore/Assets/ModelRendererConstruction.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderOverlays/AnimationVisualization.h"
#include "../../SceneEngine/IScene.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/IArtifact.h"
#include "../../OSServices/TimeUtils.h"

#pragma warning(disable:4505)

namespace ToolsRig
{
	using RenderCore::Assets::ModelScaffold;
	using RenderCore::Assets::MaterialScaffold;
	using RenderCore::Assets::AnimationSetScaffold;
	using RenderCore::Assets::SkeletonScaffold;
	using RenderCore::Assets::SkeletonMachine;
	using RenderCore::Assets::ModelRendererConstruction;
	using RenderCore::Techniques::SimpleModelRenderer;
	using RenderCore::Techniques::ICustomDrawDelegate;
	using RenderCore::Techniques::RendererSkeletonInterface;
	using RenderCore::Techniques::DeformerConstruction;

///////////////////////////////////////////////////////////////////////////////////////////////////

	class MaterialFilterDelegate : public RenderCore::Techniques::ICustomDrawDelegate
	{
	public:
		virtual void OnDraw(
			RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& executeContext,
			const RenderCore::Techniques::Drawable& d) override
		{
			if (GetMaterialGuid(d) == _activeMaterial)
				ExecuteStandardDraw(parsingContext, executeContext, d);
		}

		MaterialFilterDelegate(uint64_t activeMaterial) : _activeMaterial(activeMaterial) {}
	private:
		uint64_t _activeMaterial;
	};

	static std::shared_ptr<RendererSkeletonInterface> BuildSkeletonInterface(
		SimpleModelRenderer& renderer,
		RenderCore::Techniques::IDeformAcceleratorPool& deformAccelerators,
		const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface)
	{
		auto* deformAcc = renderer.GetDeformAccelerator().get();
		if (!deformAcc) return nullptr;
		auto* deformerInfrastructure = deformAccelerators.GetDeformGeoAttachment(*deformAcc).get();
		if (deformerInfrastructure)
			return std::make_shared<RendererSkeletonInterface>(smOutputInterface, *deformerInfrastructure);
		return nullptr;
	}

	struct ModelSceneRendererState
	{
		std::shared_ptr<SimpleModelRenderer>		_renderer;
		std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> _rendererConstruction;
		std::shared_ptr<ModelScaffold>	_modelScaffoldForEmbeddedSkeleton;
		std::shared_ptr<SkeletonScaffold>			_skeletonScaffold;
		std::shared_ptr<AnimationSetScaffold>		_animationScaffold;
		std::shared_ptr<RendererSkeletonInterface>	_skeletonInterface;
		RenderCore::Assets::AnimationSetBinding		_animSetBinding;
		::Assets::DependencyValidation				_depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		const SkeletonMachine* GetSkeletonMachine() const
		{
			const SkeletonMachine* skeletonMachine = nullptr;
			if (_skeletonScaffold) {
				skeletonMachine = &_skeletonScaffold->GetSkeletonMachine();
			} else if (_modelScaffoldForEmbeddedSkeleton)
				skeletonMachine = _modelScaffoldForEmbeddedSkeleton->EmbeddedSkeleton();
			return skeletonMachine;
		}

		void BindAnimState(VisAnimationState& animState)
		{
			animState._animationList.clear();
			if (_animationScaffold) {
				for (const auto&anim:_animationScaffold->ImmutableData()._animationSet.GetAnimations()) {
					auto query = _animationScaffold->ImmutableData()._animationSet.FindAnimation(anim.first);
					assert(query.has_value());
					if (!query->_stringName.IsEmpty()) {
						animState._animationList.push_back({query->_stringName.AsString(), 0.f, query->_durationInFrames / query->_framesPerSecond});
					} else {
						char buffer[64];
						XlUI64toA(anim.first, buffer, dimof(buffer), 16);
						animState._animationList.push_back({buffer, 0.f, query->_durationInFrames / query->_framesPerSecond});
					}
				}
			}
			animState._changeEvent.Invoke();
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ModelSceneRendererState>>&& promise,
			const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>& drawablesPool,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators,
			const std::shared_ptr<::Assets::OperationContext>& loadingContext,
			const ModelVisSettings& settings)
		{
			auto construction = std::make_shared<ModelRendererConstruction>();
			construction->SetOperationContext(loadingContext);
			auto ele = construction->AddElement();
			if (!settings._compilationConfigurationName.empty())
				ele.SetCompilationConfiguration(settings._compilationConfigurationName);
			ele.SetModelAndMaterialScaffolds(settings._modelName, settings._materialName);
			if (!settings._skeletonFileName.empty())
				construction->SetSkeletonScaffold(settings._skeletonFileName);

			std::shared_future<std::shared_ptr<AnimationSetScaffold>> futureAnimationSet;
			if (!settings._animationFileName.empty())
				futureAnimationSet = ::Assets::GetAssetFuturePtr<AnimationSetScaffold>(settings._animationFileName);
			
			ConstructToPromise(std::move(promise), drawablesPool, pipelineAcceleratorPool, deformAccelerators, loadingContext, construction, std::move(futureAnimationSet), nullptr);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ModelSceneRendererState>>&& promise,
			const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>& drawablesPool,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators,
			const std::shared_ptr<::Assets::OperationContext>& loadingContext,
			std::shared_ptr<ModelRendererConstruction> construction,
			std::shared_future<std::shared_ptr<AnimationSetScaffold>> futureAnimationSet,
			std::shared_ptr<DeformerConstruction> deformerConstruction)
		{
			std::shared_future<std::shared_ptr<SimpleModelRenderer>> rendererFuture;
			std::future<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> futureConstruction;

			TRY {
				if (construction->CanBeHashed() && !deformerConstruction) {
					rendererFuture = ::Assets::GetAssetFuturePtr<SimpleModelRenderer>(drawablesPool, pipelineAcceleratorPool, nullptr, construction, deformAccelerators, deformerConstruction);
				} else {
					rendererFuture = ::Assets::ConstructToFuturePtr<SimpleModelRenderer>(drawablesPool, pipelineAcceleratorPool, nullptr, construction, deformAccelerators, deformerConstruction);
				}

				std::promise<std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>> promisedConstruction;
				futureConstruction = promisedConstruction.get_future();	// we do have to wait on the construction, because there's a chance that GetAssetFuturePtr<SimpleModelRenderer> will return something that was previously constructed and already setup
				construction->FulfillWhenNotPending(std::move(promisedConstruction));
			} CATCH (...) {
				promise.set_exception(std::current_exception());
				return;
			} CATCH_END

			if (futureAnimationSet.valid()) {
				::Assets::WhenAll(rendererFuture, std::move(futureAnimationSet), std::move(futureConstruction)).ThenConstructToPromise(
					std::move(promise), 
					[deformAccelerators](
						std::shared_ptr<SimpleModelRenderer> renderer,
						std::shared_ptr<AnimationSetScaffold> animationSet,
						std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> construction) {

						auto result = std::make_shared<ModelSceneRendererState>();
						result->_renderer = std::move(renderer);
						result->_rendererConstruction = std::move(construction);
						result->_animationScaffold = std::move(animationSet);
						
						const SkeletonMachine* skeleMachine;
						result->_skeletonScaffold = result->_rendererConstruction->GetSkeletonScaffold();
						if (result->_skeletonScaffold) {
							skeleMachine = &result->_skeletonScaffold->GetSkeletonMachine();
						} else {
							result->_modelScaffoldForEmbeddedSkeleton = result->_rendererConstruction->GetElement(0)->GetModelScaffold();
							skeleMachine = result->_modelScaffoldForEmbeddedSkeleton->EmbeddedSkeleton();
						}
						assert(skeleMachine);
						
						result->_animSetBinding = RenderCore::Assets::AnimationSetBinding{
							result->_animationScaffold->ImmutableData()._animationSet.GetOutputInterface(),
							*skeleMachine};

						result->_skeletonInterface = BuildSkeletonInterface(*result->_renderer, *deformAccelerators, skeleMachine->GetOutputInterface());

						result->_depVal = ::Assets::GetDepValSys().Make();
						result->_depVal.RegisterDependency(result->_renderer->GetDependencyValidation());
						result->_depVal.RegisterDependency(result->_animationScaffold->GetDependencyValidation());
						return result;
					});
			} else {
				::Assets::WhenAll(rendererFuture, std::move(futureConstruction)).ThenConstructToPromise(
					std::move(promise), 
					[](std::shared_ptr<SimpleModelRenderer> renderer, std::shared_ptr<RenderCore::Assets::ModelRendererConstruction> construction) {
						auto result = std::make_shared<ModelSceneRendererState>();
						result->_renderer = std::move(renderer);
						result->_rendererConstruction = std::move(construction);
						result->_modelScaffoldForEmbeddedSkeleton = result->_rendererConstruction->GetElement(0)->GetModelScaffold();
						result->_depVal = result->_renderer->GetDependencyValidation();
						return result;
					});
			}
		}
	};

	class ModelScene : public SceneEngine::IScene, public IVisContent
	{
	public:
		virtual void ExecuteScene(
			RenderCore::IThreadContext& threadContext,
			SceneEngine::ExecuteSceneContext& executeContext) const override
		{
			const auto instanceIdx = 0u;
			UpdateSkeletonInterface(instanceIdx);
			auto localToWorld = Identity<Float4x4>();
			if (executeContext._views.size() <= 1) {
				_actualized->_renderer->BuildDrawables(executeContext._destinationPkts, localToWorld, {}, instanceIdx, _preDrawDelegate);
			} else {
				assert(executeContext._views.size() > 0 && executeContext._views.size() < 32);
				uint32_t viewMask = (1 << unsigned(executeContext._views.size()))-1;
				_actualized->_renderer->BuildDrawables(executeContext._destinationPkts, localToWorld, {}, instanceIdx, _preDrawDelegate, viewMask);
			}
			executeContext._completionCmdList = std::max(executeContext._completionCmdList, _actualized->_renderer->GetCompletionCommandList());
		}

		void LookupDrawableMetadata(
			SceneEngine::ExecuteSceneContext& exeContext,
			SceneEngine::DrawableMetadataLookupContext& context) const override
		{
			_actualized->_renderer->LookupDrawableMetadata(context);
		}
		std::pair<Float3, Float3> GetBoundingBox() const override
		{
			assert(_actualized->_rendererConstruction->GetElementCount() >= 1);
			return _actualized->_rendererConstruction->GetElement(0)->GetModelScaffold()->GetStaticBoundingBox(); 
			assert(0); return {};
		}

		std::shared_ptr<ICustomDrawDelegate> SetCustomDrawDelegate(const std::shared_ptr<ICustomDrawDelegate>& delegate) override
		{
			auto oldDelegate = delegate;
			std::swap(_preDrawDelegate, oldDelegate);
			return oldDelegate;
		}

		void RenderSkeleton(
			RenderOverlays::IOverlayContext& overlayContext, 
			RenderCore::Techniques::ParsingContext& parserContext, 
			bool drawBoneNames) const override
		{
			if (_actualized->_animationScaffold && _animationState && _animationState->_state != VisAnimationState::State::BindPose) {
				auto& animData = _actualized->_animationScaffold->ImmutableData();

				auto animHash = Hash64(_animationState->_activeAnimation);
				auto foundAnimation = animData._animationSet.FindAnimation(animHash);
				if (foundAnimation) {
					float time = _animationState->_animationTime;
					if (_animationState->_state == VisAnimationState::State::Playing) {
						time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
						time = fmodf(time, foundAnimation->_durationInFrames / foundAnimation->_framesPerSecond);
					}

					auto parameterBlockSize = _actualized->_animSetBinding.GetParameterDefaultsBlock().size();
					VLA(uint8_t, parameterBlock, parameterBlockSize);
					std::memcpy(parameterBlock, _actualized->_animSetBinding.GetParameterDefaultsBlock().begin(), parameterBlockSize);

					animData._animationSet.CalculateOutput(
						MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]),
						{time, animHash},
						_actualized->_animSetBinding.GetParameterBindingRules());

					// We have to use the "specialized" skeleton in _animSetBinding
					auto outputMatrixCount = _actualized->_animSetBinding.GetOutputMatrixCount();
					VLA_UNSAFE_FORCE(Float4x4, skeletonOutput, outputMatrixCount);
					_actualized->_animSetBinding.GenerateOutputTransforms(
						MakeIteratorRange(skeletonOutput, &skeletonOutput[outputMatrixCount]),
						MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]));

					RenderOverlays::RenderSkeleton(
						overlayContext,
						parserContext,
						*_actualized->GetSkeletonMachine(),
						MakeIteratorRange(skeletonOutput, &skeletonOutput[outputMatrixCount]),
						Identity<Float4x4>(),
						drawBoneNames);
					return;
				}
			}

			// fallback to unanimated skeleton
			RenderOverlays::RenderSkeleton(
				overlayContext,
				parserContext,
				*_actualized->GetSkeletonMachine(),
				Identity<Float4x4>(),
				drawBoneNames);
		}

		void BindAnimationState(const std::shared_ptr<VisAnimationState>& animState) override
		{
			_animationState = animState;
			if (_actualized)
				_actualized->BindAnimState(*_animationState);
		}

		bool HasActiveAnimation() const override
		{
			return _actualized->_animationScaffold && _animationState && _animationState->_state == VisAnimationState::State::Playing;
		}

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _actualized->GetDependencyValidation(); }

		ModelScene(
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
			std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
			std::shared_ptr<ModelSceneRendererState> actualized,
			uint64_t materialBindingFilter = 0)
		: _pipelineAcceleratorPool(std::move(pipelineAcceleratorPool))
		, _deformAcceleratorPool(std::move(deformAcceleratorPool))
		, _actualized(std::move(actualized))
		{
			if (materialBindingFilter)
				_preDrawDelegate = std::make_shared<MaterialFilterDelegate>(materialBindingFilter);
		}

	protected:
		std::shared_ptr<ICustomDrawDelegate>			_preDrawDelegate;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<ModelSceneRendererState>		_actualized;
		std::shared_ptr<VisAnimationState>				_animationState;

		std::vector<std::shared_ptr<RenderCore::Techniques::ISkinDeformer>> _skinDeformers;

		void UpdateSkeletonInterface(unsigned instanceIdx) const
		{
			auto skeletonMachine = _actualized->GetSkeletonMachine();
			assert(skeletonMachine);

			auto outputMatrixCount = skeletonMachine->GetOutputMatrixCount();
			bool foundGoodAnimation = false;
			VLA_UNSAFE_FORCE(Float4x4, skeletonMachineOutput, outputMatrixCount);
			if (_actualized->_animationScaffold && _animationState && _animationState->_state != VisAnimationState::State::BindPose) {
				auto& animData = _actualized->_animationScaffold->ImmutableData();

				auto animHash = Hash64(_animationState->_activeAnimation);
				auto foundAnimation = animData._animationSet.FindAnimation(animHash);
				if (foundAnimation) {
					float time = _animationState->_animationTime;
					if (_animationState->_state == VisAnimationState::State::Playing)
						time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
					time = fmodf(time, foundAnimation->_durationInFrames / foundAnimation->_framesPerSecond);

					auto parameterBlockSize = _actualized->_animSetBinding.GetParameterDefaultsBlock().size();
					VLA(uint8_t, parameterBlock, parameterBlockSize);
					std::memcpy(parameterBlock, _actualized->_animSetBinding.GetParameterDefaultsBlock().begin(), parameterBlockSize);

					animData._animationSet.CalculateOutput(
						MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]),
						{time, animHash},
						_actualized->_animSetBinding.GetParameterBindingRules());

					// We have to use the "specialized" skeleton in _animSetBinding
					assert(_actualized->_animSetBinding.GetOutputMatrixCount() == outputMatrixCount);
					_actualized->_animSetBinding.GenerateOutputTransforms(
						MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[outputMatrixCount]),
						MakeIteratorRange(parameterBlock, &parameterBlock[parameterBlockSize]));
					foundGoodAnimation = true;
				}
			}

			if (!foundGoodAnimation)
				skeletonMachine->GenerateOutputTransforms(MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[outputMatrixCount]));

			if (_actualized->_skeletonInterface)
				_actualized->_skeletonInterface->FeedInSkeletonMachineResults(instanceIdx, MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[outputMatrixCount]));
		}
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	Assets::PtrToMarkerPtr<SceneEngine::IScene> ModelVisUtility::MakeScene(
		const ModelVisSettings& settings)
	{
		auto rendererFuture = ::Assets::ConstructToMarkerPtr<ModelSceneRendererState>(_drawablesPool, _pipelineAcceleratorPool, _deformAcceleratorPool, _loadingContext, settings);
		// Must use a marker to ModelScene, and then reinterpret it over to the generic type, in order
		// to propagate dependency validations correctly (since GetDependencyValidation is part of ModelScene, not IScene)
		auto result = std::make_shared<Assets::MarkerPtr<ModelScene>>();
		::Assets::WhenAll(rendererFuture).ThenConstructToPromise(
			result->AdoptPromise(),
			[pipelineAcceleratorPool=_pipelineAcceleratorPool, deformAcceleratorPool=_deformAcceleratorPool, settings](auto renderer) mutable {
				return std::make_shared<ModelScene>(std::move(pipelineAcceleratorPool), std::move(deformAcceleratorPool), std::move(renderer), settings._materialBindingFilter);
			});
		return std::reinterpret_pointer_cast<Assets::MarkerPtr<SceneEngine::IScene>>(result);
	}

	void ModelVisUtility::MakeScene(
		std::promise<std::shared_ptr<SceneEngine::IScene>>&& promise,
		std::shared_ptr<ModelRendererConstruction> construction,
		std::shared_future<std::shared_ptr<AnimationSetScaffold>> futureAnimationSet,
		std::shared_ptr<RenderCore::Techniques::DeformerConstruction> deformerConstruction)
	{
		auto rendererFuture = ::Assets::ConstructToFuturePtr<ModelSceneRendererState>(
			_drawablesPool, _pipelineAcceleratorPool, _deformAcceleratorPool, _loadingContext,
			std::move(construction), std::move(futureAnimationSet), std::move(deformerConstruction));
		::Assets::WhenAll(std::move(rendererFuture)).ThenConstructToPromise(
			std::move(promise),
			[pipelineAcceleratorPool=_pipelineAcceleratorPool, deformAcceleratorPool=_deformAcceleratorPool](auto renderer) mutable {
				return std::make_shared<ModelScene>(std::move(pipelineAcceleratorPool), std::move(deformAcceleratorPool), renderer);
			});
	}

	uint64_t ModelVisSettings::GetHash() const
	{
		auto hash = Hash64(_modelName);
		hash = Hash64(_materialName, hash);
		hash = Hash64(_compilationConfigurationName, hash);
		hash = Hash64(_supplements, hash);
		hash = HashCombine(_levelOfDetail, hash);
		hash = Hash64(_animationFileName, hash);
		hash = Hash64(_skeletonFileName, hash);
		hash = HashCombine(_materialBindingFilter, hash);
		return hash;
	}

	ModelVisSettings::ModelVisSettings()
	{
		_levelOfDetail = 0;
		_materialBindingFilter = 0;
	}

}

