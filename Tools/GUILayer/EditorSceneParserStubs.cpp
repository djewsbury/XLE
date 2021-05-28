// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LevelEditorScene.h"
#include "IOverlaySystem.h"
#include "MarshalString.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "ExportedNativeTypes.h"

#include "../ToolsRig/ObjectPlaceholders.h"
#include "../ToolsRig/ManipulatorsRender.h"
#include "../EntityInterface/RetainedEntities.h"
#include "../EntityInterface/EnvironmentSettings.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/ExecuteScene.h"

#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/IDevice.h"

#include "../../ConsoleRig/Console.h"

#include "../ToolsRig/VisualisationUtils.h"     // for AsCameraDesc

namespace GUILayer
{
    using namespace SceneEngine;
 
//////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class EditorSceneOverlay : public IOverlaySystem
    {
    public:
        void Render(
            RenderCore::IThreadContext& threadContext,
            RenderCore::Techniques::ParsingContext& parserContext) override;

        EditorSceneOverlay(
            const std::shared_ptr<EditorScene>& sceneParser,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings);
        ~EditorSceneOverlay();
    protected:
        clix::shared_ptr<EditorScene> _scene;
		clix::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		clix::shared_ptr<ToolsRig::VisCameraSettings> _camera;
		clix::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingApparatus;
        EditorSceneRenderSettings^ _renderSettings;
    };

	class EditorLightingParserDelegate : public SceneEngine::BasicLightingStateDelegate
	{
	public:
        void PrepareEnvironmentalSettings(const char envSettings[]);

		EditorLightingParserDelegate(const std::shared_ptr<EditorScene>& editorScene);
		~EditorLightingParserDelegate();
	protected:
		std::shared_ptr<EditorScene> _editorScene;
	};

	static void BuildDrawables(
		EditorScene& scene,
		ToolsRig::VisCameraSettings& camera,
		UInt2 viewportDims,
		RenderCore::Techniques::BatchFilter batchFilter,
		RenderCore::Techniques::DrawablesPacket& pkt)
	{
		SceneEngine::ExecuteSceneContext exeContext;
		exeContext._batchFilter = batchFilter;
		exeContext._destinationPkt = &pkt;
		auto camDesc = ToolsRig::AsCameraDesc(camera);
		exeContext._view = SceneEngine::SceneView { SceneEngine::SceneView::Type::Normal, RenderCore::Techniques::BuildProjectionDesc(camDesc, viewportDims) };
		scene._placementsManager->GetRenderer()->BuildDrawables(exeContext, *scene._placementsCells);
		scene._placeholders->BuildDrawables(exeContext);
	}
    
    void EditorSceneOverlay::Render(
        RenderCore::IThreadContext& threadContext,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        UInt2 viewportDims { parserContext.GetViewport()._width, parserContext.GetViewport()._height };

        auto& stitchingContext = parserContext.GetFragmentStitchingContext();
		EditorLightingParserDelegate lightingDelegate(_scene.GetNativePtr());
		auto compiledTechniqueFuture = RenderCore::LightingEngine::CreateDeferredLightingTechnique(
			_lightingApparatus.GetNativePtr(),
            lightingDelegate.GetLightResolveOperators(),
			lightingDelegate.GetShadowResolveOperators(),
			stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
        auto compiledTechnique = SceneEngine::StallAndActualize(*compiledTechniqueFuture);

        {
			ToolsRig::ConfigureParsingContext(parserContext, *_camera.get());

            {
				lightingDelegate.PrepareEnvironmentalSettings(
					clix::marshalString<clix::E_UTF8>(_renderSettings->_activeEnvironmentSettings).c_str());

				auto lightingIterator = SceneEngine::BeginLightingTechnique(
					threadContext, parserContext, *_pipelineAcceleratorPool.get(),
					lightingDelegate, *compiledTechnique);

				for (;;) {
					auto next = lightingIterator.GetNextStep();
					if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
					if (next._type == RenderCore::LightingEngine::StepType::ParseScene) {
					    assert(next._pkt);
					    BuildDrawables(*_scene.get(), *_camera.get(), viewportDims, next._batch, *next._pkt);
                    }
				}

                auto& lightScene = RenderCore::LightingEngine::GetLightScene(*compiledTechnique);
				lightScene.Clear();
			}
		}

		if (_renderSettings->_selection && _renderSettings->_selection->_nativePlacements->size() > 0) {
            // Draw a selection highlight for these items
            // at the moment, only placements can be selected... So we need to assume that 
            // they are all placements.
            ToolsRig::Placements_RenderHighlight(
                threadContext, parserContext, *_pipelineAcceleratorPool.get(),
                *_scene->_placementsManager->GetRenderer().get(), *_scene->_placementsCells.get(),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cbegin()),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cend()));
        }

        // render shadow for hidden placements
        if (::ConsoleRig::Detail::FindTweakable("ShadowHiddenPlacements", true)) {
            ToolsRig::Placements_RenderShadow(
                threadContext, parserContext, *_pipelineAcceleratorPool.get(),
                *_scene->_placementsManager->GetRenderer().get(), *_scene->_placementsCellsHidden.get());
        }
    }

    EditorSceneOverlay::EditorSceneOverlay(
        const std::shared_ptr<EditorScene>& sceneParser,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
        EditorSceneRenderSettings^ renderSettings)
    {
        _scene = sceneParser;
		_pipelineAcceleratorPool = pipelineAcceleratorPool;
		_camera = camera;
        _renderSettings = renderSettings;
		_lightingApparatus = EngineDevice::GetInstance()->GetNative().GetLightingEngineApparatus();
    }

    EditorSceneOverlay::~EditorSceneOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void EditorLightingParserDelegate::PrepareEnvironmentalSettings(const char envSettings[])
    {
        for (const auto& i:_editorScene->_prepareSteps)
            i();

        using namespace EntityInterface;
        const auto& objs = *_editorScene->_flexObjects;
        const RetainedEntity* settings = nullptr;
        const auto typeSettings = objs.GetTypeId("EnvSettings");

        {
            static const auto nameHash = ParameterBox::MakeParameterNameHash("Name");
            auto allSettings = objs.FindEntitiesOfType(typeSettings);
            for (const auto& s : allSettings)
                if (!XlCompareStringI(MakeStringSection(s->_properties.GetParameterAsString(nameHash).value()), envSettings)) {
                    settings = s;
                    break;
                }
        }

        if (settings) {
            *_envSettings = BuildEnvironmentSettings(objs, *settings);
        } else {
            _envSettings->_lights.clear();
            _envSettings->_sunSourceShadowProj.clear();
            _envSettings->_environmentalLightingDesc = SceneEngine::DefaultEnvironmentalLightingDesc();
        }
    }

	EditorLightingParserDelegate::EditorLightingParserDelegate(const std::shared_ptr<EditorScene>& editorScene)
	: BasicLightingStateDelegate(std::make_shared<SceneEngine::EnvironmentSettings>(SceneEngine::DefaultEnvironmentSettings()))
	, _editorScene(editorScene)
	{
	}

	EditorLightingParserDelegate::~EditorLightingParserDelegate() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Internal
    {
        IOverlaySystem^ CreateOverlaySystem(
            const std::shared_ptr<EditorScene>& scene, 
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
            const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings)
        {
            return gcnew EditorSceneOverlay(
                scene, 
				pipelineAcceleratorPool,
				camera,
                renderSettings);
        }
    }
}

