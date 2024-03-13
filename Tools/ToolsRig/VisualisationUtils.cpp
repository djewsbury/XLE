// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VisualisationUtils.h"
#include "MaterialVisualisation.h"
#include "ModelVisualisation.h"
#include "../../SceneEngine/RayVsModel.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../SceneEngine/ExecuteScene.h"
#include "../../OSServices/OverlappedWindow.h"	// (for GetOSRunLoop())
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/HighlightEffects.h"
#include "../../RenderOverlays/SimpleVisualization.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderCore/Techniques/SubFrameEvents.h"
#include "../../RenderCore/BufferUploads/IBufferUploads.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Tools/EntityInterface/FormatterAdapters.h"
#include "../../Math/Transformations.h"
#include "../../ConsoleRig/Console.h"
#include "../../OSServices/Log.h"
#include "../../Utility/FunctionUtils.h"
#include <iomanip>
#include <chrono>

#pragma warning(disable:4505) // unreferenced local function has been removed
using namespace Utility::Literals;

namespace ToolsRig
{
    RenderCore::Techniques::CameraDesc AsCameraDesc(const VisCameraSettings& camSettings)
    {
        RenderCore::Techniques::CameraDesc result;
        result._cameraToWorld = MakeCameraToWorld(
            Normalize(camSettings._focus - camSettings._position),
            Float3(0.f, 0.f, 1.f), camSettings._position);
        result._farClip = camSettings._farClip;
        result._nearClip = camSettings._nearClip;
        result._verticalFieldOfView = camSettings._verticalFieldOfView;
        result._left = camSettings._left;
        result._top = camSettings._top;
        result._right = camSettings._right;
        result._bottom = camSettings._bottom;
        result._projection = 
            (camSettings._projection == VisCameraSettings::Projection::Orthogonal)
             ? RenderCore::Techniques::CameraDesc::Projection::Orthogonal
             : RenderCore::Techniques::CameraDesc::Projection::Perspective;
        assert(std::isfinite(result._cameraToWorld(0,0)) && !std::isnan(result._cameraToWorld(0,0)));
        return result;
    }

	VisCameraSettings AsVisCameraSettings(const RenderCore::Techniques::CameraDesc& cameraDesc, float distanceToFocus)
	{
		using CameraDesc = RenderCore::Techniques::CameraDesc;
		VisCameraSettings result;
		result._position = ExtractTranslation(cameraDesc._cameraToWorld);
		result._nearClip = cameraDesc._nearClip;
		result._farClip = cameraDesc._farClip;
		result._projection = (cameraDesc._projection == CameraDesc::Projection::Orthogonal)
			? VisCameraSettings::Projection::Orthogonal
			: VisCameraSettings::Projection::Perspective;
		result._verticalFieldOfView = cameraDesc._verticalFieldOfView;
		result._left = cameraDesc._left;
		result._top = cameraDesc._top;
		result._right = cameraDesc._right;
		result._bottom = cameraDesc._bottom;
		result._focus = result._position + distanceToFocus * Normalize(ExtractForward_Cam(cameraDesc._cameraToWorld));
		return result;
	}

	void ConfigureParsingContext(RenderCore::Techniques::ParsingContext& parsingContext, const VisCameraSettings& cam)
	{
		UInt2 viewportDims { parsingContext.GetViewport()._width, parsingContext.GetViewport()._height };
		auto camDesc = ToolsRig::AsCameraDesc(cam);
        parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(camDesc, viewportDims[0] / float(viewportDims[1]));
	}

    VisCameraSettings AlignCameraToBoundingBox(
        float verticalFieldOfView, 
        const std::pair<Float3, Float3>& boxIn)
    {
        auto box = boxIn;

            // convert empty/inverted boxes into something rational...
        if (    box.first[0] >= box.second[0] 
            ||  box.first[1] >= box.second[1] 
            ||  box.first[2] >= box.second[2]) {
            box.first = Float3(-10.f, -10.f, -10.f);
            box.second = Float3( 10.f,  10.f,  10.f);
        }

        const float border = 0.0f;
        Float3 position = .5f * (box.first + box.second);

            // push back to attempt to fill the viewport with the bounding box
			// Expecting object to be looking along +Y, which is out normal object-to-world
			// orientation
        float verticalHalfDimension = .5f * std::max(box.second[0] - box.first[0], box.second[2] - box.first[2]);
		verticalHalfDimension *= 1.15f;		// expand out a bit so the model doesn't touch the edges of the viewport
        position[1] = (box.first[1]+box.second[1])/2.f + (verticalHalfDimension * (1.f + border)) / XlTan(.5f * verticalFieldOfView);

        VisCameraSettings result;
        result._position = position;
        result._focus = .5f * (box.first + box.second);
        result._verticalFieldOfView = verticalFieldOfView;
        float maxDim = std::max(std::max(std::abs(box.second[0] - box.first[0]), std::abs(box.second[1] - box.first[1])), std::abs(box.second[2] - box.first[2]));
		result._farClip = std::max(maxDim, 10.f * Magnitude(result._focus - result._position));
        result._nearClip = result._farClip / 100000.f;

		assert(std::isfinite(result._position[0]) && !std::isnan(result._position[0]));
		assert(std::isfinite(result._position[1]) && !std::isnan(result._position[1]));
		assert(std::isfinite(result._position[2]) && !std::isnan(result._position[2]));

        return result;
    }

	VisCameraSettings AlignCameraToBoundingBoxFromAbove(
        float verticalFieldOfView, 
        const std::pair<Float3, Float3>& boxIn)
	{
        auto box = boxIn;

            // convert empty/inverted boxes into something rational...
        if (    box.first[0] >= box.second[0] 
            ||  box.first[1] >= box.second[1] 
            ||  box.first[2] >= box.second[2]) {
            box.first = Float3(-10.f, -10.f, -10.f);
            box.second = Float3( 10.f,  10.f,  10.f);
        }

        const float border = 0.0f;
        Float3 position = .5f * (box.first + box.second);

            // push back to attempt to fill the viewport with the bounding box
			// Expecting object to be looking along +Y, which is out normal object-to-world
			// orientation
        float verticalHalfDimension = .5f * std::max(std::abs(box.second[0] - box.first[0]), std::abs(box.second[1] - box.first[1]));
		verticalHalfDimension *= 1.15f;		// expand out a bit so the model doesn't touch the edges of the viewport
        position[2] = (box.first[2]+box.second[2])/2.f + (verticalHalfDimension * (1.f + border)) / XlTan(.5f * verticalFieldOfView);

        VisCameraSettings result;
        result._position = position;
        result._focus = .5f * (box.first + box.second);
        result._verticalFieldOfView = verticalFieldOfView;
		float maxDim = std::max(std::max(std::abs(box.second[0] - box.first[0]), std::abs(box.second[1] - box.first[1])), std::abs(box.second[2] - box.first[2]));
		result._farClip = std::max(maxDim, 10.f * Magnitude(result._focus - result._position));
        result._nearClip = result._farClip / 100000.f;

		assert(std::isfinite(result._position[0]) && !std::isnan(result._position[0]));
		assert(std::isfinite(result._position[1]) && !std::isnan(result._position[1]));
		assert(std::isfinite(result._position[2]) && !std::isnan(result._position[2]));

        return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleSceneOverlay : public ISimpleSceneOverlay
    {
    public:
        void Render(RenderCore::Techniques::ParsingContext& parserContext) override;

		void Set(std::shared_ptr<SceneEngine::ILightingStateDelegate> envSettings) override;
		void Set(std::shared_ptr<SceneEngine::IScene> scene, std::shared_ptr<::Assets::OperationContext> loadingContext) override;
		void SetEmptyScene() override;
		void ShowLoadingIndicator() override;

		void Set(std::shared_ptr<VisCameraSettings>, bool) override;
		void ResetCamera() override;
		OverlayState GetOverlayState() const override;
		void ReportError(StringSection<> msg) override;

		void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;

        SimpleSceneOverlay(
            const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus,
            const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators);
        ~SimpleSceneOverlay();
    protected:
		std::shared_ptr<VisCameraSettings> _camera;

		class PreparedScene
		{
		public:
			std::shared_ptr<SceneEngine::IScene> _scene;
			std::shared_ptr<SceneEngine::ILightingStateDelegate> _envSettings;
			std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _compiledLightingTechnique;
			::Assets::DependencyValidation _depVal;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

			~PreparedScene()
			{
				if (_envSettings && _compiledLightingTechnique) {
					auto& lightScene = RenderCore::LightingEngine::GetLightScene(*_compiledLightingTechnique);
					_envSettings->UnbindScene(lightScene);
				}
			}
			PreparedScene() = default;
			PreparedScene(PreparedScene&&) = default;
			PreparedScene& operator=(PreparedScene&&) = default;
		};
		::Assets::PtrToMarkerPtr<PreparedScene> _preparedSceneFuture;

		std::shared_ptr<SceneEngine::IScene> _scene;
		std::shared_ptr<SceneEngine::ILightingStateDelegate> _envSettings;
		std::shared_ptr<::Assets::OperationContext> _loadingContext;
		void RebuildPreparedScene();
		
		unsigned _loadingIndicatorCounter = 0;
		bool _pendingCameraReset = true;
		bool _useNullScene = false;
		std::string _errorMsg;
		bool _showingLoadingIndicator = false;

		uint64_t _lightingTechniqueTargetsHash = 0ull;
		std::vector<RenderCore::Techniques::PreregisteredAttachment> _lightingTechniqueTargets;
		RenderCore::FrameBufferProperties _lightingTechniqueFBProps;

		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAccelerators;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _debugShapesDelegate;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;
		std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingApparatus;
    };

    void SimpleSceneOverlay::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace SceneEngine;
		#if defined(_DEBUG)
			auto& stitchingContext = parserContext.GetFragmentStitchingContext();
			auto validationHash = RenderCore::Techniques::HashPreregisteredAttachments(stitchingContext.GetPreregisteredAttachments(), parserContext.GetFrameBufferProperties());
			assert(_lightingTechniqueTargetsHash == validationHash);		// If you get here, it means that this render target configuration doesn't match what was last used with OnRenderTargetUpdate()
		#endif
		if (!(parserContext.GetViewport()._width * parserContext.GetViewport()._height))
			return;

		PreparedScene* actualizedScene = nullptr;
		if (_preparedSceneFuture) {
			if (_preparedSceneFuture->GetDependencyValidation() && _preparedSceneFuture->GetDependencyValidation().GetValidationIndex() != 0) {
				RebuildPreparedScene();
			} else {
				auto* t = _preparedSceneFuture->TryActualize();
				if (t) actualizedScene = t->get();
			}
		}

		if (actualizedScene) {

			// Have to do camera reset here after load to avoid threading issues
			if (_pendingCameraReset) {
				ResetCamera();
				_pendingCameraReset = false;
			}

			auto cam = _camera ? AsCameraDesc(*_camera) : RenderCore::Techniques::CameraDesc{};
			parserContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(cam, parserContext.GetViewport()._width / float(parserContext.GetViewport()._height));

			auto& lightScene = RenderCore::LightingEngine::GetLightScene(*actualizedScene->_compiledLightingTechnique);
			parserContext.GetAttachmentReservation().DefineDoubleBufferAttachments(RenderCore::LightingEngine::GetDoubleBufferAttachments(*actualizedScene->_compiledLightingTechnique));
			actualizedScene->_envSettings->PreRender(parserContext.GetProjectionDesc(), lightScene);

			TRY {
				auto lightingIterator = RenderCore::LightingEngine::BeginLightingTechniquePlayback(
					parserContext, *actualizedScene->_compiledLightingTechnique );

				for (;;) {
					auto next = lightingIterator.GetNextStep();
					if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
					if (next._type == RenderCore::LightingEngine::StepType::ParseScene) {
						assert(!next._pkts.empty());
						if (actualizedScene->_scene) {
							SceneEngine::ExecuteSceneContext executeContext{MakeIteratorRange(next._pkts), MakeIteratorRange(&parserContext.GetProjectionDesc(), &parserContext.GetProjectionDesc()+1), next._complexCullingVolume};
							actualizedScene->_scene->ExecuteScene(parserContext.GetThreadContext(), executeContext);
							parserContext.RequireCommandList(executeContext._completionCmdList);
						}
					} else if (next._type == RenderCore::LightingEngine::StepType::MultiViewParseScene) {
						assert(!next._pkts.empty());
						assert(!next._multiViewDesc.empty());
						if (actualizedScene->_scene) {
							SceneEngine::ExecuteSceneContext executeContext{MakeIteratorRange(next._pkts), next._multiViewDesc, next._complexCullingVolume};
							actualizedScene->_scene->ExecuteScene(parserContext.GetThreadContext(), executeContext);
							parserContext.RequireCommandList(executeContext._completionCmdList);
						}
					} else if (next._type == RenderCore::LightingEngine::StepType::ReadyInstances) {
						_deformAccelerators->ReadyInstances(parserContext.GetThreadContext());
					}
				}
			} CATCH(...) {
				actualizedScene->_envSettings->PostRender(lightScene);
				throw;
			} CATCH_END

			actualizedScene->_envSettings->PostRender(lightScene);

		} else {

			if (_errorMsg.empty()) {

				if (_preparedSceneFuture && _preparedSceneFuture->GetAssetState() == ::Assets::AssetState::Invalid) {
					auto log = ::Assets::AsString(_preparedSceneFuture->GetActualizationLog());
					assert(_fontRenderingManager);
					RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *_immediateDrawables, *_fontRenderingManager, *_debugShapesDelegate, "SimpleSceneOverlay failed with: " + log);
				} else if (_preparedSceneFuture || _showingLoadingIndicator) {
					// Draw a loading indicator,
					using namespace RenderOverlays::DebuggingDisplay;
					using namespace RenderOverlays;
					RenderOverlays::ImmediateOverlayContext overlays(parserContext.GetThreadContext(), *_immediateDrawables, _fontRenderingManager.get());
					overlays.CaptureState();
					auto viewportDims = Coord2(parserContext.GetViewport()._width, parserContext.GetViewport()._height);
					Rect rect { Coord2{0, 0}, viewportDims };
					RenderLoadingIndicator(overlays, rect, _loadingIndicatorCounter++);
					overlays.ReleaseState();

					auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, RenderCore::LoadStore::Clear);
					RenderOverlays::ExecuteDraws(parserContext, rpi, *_immediateDrawables, *_debugShapesDelegate);

					StringMeldAppend(parserContext._stringHelpers->_pendingAssets, ArrayEnd(parserContext._stringHelpers->_pendingAssets)) << "Scene Layer\n";
				} else {
					// clear, but don't draw anything
					auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, RenderCore::LoadStore::Clear);
				}

			} else {

				assert(_fontRenderingManager);
				RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *_immediateDrawables, *_fontRenderingManager, *_debugShapesDelegate, _errorMsg);

			}

		}
    }

	void SimpleSceneOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
	{
		assert(_pipelineAccelerators && _lightingApparatus);
		_lightingTechniqueTargetsHash = RenderCore::Techniques::HashPreregisteredAttachments(preregAttachments, fbProps);
		_lightingTechniqueTargets = {preregAttachments.begin(), preregAttachments.end()};
		_lightingTechniqueFBProps = fbProps;
		RebuildPreparedScene();
	}

	void SimpleSceneOverlay::RebuildPreparedScene()
	{
		if (!_envSettings || _lightingTechniqueTargets.empty() || (!_scene && !_useNullScene)) {
			_preparedSceneFuture = nullptr;
			_showingLoadingIndicator = false;
			return;
		}

		// If there's a previous construction operation still running, we have to stall for it to complete
		// Since we can share the env settings, we don't want to have to PreparedScene constructions in flight
		// at the same time
		if (_preparedSceneFuture)
			_preparedSceneFuture->StallWhilePending();
		_preparedSceneFuture = nullptr;
		_showingLoadingIndicator = false;

		_preparedSceneFuture = std::make_shared<::Assets::MarkerPtr<PreparedScene>>("simple-scene-layer");

		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[	promise = _preparedSceneFuture->AdoptPromise(),
				targets = _lightingTechniqueTargets, lightingApparatus = _lightingApparatus, 
				scene = _scene, envSettings = _envSettings, pipelineAccelerators = _pipelineAccelerators,
				loadingContext = _loadingContext]() mutable {

				TRY {
					SceneEngine::MergedLightingEngineCfg lightingEngineCfg;
					envSettings->BindCfg(lightingEngineCfg);
					auto compiledLightingTechniqueFuture = RenderCore::LightingEngine::CreationUtility{*lightingApparatus}
						.CreateToFuture(lightingEngineCfg.GetLightOperators(), lightingEngineCfg.GetShadowOperators(), lightingEngineCfg.GetChainedGlobalOperators(), {targets});

					::Assets::WhenAll(std::move(compiledLightingTechniqueFuture)).ThenConstructToPromise(
						std::move(promise),
						[pipelineAccelerators, loadingContext, envSettings, scene=std::move(scene)](std::promise<std::shared_ptr<PreparedScene>>&& thatPromise, auto compiledLightingTechnique) mutable {
							
							TRY {
								auto preparedScene = std::make_shared<PreparedScene>();
								preparedScene->_envSettings = envSettings;
								preparedScene->_compiledLightingTechnique = std::move(compiledLightingTechnique);
								preparedScene->_scene = std::move(scene);
								preparedScene->_depVal = RenderCore::LightingEngine::GetDependencyValidation(*preparedScene->_compiledLightingTechnique);

								auto& lightScene = RenderCore::LightingEngine::GetLightScene(*preparedScene->_compiledLightingTechnique);
								preparedScene->_envSettings->BindScene(lightScene, loadingContext);

								auto threadContext = RenderCore::Techniques::GetThreadContext();
								std::future<RenderCore::Techniques::PreparedResourcesVisibility> pendingResources;
								if (preparedScene->_scene)
									pendingResources = SceneEngine::PrepareResources(
										*threadContext,
										*preparedScene->_compiledLightingTechnique, *pipelineAccelerators, *preparedScene->_scene);
								if (pendingResources.valid()) {
									::Assets::WhenAll(std::move(pendingResources)).ThenConstructToPromise(
										std::move(thatPromise),
										[preparedScene](auto) mutable {
											return std::move(preparedScene);
										});
								} else {
									thatPromise.set_value(std::move(preparedScene));
								}
							} CATCH(...) {
								thatPromise.set_exception(std::current_exception());
							} CATCH_END
						});
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});

	}

    void SimpleSceneOverlay::Set(std::shared_ptr<SceneEngine::ILightingStateDelegate> envSettings)
    {
		_envSettings = std::move(envSettings);
		RebuildPreparedScene();
    }

	void SimpleSceneOverlay::Set(std::shared_ptr<SceneEngine::IScene> scene, std::shared_ptr<::Assets::OperationContext> loadingContext)
	{
		_loadingContext = std::move(loadingContext);
		_scene = std::move(scene);
		_useNullScene = false;
		RebuildPreparedScene();
	}

	void SimpleSceneOverlay::ShowLoadingIndicator()
	{
		_showingLoadingIndicator = true;
	}

	void SimpleSceneOverlay::SetEmptyScene()
	{
		_loadingContext = nullptr;
		_scene = nullptr;
		_useNullScene = true;
		RebuildPreparedScene();
	}

	void SimpleSceneOverlay::Set(std::shared_ptr<VisCameraSettings> camera, bool resetCamera)
	{
		_camera = std::move(camera);
		_pendingCameraReset = resetCamera;
	}

	void SimpleSceneOverlay::ResetCamera()
	{
		auto* t = _preparedSceneFuture ? _preparedSceneFuture->TryActualize() : nullptr;
		if (!t || !_camera) return;

		auto* visContentScene = dynamic_cast<ToolsRig::IVisContent*>((*t)->_scene.get());
		if (visContentScene) {
			auto boundingBox = visContentScene->GetBoundingBox();

			// make sure we always have some volume in the bounding box, to avoid degenerate cases
			if ((boundingBox.second[0] - boundingBox.first[0]) < 0.5f) {
				float a = 0.5f - (boundingBox.second[0] - boundingBox.first[0]);
				boundingBox.first[0] -= 0.5f * a;
				boundingBox.second[0] += 0.5f * a;
			}
			if ((boundingBox.second[1] - boundingBox.first[1]) < 0.5f) {
				float a = 0.5f - (boundingBox.second[1] - boundingBox.first[1]);
				boundingBox.first[1] -= 0.5f * a;
				boundingBox.second[1] += 0.5f * a;
			}
			if ((boundingBox.second[2] - boundingBox.first[2]) < 0.5f) {
				float a = 0.5f - (boundingBox.second[2] - boundingBox.first[2]);
				boundingBox.first[2] -= 0.5f * a;
				boundingBox.second[2] += 0.5f * a;
			}
			
			*_camera = ToolsRig::AlignCameraToBoundingBox(_camera->_verticalFieldOfView, boundingBox);
		}
	}

	auto SimpleSceneOverlay::GetOverlayState() const -> OverlayState
	{
		if (_preparedSceneFuture && _preparedSceneFuture->GetAssetState() == ::Assets::AssetState::Pending)
			return { RefreshMode::RegularAnimation };

		if (_showingLoadingIndicator) {
			// Loading indicator is showing -- we're expecting regular animation
			// Note that we can check the completion state there, but this works better -- because it ensures we
			// stay in the state until the first frame out of loading has been rendered
			return { RefreshMode::RegularAnimation };
		}

		RefreshMode refreshMode = RefreshMode::EventBased;
		auto* t = _preparedSceneFuture ? _preparedSceneFuture->TryActualize() : nullptr;

		// Need regular updates if the scene future hasn't been fully loaded yet
		// Or if there's active animation playing in the scene
		if (t) {
			auto* visContext = dynamic_cast<IVisContent*>((*t)->_scene.get());
			if (visContext && visContext->HasActiveAnimation())
				refreshMode = RefreshMode::RegularAnimation;
		}
		
		return { refreshMode };
	}

	void SimpleSceneOverlay::ReportError(StringSection<> msg)
	{
		_errorMsg = msg.AsString();
	}
	
    SimpleSceneOverlay::SimpleSceneOverlay(
		const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus,
		const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus,
		const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators)
    {
		_pipelineAccelerators = immediateDrawingApparatus->_mainDrawingApparatus->_pipelineAccelerators;
		_deformAccelerators = deformAccelerators;
		_immediateDrawables = immediateDrawingApparatus->_immediateDrawables;
		_debugShapesDelegate = immediateDrawingApparatus->_shapeRenderingDelegate;
		_fontRenderingManager = immediateDrawingApparatus->_fontRenderingManager;
		_lightingApparatus = lightingEngineApparatus;
    }

    SimpleSceneOverlay::~SimpleSceneOverlay() {}

	std::shared_ptr<ISimpleSceneOverlay> CreateSimpleSceneOverlay(
        const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus,
        const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus,
		const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators)
	{
		return std::make_shared<SimpleSceneOverlay>(immediateDrawingApparatus, lightingEngineApparatus, deformAccelerators);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::pair<DrawPreviewResult, std::string> DrawPreview(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		VisCameraSettings& cameraSettings,
		StringSection<> envSettings,
		SceneEngine::IScene& scene,
		const std::shared_ptr<SceneEngine::IRenderStep>& renderStep)
    {
		assert(0);	// update for LightingEngine
#if 0
		try
        {
			auto future = ::Assets::GetAssetFuture<SceneEngine::EnvironmentSettings>(envSettings._envConfigFile);
			future->StallWhilePending();
			SceneEngine::BasicLightingStateDelegate lightingParserDelegate(future->Actualize());

			auto renderSteps = SceneEngine::CreateStandardRenderSteps(AsLightingModel(envSettings._lightingType));
			if (renderStep) {		// if we've got a custom render step, override the default
				if (renderSteps.size() > 0) {
					renderSteps[0] = renderStep;
				} else {
					renderSteps.push_back(renderStep);
				}
			}

			std::shared_ptr<SceneEngine::ILightingParserPlugin> lightingPlugins[] = {
				std::make_shared<SceneEngine::LightingParserStandardPlugin>()
			};
			SceneEngine::SceneTechniqueDesc techniqueDesc{
				MakeIteratorRange(renderSteps),
				MakeIteratorRange(lightingPlugins)};

			auto compiledTechnique = CreateCompiledSceneTechnique(
				techniqueDesc,
				pipelineAccelerators,
				RenderCore::AsAttachmentDesc(renderTarget->GetDesc()));

			SceneEngine::LightingParser_ExecuteScene(
				context, renderTarget, parserContext,
				*compiledTechnique,
				lightingParserDelegate,
				scene, AsCameraDesc(cameraSettings));

            if (parserContext.HasErrorString())
				return std::make_pair(DrawPreviewResult::Error, parserContext._stringHelpers->_errorString);
			if (parserContext.HasInvalidAssets())
				return std::make_pair(DrawPreviewResult::Error, "Invalid assets encountered");
            if (parserContext.HasPendingAssets())
				return std::make_pair(DrawPreviewResult::Pending, std::string());

            return std::make_pair(DrawPreviewResult::Success, std::string());
        }
        catch (::Assets::Exceptions::InvalidAsset& e) { return std::make_pair(DrawPreviewResult::Error, e.what()); }
        catch (::Assets::Exceptions::PendingAsset& e) { return std::make_pair(DrawPreviewResult::Pending, e.Initializer()); }
#endif

        return std::make_pair(DrawPreviewResult::Error, std::string());
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	static RenderCore::Techniques::TechniqueContext MakeRayTestTechniqueContext(RenderCore::Techniques::DrawingApparatus& drawingApparatus)
    {
        RenderCore::Techniques::TechniqueContext techniqueContext;
		RenderCore::Techniques::InitializeTechniqueContext(techniqueContext, drawingApparatus);
        return techniqueContext;
    }

	static SceneEngine::IntersectionTestResult FirstRayIntersection(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::DrawingApparatus& drawingApparatus,
        std::pair<Float3, Float3> worldSpaceRay,
		SceneEngine::IScene& scene,
		std::optional<RenderCore::Techniques::ProjectionDesc> viewProjDesc)
	{
		using namespace RenderCore;
		std::vector<SceneEngine::ModelIntersectionStateContext::ResultEntry> results;

		RenderCore::Techniques::DrawablesPacket pkt;
		RenderCore::Techniques::DrawablesPacket* pkts[(unsigned)RenderCore::Techniques::Batch::Max] {};
		pkts[(unsigned)RenderCore::Techniques::Batch::Opaque] = &pkt;
		SceneEngine::ExecuteSceneContext sceneExecuteContext{MakeIteratorRange(pkts), {}};
		if (viewProjDesc)
			sceneExecuteContext._views = { &(*viewProjDesc), &(*viewProjDesc)+1 };

		TRY {
			auto techniqueContext = MakeRayTestTechniqueContext(drawingApparatus);
			Techniques::ParsingContext parserContext { techniqueContext, threadContext };
			if (viewProjDesc)
				parserContext.GetProjectionDesc() = *viewProjDesc;
			parserContext.SetPipelineAcceleratorsVisibility(techniqueContext._pipelineAccelerators->VisibilityBarrier());

			scene.ExecuteScene(threadContext, sceneExecuteContext);
			parserContext.RequireCommandList(sceneExecuteContext._completionCmdList);

			if (drawingApparatus._deformAccelerators)
				drawingApparatus._deformAccelerators->ReadyInstances(threadContext);
			
			SceneEngine::ModelIntersectionStateContext stateContext {
				SceneEngine::ModelIntersectionStateContext::RayTest,
				threadContext, drawingApparatus._pipelineAccelerators,
				parserContext.GetPipelineAcceleratorsVisibility() };
			stateContext.SetRay(worldSpaceRay);
			stateContext.ExecuteDrawables(parserContext, pkt, 0);

			// Stall if we haven't yet submitted required buffer uploads command lists
			// (if we bail here, the draw commands are have still be submitted and we will run into ordering problems later)
			auto requiredBufferUploads = parserContext._requiredBufferUploadsCommandList;
			if (requiredBufferUploads) {
				auto& bu=RenderCore::Techniques::Services::GetBufferUploads();
				bu.StallAndMarkCommandListDependency(threadContext, parserContext._requiredBufferUploadsCommandList);
			}
			
			results = stateContext.GetResults();
		} CATCH(...) {
			// suppress exceptions during intersection detection
			// we can get pending assets, etc
		} CATCH_END

		if (drawingApparatus._deformAccelerators)
			drawingApparatus._deformAccelerators->OnFrameBarrier(); // must create a fake "frame barrier" -- to reset before whatever comes after this

		if (!results.empty()) {
			const auto& r = results[0];

			SceneEngine::IntersectionTestResult result;
			result._type = SceneEngine::IntersectionTestResult::Type::Extra;
			result._worldSpaceIntersectionPt = 
				worldSpaceRay.first + r._intersectionDepth * Normalize(worldSpaceRay.second - worldSpaceRay.first);
			result._worldSpaceIntersectionNormal = {0,0,0};
			result._distance = r._intersectionDepth;

			auto* visContent = dynamic_cast<IVisContent*>(&scene);
			if (visContent) {
				SceneEngine::DrawableMetadataLookupContext lookupContext{
					{&r._drawableIndex, &r._drawableIndex+1}, 0
				};
				visContent->LookupDrawableMetadata(sceneExecuteContext, lookupContext);
				if (!lookupContext.GetProviders().empty())
					result._metadataQuery = lookupContext.GetProviders()[0];
			}

			return result;
		}
		return {};
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename T>
		T TryAnyCast(std::any&& any, T defaultValue)
	{
		if (any.has_value() && any.type() == typeid(T))
			return std::any_cast<T>(std::move(any));
		return defaultValue;
	}

    class MouseOverTrackingListener : public PlatformRig::IInputListener, public std::enable_shared_from_this<MouseOverTrackingListener>
    {
    public:
        ProcessInputResult OnInputEvent(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt)
        {
			if (evnt._mouseDelta == OSServices::Coord2 { 0, 0 } && _mouseOver->_state != ContinuousSceneQuery::State::Pending)
				return ProcessInputResult::Passthrough;

			// early out with zero area viewport
			if (!((context._view._viewMaxs[0] - context._view._viewMins[0]) * (context._view._viewMaxs[1] - context._view._viewMins[1])))
				return ProcessInputResult::Passthrough;

			// Limit the update frequency by ensuring that enough time has
			// passed since the last time we did an update. If there hasn't
			// been enough time, we should schedule a timeout event to trigger.
			//
			// If there has already been a timeout event scheduled, we have 2 options.
			// Either we reschedule it, or we just allow the previous timeout to 
			// finish as normal.
			//			
			// If we rescheduled the event, it would mean that fast movement of the 
			// mouse would disable all update events, and we would only get new information
			// after the mouse has come to rest for the timeout period.
			//
			// The preferred option may depend on the particular use case.
			auto time = std::chrono::steady_clock::now();
			const auto timePeriod = std::chrono::milliseconds(200u);
			_timeoutContext = context._view;
			_timeoutMousePosition = {evnt._mousePosition._x, evnt._mousePosition._y};
			if ((time - _timeOfLastCalculate) < timePeriod) {
				auto* osRunLoop = OSServices::GetOSRunLoop();
				if (_timeoutEvent == ~0u && osRunLoop) {
					std::weak_ptr<MouseOverTrackingListener> weakThis = weak_from_this();
					std::weak_ptr<RenderCore::IThreadContext> weakThreadContext = RenderCore::Techniques::GetThreadContext();
					_timeoutEvent = osRunLoop->ScheduleTimeoutEvent(
						time + timePeriod,
						[weakThis, weakThreadContext]() {
							auto l = weakThis.lock();
							auto l2 = weakThreadContext.lock();
							if (l && l2) {
								auto oldTC = RenderCore::Techniques::SetThreadContext(std::move(l2));
								PlatformRig::InputContext context;
								context._view = l->_timeoutContext;
								l->_timeOfLastCalculate = std::chrono::steady_clock::now();
								l->CalculateForMousePosition(context, l->_timeoutMousePosition);
								l->_timeoutEvent = ~0u;
								RenderCore::Techniques::SetThreadContext(std::move(oldTC));
							}
						});
				}
			} else {
				auto* osRunLoop = OSServices::GetOSRunLoop();
				if (_timeoutEvent != ~0u && osRunLoop) {
					osRunLoop->RemoveEvent(_timeoutEvent);
					_timeoutEvent = ~0u;
				}

				CalculateForMousePosition(context, {evnt._mousePosition._x, evnt._mousePosition._y});
				_timeOfLastCalculate = time;
			}

			return ProcessInputResult::Passthrough;
		}

		void CalculateForMousePosition(
			const PlatformRig::InputContext& context,
			PlatformRig::Coord2 mousePosition)
		{
            auto worldSpaceRay = SceneEngine::CalculateWorldSpaceRay(
				AsCameraDesc(*_camera), mousePosition, context._view._viewMins, context._view._viewMaxs);

            if (!_scene) {
				auto oldState = _mouseOver->_state;
				_mouseOver->_state = ContinuousSceneQuery::State::Pending;
				_mouseOver->_metadataQuery = {};
				if (_mouseOver->_state != oldState)
					_mouseOver->_changeEvent.Invoke();
				return;
			}

			auto cameraProjDesc = RenderCore::Techniques::BuildProjectionDesc(AsCameraDesc(*_camera), (context._view._viewMaxs[0] - context._view._viewMins[0]) / float(context._view._viewMaxs[1] - context._view._viewMins[1]));

			auto threadContext = RenderCore::Techniques::GetThreadContext();
			auto intr = FirstRayIntersection(*threadContext, *_drawingApparatus, worldSpaceRay, *_scene, cameraProjDesc);
			if (intr._type != 0) {
				unsigned drawCallIndex = ~0u;
				uint64_t materialGuid = ~0ull;
				if (intr._metadataQuery) {
					drawCallIndex = TryAnyCast(intr._metadataQuery("DrawCallIndex"_h), drawCallIndex);
					materialGuid = TryAnyCast(intr._metadataQuery("MaterialGuid"_h), materialGuid);
				}
				if (        drawCallIndex != _mouseOver->_drawCallIndex
						||  materialGuid != _mouseOver->_materialGuid
						||  _mouseOver->_state != ContinuousSceneQuery::State::Good) {

					_mouseOver->_state = ContinuousSceneQuery::State::Good;
					_mouseOver->_drawCallIndex = drawCallIndex;
					_mouseOver->_materialGuid = materialGuid;
					_mouseOver->_metadataQuery = std::move(intr._metadataQuery);
					_mouseOver->_changeEvent.Invoke();
				}
			} else {
				auto oldState = _mouseOver->_state;
				_mouseOver->_state = ContinuousSceneQuery::State::Empty;
				_mouseOver->_metadataQuery = {};
				if (oldState != ContinuousSceneQuery::State::Empty) {
					_mouseOver->_changeEvent.Invoke();
				}
			}
        }

		void Set(std::shared_ptr<SceneEngine::IScene> scene) { _scene = std::move(scene); }

        MouseOverTrackingListener(
            std::shared_ptr<ContinuousSceneQuery> mouseOver,
            std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
            std::shared_ptr<VisCameraSettings> camera)
        : _mouseOver(std::move(mouseOver))
        , _drawingApparatus(std::move(drawingApparatus))
        , _camera(std::move(camera))
        {}
        ~MouseOverTrackingListener() {}

    protected:
        std::shared_ptr<ContinuousSceneQuery> _mouseOver;
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
        std::shared_ptr<VisCameraSettings> _camera;
        
		std::shared_ptr<SceneEngine::IScene> _scene;
		std::chrono::time_point<std::chrono::steady_clock> _timeOfLastCalculate;

		PlatformRig::WindowingSystemView _timeoutContext;
		PlatformRig::Coord2 _timeoutMousePosition;
		unsigned _timeoutEvent = ~0u;
    };

	std::shared_ptr<PlatformRig::IInputListener> CreateMouseTrackingInputListener(
        std::shared_ptr<ContinuousSceneQuery> mouseOver,
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::IScene> scene,
        std::shared_ptr<VisCameraSettings> camera)
	{
		auto res =  std::make_shared<MouseOverTrackingListener>(std::move(mouseOver), std::move(drawingApparatus), std::move(camera));
		res->Set(std::move(scene));
		return res;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class StencilRefDelegate : public RenderCore::Techniques::ICustomDrawDelegate
	{
	public:
		virtual void OnDraw(
			RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& executeContext,
			const RenderCore::Techniques::Drawable& d) override
		{
			auto drawCallIdx = GetDrawCallIndex(d);
			executeContext.SetStencilRef(drawCallIdx+1, drawCallIdx+1);
			ExecuteStandardDraw(parsingContext, executeContext, d);
		}
	};

    class VisualisationOverlay::Pimpl
    {
    public:
		VisOverlaySettings _settings;
        std::shared_ptr<ContinuousSceneQuery> _mouseOver;
		std::shared_ptr<VisCameraSettings> _cameraSettings;
		std::shared_ptr<VisAnimationState> _animState;
		std::shared_ptr<MouseOverTrackingListener> _inputListener;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> _debugShapesDelegate;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;

		std::shared_ptr<SceneEngine::IScene> _scene;
		std::shared_ptr<::Assets::OperationContext> _loadingContext;
		bool _pendingAnimStateBind = false;
		uint64_t _renderTargetHashes = 0;

		std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> _stencilPrimeDelegate;

		struct SequencerCfgs
		{
			std::shared_ptr<RenderCore::Techniques::SequencerConfig> _visWireframeCfg;
			std::shared_ptr<RenderCore::Techniques::SequencerConfig> _visNormalsCfg;
			std::shared_ptr<RenderCore::Techniques::SequencerConfig> _primeStencilCfg;
			::Assets::DependencyValidation _depVals;
		};
		std::shared_future<SequencerCfgs> _futureSequencerCfgs;

		std::string _errorMsg;

		Pimpl()
		{
			_stencilPrimeDelegate = std::make_shared<StencilRefDelegate>();
		}
    };

	static void RenderTrackingOverlay(
        RenderOverlays::IOverlayContext& context,
		const RenderOverlays::Rect& viewport,
		const ToolsRig::ContinuousSceneQuery& mouseOver, 
		const SceneEngine::IScene& scene)
    {
        using namespace RenderOverlays::DebuggingDisplay;
		using namespace RenderOverlays;

        const auto textHeight = 20u;
        std::string matName;
		if (mouseOver._metadataQuery)
			matName = TryAnyCast(mouseOver._metadataQuery("MaterialName"_h), matName);
        DrawText()
			.Color(ColorB(0xffafafaf))
			.Draw(context, Rect(Coord2(viewport._topLeft[0]+3, viewport._bottomRight[1]-textHeight-8), Coord2(viewport._bottomRight[0]-6, viewport._bottomRight[1]-8)),
				StringMeld<512>() 
					<< "Material: {Color:7f3faf}" << matName
					<< "{Color:afafaf}, Draw call: " << mouseOver._drawCallIndex
					<< std::setprecision(4)
					<< ", (" << mouseOver._intersectionPt[0]
					<< ", "  << mouseOver._intersectionPt[1]
					<< ", "  << mouseOver._intersectionPt[2]
					<< ")");
    }

	static RenderCore::Techniques::FrameBufferDescFragment CreateVisFBFrag()
	{
		using namespace RenderCore;
		Techniques::FrameBufferDescFragment fbDesc;
		SubpassDesc mainPass;
		mainPass.SetName("VisualisationOverlay");
		mainPass.AppendOutput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR));
		mainPass.SetDepthStencil(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear));		// ensure stencil is cleared (but ok to keep depth)
		fbDesc.AddSubpass(std::move(mainPass));
		return fbDesc;
	}

	static RenderCore::Techniques::FrameBufferDescFragment CreateVisJustStencilFrag()
	{
		using namespace RenderCore;
		Techniques::FrameBufferDescFragment fbDesc;
		SubpassDesc mainPass;
		mainPass.SetName("VisualisationOverlay");
		mainPass.SetDepthStencil(
			fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear, BindFlag::DepthStencil).FinalState(BindFlag::ShaderResource),
			TextureViewDesc{TextureViewDesc::Aspect::Stencil});
		fbDesc.AddSubpass(std::move(mainPass));
		return fbDesc;
	}

	static bool HasDepthAttachment(RenderCore::Techniques::ParsingContext& parserContext)
	{
		using namespace RenderCore;
		if (!parserContext.GetAttachmentReservation().MapSemanticToResource(Techniques::AttachmentSemantics::MultisampleDepth))
			return false;

		auto preRegs = parserContext.GetFragmentStitchingContext().GetPreregisteredAttachments();
		auto q = std::find_if(
			preRegs.begin(), preRegs.end(),
			[](const auto&a) { return a._semantic == Techniques::AttachmentSemantics::MultisampleDepth; });
		if (q == preRegs.end())
			return false;
		return true;
	}

    void VisualisationOverlay::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace RenderCore;

		if (!HasDepthAttachment(parserContext))
			return;		// we need this attachment to continue

		if (!_pimpl->_cameraSettings) return;

		if (_pimpl->_pendingAnimStateBind && _pimpl->_scene) {
			auto* visContext = dynamic_cast<IVisContent*>(_pimpl->_scene.get());
			if (visContext && _pimpl->_animState)
				visContext->BindAnimationState(_pimpl->_animState);
			_pimpl->_pendingAnimStateBind = false;
		}

		if (_pimpl->_futureSequencerCfgs.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			return;

		auto cfgs = _pimpl->_futureSequencerCfgs.get();

		UInt2 viewportDims(parserContext.GetViewport()._width, parserContext.GetViewport()._height);
		assert(viewportDims[0] && viewportDims[1]);
		auto cam = AsCameraDesc(*_pimpl->_cameraSettings);
		auto sceneView = Techniques::BuildProjectionDesc(cam, viewportDims[0] / float(viewportDims[1]));

		bool doColorByMaterial = 
			(_pimpl->_settings._colourByMaterial == 1)
			|| (_pimpl->_settings._colourByMaterial == 2 && _pimpl->_mouseOver->_state == ContinuousSceneQuery::State::Good);
		doColorByMaterial &= _pimpl->_scene != nullptr;

		if (_pimpl->_settings._drawWireframe || _pimpl->_settings._drawNormals || _pimpl->_settings._skeletonMode || doColorByMaterial) {

			bool drawImmediateDrawables = false;
			if (_pimpl->_settings._skeletonMode && _pimpl->_scene) {
				CATCH_ASSETS_BEGIN
					auto* visContent = dynamic_cast<IVisContent*>(_pimpl->_scene.get());
					if (visContent) {
						// awkwardly, we don't call RenderSkeleton during an rpi because it can render glyphs to a font texture
						RenderOverlays::ImmediateOverlayContext overlays(parserContext.GetThreadContext(), *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
						visContent->RenderSkeleton(
							overlays, parserContext,
							_pimpl->_settings._skeletonMode == 2);
						drawImmediateDrawables = true;
					}
				CATCH_ASSETS_END(parserContext)
			}
			
			{
				auto fbFrag = CreateVisFBFrag();
				Techniques::RenderPassInstance rpi { parserContext, fbFrag };

				if (_pimpl->_settings._drawWireframe && _pimpl->_scene) {
					SceneEngine::ExecuteSceneRaw(
						parserContext, *_pimpl->_pipelineAccelerators,
						*cfgs._visWireframeCfg,
						sceneView, RenderCore::Techniques::Batch::Opaque,
						*_pimpl->_scene);
				}

				if (_pimpl->_settings._drawNormals && _pimpl->_scene) {
					SceneEngine::ExecuteSceneRaw(
						parserContext, *_pimpl->_pipelineAccelerators,
						*cfgs._visNormalsCfg,
						sceneView, RenderCore::Techniques::Batch::Opaque,
						*_pimpl->_scene);
				}

				if (drawImmediateDrawables)
					RenderOverlays::ExecuteDraws(parserContext, rpi, *_pimpl->_immediateDrawables, *_pimpl->_debugShapesDelegate);
			}

			if (doColorByMaterial) {
				auto fbFrag = CreateVisJustStencilFrag();
				Techniques::RenderPassInstance rpi { parserContext, fbFrag };

				auto *visContent = dynamic_cast<IVisContent*>(_pimpl->_scene.get());
				std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> oldDelegate;
				if (visContent)
					oldDelegate = visContent->SetCustomDrawDelegate(_pimpl->_stencilPrimeDelegate);
				// Prime the stencil buffer with draw call indices
				SceneEngine::ExecuteSceneRaw(
					parserContext, *_pimpl->_pipelineAccelerators,
					*cfgs._primeStencilCfg,
					sceneView, RenderCore::Techniques::Batch::Opaque,
					*_pimpl->_scene);
				if (visContent)
					visContent->SetCustomDrawDelegate(oldDelegate);
			}
		}
		
            //  Draw an overlay over the scene, 
            //  containing debugging / profiling information
        if (doColorByMaterial) {
			CATCH_ASSETS_BEGIN
                RenderOverlays::HighlightByStencilSettings settings;

				// The highlight shader supports remapping the 8 bit stencil value to through an array
				// to some other value. This is useful for ignoring bits or just making 2 different stencil
				// buffer values mean the same thing. We don't need it right now though, we can just do a
				// direct mapping here --
				auto marker = _pimpl->_mouseOver->_drawCallIndex;
				settings._highlightedMarker = marker+1;
				settings._backgroundMarker = marker;

                ExecuteHighlightByStencil(
                    parserContext, 
                    settings, _pimpl->_settings._colourByMaterial==2);
            CATCH_ASSETS_END(parserContext)
        }

		bool writeMaterialName = 
			(_pimpl->_settings._colourByMaterial == 2 && _pimpl->_mouseOver->_state == ContinuousSceneQuery::State::Good && _pimpl->_scene);

		if (writeMaterialName || _pimpl->_settings._drawBasisAxis || _pimpl->_settings._drawGrid) {

			CATCH_ASSETS_BEGIN

				using namespace RenderOverlays::DebuggingDisplay;
				using namespace RenderOverlays;
				ImmediateOverlayContext overlays(parserContext.GetThreadContext(), *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
				overlays.CaptureState();
				Rect rect { Coord2{0, 0}, Coord2(viewportDims[0], viewportDims[1]) };
				if (writeMaterialName)
					RenderTrackingOverlay(overlays, rect, *_pimpl->_mouseOver, *_pimpl->_scene);
				if (_pimpl->_settings._drawBasisAxis)
					RenderOverlays::DrawBasisAxes(overlays.GetImmediateDrawables(), parserContext);
				if (_pimpl->_settings._drawGrid)
					RenderOverlays::DrawGrid(overlays.GetImmediateDrawables(), parserContext, std::abs(Magnitude(ExtractTranslation(cam._cameraToWorld))));
				overlays.ReleaseState();

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithDepthStencil(parserContext);
				RenderOverlays::ExecuteDraws(parserContext, rpi, *_pimpl->_immediateDrawables, *_pimpl->_debugShapesDelegate);

			CATCH_ASSETS_END(parserContext)
		}

		if (!_pimpl->_errorMsg.empty())
			RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *_pimpl->_immediateDrawables, *_pimpl->_fontRenderingManager, *_pimpl->_debugShapesDelegate, _pimpl->_errorMsg);

		const bool dummyCalculation = false;
		if constexpr (dummyCalculation) {
			PlatformRig::InputContext inputContext;
			PlatformRig::WindowingSystemView view { {0, 0}, {256, 256} };
			inputContext.AttachService2(view);
			PlatformRig::Coord2 mousePosition {128, 128};
			_pimpl->_inputListener->CalculateForMousePosition(inputContext, mousePosition);
		}
    }

	void VisualisationOverlay::Set(std::shared_ptr<SceneEngine::IScene> scene)
	{
		_pimpl->_scene = std::move(scene);
		_pimpl->_pendingAnimStateBind = true;
		if (_pimpl->_inputListener)
			_pimpl->_inputListener->Set(_pimpl->_scene);
	}

	void VisualisationOverlay::Set(const std::shared_ptr<VisCameraSettings>& camera, bool)
	{
		_pimpl->_cameraSettings = camera;
		_pimpl->_inputListener = nullptr;
		_pimpl->_inputListener = std::make_shared<MouseOverTrackingListener>(_pimpl->_mouseOver, _pimpl->_drawingApparatus, _pimpl->_cameraSettings);
		_pimpl->_inputListener->Set(_pimpl->_scene);
	}

	void VisualisationOverlay::Set(const VisOverlaySettings& overlaySettings)
	{
		_pimpl->_settings = overlaySettings;
	}

	void VisualisationOverlay::ReportError(StringSection<> msg)
	{
		_pimpl->_errorMsg = msg.AsString();
	}

	const VisOverlaySettings& VisualisationOverlay::GetOverlaySettings() const
	{
		return _pimpl->_settings;
	}

	std::shared_ptr<ContinuousSceneQuery> VisualisationOverlay::GetMouseOver() const
	{
		return _pimpl->_mouseOver;
	}

	void VisualisationOverlay::Set(const std::shared_ptr<VisAnimationState>& animState)
	{
		_pimpl->_animState = animState;
		_pimpl->_pendingAnimStateBind = true;
	}

    auto VisualisationOverlay::GetOverlayState() const -> OverlayState
	{
		// Need regular updates if the scene future hasn't been fully loaded yet
		// Or if there's active animation playing in the scene
		RefreshMode refreshMode = RefreshMode::EventBased;
		
		if (_pimpl->_scene) {
			auto* visContext = dynamic_cast<IVisContent*>(_pimpl->_scene.get());
			if (visContext && visContext->HasActiveAnimation())
				refreshMode = RefreshMode::RegularAnimation;
		}
		
		return { refreshMode };
	}

	PlatformRig::ProcessInputResult VisualisationOverlay::ProcessInput(
		const PlatformRig::InputContext& context,
		const OSServices::InputSnapshot& evnt)
	{
		if (_pimpl->_inputListener)
			return _pimpl->_inputListener->OnInputEvent(context, evnt);
		return PlatformRig::ProcessInputResult::Passthrough;
	}

	void VisualisationOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
	{
		using namespace RenderCore;

		auto hash = HashPreregisteredAttachmentsResolutionIndependent(preregAttachments, fbProps);
		hash = Hash64(systemAttachmentFormats.begin(), systemAttachmentFormats.end(), hash);
		if (hash == _pimpl->_renderTargetHashes)
			return;		// if it's just a resolution change, or something, we don't have to rebuild anything -- just move on

		_pimpl->_renderTargetHashes = hash;

		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> visWireframeDelegate;
		auto visWireframeDelegateFuture = visWireframeDelegate.get_future();
		RenderCore::Techniques::CreateTechniqueDelegateLegacy(
			std::move(visWireframeDelegate),
			Techniques::TechniqueIndex::VisWireframe, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);

		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> visNormalsDelegate;
		auto visNormalsDelegateFuture = visNormalsDelegate.get_future();
		RenderCore::Techniques::CreateTechniqueDelegateLegacy(
			std::move(visNormalsDelegate),
			Techniques::TechniqueIndex::VisNormals, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);

		DepthStencilDesc ds {
			RenderCore::CompareOp::GreaterEqual, true, true,
			0xff, 0xff,
			RenderCore::StencilDesc::AlwaysWrite,
			RenderCore::StencilDesc::NoEffect };
		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> primeStencilBufferDelegate;
		auto primeStencilBufferDelegateFuture = primeStencilBufferDelegate.get_future();
		RenderCore::Techniques::CreateTechniqueDelegateLegacy(
			std::move(primeStencilBufferDelegate),
			Techniques::TechniqueIndex::DepthOnly, {}, {}, ds);

		std::promise<Pimpl::SequencerCfgs> promisedSequencerCfgs;
		_pimpl->_futureSequencerCfgs = promisedSequencerCfgs.get_future();
		std::vector<RenderCore::Techniques::PreregisteredAttachment> attachments { preregAttachments.begin(), preregAttachments.end() };
		std::vector<RenderCore::Format> sysFormat { systemAttachmentFormats.begin(), systemAttachmentFormats.end() };
		::Assets::WhenAll(std::move(visWireframeDelegateFuture), std::move(visNormalsDelegateFuture), std::move(primeStencilBufferDelegateFuture))
			.ThenConstructToPromise(
				std::move(promisedSequencerCfgs),
				[pipelineAccelerators=_pimpl->_pipelineAccelerators, fbProps, preregAttachments=std::move(attachments), systemAttachmentFormats=std::move(sysFormat)](auto visWireframeDelegate, auto visNormalsDelegate, auto primeStencilBufferDelegate) {

					RenderCore::Techniques::FragmentStitchingContext stitching;

					// We can't register the given preregistered attachments directly -- instead we have to 
					// register what we're expecting to be given when we actually begin our render
					auto color = std::find_if(
						preregAttachments.begin(), preregAttachments.end(), 
						[](auto c) { return c._semantic == Techniques::AttachmentSemantics::ColorLDR; });
					if (color != preregAttachments.end()) {
						// register an initialized color texture
						auto colorPreg = *color;
						colorPreg._state = Techniques::PreregisteredAttachment::State::Initialized;
						colorPreg._layout = BindFlag::RenderTarget;
						stitching.DefineAttachment(colorPreg);

						// register a default depth texture
						auto depthDesc = colorPreg._desc;
						depthDesc._bindFlags = BindFlag::DepthStencil|BindFlag::TransferSrc|BindFlag::ShaderResource;
						assert(systemAttachmentFormats.size() > (unsigned)Techniques::SystemAttachmentFormat::MainDepthStencil);
						depthDesc._textureDesc._format = systemAttachmentFormats[(unsigned)Techniques::SystemAttachmentFormat::MainDepthStencil];
						stitching.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, depthDesc, "main-depth", Techniques::PreregisteredAttachment::State::Initialized, BindFlag::DepthStencil);
					}

					Pimpl::SequencerCfgs cfgs;
					auto fbFrag = CreateVisFBFrag();
					auto stitched = stitching.TryStitchFrameBufferDesc({&fbFrag, &fbFrag+1}, fbProps);
					cfgs._visWireframeCfg = pipelineAccelerators->CreateSequencerConfig("vis-wireframe");
					pipelineAccelerators->SetTechniqueDelegate(*cfgs._visWireframeCfg, std::move(visWireframeDelegate));
					pipelineAccelerators->SetFrameBufferDesc(*cfgs._visWireframeCfg, stitched._fbDesc);
					cfgs._visNormalsCfg = pipelineAccelerators->CreateSequencerConfig("vis-normals");
					pipelineAccelerators->SetTechniqueDelegate(*cfgs._visNormalsCfg, std::move(visNormalsDelegate));
					pipelineAccelerators->SetFrameBufferDesc(*cfgs._visNormalsCfg, stitched._fbDesc);

					auto justStencilFrag = CreateVisJustStencilFrag();
					auto justStencilStitched = stitching.TryStitchFrameBufferDesc({&justStencilFrag, &justStencilFrag+1}, fbProps);
					cfgs._primeStencilCfg = pipelineAccelerators->CreateSequencerConfig("vis-prime-stencil");
					pipelineAccelerators->SetTechniqueDelegate(*cfgs._primeStencilCfg, std::move(primeStencilBufferDelegate));
					pipelineAccelerators->SetFrameBufferDesc(*cfgs._primeStencilCfg, justStencilStitched._fbDesc);
					return cfgs;
				});
	}

    VisualisationOverlay::VisualisationOverlay(
		const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus,
		const VisOverlaySettings& overlaySettings)
    {
		using namespace RenderCore;
        _pimpl = std::make_unique<Pimpl>();
		_pimpl->_pipelineAccelerators = immediateDrawingApparatus->_mainDrawingApparatus->_pipelineAccelerators;
		_pimpl->_immediateDrawables = immediateDrawingApparatus->_immediateDrawables;
		_pimpl->_debugShapesDelegate = immediateDrawingApparatus->_shapeRenderingDelegate;
		_pimpl->_fontRenderingManager = immediateDrawingApparatus->_fontRenderingManager;
		_pimpl->_drawingApparatus = immediateDrawingApparatus->_mainDrawingApparatus;
        _pimpl->_settings = overlaySettings;

        _pimpl->_mouseOver = std::make_shared<ContinuousSceneQuery>();
    }

    VisualisationOverlay::~VisualisationOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class InputLayer : public PlatformRig::IOverlaySystem
    {
    public:
        virtual PlatformRig::ProcessInputResult ProcessInput(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt) override;

        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        InputLayer(std::shared_ptr<PlatformRig::IInputListener> listener);
        ~InputLayer();
    protected:
        std::shared_ptr<PlatformRig::IInputListener> _listener;
    };

	PlatformRig::ProcessInputResult InputLayer::ProcessInput(
		const PlatformRig::InputContext& context,
		const OSServices::InputSnapshot& evnt)
	{
		return _listener->OnInputEvent(context, evnt);
	}

    void InputLayer::Render(
		RenderCore::Techniques::ParsingContext&) {}

    InputLayer::InputLayer(std::shared_ptr<PlatformRig::IInputListener> listener) : _listener(std::move(listener)) {}
    InputLayer::~InputLayer() {}

	std::shared_ptr<PlatformRig::IOverlaySystem> MakeLayerForInput(std::shared_ptr<PlatformRig::IInputListener> listener)
	{
		return std::make_shared<InputLayer>(std::move(listener));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	struct VisOverlayController::Pimpl
	{
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
        std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
        std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAcceleratorPool;

        std::shared_ptr<ISimpleSceneOverlay> _sceneOverlay;
        std::vector<std::shared_ptr<IVisualisationOverlay>> _visualisationOverlays;

        enum class SceneBindType { ModelVisSettings, MaterialVisSettings, Ptr, Marker };
        SceneBindType _sceneBindType = SceneBindType::Ptr;
        ModelVisSettings _modelVisSettings;
        MaterialVisSettings _materialVisSettings;
        std::shared_ptr<SceneEngine::IScene> _scene;
        Assets::PtrToMarkerPtr<SceneEngine::IScene> _sceneMarker;

		enum LightingStateBindType { Filename, Ptr, Marker };
		LightingStateBindType _lightingStateBindType = LightingStateBindType::Ptr;
		std::string _lightingStateFilename;
		std::shared_ptr<SceneEngine::ILightingStateDelegate> _lightingState;
        Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> _lightingStateMarker;

		std::shared_ptr<::Assets::OperationContext> _loadingContext;

		bool _pendingSceneActualize = false;
		bool _pendingLightingStateActualize = false;
		std::string _sceneReportedError, _lightingStateReportedError;
		unsigned _lastGlobalDepValChangeIndex = 0;

		unsigned _mainThreadTickFn = ~0u;

		void MainThreadTick();
		void UpdateVisualizationError();
	};

	void VisOverlayController::Pimpl::MainThreadTick()
	{
		if (_pendingSceneActualize && _sceneMarker) {
			if (auto* actualized = _sceneMarker->TryActualize()) {
				if (_sceneOverlay) _sceneOverlay->Set(*actualized, _loadingContext);
				for (auto& v:_visualisationOverlays) v->Set(*actualized);
				_pendingSceneActualize = false;
			} else if (_sceneMarker->GetAssetState() == ::Assets::AssetState::Invalid) {
				_sceneReportedError = "Scene load failed with error: " + ::Assets::AsString(_sceneMarker->GetActualizationLog());
				UpdateVisualizationError();
				_pendingSceneActualize = false;
			}
		}
		if (_pendingLightingStateActualize && _lightingStateMarker) {
			if (auto* actualized = _lightingStateMarker->TryActualize()) {
				if (_sceneOverlay) _sceneOverlay->Set(*actualized);
				_pendingLightingStateActualize = false;
			} else if (_lightingStateMarker->GetAssetState() == ::Assets::AssetState::Invalid) {
				_lightingStateReportedError = "Lighting state load failed with error: " + ::Assets::AsString(_lightingStateMarker->GetActualizationLog());
				UpdateVisualizationError();
				_pendingLightingStateActualize = false;
			}
		}

		auto depValChangeIndex = ::Assets::GetDepValSys().GlobalChangeIndex();
		if (depValChangeIndex != _lastGlobalDepValChangeIndex) {
			_lastGlobalDepValChangeIndex = depValChangeIndex;
			if (_sceneBindType == SceneBindType::ModelVisSettings && _sceneMarker && ::Assets::IsInvalidated(*_sceneMarker)) {
				// scene hot reload
				if (_sceneOverlay) _sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});
				for (auto& v:_visualisationOverlays) v->Set(std::shared_ptr<SceneEngine::IScene>{});

				if (!_modelVisSettings._modelName.empty()) {
					_sceneMarker = ModelVisUtility{_drawablesPool, _pipelineAcceleratorPool, _deformAcceleratorPool, _loadingContext}
						.MakeScene(_modelVisSettings);
					_pendingSceneActualize = true;
				} else {
					if (_sceneOverlay) _sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{}, _loadingContext);
					for (auto& v:_visualisationOverlays) v->Set(std::shared_ptr<SceneEngine::IScene>{});
					_pendingSceneActualize = false;
				}
				
				_sceneReportedError = {};
				UpdateVisualizationError();
			}

			if (_lightingStateBindType == LightingStateBindType::Filename && _lightingStateMarker && ::Assets::IsInvalidated(*_lightingStateMarker)) {
				// lighting state hot reload
				if (_sceneOverlay) _sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});

				_lightingStateMarker = SceneEngine::CreateBasicLightingStateDelegate(_lightingStateFilename);
				_pendingLightingStateActualize = true;
				_lightingStateReportedError = {};
				UpdateVisualizationError();
			}
		}
	}

	void VisOverlayController::Pimpl::UpdateVisualizationError()
	{
		if (_sceneOverlay) {
			if (!_sceneReportedError.empty() && !_lightingStateReportedError.empty()) {
				_sceneOverlay->ReportError(_lightingStateReportedError + "\n" + _sceneReportedError);
			} else if (!_sceneReportedError.empty()) {
				_sceneOverlay->ReportError(_sceneReportedError);
			} else
				_sceneOverlay->ReportError(_lightingStateReportedError);	// might be empty
		}
	}

	void VisOverlayController::SetScene(const ModelVisSettings& visSettings)
	{
		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});
		for (auto& v:_pimpl->_visualisationOverlays) v->Set(std::shared_ptr<SceneEngine::IScene>{});

		_pimpl->_scene = nullptr;
		_pimpl->_sceneMarker = nullptr;
		_pimpl->_sceneBindType = Pimpl::SceneBindType::ModelVisSettings;
		_pimpl->_modelVisSettings = visSettings;
		if (!visSettings._modelName.empty()) {
			_pimpl->_sceneMarker = ModelVisUtility{_pimpl->_drawablesPool, _pimpl->_pipelineAcceleratorPool, _pimpl->_deformAcceleratorPool, _pimpl->_loadingContext}
				.MakeScene(visSettings);
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->ShowLoadingIndicator();
			_pimpl->_pendingSceneActualize = true;
		} else {
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->SetEmptyScene();
			_pimpl->_pendingSceneActualize = false;
		}
		_pimpl->_sceneReportedError = {};
		_pimpl->UpdateVisualizationError();
	}

	void VisOverlayController::SetScene(const MaterialVisSettings& visSettings, std::shared_ptr<RenderCore::Assets::RawMaterial> material)
	{
		_pimpl->_scene = nullptr;
		_pimpl->_sceneMarker = nullptr;
		_pimpl->_sceneBindType = Pimpl::SceneBindType::MaterialVisSettings;
		_pimpl->_materialVisSettings = visSettings;
		_pimpl->_scene = MakeScene(
			_pimpl->_drawablesPool, _pimpl->_pipelineAcceleratorPool,
			visSettings, material);
		_pimpl->_pendingSceneActualize = false;
		_pimpl->_sceneReportedError = {};
		_pimpl->UpdateVisualizationError();

		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(_pimpl->_scene);
		for (auto& v:_pimpl->_visualisationOverlays) v->Set(_pimpl->_scene);
	}

	void VisOverlayController::SetScene(std::shared_ptr<SceneEngine::IScene> scene)
	{
		_pimpl->_scene = std::move(scene);
		_pimpl->_sceneMarker = nullptr;
		_pimpl->_sceneBindType = Pimpl::SceneBindType::Ptr;
		_pimpl->_pendingSceneActualize = false;
		_pimpl->_sceneReportedError = {};
		_pimpl->UpdateVisualizationError();

		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(_pimpl->_scene, _pimpl->_loadingContext);
		for (auto& v:_pimpl->_visualisationOverlays) v->Set(_pimpl->_scene);
	}

	void VisOverlayController::SetScene(Assets::PtrToMarkerPtr<SceneEngine::IScene> marker)
	{
		assert(marker);

		_pimpl->_scene = nullptr;
		_pimpl->_sceneMarker = std::move(marker);
		_pimpl->_sceneBindType = Pimpl::SceneBindType::Marker;
		auto* actual = _pimpl->_sceneMarker->TryActualize();
		if (actual) {
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(*actual, _pimpl->_loadingContext);
			for (auto& v:_pimpl->_visualisationOverlays) v->Set(*actual);
			_pimpl->_pendingSceneActualize = false;
		} else {
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->ShowLoadingIndicator();
			for (auto& v:_pimpl->_visualisationOverlays) v->Set(std::shared_ptr<SceneEngine::IScene>{});
			_pimpl->_pendingSceneActualize = true;
		}
		_pimpl->_sceneReportedError = {};
		_pimpl->UpdateVisualizationError();
	}

	void VisOverlayController::SetEnvSettings(StringSection<> envSettings)
	{
		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});

		_pimpl->_lightingState = nullptr;
		_pimpl->_lightingStateMarker = nullptr;
		_pimpl->_lightingStateBindType = Pimpl::LightingStateBindType::Filename;
		_pimpl->_lightingStateFilename = envSettings.AsString();
		_pimpl->_lightingStateMarker = SceneEngine::CreateBasicLightingStateDelegate(envSettings);
		_pimpl->_pendingLightingStateActualize = true;
		_pimpl->_lightingStateReportedError = {};
		_pimpl->UpdateVisualizationError();
	}

	void VisOverlayController::SetEnvSettings(::Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> marker)
	{
		assert(marker);

		_pimpl->_lightingState = nullptr;
		_pimpl->_lightingStateMarker = std::move(marker);
		_pimpl->_lightingStateBindType = Pimpl::LightingStateBindType::Marker;

		if (auto* actualized = _pimpl->_lightingStateMarker->TryActualize()) {
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(*actualized);
			_pimpl->_pendingLightingStateActualize = false;
		} else {
			if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});
			_pimpl->_pendingLightingStateActualize = true;
		}
		_pimpl->_lightingStateReportedError = {};
		_pimpl->UpdateVisualizationError();
	}

	void VisOverlayController::SetEnvSettings(std::shared_ptr<SceneEngine::ILightingStateDelegate> lightingState)
	{
		_pimpl->_lightingState = std::move(lightingState);
		_pimpl->_lightingStateMarker = nullptr;
		_pimpl->_lightingStateBindType = Pimpl::LightingStateBindType::Ptr;
		_pimpl->_pendingLightingStateActualize = false;
		_pimpl->_lightingStateReportedError = {};
		_pimpl->UpdateVisualizationError();

		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(_pimpl->_lightingState);
	}

	void VisOverlayController::SetCamera(std::shared_ptr<VisCameraSettings> camera, bool resetCamera)
	{
		if (_pimpl->_sceneOverlay) _pimpl->_sceneOverlay->Set(camera, resetCamera);
		for (auto& v:_pimpl->_visualisationOverlays) v->Set(camera, resetCamera);
	}

	void VisOverlayController::AttachSceneOverlay(std::shared_ptr<ISimpleSceneOverlay> sceneOverlay)
	{
		if (_pimpl->_sceneOverlay && _pimpl->_sceneOverlay != sceneOverlay) {
			_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});
			_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});
		}

		_pimpl->_sceneOverlay = std::move(sceneOverlay);

		// set current scene state
		if (_pimpl->_scene) {
			_pimpl->_sceneOverlay->Set(_pimpl->_scene, _pimpl->_loadingContext);
		} else if (_pimpl->_sceneMarker) {
			if (auto* actual = _pimpl->_sceneMarker->TryActualize()) {
				_pimpl->_sceneOverlay->Set(*actual, _pimpl->_loadingContext);
			} else
				_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});
		} else
			_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::IScene>{});

		// set current lighting state
		if (_pimpl->_lightingState) {
			_pimpl->_sceneOverlay->Set(_pimpl->_lightingState);
		} else if (_pimpl->_lightingStateMarker) {
			if (auto* actual = _pimpl->_lightingStateMarker->TryActualize()) {
				_pimpl->_sceneOverlay->Set(*actual);
			} else
				_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});
		} else
			_pimpl->_sceneOverlay->Set(std::shared_ptr<SceneEngine::ILightingStateDelegate>{});
	}

	void VisOverlayController::AttachVisualisationOverlay(std::shared_ptr<IVisualisationOverlay> visualisationOverlay)
	{
		for (auto& v:_pimpl->_visualisationOverlays)
			if (v == visualisationOverlay)
				return;

		auto* v = visualisationOverlay.get();
		_pimpl->_visualisationOverlays.push_back(std::move(visualisationOverlay));

		// set current scene state
		if (_pimpl->_scene) {
			v->Set(_pimpl->_scene);
		} else if (_pimpl->_sceneMarker) {
			if (auto* actual = _pimpl->_sceneMarker->TryActualize()) {
				v->Set(*actual);
			} else
				v->Set(std::shared_ptr<SceneEngine::IScene>{});
		} else
			v->Set(std::shared_ptr<SceneEngine::IScene>{});

		_pimpl->UpdateVisualizationError();
	}

	SceneEngine::IScene* VisOverlayController::TryGetScene()
	{
		if (_pimpl->_scene)
			return _pimpl->_scene.get();
		if (_pimpl->_sceneMarker) {
			auto* actual = _pimpl->_sceneMarker->TryActualize();
			if (actual)
				return actual->get();
		}
		return nullptr;
	}

	const std::shared_ptr<::Assets::OperationContext>& VisOverlayController::GetLoadingContext()
	{
		return _pimpl->_loadingContext;
	}

	VisOverlayController::VisOverlayController(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<::Assets::OperationContext> loadingContext)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_drawablesPool = std::move(drawablesPool);
		_pimpl->_pipelineAcceleratorPool = std::move(pipelineAcceleratorPool);
		_pimpl->_deformAcceleratorPool = std::move(deformAcceleratorPool);
		_pimpl->_loadingContext = std::move(loadingContext);

		_pimpl->_mainThreadTickFn = RenderCore::Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Bind(
			[this]() { this->_pimpl->MainThreadTick(); });
	}

	VisOverlayController::~VisOverlayController()
	{
		if (_pimpl->_mainThreadTickFn != ~0u)
			RenderCore::Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Unbind(_pimpl->_mainThreadTickFn);
	}

	VisOverlayController::IVisualisationOverlay::~IVisualisationOverlay() = default;

///////////////////////////////////////////////////////////////////////////////////////////////////

	void StallWhilePending(SceneEngine::IScene& scene)
	{
		auto* marker = dynamic_cast<::Assets::IAsyncMarker*>(&scene);
		if (marker)
			marker->StallWhilePending();
	}

    void ChangeEvent::Invoke() 
    {
        for (auto i=_callbacks.begin(); i!=_callbacks.end(); ++i) {
            (*i)->OnChange();
        }
    }
    ChangeEvent::~ChangeEvent() {}

	IVisContent::~IVisContent() {}

	const char* AsString(VisCameraSettings::Projection proj)
	{
		switch (proj) {
		case VisCameraSettings::Projection::Perspective: return "Perspective";
		case VisCameraSettings::Projection::Orthogonal: return "Orthogonal";
		}
		return nullptr;
	}

    std::optional<VisCameraSettings::Projection> AsProjection(StringSection<> str)
	{
		if (XlEqString(str, "Perspective")) return VisCameraSettings::Projection::Perspective;
		else if (XlEqString(str, "Orthogonal")) return VisCameraSettings::Projection::Orthogonal;
		return {};
	}

}

