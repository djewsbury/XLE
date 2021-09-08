// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VisualisationUtils.h"
#include "../../SceneEngine/RayVsModel.h"
#include "../../SceneEngine/IntersectionTest.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../SceneEngine/ExecuteScene.h"
#include "../../PlatformRig/OverlappedWindow.h"	// (for GetOSRunLoop())
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/HighlightEffects.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderOverlays/SimpleVisualization.h"
#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/BasicDelegates.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../RenderCore/IThreadContext.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/Assets.h"
#include "../../Math/Transformations.h"
#include "../../ConsoleRig/Console.h"
#include "../../OSServices/Log.h"
#include "../../Utility/FunctionUtils.h"
#include <iomanip>
#include <chrono>

#pragma warning(disable:4505) // unreferenced local function has been removed

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
        result._verticalFieldOfView = Deg2Rad(camSettings._verticalFieldOfView);
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

	void ConfigureParsingContext(RenderCore::Techniques::ParsingContext& parsingContext, const VisCameraSettings& cam)
	{
		UInt2 viewportDims { parsingContext.GetViewport()._width, parsingContext.GetViewport()._height };
		auto camDesc = ToolsRig::AsCameraDesc(cam);
        parsingContext.GetProjectionDesc() = RenderCore::Techniques::BuildProjectionDesc(camDesc, viewportDims);
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
        float verticalHalfDimension = .5f * box.second[2] - box.first[2];
        position[0] = box.first[0] - (verticalHalfDimension * (1.f + border)) / XlTan(.5f * Deg2Rad(verticalFieldOfView));

        VisCameraSettings result;
        result._position = position;
        result._focus = .5f * (box.first + box.second);
        result._verticalFieldOfView = verticalFieldOfView;
        result._farClip = 5.25f * Magnitude(result._focus - result._position);
        result._nearClip = result._farClip / 10000.f;

		assert(std::isfinite(result._position[0]) && !std::isnan(result._position[0]));
		assert(std::isfinite(result._position[1]) && !std::isnan(result._position[1]));
		assert(std::isfinite(result._position[2]) && !std::isnan(result._position[2]));

        return result;
    }

	VisEnvSettings::VisEnvSettings() : _envConfigFile("defaultenv.txt:environment"), _lightingType(LightingType::Deferred) {}
	VisEnvSettings::VisEnvSettings(const std::string& envConfigFile) : _envConfigFile(envConfigFile) {}

	Assets::PtrToFuturePtr<SceneEngine::ILightingStateDelegate> MakeLightingStateDelegate(const VisEnvSettings& visSettings)
	{
		auto result = ::Assets::MakeFuture<std::shared_ptr<SceneEngine::BasicLightingStateDelegate>>(visSettings._envConfigFile);
		return std::reinterpret_pointer_cast<Assets::FuturePtr<SceneEngine::ILightingStateDelegate>>(result);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleSceneLayer : public ISimpleSceneLayer
    {
    public:
        virtual void Render(
            RenderCore::IThreadContext& context,
            RenderCore::Techniques::ParsingContext& parserContext) override;

        void Set(Assets::PtrToFuturePtr<SceneEngine::ILightingStateDelegate> envSettings) override;
		void Set(Assets::PtrToFuturePtr<SceneEngine::IScene> scene) override;

		std::shared_ptr<VisCameraSettings> GetCamera() override;
		void ResetCamera() override;
		virtual OverlayState GetOverlayState() const override;

		virtual void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps) override;

        SimpleSceneLayer(
            const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
            const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus);
        ~SimpleSceneLayer();
    protected:
		std::shared_ptr<VisCameraSettings> _camera;

		class PreparedScene
		{
		public:
			std::shared_ptr<SceneEngine::IScene> _scene;
			std::shared_ptr<SceneEngine::ILightingStateDelegate> _envSettings;
			std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _compiledLightingTechnique;
			::Assets::DependencyValidation _depVal;
			bool _pendingCameraReset = false;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		};
		::Assets::PtrToFuturePtr<PreparedScene> _preparedSceneFuture;

		::Assets::PtrToFuturePtr<SceneEngine::IScene> _sceneFuture;
		::Assets::PtrToFuturePtr<SceneEngine::ILightingStateDelegate> _envSettingsFuture;
		void RebuildPreparedScene();
		
		unsigned _loadingIndicatorCounter = 0;

		uint64_t _lightingTechniqueTargetsHash = 0ull;
		std::vector<RenderCore::Techniques::PreregisteredAttachment> _lightingTechniqueTargets;
		RenderCore::FrameBufferProperties _lightingTechniqueFBProps;

		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;
		std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingApparatus;
    };

	static ::Assets::AssetState GetAsyncSceneState(SceneEngine::IScene& scene)
	{
		auto* asyncScene = dynamic_cast<::Assets::IAsyncMarker*>(&scene);
		if (asyncScene)
			return asyncScene->GetAssetState();
		return ::Assets::AssetState::Ready;
	}

	static void DrawDiamond(RenderOverlays::IOverlayContext* context, const RenderOverlays::DebuggingDisplay::Rect& rect, RenderOverlays::ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

		using namespace RenderOverlays;
		using namespace RenderOverlays::DebuggingDisplay;
        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._bottomRight[0],								0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour,
            AsPixelCoords(Coord2(0.5f * (rect._topLeft[0] + rect._bottomRight[0]),	rect._topLeft[1])), colour,
            AsPixelCoords(Coord2(rect._topLeft[0],									0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour);

        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],									0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour,
            AsPixelCoords(Coord2(0.5f * (rect._topLeft[0] + rect._bottomRight[0]),	rect._bottomRight[1])), colour,
            AsPixelCoords(Coord2(rect._bottomRight[0],								0.5f * (rect._topLeft[1] + rect._bottomRight[1]))), colour);
    }

	static void RenderLoadingIndicator(
		RenderOverlays::IOverlayContext& context,
		const RenderOverlays::DebuggingDisplay::Rect& viewport,
		unsigned animationCounter)
    {
        using namespace RenderOverlays::DebuggingDisplay;

		const unsigned indicatorWidth = 80;
		const unsigned indicatorHeight = 120;
		RenderOverlays::DebuggingDisplay::Rect outerRect;
		outerRect._topLeft[0] = std::max(viewport._topLeft[0]+12u, viewport._bottomRight[0]-indicatorWidth-12u);
		outerRect._topLeft[1] = std::max(viewport._topLeft[1]+12u, viewport._bottomRight[1]-indicatorHeight-12u);
		outerRect._bottomRight[0] = viewport._bottomRight[0]-12u;
		outerRect._bottomRight[1] = viewport._bottomRight[1]-12u;

		Float2 center {
			(outerRect._bottomRight[0] + outerRect._topLeft[0]) / 2.0f,
			(outerRect._bottomRight[1] + outerRect._topLeft[1]) / 2.0f };

		const unsigned cycleCount = 1080;
		// there are always 3 diamonds, distributed evenly throughout the animation....
		unsigned oldestIdx = (unsigned)std::ceil(animationCounter / float(cycleCount/3));
		int oldestStartPoint = -int(animationCounter % (cycleCount/3));
		float phase = -oldestStartPoint / float(cycleCount/3);
		for (unsigned c=0; c<3; ++c) {
			unsigned idx = oldestIdx+c;

			float a = (phase + (2-c)) / 3.0f;
			float a2 = std::fmodf(idx / 10.f, 1.0f);
			a2 = 0.5f + 0.5f * a2;

			Rect r;
			r._topLeft[0] = unsigned(center[0] - a * 0.5f * (outerRect._bottomRight[0] - outerRect._topLeft[0]));
			r._topLeft[1] = unsigned(center[1] - a * 0.5f * (outerRect._bottomRight[1] - outerRect._topLeft[1]));
			r._bottomRight[0] = unsigned(center[0] + a * 0.5f * (outerRect._bottomRight[0] - outerRect._topLeft[0]));
			r._bottomRight[1] = unsigned(center[1] + a * 0.5f * (outerRect._bottomRight[1] - outerRect._topLeft[1]));

			using namespace RenderOverlays::DebuggingDisplay;
			float fadeOff = std::min((1.0f - a) * 10.f, 1.0f);
			DrawDiamond(&context, r, RenderOverlays::ColorB { uint8_t(0xff * fadeOff * a2), uint8_t(0xff * fadeOff * a2), uint8_t(0xff * fadeOff * a2), 0xff });
		}
	}

    void SimpleSceneLayer::Render(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace SceneEngine;
		#if defined(_DEBUG)
			auto& stitchingContext = parserContext.GetFragmentStitchingContext();
			auto validationHash = RenderCore::Techniques::HashPreregisteredAttachments(stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
			assert(_lightingTechniqueTargetsHash == validationHash);		// If you get here, it means that this render target configuration doesn't match what was last used with OnRenderTargetUpdate()
		#endif

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

			// Have to do camera reset here after load to avoid therading issues
			if (actualizedScene->_pendingCameraReset) {
				ResetCamera();
				actualizedScene->_pendingCameraReset = false;
			}

			auto cam = AsCameraDesc(*_camera);
			SceneEngine::SceneView sceneView {
				SceneEngine::SceneView::Type::Normal,
				RenderCore::Techniques::BuildProjectionDesc(cam, {parserContext.GetViewport()._width, parserContext.GetViewport()._height})
			};
			
			parserContext.GetProjectionDesc() = sceneView._projection;
			{
				auto lightingIterator = SceneEngine::BeginLightingTechnique(
					threadContext, parserContext,
					*actualizedScene->_envSettings, *actualizedScene->_compiledLightingTechnique);

				for (;;) {
					auto next = lightingIterator.GetNextStep();
					if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
					if (next._type == RenderCore::LightingEngine::StepType::ParseScene) {
						assert(next._pkt);
						actualizedScene->_scene->ExecuteScene(threadContext, SceneEngine::ExecuteSceneContext{SceneEngine::SceneView{}, next._batch, next._pkt});
					}
				}

				auto& lightScene = RenderCore::LightingEngine::GetLightScene(*actualizedScene->_compiledLightingTechnique);
				lightScene.Clear();
			}

			// Draw debugging overlays -- 
			{
				// auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, renderTarget, parserContext);
				// SceneEngine::LightingParser_Overlays(threadContext, parserContext, lightingParserContext);
			}
		} else {
			// Draw a loading indicator, 
			using namespace RenderOverlays::DebuggingDisplay;
			RenderOverlays::ImmediateOverlayContext overlays(threadContext, *_immediateDrawables, _fontRenderingManager.get());
			overlays.CaptureState();
			auto viewportDims = Coord2(parserContext.GetViewport()._width, parserContext.GetViewport()._height);
			Rect rect { Coord2{0, 0}, viewportDims };
			RenderLoadingIndicator(overlays, rect, _loadingIndicatorCounter++);
			overlays.ReleaseState();

			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, RenderCore::LoadStore::Clear);
			_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi);

			StringMeldAppend(parserContext._stringHelpers->_pendingAssets, ArrayEnd(parserContext._stringHelpers->_pendingAssets)) << "Scene Layer\n";
		}

		/*if (!_envSettingsErrorMessage.empty()) {
			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext);
			if (!_envSettingsErrorMessage.empty()) {
				assert(0);
				// SceneEngine::DrawString(threadContext, RenderOverlays::GetDefaultFont(), _envSettingsErrorMessage);
			}
		}*/
    }

	void SimpleSceneLayer::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps)
	{
		assert(_pipelineAccelerators && _lightingApparatus);
		_lightingTechniqueTargetsHash = RenderCore::Techniques::HashPreregisteredAttachments(preregAttachments, fbProps);
		_lightingTechniqueTargets = {preregAttachments.begin(), preregAttachments.end()};
		_lightingTechniqueFBProps = fbProps;
		RebuildPreparedScene();
	}

	void SimpleSceneLayer::RebuildPreparedScene()
	{
		if (!_envSettingsFuture || _lightingTechniqueTargets.empty() || !_sceneFuture) {
			_preparedSceneFuture = nullptr;
			return;
		}

		//
		// envSettings -> compiledLightingTechnique -> preparedShaders -> PreparedScene
		//                SceneEngine::IScene ----------------^
		//
		_preparedSceneFuture = std::make_shared<::Assets::FuturePtr<PreparedScene>>("simple-scene-layer");

		::Assets::WhenAll(_envSettingsFuture).ThenConstructToFuture(
			*_preparedSceneFuture,
			[targets = _lightingTechniqueTargets, fbProps = _lightingTechniqueFBProps, lightingApparatus = _lightingApparatus, sceneFuture = _sceneFuture, pipelineAccelerators = _pipelineAccelerators](
				::Assets::FuturePtr<PreparedScene>& thatFuture, 
				std::shared_ptr<SceneEngine::ILightingStateDelegate> envSettings) {

				RenderCore::LightingEngine::AmbientLightOperatorDesc ambientLightOperatorDesc;
				auto compiledLightingTechniqueFuture = RenderCore::LightingEngine::CreateForwardLightingTechnique(
					lightingApparatus,
					envSettings->GetLightResolveOperators(),
					envSettings->GetShadowResolveOperators(),
					ambientLightOperatorDesc,
					targets, fbProps);

				::Assets::WhenAll(sceneFuture, compiledLightingTechniqueFuture).ThenConstructToFuture(
					thatFuture,
					[pipelineAccelerators, envSettings](Assets::FuturePtr<PreparedScene>& thatFuture, auto scene, auto compiledLightingTechnique) {
						auto preparedScene = std::make_shared<PreparedScene>();
						preparedScene->_envSettings = envSettings;
						preparedScene->_compiledLightingTechnique = std::move(compiledLightingTechnique);
						preparedScene->_pendingCameraReset = true;
						preparedScene->_scene = std::move(scene);
						preparedScene->_depVal = ::Assets::GetDepValSys().Make();
						preparedScene->_depVal.RegisterDependency(preparedScene->_envSettings->GetDependencyValidation());
						preparedScene->_depVal.RegisterDependency(RenderCore::LightingEngine::GetDependencyValidation(*preparedScene->_compiledLightingTechnique));

						auto pendingResources = SceneEngine::PrepareResources(
							*RenderCore::Techniques::GetThreadContext(),
							*preparedScene->_compiledLightingTechnique, *preparedScene->_scene);
						if (pendingResources) {
							thatFuture.SetPollingFunction(
								[pendingResources, preparedScene](::Assets::FuturePtr<PreparedScene>& thatFuture) {
									auto state = pendingResources->GetAssetState();
									if (state == ::Assets::AssetState::Pending) return true;
									if (state == ::Assets::AssetState::Invalid) {
										thatFuture.SetInvalidAsset({}, ::Assets::AsBlob("Invalid asset during prepare resources"));
									} else {
										thatFuture.SetAsset(std::shared_ptr<PreparedScene>{preparedScene}, {});
									}
									return false;
								});
						} else {
							thatFuture.SetAsset(std::shared_ptr<PreparedScene>{preparedScene}, {});
						}
						return false;
					});
			});

	}

    void SimpleSceneLayer::Set(Assets::PtrToFuturePtr<SceneEngine::ILightingStateDelegate> envSettings)
    {
		_envSettingsFuture = std::move(envSettings);
		RebuildPreparedScene();
    }

	void SimpleSceneLayer::Set(Assets::PtrToFuturePtr<SceneEngine::IScene> scene)
	{
		_sceneFuture = std::move(scene);
		RebuildPreparedScene();
	}

	std::shared_ptr<VisCameraSettings> SimpleSceneLayer::GetCamera()
	{
		return _camera;
	}

	void SimpleSceneLayer::ResetCamera()
	{
		auto* t = _preparedSceneFuture ? _preparedSceneFuture->TryActualize() : nullptr;
		if (!t) return;

		auto* visContentScene = dynamic_cast<ToolsRig::IVisContent*>((*t)->_scene.get());
		if (visContentScene) {
			auto boundingBox = visContentScene->GetBoundingBox();
			*_camera = ToolsRig::AlignCameraToBoundingBox(_camera->_verticalFieldOfView, boundingBox);
		}
	}

	auto SimpleSceneLayer::GetOverlayState() const -> OverlayState
	{
		RefreshMode refreshMode = RefreshMode::EventBased;

		if (_preparedSceneFuture && _preparedSceneFuture->GetAssetState() == ::Assets::AssetState::Pending)
			return { RefreshMode::RegularAnimation };

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
	
    SimpleSceneLayer::SimpleSceneLayer(
		const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
		const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus)
    {
		_camera = std::make_shared<VisCameraSettings>();
		_pipelineAccelerators = immediateDrawingApparatus->_mainDrawingApparatus->_pipelineAccelerators;
		_immediateDrawables = immediateDrawingApparatus->_immediateDrawables;
		_fontRenderingManager = immediateDrawingApparatus->_fontRenderingManager;
		_lightingApparatus = lightingEngineApparatus;
    }

    SimpleSceneLayer::~SimpleSceneLayer() {}

	std::shared_ptr<ISimpleSceneLayer> CreateSimpleSceneLayer(
        const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
        const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& lightingEngineApparatus)
	{
		return std::make_shared<SimpleSceneLayer>(immediateDrawingApparatus, lightingEngineApparatus);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
	static SceneEngine::LightingModel AsLightingModel(VisEnvSettings::LightingType lightingType)
	{
		switch (lightingType) {
		case VisEnvSettings::LightingType::Deferred:
			return SceneEngine::LightingModel::Deferred;
		case VisEnvSettings::LightingType::Forward:
			return SceneEngine::LightingModel::Forward;
		default:
		case VisEnvSettings::LightingType::Direct:
			return SceneEngine::LightingModel::Direct;
		}
	}
#endif

	std::pair<DrawPreviewResult, std::string> DrawPreview(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		VisCameraSettings& cameraSettings,
		VisEnvSettings& envSettings,
		SceneEngine::IScene& scene,
		const std::shared_ptr<SceneEngine::IRenderStep>& renderStep)
    {
		assert(0);	// update for LightingEngine
#if 0
		try
        {
			auto future = ::Assets::MakeAsset<SceneEngine::EnvironmentSettings>(envSettings._envConfigFile);
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
        std::shared_ptr<VisMouseOver> _mouseOver;
		std::shared_ptr<VisCameraSettings> _cameraSettings;
		std::shared_ptr<VisAnimationState> _animState;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;

		::Assets::PtrToFuturePtr<SceneEngine::IScene> _scene;
		bool _pendingAnimStateBind = false;

		std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> _stencilPrimeDelegate;

		Pimpl()
		{
			_stencilPrimeDelegate = std::make_shared<StencilRefDelegate>();
		}
    };

	static void RenderTrackingOverlay(
        RenderOverlays::IOverlayContext& context,
		const RenderOverlays::DebuggingDisplay::Rect& viewport,
		const ToolsRig::VisMouseOver& mouseOver, 
		const SceneEngine::IScene& scene)
    {
        using namespace RenderOverlays::DebuggingDisplay;

        auto textHeight = (int)RenderOverlays::GetDefaultFont()->GetFontProperties()._lineHeight;
        std::string matName;
		auto* visContent = dynamic_cast<const ToolsRig::IVisContent*>(&scene);
		if (visContent)
			matName = visContent->GetDrawCallDetails(mouseOver._drawCallIndex, mouseOver._materialGuid)._materialName;
        DrawText(
            &context,
            Rect(Coord2(viewport._topLeft[0]+3, viewport._bottomRight[1]-textHeight-8), Coord2(viewport._bottomRight[0]-6, viewport._bottomRight[1]-8)),
            nullptr, RenderOverlays::ColorB(0xffafafaf),
            StringMeld<512>() 
                << "Material: {Color:7f3faf}" << matName
                << "{Color:afafaf}, Draw call: " << mouseOver._drawCallIndex
                << std::setprecision(4)
                << ", (" << mouseOver._intersectionPt[0]
                << ", "  << mouseOver._intersectionPt[1]
                << ", "  << mouseOver._intersectionPt[2]
                << ")");
    }

    void VisualisationOverlay::Render(
        RenderCore::IThreadContext& threadContext, 
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        using namespace RenderCore;
		if (!parserContext.GetTechniqueContext()._attachmentPool->GetBoundResource(Techniques::AttachmentSemantics::MultisampleDepth))		// we need this attachment to continue
			return;

		if (!_pimpl->_scene || !_pimpl->_cameraSettings) return;
		auto scene = _pimpl->_scene->TryActualize();
		if (!scene) return;

		if (_pimpl->_pendingAnimStateBind) {
			auto* visContext = dynamic_cast<IVisContent*>(scene->get());
			if (visContext && _pimpl->_animState)
				visContext->BindAnimationState(_pimpl->_animState);
			_pimpl->_pendingAnimStateBind = false;
		}

		UInt2 viewportDims(parserContext.GetViewport()._width, parserContext.GetViewport()._height);
		assert(viewportDims[0] && viewportDims[1]);
		auto cam = AsCameraDesc(*_pimpl->_cameraSettings);
		SceneEngine::SceneView sceneView {
			SceneEngine::SceneView::Type::Normal,
			Techniques::BuildProjectionDesc(cam, viewportDims),
		};

		bool doColorByMaterial = 
			(_pimpl->_settings._colourByMaterial == 1)
			|| (_pimpl->_settings._colourByMaterial == 2 && _pimpl->_mouseOver->_hasMouseOver);

		if (_pimpl->_settings._drawWireframe || _pimpl->_settings._drawNormals || _pimpl->_settings._skeletonMode || doColorByMaterial) {
			
			Techniques::FrameBufferDescFragment fbDesc;
			SubpassDesc mainPass;
			mainPass.SetName("VisualisationOverlay");
			mainPass.AppendOutput(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR));
			mainPass.SetDepthStencil(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Retain_StencilClear));		// ensure stencil is cleared (but ok to keep depth)
			fbDesc.AddSubpass(std::move(mainPass));
			Techniques::RenderPassInstance rpi { threadContext, parserContext, fbDesc };

			static auto visWireframeDelegate =
				RenderCore::Techniques::CreateTechniqueDelegateLegacy(
					Techniques::TechniqueIndex::VisWireframe, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);
			static auto visNormals =
				RenderCore::Techniques::CreateTechniqueDelegateLegacy(
					Techniques::TechniqueIndex::VisNormals, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);

			DepthStencilDesc ds {
				RenderCore::CompareOp::GreaterEqual, true, true,
				0xff, 0xff,
				RenderCore::StencilDesc::AlwaysWrite,
				RenderCore::StencilDesc::NoEffect };
			static auto primeStencilBuffer =
				RenderCore::Techniques::CreateTechniqueDelegateLegacy(
					Techniques::TechniqueIndex::DepthOnly, {}, {}, ds);

			if (_pimpl->_settings._drawWireframe) {
				auto sequencerConfig = _pimpl->_pipelineAccelerators->CreateSequencerConfig(visWireframeDelegate, ParameterBox{}, rpi.GetFrameBufferDesc());
				SceneEngine::ExecuteSceneRaw(
					threadContext, parserContext, *_pimpl->_pipelineAccelerators,
					*sequencerConfig,
					sceneView, RenderCore::Techniques::BatchFilter::General,
					**scene);
			}

			if (_pimpl->_settings._drawNormals) {
				auto sequencerConfig = _pimpl->_pipelineAccelerators->CreateSequencerConfig(visNormals, ParameterBox{}, rpi.GetFrameBufferDesc());
				SceneEngine::ExecuteSceneRaw(
					threadContext, parserContext, *_pimpl->_pipelineAccelerators,
					*sequencerConfig,
					sceneView, RenderCore::Techniques::BatchFilter::General,
					**scene);
			}

			if (_pimpl->_settings._skeletonMode) {
				auto* visContent = dynamic_cast<IVisContent*>(scene->get());
				if (visContent) {
					CATCH_ASSETS_BEGIN
						rpi = {};		// awkwardly, we don't call RenderSkeleton during an rpi because it can render glyphs to a font texture
						RenderOverlays::ImmediateOverlayContext overlays(threadContext, *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
						visContent->RenderSkeleton(
							overlays, parserContext,
							_pimpl->_settings._skeletonMode == 2);
						rpi = Techniques::RenderPassInstance { threadContext, parserContext, fbDesc };
						_pimpl->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi);
					CATCH_ASSETS_END(parserContext)
				}
			}

			if (doColorByMaterial) {
				auto *visContent = dynamic_cast<IVisContent*>(scene->get());
				std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> oldDelegate;
				if (visContent)
					oldDelegate = visContent->SetCustomDrawDelegate(_pimpl->_stencilPrimeDelegate);
				// Prime the stencil buffer with draw call indices
				auto sequencerCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig(primeStencilBuffer, ParameterBox{}, rpi.GetFrameBufferDesc());
				SceneEngine::ExecuteSceneRaw(
					threadContext, parserContext, *_pimpl->_pipelineAccelerators,
					*sequencerCfg,
					sceneView, RenderCore::Techniques::BatchFilter::General,
					**scene);
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
                    threadContext, parserContext, 
                    settings, _pimpl->_settings._colourByMaterial==2);
            CATCH_ASSETS_END(parserContext)
        }

		bool writeMaterialName = 
			(_pimpl->_settings._colourByMaterial == 2 && _pimpl->_mouseOver->_hasMouseOver);

		if (writeMaterialName || _pimpl->_settings._drawBasisAxis || _pimpl->_settings._drawGrid) {

			CATCH_ASSETS_BEGIN

				using namespace RenderOverlays::DebuggingDisplay;
				RenderOverlays::ImmediateOverlayContext overlays(threadContext, *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
				overlays.CaptureState();
				Rect rect { Coord2{0, 0}, Coord2(viewportDims[0], viewportDims[1]) };
				RenderTrackingOverlay(overlays, rect, *_pimpl->_mouseOver, **scene);
				if (_pimpl->_settings._drawBasisAxis)
					RenderOverlays::DrawBasisAxes(overlays, parserContext);
				if (_pimpl->_settings._drawGrid)
					RenderOverlays::DrawGrid(overlays, parserContext, 100.f);
				overlays.ReleaseState();

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithDepthStencil(threadContext, parserContext);
				_pimpl->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi);

			CATCH_ASSETS_END(parserContext)
		}
    }

	void VisualisationOverlay::Set(Assets::PtrToFuturePtr<SceneEngine::IScene> scene)
	{
		_pimpl->_scene = scene;
		_pimpl->_pendingAnimStateBind = true;
	}

	void VisualisationOverlay::Set(const std::shared_ptr<VisCameraSettings>& camera)
	{
		_pimpl->_cameraSettings = camera;
	}

	void VisualisationOverlay::Set(const VisOverlaySettings& overlaySettings)
	{
		_pimpl->_settings = overlaySettings;
	}

	const VisOverlaySettings& VisualisationOverlay::GetOverlaySettings() const
	{
		return _pimpl->_settings;
	}

	void VisualisationOverlay::Set(const std::shared_ptr<VisAnimationState>& animState)
	{
		_pimpl->_animState = animState;
		_pimpl->_pendingAnimStateBind = true;
	}

    auto VisualisationOverlay::GetOverlayState() const -> OverlayState
	{
		RefreshMode refreshMode = RefreshMode::EventBased;

		// Need regular updates if the scene future hasn't been fully loaded yet
		// Or if there's active animation playing in the scene
		if (_pimpl->_scene) {
			if (_pimpl->_scene->GetAssetState() == ::Assets::AssetState::Pending) { 	
				refreshMode = RefreshMode::RegularAnimation;
			} else {
				auto* actual = _pimpl->_scene->TryActualize();
				auto* visContext = dynamic_cast<IVisContent*>(actual->get());
				if (visContext && visContext->HasActiveAnimation())
					refreshMode = RefreshMode::RegularAnimation;
			}
		}
		
		return { refreshMode };
	}

    VisualisationOverlay::VisualisationOverlay(
		const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
		const VisOverlaySettings& overlaySettings,
        std::shared_ptr<VisMouseOver> mouseOver)
    {
        _pimpl = std::make_unique<Pimpl>();
		_pimpl->_pipelineAccelerators = immediateDrawingApparatus->_mainDrawingApparatus->_pipelineAccelerators;
		_pimpl->_immediateDrawables = immediateDrawingApparatus->_immediateDrawables;
		_pimpl->_fontRenderingManager = immediateDrawingApparatus->_fontRenderingManager;
        _pimpl->_settings = overlaySettings;
        _pimpl->_mouseOver = std::move(mouseOver);
    }

    VisualisationOverlay::~VisualisationOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static RenderCore::Techniques::TechniqueContext MakeTechniqueContext(RenderCore::Techniques::DrawingApparatus& drawingApparatus)
    {
        RenderCore::Techniques::TechniqueContext techniqueContext;
        techniqueContext._systemUniformsDelegate = drawingApparatus._systemUniformsDelegate;
		techniqueContext._commonResources = drawingApparatus._commonResources;
		techniqueContext._sequencerDescSetLayout = drawingApparatus._sequencerDescSetLayout;
        return techniqueContext;
    }

	static SceneEngine::IntersectionTestResult FirstRayIntersection(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::DrawingApparatus& drawingApparatus,
        std::pair<Float3, Float3> worldSpaceRay,
		SceneEngine::IScene& scene)
	{
		using namespace RenderCore;

		auto techniqueContext = MakeTechniqueContext(drawingApparatus);
		Techniques::ParsingContext parserContext { techniqueContext };

		RenderCore::Techniques::DrawablesPacket pkt;
        scene.ExecuteScene(threadContext, SceneEngine::ExecuteSceneContext{{SceneEngine::SceneView::Type::Other}, RenderCore::Techniques::BatchFilter::General, &pkt});
		
		SceneEngine::ModelIntersectionStateContext stateContext {
            SceneEngine::ModelIntersectionStateContext::RayTest,
            threadContext, *drawingApparatus._pipelineAccelerators };
        stateContext.SetRay(worldSpaceRay);
		stateContext.ExecuteDrawables(parserContext, pkt);

		// Stall if we haven't yet submitted required buffer uploads command lists
		// (if we bail here, the draw commands are have still be submitted and we will run into ordering problems later)
		auto requiredBufferUploads = parserContext._requiredBufferUploadsCommandList;
		if (requiredBufferUploads) {
			auto& bu=RenderCore::Techniques::Services::GetBufferUploads();
			bu.StallUntilCompletion(threadContext, parserContext._requiredBufferUploadsCommandList);
		}
		
        auto results = stateContext.GetResults();
        if (!results.empty()) {
            const auto& r = results[0];

            SceneEngine::IntersectionTestResult result;
            result._type = SceneEngine::IntersectionTestResult::Type::Extra;
            result._worldSpaceCollision = 
                worldSpaceRay.first + r._intersectionDepth * Normalize(worldSpaceRay.second - worldSpaceRay.first);
            result._distance = r._intersectionDepth;
            result._drawCallIndex = r._drawCallIndex;
            result._materialGuid = r._materialGuid;

			result._modelName = "Model";
			result._materialName = "Material";
			auto* visContent = dynamic_cast<IVisContent*>(&scene);
			if (visContent) {
				auto details = visContent->GetDrawCallDetails(result._drawCallIndex, result._materialGuid);
				result._modelName = details._modelName;
				result._materialName = details._materialName;
			}

            return result;
        }

		return {};
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class MouseOverTrackingListener : public PlatformRig::IInputListener, public std::enable_shared_from_this<MouseOverTrackingListener>
    {
    public:
        bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const PlatformRig::InputSnapshot& evnt)
        {
			if (evnt._mouseDelta == PlatformRig::Coord2 { 0, 0 })
				return false;

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
			_timeoutContext = context;
			_timeoutMousePosition = evnt._mousePosition;
			if ((time - _timeOfLastCalculate) < timePeriod) {
				auto* osRunLoop = PlatformRig::GetOSRunLoop();
				if (_timeoutEvent == ~0u && osRunLoop) {
					std::weak_ptr<MouseOverTrackingListener> weakThis = weak_from_this();
					_timeoutEvent = osRunLoop->ScheduleTimeoutEvent(
						time + timePeriod,
						[weakThis]() {
							auto l = weakThis.lock();
							if (l) {
								l->_timeOfLastCalculate = std::chrono::steady_clock::now();
								l->CalculateForMousePosition(
									l->_timeoutContext,
									l->_timeoutMousePosition);
								l->_timeoutEvent = ~0u;								
							}
						});
				}
			} else {
				auto* osRunLoop = PlatformRig::GetOSRunLoop();
				if (_timeoutEvent != ~0u && osRunLoop) {
					osRunLoop->RemoveEvent(_timeoutEvent);
					_timeoutEvent = ~0u;
				}

				CalculateForMousePosition(context, evnt._mousePosition);
				_timeOfLastCalculate = time;
			}

			return false;
		}

		void CalculateForMousePosition(
			const PlatformRig::InputContext& context,
			PlatformRig::Coord2 mousePosition)
		{
            auto worldSpaceRay = SceneEngine::IntersectionTestContext::CalculateWorldSpaceRay(
				AsCameraDesc(*_camera), mousePosition, context._viewMins, context._viewMaxs);

            if (!_scene) return;

			auto sceneActual = _scene->TryActualize();
			if (!sceneActual) return;

			auto intr = FirstRayIntersection(*RenderCore::Techniques::GetThreadContext(), *_drawingApparatus, worldSpaceRay, **sceneActual);
			if (intr._type != 0) {
				if (        intr._drawCallIndex != _mouseOver->_drawCallIndex
						||  intr._materialGuid != _mouseOver->_materialGuid
						||  !_mouseOver->_hasMouseOver) {

					_mouseOver->_hasMouseOver = true;
					_mouseOver->_drawCallIndex = intr._drawCallIndex;
					_mouseOver->_materialGuid = intr._materialGuid;
					_mouseOver->_changeEvent.Invoke();
				}
			} else {
				if (_mouseOver->_hasMouseOver) {
					_mouseOver->_hasMouseOver = false;
					_mouseOver->_changeEvent.Invoke();
				}
			}
        }

		void Set(Assets::PtrToFuturePtr<SceneEngine::IScene> scene) { _scene = std::move(scene); }

        MouseOverTrackingListener(
            const std::shared_ptr<VisMouseOver>& mouseOver,
            const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus,
            const std::shared_ptr<VisCameraSettings>& camera)
        : _mouseOver(mouseOver)
        , _drawingApparatus(drawingApparatus)
        , _camera(camera)
        {}
        ~MouseOverTrackingListener() {}

    protected:
        std::shared_ptr<VisMouseOver> _mouseOver;
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
        std::shared_ptr<VisCameraSettings> _camera;
        
		Assets::PtrToFuturePtr<SceneEngine::IScene> _scene;
		std::chrono::time_point<std::chrono::steady_clock> _timeOfLastCalculate;

		PlatformRig::InputContext _timeoutContext;
		PlatformRig::Coord2 _timeoutMousePosition;
		unsigned _timeoutEvent = ~0u;
    };

    auto MouseOverTrackingOverlay::GetInputListener() -> std::shared_ptr<PlatformRig::IInputListener>
    {
        return std::static_pointer_cast<PlatformRig::IInputListener>(_inputListener);
    }

    void MouseOverTrackingOverlay::Render(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
		const bool dummyCalculation = false;
		if (dummyCalculation) {
			PlatformRig::InputContext inputContext { {0, 0}, {256, 256} };
			PlatformRig::Coord2 mousePosition {128, 128};
			_inputListener->CalculateForMousePosition(inputContext, mousePosition);
		}
    }

	void MouseOverTrackingOverlay::Set(Assets::PtrToFuturePtr<SceneEngine::IScene> scene)
	{
		_inputListener->Set(std::move(scene));
	}

    MouseOverTrackingOverlay::MouseOverTrackingOverlay(
        const std::shared_ptr<VisMouseOver>& mouseOver,
        const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus,
        const std::shared_ptr<VisCameraSettings>& camera)
    {
        _mouseOver = mouseOver;
        _inputListener = std::make_shared<MouseOverTrackingListener>(
            mouseOver,
            drawingApparatus, 
            camera);
    }

    MouseOverTrackingOverlay::~MouseOverTrackingOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class InputLayer : public PlatformRig::IOverlaySystem
    {
    public:
        std::shared_ptr<PlatformRig::IInputListener> GetInputListener() override;

        void Render(
            RenderCore::IThreadContext& context,
            RenderCore::Techniques::ParsingContext& parserContext) override;

        InputLayer(std::shared_ptr<PlatformRig::IInputListener> listener);
        ~InputLayer();
    protected:
        std::shared_ptr<PlatformRig::IInputListener> _listener;
    };

    auto InputLayer::GetInputListener() -> std::shared_ptr<PlatformRig::IInputListener>
    {
        return _listener;
    }

    void InputLayer::Render(
        RenderCore::IThreadContext&,
		RenderCore::Techniques::ParsingContext&) {}

    InputLayer::InputLayer(std::shared_ptr<PlatformRig::IInputListener> listener) : _listener(listener) {}
    InputLayer::~InputLayer() {}

	std::shared_ptr<PlatformRig::IOverlaySystem> MakeLayerForInput(const std::shared_ptr<PlatformRig::IInputListener>& listener)
	{
		return std::make_shared<InputLayer>(listener);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void StallWhilePending(SceneEngine::IScene& scene)
	{
		auto* marker = dynamic_cast<::Assets::IAsyncMarker*>(&scene);
		if (marker)
			marker->StallWhilePending();
	}
	
	/*const std::shared_ptr<SceneEngine::IScene>& TryActualize(const ::Assets::FuturePtr<SceneEngine::IScene>& future)
	{
		// This function exists because we can't call TryActualize() from a C++/CLR source file because
		// of the problem related to including <mutex>
		return future.TryActualize();
	}

	std::optional<std::string> GetActualizationError(const ::Assets::FuturePtr<SceneEngine::IScene>& future)
	{
		auto state = future.GetAssetState();
		if (state != ::Assets::AssetState::Invalid)
			return {};
		return ::Assets::AsString(future.GetActualizationLog());
	}*/

    void ChangeEvent::Invoke() 
    {
        for (auto i=_callbacks.begin(); i!=_callbacks.end(); ++i) {
            (*i)->OnChange();
        }
    }
    ChangeEvent::~ChangeEvent() {}

	IVisContent::~IVisContent() {}

}

