// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LevelEditorScene.h"
#include "EditorInterfaceUtils.h"
#include "IOverlaySystem.h"
#include "MarshalString.h"
#include "ExportedNativeTypes.h"
#include "NativeEngineDevice.h"

#include "../EntityInterface/RetainedEntities.h"

#include "../ToolsRig/VisualisationUtils.h"     // for AsCameraDesc
#include "../ToolsRig/ManipulatorsRender.h"
#include "../ToolsRig/ObjectPlaceholders.h"
#include "../../SceneEngine/BasicLightingStateDelegate.h"
#include "../../PlatformRig/Screenshot.h"

#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/VegetationSpawn.h"
#include "../../SceneEngine/VolumetricFog.h"
#include "../../SceneEngine/ShallowSurface.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/PreparedScene.h"
#include "../../SceneEngine/SurfaceHeightsProvider.h"

#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../Utility/StringUtils.h"
#include "../../ConsoleRig/Console.h"

namespace GUILayer
{
    using namespace SceneEngine;
 
///////////////////////////////////////////////////////////////////////////////////////////////////

	class EditorScenePlugin : public SceneEngine::ILightingParserPlugin
    {
	public:
		virtual void OnPreScenePrepare(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&) const override;

		virtual void OnPostSceneRender(
            RenderCore::IThreadContext&, RenderCore::Techniques::ParsingContext&, LightingParserContext&, 
			RenderCore::Techniques::BatchFilter filter, unsigned techniqueIndex) const override;

		EditorScenePlugin(const std::shared_ptr<EditorScene>& editorScene);
		~EditorScenePlugin();
	protected:
		std::shared_ptr<EditorScene> _editorScene;
	};

    static ISurfaceHeightsProvider* GetSurfaceHeights(EditorScene& scene)
    {
        return scene._terrainManager ? scene._terrainManager->GetHeightsProvider().get() : nullptr;
    }

    void EditorScenePlugin::OnPostSceneRender(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext, 
		LightingParserContext& lightingParserContext, 
        RenderCore::Techniques::BatchFilter batchFilter,
        unsigned techniqueIndex) const
    {
		auto& preparedPackets = *lightingParserContext._preparedScene;

        using BF = RenderCore::Techniques::BatchFilter;
        auto& scene = *_editorScene;

        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(context);

        if (scene._terrainManager) { // parseSettings._toggles & SceneParseSettings::Toggles::Terrain
            if (batchFilter == BF::General) {
                CATCH_ASSETS_BEGIN
                    scene._terrainManager->Render(context, parserContext, lightingParserContext._delegate, preparedPackets, techniqueIndex);
                CATCH_ASSETS_END(parserContext)
            }
        }

        if (1) { // parseSettings._toggles & SceneParseSettings::Toggles::NonTerrain) {
            CATCH_ASSETS_BEGIN
                //for (auto i:delaySteps)
                //    scene._vegetationSpawnManager->Render(context, parserContext, techniqueIndex, i);
            CATCH_ASSETS_END(parserContext)
        
            if (batchFilter == BF::PostOpaque) {
                CATCH_ASSETS_BEGIN
                    scene._placeholders->Render(metalContext, parserContext, techniqueIndex);
                    scene._shallowSurfaceManager->RenderDebugging(metalContext, parserContext, techniqueIndex, GetSurfaceHeights(scene), lightingParserContext._delegate->GetGlobalLightingDesc());
                CATCH_ASSETS_END(parserContext)
            }
        }
    }

    void EditorScenePlugin::OnPreScenePrepare(
        RenderCore::IThreadContext& context,
		RenderCore::Techniques::ParsingContext& parserContext, 
        LightingParserContext& lightingParserContext) const
    {
		auto& preparedPackets = *lightingParserContext._preparedScene;

        auto& scene = *_editorScene;
        if (scene._terrainManager) {
            scene._terrainManager->Prepare(context, parserContext, preparedPackets);
            // scene._placementsManager->GetRenderer()->CullToPreparedScene(preparedPackets, parserContext, *scene._placementsCells);
        }
    }

	EditorScenePlugin::EditorScenePlugin(const std::shared_ptr<EditorScene>& editorScene)
	: _editorScene(editorScene)
	{}
		
	EditorScenePlugin::~EditorScenePlugin() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class EditorLightingParserDelegate : public SceneEngine::BasicLightingStateDelegate
	{
	public:
		float GetTimeValue() const;
        void PrepareEnvironmentalSettings(const char envSettings[]);

		EditorLightingParserDelegate(const std::shared_ptr<EditorScene>& editorScene);
		~EditorLightingParserDelegate();
	protected:
		std::shared_ptr<EditorScene> _editorScene;
	};

    float EditorLightingParserDelegate::GetTimeValue() const { return _editorScene->_currentTime; }

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
                if (!XlCompareStringI(s->_properties.GetString<char>(nameHash).c_str(), envSettings)) {
                    settings = s;
                    break;
                }
        }

        if (settings) {
            *_envSettings = BuildEnvironmentSettings(objs, *settings);
        } else {
            _envSettings->_lights.clear();
            _envSettings->_shadowProj.clear();
            _envSettings->_globalLightingDesc = PlatformRig::DefaultGlobalLightingDesc();
        }
    }

	EditorLightingParserDelegate::EditorLightingParserDelegate(const std::shared_ptr<EditorScene>& editorScene)
	: BasicLightingStateDelegate(std::make_shared<SceneEngine::EnvironmentSettings>(PlatformRig::DefaultEnvironmentSettings()))
	, _editorScene(editorScene)
	{
	}

	EditorLightingParserDelegate::~EditorLightingParserDelegate() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	class EditorSceneParser : public SceneEngine::IScene
    {
    public:
		virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
			SceneExecuteContext& executeContext) const;

        EditorSceneParser(const std::shared_ptr<EditorScene>& editorScene);
        ~EditorSceneParser();
    protected:
        std::shared_ptr<EditorScene> _editorScene;
    };

	void EditorSceneParser::ExecuteScene(
        RenderCore::IThreadContext& threadContext,
		SceneExecuteContext& executeContext) const
	{
		_editorScene->_placementsManager->GetRenderer()->BuildDrawables(executeContext, *_editorScene->_placementsCells);

		for (unsigned v=0; v<executeContext.GetViews().size(); ++v) {
			RenderCore::Techniques::DrawablesPacket* pkts[unsigned(RenderCore::Techniques::BatchFilter::Max)];
			for (unsigned c=0; c<unsigned(RenderCore::Techniques::BatchFilter::Max); ++c)
				pkts[c] = executeContext.GetDrawablesPacket(v, RenderCore::Techniques::BatchFilter(c));
			_editorScene->_placeholders->BuildDrawables(MakeIteratorRange(pkts));
		}
	}

    EditorSceneParser::EditorSceneParser(const std::shared_ptr<EditorScene>& editorScene)
    : _editorScene(editorScene)
    {}
    EditorSceneParser::~EditorSceneParser() {}

//////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class EditorSceneOverlay : public IOverlaySystem
    {
    public:
        void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        EditorSceneOverlay(
            const std::shared_ptr<EditorScene>& sceneParser,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
            EditorSceneRenderSettings^ renderSettings,
            const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCells,
            const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCellsHidden,
            const std::shared_ptr<SceneEngine::PlacementsRenderer>& placementsRenderer);
        ~EditorSceneOverlay();
    protected:
        clix::shared_ptr<EditorScene> _scene;
		clix::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		clix::shared_ptr<ToolsRig::VisCameraSettings> _camera;
        EditorSceneRenderSettings^ _renderSettings;
        clix::shared_ptr<SceneEngine::PlacementCellSet> _placementCells;
        clix::shared_ptr<SceneEngine::PlacementCellSet> _placementCellsHidden;
        clix::shared_ptr<SceneEngine::PlacementsRenderer> _placementsRenderer;
    };
    
    void EditorSceneOverlay::Render(
        RenderCore::IThreadContext& threadContext,
		const RenderTargetWrapper& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        if (_scene.get()) {
			EditorSceneParser sceneParser(_scene.GetNativePtr());
			EditorLightingParserDelegate lightingDelegate(_scene.GetNativePtr());

            lightingDelegate.PrepareEnvironmentalSettings(
                clix::marshalString<clix::E_UTF8>(_renderSettings->_activeEnvironmentSettings).c_str());

			std::vector<std::shared_ptr<SceneEngine::ILightingParserPlugin>> lightingPlugins;
			lightingPlugins.push_back(
				std::make_shared<EditorScenePlugin>(_scene.GetNativePtr()));
			auto vegetationPlugin = _scene->_vegetationSpawnManager->GetParserPlugin();
			if (vegetationPlugin)
				lightingPlugins.push_back(vegetationPlugin);
			lightingPlugins.push_back(_scene->_volumeFogManager->GetParserPlugin());

			auto renderSteps = CreateStandardRenderSteps(
				(::ConsoleRig::Detail::FindTweakable("LightingModel", 0) == 0)
					? LightingModel::Deferred 
					: LightingModel::Forward);

			auto sceneTechniqueDesc = SceneEngine::SceneTechniqueDesc{
				MakeIteratorRange(renderSteps),
				MakeIteratorRange(lightingPlugins)};

			auto compiledSceneTechnique = CreateCompiledSceneTechnique(
				sceneTechniqueDesc, _pipelineAcceleratorPool.GetNativePtr(),
				RenderCore::AsAttachmentDesc(renderTarget._renderTarget->GetDesc()));

			auto camera = ToolsRig::AsCameraDesc(*_camera.get());

            auto& screenshot = ::ConsoleRig::Detail::FindTweakable("Screenshot", 0);
            if (screenshot) {
                PlatformRig::TiledScreenshot(
                    threadContext, parserContext,
                    sceneParser, camera,
                    *compiledSceneTechnique, UInt2(screenshot, screenshot));
                screenshot = 0;
            }

            SceneEngine::LightingParser_ExecuteScene(
                threadContext, renderTarget._renderTarget, parserContext, 
				*compiledSceneTechnique, lightingDelegate,
				sceneParser, camera);
        }

        if (_renderSettings->_selection && _renderSettings->_selection->_nativePlacements->size() > 0) {
            // Draw a selection highlight for these items
            // at the moment, only placements can be selected... So we need to assume that 
            // they are all placements.
            ToolsRig::Placements_RenderHighlight(
                threadContext, parserContext, 
                *_placementsRenderer.get(), *_placementCells.get(),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cbegin()),
                (const SceneEngine::PlacementGUID*)AsPointer(_renderSettings->_selection->_nativePlacements->cend()));
        }

        // render shadow for hidden placements
        if (::ConsoleRig::Detail::FindTweakable("ShadowHiddenPlacements", true)) {
            ToolsRig::Placements_RenderShadow(
                threadContext, parserContext, 
                *_placementsRenderer.get(), *_placementCellsHidden.get());
        }
    }

    EditorSceneOverlay::EditorSceneOverlay(
        const std::shared_ptr<EditorScene>& sceneParser,
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<ToolsRig::VisCameraSettings>& camera, 
        EditorSceneRenderSettings^ renderSettings,
        const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCells,
        const std::shared_ptr<SceneEngine::PlacementCellSet>& placementCellsHidden,
        const std::shared_ptr<SceneEngine::PlacementsRenderer>& placementsRenderer)
    {
        _scene = sceneParser;
		_pipelineAcceleratorPool = pipelineAcceleratorPool;
		_camera = camera;
        _renderSettings = renderSettings;
        _placementCells = placementCells;
        _placementCellsHidden = placementCellsHidden;
        _placementsRenderer = placementsRenderer;
    }

    EditorSceneOverlay::~EditorSceneOverlay() {}


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
                renderSettings, scene->_placementsCells, scene->_placementsCellsHidden,
                scene->_placementsManager->GetRenderer());
        }
    }
}

