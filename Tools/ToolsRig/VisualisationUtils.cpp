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

	Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> MakeLightingStateDelegate(StringSection<> cfgSource)
	{
		return SceneEngine::CreateBasicLightingStateDelegate(cfgSource);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleSceneLayer : public ISimpleSceneLayer
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        void Set(Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> envSettings) override;
		void Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene) override;
		void Set(RefreshableFuture<SceneEngine::ILightingStateDelegate> envSettings) override;
		void Set(RefreshableFuture<SceneEngine::IScene> scene) override;

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

		::Assets::PtrToMarkerPtr<SceneEngine::IScene> _sceneFuture;
		::Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> _envSettingsFuture;
		RefreshableFuture<SceneEngine::IScene> _refreshableSceneFuture;
		RefreshableFuture<SceneEngine::ILightingStateDelegate> _refreshableEnvSettingsFuture;
		void RebuildPreparedScene();
		
		unsigned _loadingIndicatorCounter = 0;
		bool _pendingCameraReset = true;

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

    void SimpleSceneLayer::Render(
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
			if (_pendingCameraReset) {
				ResetCamera();
				_pendingCameraReset = false;
			}

			auto cam = AsCameraDesc(*_camera);
			SceneEngine::SceneView sceneView {
				SceneEngine::SceneView::Type::Normal,
				RenderCore::Techniques::BuildProjectionDesc(cam, {parserContext.GetViewport()._width, parserContext.GetViewport()._height})
			};
			
			parserContext.GetProjectionDesc() = sceneView._projection;
			{
				auto& lightScene = RenderCore::LightingEngine::GetLightScene(*actualizedScene->_compiledLightingTechnique);
				actualizedScene->_envSettings->PreRender(parserContext.GetProjectionDesc(), lightScene);

				TRY {
					RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator { 
						parserContext, *actualizedScene->_compiledLightingTechnique };

					for (;;) {
						auto next = lightingIterator.GetNextStep();
						if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
						if (next._type == RenderCore::LightingEngine::StepType::ParseScene) {
							assert(next._pkt);
							actualizedScene->_scene->ExecuteScene(parserContext.GetThreadContext(), SceneEngine::ExecuteSceneContext{SceneEngine::SceneView{}, next._batch, next._pkt});
						}
					}
				} CATCH(...) {
					actualizedScene->_envSettings->PostRender(lightScene);
					throw;
				} CATCH_END

				actualizedScene->_envSettings->PostRender(lightScene);
			}

			// Draw debugging overlays -- 
			{
				// auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, renderTarget, parserContext);
				// SceneEngine::LightingParser_Overlays(threadContext, parserContext, lightingParserContext);
			}
		} else {
			// Draw a loading indicator, 
			using namespace RenderOverlays::DebuggingDisplay;
			using namespace RenderOverlays;
			RenderOverlays::ImmediateOverlayContext overlays(parserContext.GetThreadContext(), *_immediateDrawables, _fontRenderingManager.get());
			overlays.CaptureState();
			auto viewportDims = Coord2(parserContext.GetViewport()._width, parserContext.GetViewport()._height);
			Rect rect { Coord2{0, 0}, viewportDims };
			RenderLoadingIndicator(overlays, rect, _loadingIndicatorCounter++);

			if (_preparedSceneFuture && _preparedSceneFuture->GetAssetState() == ::Assets::AssetState::Invalid) {
				auto log = ::Assets::AsString(_preparedSceneFuture->GetActualizationLog());
				auto font = RenderOverlays::MakeFont("DosisBook", 26)->TryActualize();
				if (font) {
					overlays.DrawText(
						std::make_tuple(Float3{0.f, 0.f, 0.f}, Float3{viewportDims[0], viewportDims[1], 0.f}),
						**font, 0, 0xffffffff, RenderOverlays::TextAlignment::Center, log);
				}
			}
			overlays.ReleaseState();

			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(parserContext, RenderCore::LoadStore::Clear);
			_immediateDrawables->ExecuteDraws(parserContext, rpi);

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
		if ((!_envSettingsFuture || ::Assets::IsInvalidated(*_envSettingsFuture)) && _refreshableEnvSettingsFuture)
			_envSettingsFuture = _refreshableEnvSettingsFuture();

		if ((!_sceneFuture || ::Assets::IsInvalidated(*_sceneFuture)) && _refreshableSceneFuture)
			_sceneFuture = _refreshableSceneFuture();

		if (!_envSettingsFuture || _lightingTechniqueTargets.empty() || !_sceneFuture) {
			_preparedSceneFuture = nullptr;
			return;
		}

		// If there's a previous construction operation still running, we have to stall for it to complete
		// Since we can share the env settings, we don't want to have to PreparedScene constructions in flight
		// at the same time
		if (_preparedSceneFuture)
			_preparedSceneFuture->StallWhilePending();

		//
		// envSettings -> compiledLightingTechnique -> preparedShaders -> PreparedScene
		//                SceneEngine::IScene ----------------^
		//
		_preparedSceneFuture = std::make_shared<::Assets::MarkerPtr<PreparedScene>>("simple-scene-layer");

		::Assets::WhenAll(_envSettingsFuture).ThenConstructToPromise(
			_preparedSceneFuture->AdoptPromise(),
			[	targets = _lightingTechniqueTargets, fbProps = _lightingTechniqueFBProps, lightingApparatus = _lightingApparatus, 
				sceneFuture = _sceneFuture, pipelineAccelerators = _pipelineAccelerators,
				sceneIsRefreshable = !!_refreshableSceneFuture, envSettingsIsRefreshable = !!_refreshableEnvSettingsFuture](
				std::promise<std::shared_ptr<PreparedScene>>&& thatPromise, 
				std::shared_ptr<SceneEngine::ILightingStateDelegate> envSettings) {

				TRY {
					RenderCore::LightingEngine::AmbientLightOperatorDesc ambientLightOperatorDesc;
					ambientLightOperatorDesc._ssrOperator = RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc{};
					auto operators = envSettings->GetOperators();
					::Assets::PtrToMarkerPtr<RenderCore::LightingEngine::CompiledLightingTechnique> compiledLightingTechniqueFuture;
					const bool forwardLighting = false;
					if (forwardLighting) {
						compiledLightingTechniqueFuture = RenderCore::LightingEngine::CreateForwardLightingTechnique(
							lightingApparatus,
							operators._lightResolveOperators,
							operators._shadowResolveOperators,
							ambientLightOperatorDesc,
							targets, fbProps);
					} else {
						compiledLightingTechniqueFuture = RenderCore::LightingEngine::CreateDeferredLightingTechnique(
							lightingApparatus,
							operators._lightResolveOperators,
							operators._shadowResolveOperators,
							targets, fbProps);
					}

					::Assets::WhenAll(sceneFuture, compiledLightingTechniqueFuture).ThenConstructToPromise(
						std::move(thatPromise),
						[pipelineAccelerators, envSettings, sceneIsRefreshable, envSettingsIsRefreshable, sceneDepVal=sceneFuture->GetDependencyValidation()](std::promise<std::shared_ptr<PreparedScene>>&& thatPromise, auto scene, auto compiledLightingTechnique) {
							
							TRY {
								auto preparedScene = std::make_shared<PreparedScene>();
								preparedScene->_envSettings = envSettings;
								preparedScene->_compiledLightingTechnique = std::move(compiledLightingTechnique);
								preparedScene->_scene = std::move(scene);

								if (sceneIsRefreshable || envSettingsIsRefreshable) {
									preparedScene->_depVal = ::Assets::GetDepValSys().Make();
									if (envSettingsIsRefreshable)
										preparedScene->_depVal.RegisterDependency(preparedScene->_envSettings->GetDependencyValidation());
									if (sceneIsRefreshable)
										preparedScene->_depVal.RegisterDependency(sceneDepVal);
									preparedScene->_depVal.RegisterDependency(RenderCore::LightingEngine::GetDependencyValidation(*preparedScene->_compiledLightingTechnique));
								} else {
									preparedScene->_depVal = RenderCore::LightingEngine::GetDependencyValidation(*preparedScene->_compiledLightingTechnique);
								}

								auto& lightScene = RenderCore::LightingEngine::GetLightScene(*preparedScene->_compiledLightingTechnique);
								preparedScene->_envSettings->BindScene(lightScene);

								auto pendingResources = SceneEngine::PrepareResources(
									*RenderCore::Techniques::GetThreadContext(),
									*preparedScene->_compiledLightingTechnique, *preparedScene->_scene);
								if (pendingResources) {
									::Assets::WhenAll(::Assets::MakeASyncMarkerBridge(pendingResources)).Then(
										[pendingResources, preparedScene, thatPromise=std::move(thatPromise)](auto) mutable {
											auto state = pendingResources->GetAssetState();
											assert(state != ::Assets::AssetState::Pending);
											if (state == ::Assets::AssetState::Invalid) {
												// We should still actually set the asset here. Some resources might be valid, and might still render
												// Also, if we don't set the asset, there will be no dependency validation chain for hot reloading a fix
												Log(Warning) << "Got invalid asset during PrepareResources for scene." << std::endl;
											}

											thatPromise.set_value(std::move(preparedScene));
										});
								} else {
									thatPromise.set_value(std::move(preparedScene));
								}
							} CATCH(...) {
								thatPromise.set_exception(std::current_exception());
							} CATCH_END
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});

	}

    void SimpleSceneLayer::Set(Assets::PtrToMarkerPtr<SceneEngine::ILightingStateDelegate> envSettings)
    {
		_envSettingsFuture = std::move(envSettings);
		_refreshableEnvSettingsFuture = nullptr;
		RebuildPreparedScene();
    }

	void SimpleSceneLayer::Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene)
	{
		_sceneFuture = std::move(scene);
		_refreshableSceneFuture = nullptr;
		_pendingCameraReset = true;
		RebuildPreparedScene();
	}

	void SimpleSceneLayer::Set(RefreshableFuture<SceneEngine::ILightingStateDelegate> envSettings)
	{
		_refreshableEnvSettingsFuture = std::move(envSettings);
		_envSettingsFuture = nullptr;
		RebuildPreparedScene();
	}

	void SimpleSceneLayer::Set(RefreshableFuture<SceneEngine::IScene> scene)
	{
		_refreshableSceneFuture = std::move(scene);
		_sceneFuture = nullptr;
		_pendingCameraReset = true;
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
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAccelerators;
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> _immediateDrawables;
		std::shared_ptr<RenderOverlays::FontRenderingManager> _fontRenderingManager;

		::Assets::PtrToMarkerPtr<SceneEngine::IScene> _scene;
		bool _pendingAnimStateBind = false;

		std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> _stencilPrimeDelegate;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _visWireframeDelegate;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _visNormalsDelegate;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _primeStencilBufferDelegate;

		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _visWireframeCfg;
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _visNormalsCfg;
		std::shared_ptr<RenderCore::Techniques::SequencerConfig> _primeStencilCfg;

		Pimpl()
		{
			_stencilPrimeDelegate = std::make_shared<StencilRefDelegate>();
		}
    };

	static void RenderTrackingOverlay(
        RenderOverlays::IOverlayContext& context,
		const RenderOverlays::Rect& viewport,
		const ToolsRig::VisMouseOver& mouseOver, 
		const SceneEngine::IScene& scene)
    {
        using namespace RenderOverlays::DebuggingDisplay;
		using namespace RenderOverlays;

        const auto textHeight = 20u;
        std::string matName;
		auto* visContent = dynamic_cast<const ToolsRig::IVisContent*>(&scene);
		if (visContent)
			matName = visContent->GetDrawCallDetails(mouseOver._drawCallIndex, mouseOver._materialGuid)._materialName;
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
		mainPass.SetDepthStencil(fbDesc.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).InitialState(LoadStore::Retain_StencilClear, 0));		// ensure stencil is cleared (but ok to keep depth)
		fbDesc.AddSubpass(std::move(mainPass));
		return fbDesc;
	}

    void VisualisationOverlay::Render(
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

			bool drawImmediateDrawables = false;
			if (_pimpl->_settings._skeletonMode) {
				CATCH_ASSETS_BEGIN
					auto* visContent = dynamic_cast<IVisContent*>(scene->get());
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
			
			auto fbFrag = CreateVisFBFrag();
			Techniques::RenderPassInstance rpi { parserContext, fbFrag };

			if (_pimpl->_settings._drawWireframe) {
				SceneEngine::ExecuteSceneRaw(
					parserContext, *_pimpl->_pipelineAccelerators, _pimpl->_deformAccelerators.get(),
					*_pimpl->_visWireframeCfg,
					sceneView, RenderCore::Techniques::BatchFilter::General,
					**scene);
			}

			if (_pimpl->_settings._drawNormals) {
				SceneEngine::ExecuteSceneRaw(
					parserContext, *_pimpl->_pipelineAccelerators, _pimpl->_deformAccelerators.get(),
					*_pimpl->_visNormalsCfg,
					sceneView, RenderCore::Techniques::BatchFilter::General,
					**scene);
			}

			if (drawImmediateDrawables)
				_pimpl->_immediateDrawables->ExecuteDraws(parserContext, rpi);

			if (doColorByMaterial) {
				auto *visContent = dynamic_cast<IVisContent*>(scene->get());
				std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate> oldDelegate;
				if (visContent)
					oldDelegate = visContent->SetCustomDrawDelegate(_pimpl->_stencilPrimeDelegate);
				// Prime the stencil buffer with draw call indices
				SceneEngine::ExecuteSceneRaw(
					parserContext, *_pimpl->_pipelineAccelerators, _pimpl->_deformAccelerators.get(),
					*_pimpl->_primeStencilCfg,
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
                    parserContext, 
                    settings, _pimpl->_settings._colourByMaterial==2);
            CATCH_ASSETS_END(parserContext)
        }

		bool writeMaterialName = 
			(_pimpl->_settings._colourByMaterial == 2 && _pimpl->_mouseOver->_hasMouseOver);

		if (writeMaterialName || _pimpl->_settings._drawBasisAxis || _pimpl->_settings._drawGrid) {

			CATCH_ASSETS_BEGIN

				using namespace RenderOverlays::DebuggingDisplay;
				using namespace RenderOverlays;
				ImmediateOverlayContext overlays(parserContext.GetThreadContext(), *_pimpl->_immediateDrawables, _pimpl->_fontRenderingManager.get());
				overlays.CaptureState();
				Rect rect { Coord2{0, 0}, Coord2(viewportDims[0], viewportDims[1]) };
				RenderTrackingOverlay(overlays, rect, *_pimpl->_mouseOver, **scene);
				if (_pimpl->_settings._drawBasisAxis)
					RenderOverlays::DrawBasisAxes(overlays, parserContext);
				if (_pimpl->_settings._drawGrid)
					RenderOverlays::DrawGrid(overlays, parserContext, 100.f);
				overlays.ReleaseState();

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithDepthStencil(parserContext);
				_pimpl->_immediateDrawables->ExecuteDraws(parserContext, rpi);

			CATCH_ASSETS_END(parserContext)
		}
    }

	void VisualisationOverlay::Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene)
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

	void VisualisationOverlay::OnRenderTargetUpdate(
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps)
	{
		using namespace RenderCore;

		RenderCore::Techniques::FragmentStitchingContext stitching{{}, fbProps};

		// We can't register the given preregistered attachments directly -- instead we have to 
		// register what we're expecting to be given when we actually begin our render
		auto color = std::find_if(
			preregAttachments.begin(), preregAttachments.end(), 
			[](auto c) { return c._semantic == Techniques::AttachmentSemantics::ColorLDR; });
		if (color != preregAttachments.end()) {
			// register an initialized color texture
			auto colorPreg = *color;
			colorPreg._state = Techniques::PreregisteredAttachment::State::Initialized;
			stitching.DefineAttachment(colorPreg);

			// register a default depth texture
			auto depthDesc = colorPreg._desc;
			depthDesc._bindFlags = BindFlag::DepthStencil;
			depthDesc._textureDesc._format = Format::D24_UNORM_S8_UINT;
			stitching.DefineAttachment({Techniques::AttachmentSemantics::MultisampleDepth, depthDesc, Techniques::PreregisteredAttachment::State::Initialized});
		}

		auto fbFrag = CreateVisFBFrag();
		auto stitched = stitching.TryStitchFrameBufferDesc({&fbFrag, &fbFrag+1});

		_pimpl->_visWireframeCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig("vis-wireframe", _pimpl->_visWireframeDelegate, ParameterBox{}, stitched._fbDesc);
		_pimpl->_visNormalsCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig("vis-normals", _pimpl->_visNormalsDelegate, ParameterBox{}, stitched._fbDesc);
		_pimpl->_primeStencilCfg = _pimpl->_pipelineAccelerators->CreateSequencerConfig("vis-prime-stencil", _pimpl->_primeStencilBufferDelegate, ParameterBox{}, stitched._fbDesc);
	}

    VisualisationOverlay::VisualisationOverlay(
		const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
		const VisOverlaySettings& overlaySettings,
        std::shared_ptr<VisMouseOver> mouseOver)
    {
		using namespace RenderCore;
        _pimpl = std::make_unique<Pimpl>();
		_pimpl->_pipelineAccelerators = immediateDrawingApparatus->_mainDrawingApparatus->_pipelineAccelerators;
		_pimpl->_immediateDrawables = immediateDrawingApparatus->_immediateDrawables;
		_pimpl->_fontRenderingManager = immediateDrawingApparatus->_fontRenderingManager;
        _pimpl->_settings = overlaySettings;
        _pimpl->_mouseOver = std::move(mouseOver);

		_pimpl->_visWireframeDelegate =
			RenderCore::Techniques::CreateTechniqueDelegateLegacy(
				Techniques::TechniqueIndex::VisWireframe, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);
		_pimpl->_visNormalsDelegate =
			RenderCore::Techniques::CreateTechniqueDelegateLegacy(
				Techniques::TechniqueIndex::VisNormals, {}, {}, Techniques::CommonResourceBox::s_dsReadWrite);

		DepthStencilDesc ds {
			RenderCore::CompareOp::GreaterEqual, true, true,
			0xff, 0xff,
			RenderCore::StencilDesc::AlwaysWrite,
			RenderCore::StencilDesc::NoEffect };
		_pimpl->_primeStencilBufferDelegate =
			RenderCore::Techniques::CreateTechniqueDelegateLegacy(
				Techniques::TechniqueIndex::DepthOnly, {}, {}, ds);
    }

    VisualisationOverlay::~VisualisationOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static RenderCore::Techniques::TechniqueContext MakeTechniqueContext(RenderCore::Techniques::DrawingApparatus& drawingApparatus)
    {
        RenderCore::Techniques::TechniqueContext techniqueContext;
		techniqueContext._commonResources = drawingApparatus._commonResources;
		techniqueContext._uniformDelegateManager = drawingApparatus._mainUniformDelegateManager;
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
		Techniques::ParsingContext parserContext { techniqueContext, threadContext };

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

		void Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene) { _scene = std::move(scene); }

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
        
		Assets::PtrToMarkerPtr<SceneEngine::IScene> _scene;
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
        RenderCore::Techniques::ParsingContext& parsingContext) 
    {
		const bool dummyCalculation = false;
		if (dummyCalculation) {
			PlatformRig::InputContext inputContext { {0, 0}, {256, 256} };
			PlatformRig::Coord2 mousePosition {128, 128};
			_inputListener->CalculateForMousePosition(inputContext, mousePosition);
		}
    }

	void MouseOverTrackingOverlay::Set(Assets::PtrToMarkerPtr<SceneEngine::IScene> scene)
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
	
	/*const std::shared_ptr<SceneEngine::IScene>& TryActualize(const ::Assets::MarkerPtr<SceneEngine::IScene>& future)
	{
		// This function exists because we can't call TryActualize() from a C++/CLR source file because
		// of the problem related to including <mutex>
		return future.TryActualize();
	}

	std::optional<std::string> GetActualizationError(const ::Assets::MarkerPtr<SceneEngine::IScene>& future)
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

