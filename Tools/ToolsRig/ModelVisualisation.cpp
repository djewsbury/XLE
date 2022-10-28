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
#include "../../RenderCore/Assets/ModelRendererConstruction.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderOverlays/AnimationVisualization.h"
#include "../../SceneEngine/IScene.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/Continuation.h"
#include "../../OSServices/TimeUtils.h"

#pragma warning(disable:4505)

namespace ToolsRig
{
	using RenderCore::Assets::ModelScaffold;
    using RenderCore::Assets::MaterialScaffold;
	using RenderCore::Assets::AnimationSetScaffold;
	using RenderCore::Assets::SkeletonScaffold;
    using RenderCore::Assets::SkeletonMachine;
	using RenderCore::Techniques::SimpleModelRenderer;
	using RenderCore::Techniques::ICustomDrawDelegate;
	using RenderCore::Techniques::RendererSkeletonInterface;

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
			auto construction = std::make_shared<RenderCore::Assets::ModelRendererConstruction>();
			construction->AddElement().SetModelAndMaterialScaffolds(loadingContext, settings._modelName, settings._materialName);
			auto rendererFuture = ::Assets::MakeAssetPtr<SimpleModelRenderer>(drawablesPool, pipelineAcceleratorPool, nullptr, construction, deformAccelerators);

			if (!settings._animationFileName.empty() && !settings._skeletonFileName.empty()) {
				auto animationSetFuture = ::Assets::MakeAssetPtr<AnimationSetScaffold>(settings._animationFileName);
				auto skeletonFuture = ::Assets::MakeAssetPtr<SkeletonScaffold>(settings._skeletonFileName);
				::Assets::WhenAll(rendererFuture, animationSetFuture, skeletonFuture).ThenConstructToPromise(
					std::move(promise), 
					[construction, deformAccelerators](
						std::shared_ptr<SimpleModelRenderer> renderer,
						std::shared_ptr<AnimationSetScaffold> animationSet,
						std::shared_ptr<SkeletonScaffold> skeleton) {
						
						RenderCore::Assets::AnimationSetBinding animBinding(
							animationSet->ImmutableData()._animationSet.GetOutputInterface(), 
							skeleton->GetSkeletonMachine());

						auto skeletonInterface = BuildSkeletonInterface(*renderer, *deformAccelerators, skeleton->GetSkeletonMachine().GetOutputInterface());

						auto depVal = ::Assets::GetDepValSys().Make();
						depVal.RegisterDependency(renderer->GetDependencyValidation());
						depVal.RegisterDependency(animationSet->GetDependencyValidation());
						depVal.RegisterDependency(skeleton->GetDependencyValidation());

						return std::make_shared<ModelSceneRendererState>(
							ModelSceneRendererState {
								renderer, construction,
								nullptr, skeleton, animationSet, skeletonInterface,
								std::move(animBinding), depVal,
							});
					});
			} else if (!settings._animationFileName.empty()) {
				auto animationSetFuture = ::Assets::MakeAssetPtr<AnimationSetScaffold>(settings._animationFileName);
				::Assets::WhenAll(rendererFuture, animationSetFuture).ThenConstructToPromise(
					std::move(promise), 
					[construction, deformAccelerators](
						std::shared_ptr<SimpleModelRenderer> renderer,
						std::shared_ptr<AnimationSetScaffold> animationSet) {
						
						auto modelScaffold = construction->GetElement(0)->GetModelScaffold();
						assert(modelScaffold->EmbeddedSkeleton());
						RenderCore::Assets::AnimationSetBinding animBinding(
							animationSet->ImmutableData()._animationSet.GetOutputInterface(), 
							*modelScaffold->EmbeddedSkeleton());

						auto skeletonInterface = BuildSkeletonInterface(*renderer, *deformAccelerators, modelScaffold->EmbeddedSkeleton()->GetOutputInterface());

						auto depVal = ::Assets::GetDepValSys().Make();
						depVal.RegisterDependency(renderer->GetDependencyValidation());
						depVal.RegisterDependency(animationSet->GetDependencyValidation());

						return std::make_shared<ModelSceneRendererState>(
							ModelSceneRendererState {
								renderer, construction,
								modelScaffold, nullptr, animationSet, skeletonInterface,
								std::move(animBinding), depVal,
							});
					});
			} else {
				::Assets::WhenAll(rendererFuture).ThenConstructToPromise(
					std::move(promise), 
					[construction](std::shared_ptr<SimpleModelRenderer> renderer) {
						return std::make_shared<ModelSceneRendererState>(
							ModelSceneRendererState {
								renderer, construction,
								construction->GetElement(0)->GetModelScaffold(), nullptr, nullptr, nullptr,
								{}, renderer->GetDependencyValidation(),
							});
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

		DrawCallDetails GetDrawCallDetails(unsigned drawCallIndex, uint64_t materialGuid) const override
		{
			assert(_actualized->_rendererConstruction->GetElementCount() >= 1);
			auto matName = _actualized->_rendererConstruction->GetElement(0)->GetMaterialScaffold()->DehashMaterialName(materialGuid).AsString();
			if (matName.empty())
				matName = _actualized->_rendererConstruction->GetElement(0)->GetMaterialScaffoldName();
			return { _actualized->_rendererConstruction->GetElement(0)->GetModelScaffoldName(), matName };
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
				auto foundAnimation = animData._animationSet.FindAnimation(animHash).value();
				float time = _animationState->_animationTime;
				if (_animationState->_state == VisAnimationState::State::Playing) {
					time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
					time = fmodf(time, foundAnimation._durationInFrames / foundAnimation._framesPerSecond);
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
			} else {
				RenderOverlays::RenderSkeleton(
					overlayContext,
					parserContext,
					*_actualized->GetSkeletonMachine(),
					Identity<Float4x4>(),
					drawBoneNames);
			}
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
			const ModelVisSettings& settings)
		: _pipelineAcceleratorPool(std::move(pipelineAcceleratorPool))
		, _deformAcceleratorPool(std::move(deformAcceleratorPool))
		, _actualized(std::move(actualized))
		{
			if (settings._materialBindingFilter)
				_preDrawDelegate = std::make_shared<MaterialFilterDelegate>(settings._materialBindingFilter);
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
			VLA_UNSAFE_FORCE(Float4x4, skeletonMachineOutput, outputMatrixCount);
			if (_actualized->_animationScaffold && _animationState && _animationState->_state != VisAnimationState::State::BindPose) {
				auto& animData = _actualized->_animationScaffold->ImmutableData();

				auto animHash = Hash64(_animationState->_activeAnimation);
				auto foundAnimation = animData._animationSet.FindAnimation(animHash).value();
				float time = _animationState->_animationTime;
				if (_animationState->_state == VisAnimationState::State::Playing)
					time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
				time = fmodf(time, foundAnimation._durationInFrames / foundAnimation._framesPerSecond);

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
			} else {
				skeletonMachine->GenerateOutputTransforms(MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[outputMatrixCount]));
			}

			if (_actualized->_skeletonInterface)
				_actualized->_skeletonInterface->FeedInSkeletonMachineResults(instanceIdx, MakeIteratorRange(skeletonMachineOutput, &skeletonMachineOutput[outputMatrixCount]));
		}
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

	Assets::PtrToMarkerPtr<SceneEngine::IScene> MakeScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<::Assets::OperationContext> loadingContext,
		const ModelVisSettings& settings)
	{
		auto rendererFuture = ::Assets::ConstructToMarkerPtr<ModelSceneRendererState>(drawablesPool, pipelineAcceleratorPool, deformAcceleratorPool, loadingContext, settings);
		// Must use a marker to ModelScene, and then reinterpret it over to the generic type, in order
		// to propagate dependency validations correctly (since GetDependencyValidation is part of ModelScene, not IScene)
		auto result = std::make_shared<Assets::MarkerPtr<ModelScene>>();
		::Assets::WhenAll(rendererFuture).ThenConstructToPromise(
			result->AdoptPromise(),
			[pipelineAcceleratorPool=std::move(pipelineAcceleratorPool), deformAcceleratorPool=std::move(deformAcceleratorPool), loadingContext=std::move(loadingContext), settings](auto renderer) {
				return std::make_shared<ModelScene>(pipelineAcceleratorPool, deformAcceleratorPool, renderer, settings);
			});
		return std::reinterpret_pointer_cast<Assets::MarkerPtr<SceneEngine::IScene>>(result);
	}

	uint64_t ModelVisSettings::GetHash() const
	{
		auto hash = Hash64(_modelName);
		hash = Hash64(_materialName, hash);
		hash = Hash64(_supplements, hash);
		hash = HashCombine(_levelOfDetail, hash);
		hash = Hash64(_animationFileName, hash);
		hash = Hash64(_skeletonFileName, hash);
		hash = HashCombine(_materialBindingFilter, hash);
		return hash;
	}

	ModelVisSettings::ModelVisSettings()
    {
        _modelName = "rawos/game/model/galleon/galleon.dae";
        _materialName = "rawos/game/model/galleon/galleon.material";
		_levelOfDetail = 0;
		_materialBindingFilter = 0;

		// _modelName = "data/meshes/actors/dragon/character assets/alduin.nif";
		// _materialName = "data/meshes/actors/dragon/character assets/alduin.nif";
		// _animationFileName = "data/meshes/actors/dragon/animations/special_alduindeathagony.hkx";

        // _envSettingsFile = "defaultenv.dat:environment";
    }

#if 0

	using FixedFunctionModel::ModelRenderer;
	using FixedFunctionModel::SharedStateSet;
    using FixedFunctionModel::ModelCache;
	using FixedFunctionModel::ModelCacheModel;
    using FixedFunctionModel::DelayedDrawCallSet;

    static void RenderWithEmbeddedSkeleton(
        const FixedFunctionModel::ModelRendererContext& context,
        const ModelRenderer& model,
        const SharedStateSet& sharedStateSet,
        const ModelScaffold* scaffold)
    {
        if (scaffold) {
            model.Render(
                context, sharedStateSet, Identity<Float4x4>(), 
                FixedFunctionModel::EmbeddedSkeletonPose(*scaffold).GetMeshToModel());
        } else {
            model.Render(context, sharedStateSet, Identity<Float4x4>());
        }
    }

    static void PrepareWithEmbeddedSkeleton(
        DelayedDrawCallSet& dest, 
        const ModelRenderer& model,
        const SharedStateSet& sharedStateSet,
        const ModelScaffold* scaffold)
    {
        if (scaffold) {
            model.Prepare(
                dest, sharedStateSet, Identity<Float4x4>(), 
                FixedFunctionModel::EmbeddedSkeletonPose(*scaffold).GetMeshToModel());
        } else {
            model.Prepare(dest, sharedStateSet, Identity<Float4x4>());
        }
    }
    
    class FixedFunctionModelSceneParser : public SceneEngine::IScene
    {
    public:
        void ExecuteScene(
            RenderCore::IThreadContext& context,
			RenderCore::Techniques::ParsingContext& parserContext,
            SceneEngine::LightingParserContext& lightingParserContext, 
            RenderCore::Techniques::BatchFilter batchFilter,
            SceneEngine::PreparedScene& preparedPackets,
            unsigned techniqueIndex) const 
        {
            auto delaySteps = SceneEngine::AsDelaySteps(batchFilter);
            if (delaySteps.empty()) return;

            auto metalContext = RenderCore::Metal::DeviceContext::Get(context);

            FixedFunctionModel::SharedStateSet::CaptureMarker captureMarker;
            if (_sharedStateSet)
				captureMarker = _sharedStateSet->CaptureState(context, parserContext.GetRenderStateDelegate(), {});

            using namespace RenderCore;
            Metal::ConstantBuffer drawCallIndexBuffer(
				Metal::GetObjectFactory(), 
				CreateDesc(BindFlag::ConstantBuffer, CPUAccess::WriteDynamic, GPUAccess::Read, LinearBufferDesc::Create(sizeof(unsigned)*4), "drawCallIndex"));
            metalContext->GetNumericUniforms(ShaderStage::Geometry).Bind(MakeResourceList(drawCallIndexBuffer));

            if (Tweakable("RenderSkinned", false)) {
                if (delaySteps[0] == FixedFunctionModel::DelayStep::OpaqueRender) {
                    auto preparedAnimation = _model->CreatePreparedAnimation();
                    FixedFunctionModel::SkinPrepareMachine prepareMachine(
                        *_modelScaffold, _modelScaffold->EmbeddedSkeleton());
                    RenderCore::Assets::AnimationState animState = {0.f, 0u};
                    prepareMachine.PrepareAnimation(context, *preparedAnimation, animState);
                    _model->PrepareAnimation(context, *preparedAnimation, prepareMachine.GetSkeletonBinding());

                    FixedFunctionModel::MeshToModel meshToModel(
                        *preparedAnimation, &prepareMachine.GetSkeletonBinding());

                    _model->Render(
                        FixedFunctionModel::ModelRendererContext(*metalContext, parserContext, techniqueIndex),
                        *_sharedStateSet, Identity<Float4x4>(), 
                        meshToModel, preparedAnimation.get());

                    if (Tweakable("RenderSkeleton", false)) {
                        prepareMachine.RenderSkeleton(
                            context, parserContext, 
                            animState, Identity<Float4x4>());
                    }
                }
            } else {
                const bool fillInStencilInfo = (_settings->_colourByMaterial != 0);

                for (auto i:delaySteps)
                    ModelRenderer::RenderPrepared(
                        FixedFunctionModel::ModelRendererContext(*metalContext.get(), parserContext, techniqueIndex),
                        *_sharedStateSet, _delayedDrawCalls, i,
                        [&metalContext, &drawCallIndexBuffer, &fillInStencilInfo](ModelRenderer::DrawCallEvent evnt)
                        {
                            if (fillInStencilInfo) {
                                // hack -- we just adjust the depth stencil state to enable the stencil buffer
                                //          no way to do this currently without dropping back to low level API
                                #if GFXAPI_TARGET == GFXAPI_DX11
                                    Metal::DepthStencilState dss(*metalContext);
                                    D3D11_DEPTH_STENCIL_DESC desc;
                                    dss.GetUnderlying()->GetDesc(&desc);
                                    desc.StencilEnable = true;
                                    desc.StencilWriteMask = 0xff;
                                    desc.StencilReadMask = 0xff;
                                    desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
                                    desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                                    desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
                                    desc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
                                    auto newDSS = Metal::GetObjectFactory().CreateDepthStencilState(&desc);
                                    metalContext->GetUnderlying()->OMSetDepthStencilState(newDSS.get(), 1+evnt._drawCallIndex);
                                #endif
                            }

                            unsigned drawCallIndexB[4] = { evnt._drawCallIndex, 0, 0, 0 };
                            drawCallIndexBuffer.Update(*metalContext, drawCallIndexB, sizeof(drawCallIndexB));

                            metalContext->DrawIndexed(evnt._indexCount, evnt._firstIndex, evnt._firstVertex);
                        });
            }
        }

		virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
			SceneEngine::SceneExecuteContext& executeContext) const override
		{
		}

		VisCameraSettings AlignCamera()
		{
			return AlignCameraToBoundingBox(40.f, _boundingBox);
		}

		FixedFunctionModelSceneParser(
			const VisOverlaySettings& settings,
			ModelRenderer& model, const std::pair<Float3, Float3>& boundingBox, SharedStateSet& sharedStateSet,
			const ModelScaffold* modelScaffold = nullptr)
		: _model(&model), _boundingBox(boundingBox), _sharedStateSet(&sharedStateSet)
		, _settings(&settings), _modelScaffold(modelScaffold) 
		, _delayedDrawCalls(typeid(ModelRenderer).hash_code())
		{
			PrepareWithEmbeddedSkeleton(
				_delayedDrawCalls, *_model,
				*_sharedStateSet, modelScaffold);
			ModelRenderer::Sort(_delayedDrawCalls);
		}
	protected:
		ModelRenderer* _model;
		SharedStateSet* _sharedStateSet;
		std::pair<Float3, Float3> _boundingBox;

		const VisOverlaySettings* _settings;
		const ModelScaffold* _modelScaffold;
		DelayedDrawCallSet _delayedDrawCalls;
	};

	std::unique_ptr<SceneEngine::IScene> CreateModelScene(const ModelCacheModel& model)
    {
        ModelVisSettings settings;
        return std::make_unique<FixedFunctionModelSceneParser>(
            settings,
            *model._renderer, model._boundingBox, *model._sharedStateSet);
    }

	static ModelCacheModel GetModel(
		ModelCache& cache,
		ModelVisSettings& settings)
	{
		std::vector<ModelCache::SupplementGUID> supplements;
		{
			const auto& s = settings._supplements;
			size_t offset = 0;
			for (;;) {
				auto comma = s.find_first_of(',', offset);
				if (comma == std::string::npos) comma = s.size();
				if (offset == comma) break;
				auto hash = ConstHash64FromString(AsPointer(s.begin()) + offset, AsPointer(s.begin()) + comma);
				supplements.push_back(hash);
				offset = comma;
			}
		}

		return cache.GetModel(
			MakeStringSection(settings._modelName), 
			MakeStringSection(settings._materialName),
			MakeIteratorRange(supplements),
			settings._levelOfDetail);
	}

#endif

}

