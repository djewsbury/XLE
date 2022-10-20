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
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/ExecuteScene.h"

#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
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
        void Render(RenderCore::Techniques::ParsingContext& parserContext) override;
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

	class EditorLightingParserDelegate : public SceneEngine::ILightingStateDelegate
	{
	public:
        virtual void        PreRender(
            const RenderCore::Techniques::ProjectionDesc& mainSceneCameraDesc, 
            RenderCore::LightingEngine::ILightScene& lightScene) override
        {}
        virtual void        PostRender(RenderCore::LightingEngine::ILightScene& lightScene) override
        {}

        virtual void        BindScene(RenderCore::LightingEngine::ILightScene& lightScene, std::shared_ptr<::Assets::OperationContext>) override
        {}
        virtual void        UnbindScene(RenderCore::LightingEngine::ILightScene& lightScene) override
        {}

        virtual void        BindCfg(MergedLightingEngineCfg& cfg) override
        {}

        virtual std::shared_ptr<RenderCore::LightingEngine::IProbeRenderingInstance> BeginPrepareStep(
            RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::IThreadContext& threadContext) override
        {
            return nullptr;
        }

        void PrepareEnvironmentalSettings(const EditorScene& editorScene, const char envSettings[]);

	protected:
        ::Assets::DependencyValidation _depVal;
	};

	static void BuildDrawables(
		EditorScene& scene,
        RenderCore::Techniques::ParsingContext& parserContext,
		RenderCore::LightingEngine::LightingTechniqueInstance::Step& step)
	{
        SceneEngine::ExecuteSceneContext exeContext;
        exeContext._destinationPkts = MakeIteratorRange(step._pkts);
        if (step._type == RenderCore::LightingEngine::StepType::ParseScene) {
            exeContext._views = MakeIteratorRange(&parserContext.GetProjectionDesc(), &parserContext.GetProjectionDesc()+1);
        } else if (step._type == RenderCore::LightingEngine::StepType::MultiViewParseScene) {
            exeContext._views = MakeIteratorRange(step._multiViewDesc);
        }
        exeContext._complexCullingVolume = step._complexCullingVolume;
		scene._placementsManager->GetRenderer()->BuildDrawables(exeContext, *scene._placementsCells);
		scene._placeholders->BuildDrawables(exeContext);
        parserContext.RequireCommandList(exeContext._completionCmdList);
	}
    
    void EditorSceneOverlay::Render(
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        UInt2 viewportDims { parserContext.GetViewport()._width, parserContext.GetViewport()._height };

        auto& stitchingContext = parserContext.GetFragmentStitchingContext();
		EditorLightingParserDelegate lightingDelegate;
        lightingDelegate.PrepareEnvironmentalSettings(
            *_scene.get(),
			clix::marshalString<clix::E_UTF8>(_renderSettings->_activeEnvironmentSettings).c_str());

        SceneEngine::MergedLightingEngineCfg lightingEngineCfg;
        lightingDelegate.BindCfg(lightingEngineCfg);
		auto compiledTechnique = SceneEngine::CreateAndActualizeForwardLightingTechnique(
			_lightingApparatus.GetNativePtr(),
            lightingEngineCfg.GetLightOperators(),
			lightingEngineCfg.GetShadowOperators(),
			lightingEngineCfg.GetAmbientOperator(),
			stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);

        {
			ToolsRig::ConfigureParsingContext(parserContext, *_camera.get());

            {
				auto lightingIterator = SceneEngine::BeginLightingTechnique(
					parserContext,
					lightingDelegate, *compiledTechnique);

				for (;;) {
					auto next = lightingIterator.GetNextStep();
					if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
					if (next._type == RenderCore::LightingEngine::StepType::ParseScene || next._type == RenderCore::LightingEngine::StepType::MultiViewParseScene) {
					    assert(!next._pkts.empty());
					    BuildDrawables(*_scene.get(), parserContext, next);
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
                parserContext, *_pipelineAcceleratorPool.get(),
                *_scene->_placementsManager->GetRenderer().get(), *_scene->_placementsCells.get(),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cbegin()),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cend()));
        }

        // render shadow for hidden placements
        if (::ConsoleRig::Detail::FindTweakable("ShadowHiddenPlacements", true)) {
            ToolsRig::Placements_RenderShadow(
                parserContext, *_pipelineAcceleratorPool.get(),
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

    void EditorLightingParserDelegate::PrepareEnvironmentalSettings(const EditorScene& editorScene, const char envSettings[])
    {
        for (const auto& i:editorScene._prepareSteps)
            i();

        using namespace EntityInterface;
        const auto& objs = *editorScene._flexObjects;
        const RetainedEntity* settings = nullptr;
        const auto typeSettings = Hash64("EnvSettings");

        {
            static const auto nameHash = ParameterBox::MakeParameterNameHash("Name");
            auto allSettings = objs.FindEntitiesOfType(typeSettings);
            for (const auto& s : allSettings)
                if (!XlCompareStringI(MakeStringSection(s->_properties.GetParameterAsString(nameHash).value()), envSettings)) {
                    settings = s;
                    break;
                }
        }

        // todo -- collate lighting information from the entities
        /*
        if (settings) {
            *_envSettings = BuildEnvironmentSettings(objs, *settings);
        } else {
            _envSettings->_lights.clear();
            _envSettings->_sunSourceShadowProj.clear();
            _envSettings->_environmentalLightingDesc = SceneEngine::DefaultEnvironmentalLightingDesc();
        }
        */
    }

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

