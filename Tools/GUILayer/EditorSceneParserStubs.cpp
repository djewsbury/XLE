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
#include "../EntityInterface/LightingEngineEntityDocument.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/ExecuteScene.h"

#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/DeferredLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
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
#include "../../SceneEngine/RigidModelScene.h"      // todo -- move

namespace GUILayer
{
    using namespace SceneEngine;
 
//////////////////////////////////////////////////////////////////////////////////////////////////

    class BoundEnvironmentSettings
    {
    public:
        bool LightingTechniqueIsCompatible(
            RenderCore::LightingEngine::CompiledLightingTechnique& technique,
            unsigned& lastChangeId)
        {
            CheckLightSceneUpdate();

            if (_lightSceneChangeId == lastChangeId)
                return true;

            bool result = RenderCore::LightingEngine::ForwardLightingTechniqueIsCompatible(
                technique,
                _operatorsCfg._mergedCfg.GetLightOperators(),
                _operatorsCfg._mergedCfg.GetShadowOperators(),
                _operatorsCfg._mergedCfg.GetAmbientOperator());

            if (result)
                lastChangeId = _lightSceneChangeId;     // mark ok for next time
            return result;
        }

        const SceneEngine::MergedLightingEngineCfg& GetMergedLightingEngineCfg() const { return _operatorsCfg._mergedCfg; }
        const std::shared_ptr<RenderCore::LightingEngine::ILightScene> GetLightScene() const { return _lightScene; }

        BoundEnvironmentSettings(
            std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> apparatus,
            std::shared_ptr<EntityInterface::MultiEnvironmentSettingsDocument> envSettingsDocument,
            StringSection<> envSettingsName)
        : _envSettingsDocument(std::move(envSettingsDocument))
        , _apparatus(std::move(apparatus))
        {
            _envSettings = _envSettingsDocument->FindEnvSettingsId(envSettingsName);
            _lightSceneChangeId = _envSettingsDocument->GetChangeId(_envSettings);

            _envSettingsDocument->PrepareCfg(_envSettings, _operatorsCfg);

            // todo -- stall here
            _lightScene = SceneEngine::CreateAndActualizeForwardLightingScene(
                *_apparatus,
                _operatorsCfg._mergedCfg.GetLightOperators(),
                _operatorsCfg._mergedCfg.GetShadowOperators(),
                _operatorsCfg._mergedCfg.GetAmbientOperator());

            _envSettingsDocument->BindScene(_envSettings, _lightScene, _operatorsCfg);
        }

        ~BoundEnvironmentSettings()
        {
            if (_envSettingsDocument && _lightScene)
                _envSettingsDocument->UnbindScene(*_lightScene);
        }

        BoundEnvironmentSettings(BoundEnvironmentSettings&&) = default;
        BoundEnvironmentSettings& operator=(BoundEnvironmentSettings&&) = default;
    private:
        std::shared_ptr<EntityInterface::MultiEnvironmentSettingsDocument> _envSettingsDocument;
        std::shared_ptr<RenderCore::LightingEngine::ILightScene> _lightScene;
        unsigned _lightSceneChangeId = 0;
        EntityInterface::MultiEnvironmentSettingsDocument::EnvSettingsId _envSettings;
        EntityInterface::MergedLightingCfgHelper _operatorsCfg;

        std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _apparatus;

        void CheckLightSceneUpdate()
        {
            auto newChangeId = _envSettingsDocument->GetChangeId(_envSettings);
            if (newChangeId == _lightSceneChangeId)
                return;

            // recreate light scene completely, because the operators configuration has changed
            if (_envSettingsDocument && _lightScene)
                _envSettingsDocument->UnbindScene(*_lightScene);
            _lightScene.reset();

            _envSettingsDocument->PrepareCfg(_envSettings, _operatorsCfg);

            // todo -- stall here
            _lightScene = SceneEngine::CreateAndActualizeForwardLightingScene(
                *_apparatus,
                _operatorsCfg._mergedCfg.GetLightOperators(),
                _operatorsCfg._mergedCfg.GetShadowOperators(),
                _operatorsCfg._mergedCfg.GetAmbientOperator());

            _envSettingsDocument->BindScene(_envSettings, _lightScene, _operatorsCfg);
            _lightSceneChangeId = newChangeId;
        }
    };

    struct PreregAttachmentsHelper
    {
        uint64_t _targetsHash = 0ull;
        uint64_t _lastBuiltTargetsHash = 0ull;
		std::vector<RenderCore::Techniques::PreregisteredAttachment> _targets;
		RenderCore::FrameBufferProperties _fbProps;
    };

    public ref class EditorSceneOverlay : public IOverlaySystem
    {
    public:
        void Render(RenderCore::Techniques::ParsingContext& parserContext) override;
        void OnRenderTargetUpdate(
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
            const RenderCore::FrameBufferProperties& fbProps,
            IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override;
        EditorSceneOverlay(
            const std::shared_ptr<EditorScene>& sceneParser,
			const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings);
        ~EditorSceneOverlay();
    protected:
        clix::shared_ptr<EditorScene> _scene;
		clix::shared_ptr<ToolsRig::VisCameraSettings> _camera;
		clix::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingApparatus;
        EditorSceneRenderSettings^ _renderSettings;
        clix::shared_ptr<BoundEnvironmentSettings> _boundEnvSettings;
        clix::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _lightingTechnique;
        clix::shared_ptr<PreregAttachmentsHelper> _preregAttachmentsHelper;
        unsigned _lastLightingTechniqueChangeId = 0;
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
        if (!_boundEnvSettings) {
            // todo -- manage switching between different environmental settings
            auto envSettingsName = clix::marshalString<clix::E_UTF8>(_renderSettings->_activeEnvironmentSettings);
            _boundEnvSettings = std::make_shared<BoundEnvironmentSettings>(_lightingApparatus.GetNativePtr(), _scene->_envSettingsDocument, envSettingsName);
        }

        _scene->_rigidModelScene->OnFrameBarrier();     // todo -- move somewhere better

        auto& stitchingContext = parserContext.GetFragmentStitchingContext();

        // todo -- stitching context compatibility
        unsigned temp = _lastLightingTechniqueChangeId;
        if (!_lightingTechnique || !_boundEnvSettings->LightingTechniqueIsCompatible(*_lightingTechnique.get(), temp) || _preregAttachmentsHelper->_targetsHash != _preregAttachmentsHelper->_lastBuiltTargetsHash) {
            _lightingTechnique = SceneEngine::CreateAndActualizeForwardLightingTechnique(
                *_lightingApparatus.GetNativePtr(),
                _boundEnvSettings->GetLightScene(),
                stitchingContext.GetPreregisteredAttachments(), stitchingContext._workingProps);
            _preregAttachmentsHelper->_lastBuiltTargetsHash = _preregAttachmentsHelper->_targetsHash;
        }
        _lastLightingTechniqueChangeId = temp;

        {
			ToolsRig::ConfigureParsingContext(parserContext, *_camera.get());
            RenderCore::LightingEngine::LightingTechniqueInstance lightingIterator { parserContext, *_lightingTechnique.get() };
            for (;;) {
                auto next = lightingIterator.GetNextStep();
                if (next._type == RenderCore::LightingEngine::StepType::None || next._type == RenderCore::LightingEngine::StepType::Abort) break;
                if (next._type == RenderCore::LightingEngine::StepType::ParseScene || next._type == RenderCore::LightingEngine::StepType::MultiViewParseScene) {
                    assert(!next._pkts.empty());
                    BuildDrawables(*_scene.get(), parserContext, next);
                }
            }
		}

		if (_renderSettings->_selection && _renderSettings->_selection->_nativePlacements->size() > 0) {
            // Draw a selection highlight for these items
            // at the moment, only placements can be selected... So we need to assume that 
            // they are all placements.
            ToolsRig::Placements_RenderHighlight(
                parserContext, *_lightingApparatus->_pipelineAccelerators.get(),
                *_scene->_placementsManager->GetRenderer().get(), *_scene->_placementsCells.get(),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cbegin()),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cend()));
        }

        // render shadow for hidden placements
        if (::ConsoleRig::Detail::FindTweakable("ShadowHiddenPlacements", true)) {
            ToolsRig::Placements_RenderShadow(
                parserContext, *_lightingApparatus->_pipelineAccelerators.get(),
                *_scene->_placementsManager->GetRenderer().get(), *_scene->_placementsCellsHidden.get());
        }
    }

    void EditorSceneOverlay::OnRenderTargetUpdate(
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        _preregAttachmentsHelper->_targetsHash = RenderCore::Techniques::HashPreregisteredAttachments(preregAttachments, fbProps);
        _preregAttachmentsHelper->_targets = {preregAttachments.begin(), preregAttachments.end()};
		_preregAttachmentsHelper->_fbProps = fbProps;
    }

    EditorSceneOverlay::EditorSceneOverlay(
        const std::shared_ptr<EditorScene>& sceneParser,
		const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
        EditorSceneRenderSettings^ renderSettings)
    {
        _scene = sceneParser;
		_camera = camera;
        _renderSettings = renderSettings;
		_lightingApparatus = EngineDevice::GetInstance()->GetNative().GetLightingEngineApparatus();
        _preregAttachmentsHelper = std::make_shared<PreregAttachmentsHelper>();
    }

    EditorSceneOverlay::~EditorSceneOverlay() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Internal
    {
        IOverlaySystem^ CreateOverlaySystem(
            const std::shared_ptr<EditorScene>& scene, 
            const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings)
        {
            return gcnew EditorSceneOverlay(scene, camera, renderSettings);
        }
    }
}

