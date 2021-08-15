// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelVisualisation.h"
#include "VisualisationUtils.h"
#include "../../RenderCore/Assets/ModelScaffold.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Techniques/SkinDeformer.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../RenderCore/Techniques/SimpleModelDeform.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/AnimationVisualization.h"
#include "../../SceneEngine/IScene.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetFutureContinuation.h"
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

	class ModelScene : public SceneEngine::IScene, public IVisContent, public ::Assets::IAsyncMarker
    {
    public:
		virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
			const SceneEngine::ExecuteSceneContext& executeContext) const override
		{
			auto* r = TryActualize();
			if (!r) {
				Log(Warning) << "Trying to render invalid/pending scene: " << ::Assets::AsString(_rendererStateFuture->GetActualizationLog()) << std::endl;
				return;
			}

			auto skeletonMachine = r->GetSkeletonMachine();
			assert(skeletonMachine);

			std::vector<Float4x4> skeletonMachineOutput(skeletonMachine->GetOutputMatrixCount());

			if (r->_animationScaffold && _animationState && _animationState->_state != VisAnimationState::State::BindPose) {
				auto& animData = r->_animationScaffold->ImmutableData();

				auto animHash = Hash64(_animationState->_activeAnimation);
				auto foundAnimation = animData._animationSet.FindAnimation(animHash);
				float time = _animationState->_animationTime;
				if (_animationState->_state == VisAnimationState::State::Playing)
					time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
				time = fmodf(time - foundAnimation._beginTime, foundAnimation._endTime - foundAnimation._beginTime) + foundAnimation._beginTime;

				auto params = animData._animationSet.BuildTransformationParameterSet(
					{time, animHash},
					*skeletonMachine, r->_animSetBinding,
					MakeIteratorRange(animData._curves));

				skeletonMachine->GenerateOutputTransforms(
					MakeIteratorRange(skeletonMachineOutput),
					&params);
			} else {
				skeletonMachine->GenerateOutputTransforms(
					MakeIteratorRange(skeletonMachineOutput),
					&skeletonMachine->GetDefaultParameters());
			}

			for (unsigned c=0; c<r->_renderer->DeformOperationCount(); ++c) {
				auto* skinDeformOp = dynamic_cast<RenderCore::Techniques::SkinDeformer*>(&r->_renderer->DeformOperation(c));
				if (!skinDeformOp) continue;
				skinDeformOp->FeedInSkeletonMachineResults(
					MakeIteratorRange(skeletonMachineOutput),
					skeletonMachine->GetOutputInterface());
			}
			r->_renderer->GenerateDeformBuffer(threadContext);

			RenderCore::Techniques::DrawablesPacket* pkts[unsigned(RenderCore::Techniques::BatchFilter::Max)];
			XlZeroMemory(pkts);
			pkts[(unsigned)executeContext._batchFilter] = executeContext._destinationPkt;
			r->_renderer->BuildDrawables(MakeIteratorRange(pkts), Identity<Float4x4>(), _preDrawDelegate);
		}

		DrawCallDetails GetDrawCallDetails(unsigned drawCallIndex, uint64_t materialGuid) const override
		{
			auto* r = TryActualize();
			if (r) {
				auto matName = r->_renderer->GetMaterialScaffold()->GetMaterialName(materialGuid).AsString();
				if (matName.empty())
					matName = r->_renderer->GetMaterialScaffoldName();
				return { r->_renderer->GetModelScaffoldName(), matName };
			} else {
				return { {}, {} };
			}
		}
		std::pair<Float3, Float3> GetBoundingBox() const override
		{
			auto* r = TryActualize();
			if (!r)
				return std::make_pair(Float3{0.f, 0.f, 0.f}, Float3{0.f, 0.f, 0.f});
			return r->_renderer->GetModelScaffold()->GetStaticBoundingBox(); 
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
			auto* r = TryActualize();
			if (!r) return;

			auto skeletonMachine = r->GetSkeletonMachine();

			if (r->_animationScaffold && _animationState && _animationState->_state != VisAnimationState::State::BindPose) {
				auto& animData = r->_animationScaffold->ImmutableData();

				auto animHash = Hash64(_animationState->_activeAnimation);
				auto foundAnimation = animData._animationSet.FindAnimation(animHash);
				float time = _animationState->_animationTime;
				if (_animationState->_state == VisAnimationState::State::Playing) {
					time += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _animationState->_anchorTime).count() / 1000.f;
					time = fmodf(time - foundAnimation._beginTime, foundAnimation._endTime - foundAnimation._beginTime) + foundAnimation._beginTime;
				}

				auto params = animData._animationSet.BuildTransformationParameterSet(
					{time, animHash},
					*skeletonMachine, r->_animSetBinding,
					MakeIteratorRange(animData._curves));

				RenderOverlays::RenderSkeleton(
					overlayContext,
					parserContext,
					*skeletonMachine,
					params,
					Identity<Float4x4>(),
					drawBoneNames);
			} else {
				RenderOverlays::RenderSkeleton(
					overlayContext,
					parserContext,
					*skeletonMachine,
					Identity<Float4x4>(),
					drawBoneNames);
			}
		}

		void BindAnimationState(const std::shared_ptr<VisAnimationState>& animState) override
		{
			_animationState = animState;
			// If it previously actualized
			if (_actualized)
				_actualized->BindAnimState(*_animationState);
		}

		bool HasActiveAnimation() const override
		{
			auto* r = TryActualize();
			if (!r) return false;
			return r->_animationScaffold && _animationState && _animationState->_state == VisAnimationState::State::Playing;
		}

		::Assets::AssetState GetAssetState() const override
		{
			return _rendererStateFuture->GetAssetState();
		}

		std::optional<::Assets::AssetState> StallWhilePending(std::chrono::milliseconds timeout) const override
		{
			return _rendererStateFuture->StallWhilePending(timeout);
		}

		ModelScene(
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const ModelVisSettings& settings)
		: _pipelineAcceleratorPool(pipelineAcceleratorPool), _settings(settings)
		{
			if (settings._materialBindingFilter)
				_preDrawDelegate = std::make_shared<MaterialFilterDelegate>(settings._materialBindingFilter);

			BuildRendererStateFuture();
		}

	protected:
		void BuildRendererStateFuture() const
		{
			_rendererStateFuture = std::make_shared<::Assets::FuturePtr<RendererState>>("Model Scene Renderer");

			auto rendererFuture = ::Assets::MakeAsset<SimpleModelRenderer>(_pipelineAcceleratorPool, _settings._modelName, _settings._materialName, "skin");

			if (!_settings._animationFileName.empty() && !_settings._skeletonFileName.empty()) {
				auto animationSetFuture = ::Assets::MakeAsset<AnimationSetScaffold>(_settings._animationFileName);
				auto skeletonFuture = ::Assets::MakeAsset<SkeletonScaffold>(_settings._skeletonFileName);
				::Assets::WhenAll(rendererFuture, animationSetFuture, skeletonFuture).ThenConstructToFuture(
					*_rendererStateFuture, 
					[](	std::shared_ptr<SimpleModelRenderer> renderer,
						std::shared_ptr<AnimationSetScaffold> animationSet,
						std::shared_ptr<SkeletonScaffold> skeleton) {
						
						RenderCore::Assets::AnimationSetBinding animBinding(
							animationSet->ImmutableData()._animationSet.GetOutputInterface(), 
							skeleton->GetTransformationMachine().GetInputInterface());

						auto depVal = ::Assets::GetDepValSys().Make();
						depVal.RegisterDependency(renderer->GetDependencyValidation());
						depVal.RegisterDependency(animationSet->GetDependencyValidation());
						depVal.RegisterDependency(skeleton->GetDependencyValidation());

						return std::make_shared<RendererState>(
							RendererState {
								renderer,
								nullptr, skeleton, animationSet,
								std::move(animBinding), depVal,
							});
					});
			} else if (!_settings._animationFileName.empty()) {
				auto animationSetFuture = ::Assets::MakeAsset<AnimationSetScaffold>(_settings._animationFileName);
				::Assets::WhenAll(rendererFuture, animationSetFuture).ThenConstructToFuture(
					*_rendererStateFuture, 
					[](	std::shared_ptr<SimpleModelRenderer> renderer,
						std::shared_ptr<AnimationSetScaffold> animationSet) {
						
						RenderCore::Assets::AnimationSetBinding animBinding(
							animationSet->ImmutableData()._animationSet.GetOutputInterface(), 
							renderer->GetModelScaffold()->EmbeddedSkeleton().GetInputInterface());

						auto depVal = ::Assets::GetDepValSys().Make();
						depVal.RegisterDependency(renderer->GetDependencyValidation());
						depVal.RegisterDependency(animationSet->GetDependencyValidation());

						return std::make_shared<RendererState>(
							RendererState {
								renderer,
								renderer->GetModelScaffold(), nullptr, animationSet,
								std::move(animBinding), depVal,
							});
					});
			} else {
				::Assets::WhenAll(rendererFuture).ThenConstructToFuture(
					*_rendererStateFuture, 
					[](std::shared_ptr<SimpleModelRenderer> renderer) {
						return std::make_shared<RendererState>(
							RendererState {
								renderer,
								renderer->GetModelScaffold(), nullptr, nullptr,
								{}, renderer->GetDependencyValidation(),
							});
					});
			}
        }

		std::shared_ptr<ICustomDrawDelegate>			_preDrawDelegate;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		ModelVisSettings							_settings;
		
		struct RendererState
		{
			std::shared_ptr<SimpleModelRenderer>		_renderer;
			std::shared_ptr<ModelScaffold>				_modelScaffoldForEmbeddedSkeleton;
			std::shared_ptr<SkeletonScaffold>			_skeletonScaffold;
			std::shared_ptr<AnimationSetScaffold>		_animationScaffold;
			RenderCore::Assets::AnimationSetBinding		_animSetBinding;
			::Assets::DependencyValidation							_depVal;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

			const SkeletonMachine* GetSkeletonMachine() const
			{
				const SkeletonMachine* skeletonMachine = nullptr;
				if (_skeletonScaffold) {
					skeletonMachine = &_skeletonScaffold->GetTransformationMachine();
				} else if (_modelScaffoldForEmbeddedSkeleton)
					skeletonMachine = &_modelScaffoldForEmbeddedSkeleton->EmbeddedSkeleton();
				return skeletonMachine;
			}

			void BindAnimState(VisAnimationState& animState)
			{
				animState._animationList.clear();
				if (_animationScaffold) {
					for (const auto&anim:_animationScaffold->ImmutableData()._animationSet.GetAnimations()) {
						auto name = _animationScaffold->ImmutableData()._animationSet.LookupStringName(anim.first);
						if (!name.IsEmpty()) {
							animState._animationList.push_back({name.AsString(), anim.second._beginTime, anim.second._endTime});
						} else {
							char buffer[64];
							XlUI64toA(anim.first, buffer, dimof(buffer), 16);
							animState._animationList.push_back({buffer, anim.second._beginTime, anim.second._endTime});
						}
					}
				}
				animState._changeEvent.Invoke();
			}
		};
		mutable ::Assets::PtrToFuturePtr<RendererState> _rendererStateFuture;
		mutable std::shared_ptr<RendererState> _actualized;

		RendererState* TryActualize() const
		{
			if (_actualized && !_actualized->GetDependencyValidation().GetValidationIndex())
				return _actualized.get();

			if (_rendererStateFuture->GetAssetState() == ::Assets::AssetState::Pending)
				return nullptr;

			// Check if we need a reload -- 
			if (_rendererStateFuture->GetDependencyValidation().GetValidationIndex()) {
				_actualized = nullptr;
				BuildRendererStateFuture();
			}

			auto* t = _rendererStateFuture->TryActualize();
			_actualized = t ? *t : nullptr;
			if (_actualized && _animationState) {
				_actualized->BindAnimState(*_animationState);
			}
			return _actualized.get();
		}

		std::shared_ptr<VisAnimationState>				_animationState;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<SceneEngine::IScene> MakeScene(
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const ModelVisSettings& settings)
	{
		return std::make_shared<ModelScene>(pipelineAcceleratorPool, settings);
	}

	ModelVisSettings::ModelVisSettings()
    {
        _modelName = "game/model/galleon/galleon.dae";
        _materialName = "game/model/galleon/galleon.material";
		_levelOfDetail = 0;
		_materialBindingFilter = 0;

		// _modelName = "data/meshes/actors/dragon/character assets/alduin.nif";
		// _materialName = "data/meshes/actors/dragon/character assets/alduin.nif";
		// _animationFileName = "data/meshes/actors/dragon/animations/special_alduindeathagony.hkx";

        // _envSettingsFile = "defaultenv.txt:environment";
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

