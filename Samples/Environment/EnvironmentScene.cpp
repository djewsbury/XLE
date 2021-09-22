// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EnvironmentScene.h"
#include "../Shared/CharactersScene.h"
#include "../Shared/SampleGlobals.h"
#include "../Shared/IScenePlugin.h"
#include "../Shared/EnvFeatures.h"

#include "../../PlatformRig/PlatformRigUtil.h"

#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/TerrainFormat.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../SceneEngine/TerrainMaterial.h"
#include "../../SceneEngine/DynamicImposters.h"
#include "../../SceneEngine/SceneEngineUtils.h"     // for AsDelaySteps

#include "../../RenderCore/IAnnotator.h"
#include "../../RenderCore/Assets/ModelCache.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../Tools/EntityInterface/RetainedEntities.h"

#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetTraits.h"
#include "../../ConsoleRig/Console.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Streams/PathUtils.h"

namespace Sample
{
    std::shared_ptr<SceneEngine::ITerrainFormat>     MainTerrainFormat;
    SceneEngine::TerrainCoordinateSystem             MainTerrainCoords;
    SceneEngine::TerrainConfig                       MainTerrainConfig;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class EnvironmentSceneParser::Pimpl
    {
    public:
        std::shared_ptr<CharactersScene>                    _characters;
        std::shared_ptr<SceneEngine::TerrainManager>        _terrainManager;
        std::shared_ptr<SceneEngine::PlacementsManager>     _placementsManager;
        std::shared_ptr<SceneEngine::PlacementsRenderer>    _placementsRenderer;
        std::shared_ptr<SceneEngine::PlacementCellSet>      _placementsCells;
        std::shared_ptr<RenderCore::Techniques::CameraDesc> _cameraDesc;
        std::shared_ptr<RenderCore::Assets::ModelCache>     _modelCache;
        std::shared_ptr<SceneEngine::DynamicImposters>      _imposters;
        SceneEngine::EnvironmentSettings                    _envSettings;

        ::Assets::DependencyValidation     _terrainCfgVal;
        ::Assets::DependencyValidation     _terrainTexturesCfgVal;
        ::Assets::DependencyValidation     _placementsCfgVal;
        ::Assets::DependencyValidation     _environmentCfgVal;
        ::Assets::DependencyValidation     _gameObjectsCfgVal;

        std::shared_ptr<EntityInterface::RetainedEntities>  _retainedEntities;

        std::vector<std::shared_ptr<IScenePlugin>>          _scenePlugins;
        std::shared_ptr<ScenePlugin_EnvironmentFeatures>    _envFeatures;

        ::Assets::rstring _cfgDir;
        float _time;

        void LoadGameObjects(StringSection<::Assets::ResChar> filename);

        ::Assets::rstring MakeCfgName(const ::Assets::ResChar cfg[])
        {
            return _cfgDir + cfg;
        }

    };

    void EnvironmentSceneParser::Pimpl::LoadGameObjects(StringSection<::Assets::ResChar> filename)
    {
        size_t fileSize = 0;
        auto sourceFile = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(filename, &fileSize);
        using Formatter = InputStreamFormatter<utf8>;
        Formatter formatter(
            MemoryMappedInputStream(sourceFile.get(), PtrAdd(sourceFile.get(), fileSize)));

            // Parse the list of game objects, and create true native objects
            // as required.
            //
            // Note that this path is designed to be generic and flexible...
            // A final solution would do a lot of this work in the "export" step
            // of the editor -- and write out objects that are more closely
            // associated with the native types.
            //
            // But of this sample, we'll just use a simple generic approach.
            // We'll use the EntityInterface library -- this is the library
            // that is used to drive the native objects in the editor.
            // The EntityInterface provides a layer
            // over many native engine types, so they appear as generic
            // containers of names and values called "entities".

        using namespace EntityInterface;
        RetainedEntitiesAdapter interf(_retainedEntities);
        Deserialize(
            formatter, interf, 
            interf.GetDocumentTypeId("GameObjects"));

        _gameObjectsCfgVal = ::Assets::GetDepValSys().Make(filename);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void EnvironmentSceneParser::PrepareFrame(
        RenderCore::IThreadContext& context, 
        SceneEngine::LightingParserContext& parserContext)
    {
        //////////////////////////////////////////////////////////// //// //// //// .... ....
            // Reload anything that might have changed on disk
        FlushLoading();
        for (const auto& p:_pimpl->_scenePlugins)
            p->LoadingPhase();

        //////////////////////////////////////////////////////////// //// //// //// .... ....
            // Culling, etc

        const auto& worldToProjection = parserContext.GetProjectionDesc()._worldToProjection;
        _pimpl->_characters->Cull(worldToProjection);
        _pimpl->_characters->Prepare(context);

        for (const auto&p:_pimpl->_scenePlugins)
            p->PrepareFrame(context, parserContext);
    }

    static const char* PlacementsRenderName(RenderCore::Techniques::BatchFilter batchFilter)
    {
        using BF = RenderCore::Techniques::BatchFilter;
        switch (batchFilter) {
        case BF::General: return "PlacementsRender-General";
        case BF::Transparent:
        case BF::OITransparent: return "PlacementsRender-Transparent";
        case BF::PreDepth:
        case BF::TransparentPreDepth: return "PlacementsRender-PreDepth";
        case BF::DMShadows:
        case BF::RayTracedShadows: return "PlacementsRender-Shadows";
        default: return "PlacementsRender-Unknown";
        }
    }

    void EnvironmentSceneParser::PrepareScene(
        RenderCore::IThreadContext& context, 
        RenderCore::Techniques::ParsingContext& parserContext,
        SceneEngine::PreparedScene& preparedPackets) const
    {
        #if defined(ENABLE_TERRAIN)
            if (Tweakable("DoTerrain", true)) {
                auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
                _pimpl->_terrainManager->Prepare(metalContext.get(), parserContext, preparedPackets);
            }
        #endif
    }

    void EnvironmentSceneParser::ExecuteScene(   
        RenderCore::IThreadContext& context, 
        LightingParserContext& parserContext, 
        const SceneParseSettings& parseSettings,
        SceneEngine::PreparedScene& preparedPackets,
        unsigned techniqueIndex) const
    {
        CPUProfileEvent pEvnt2("ExecuteScene", g_cpuProfiler);
        
		auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
        using Toggles = SceneParseSettings::Toggles;
        using BF = RenderCore::Techniques::BatchFilter;
        #if defined(ENABLE_TERRAIN)
            if (    parseSettings._toggles & Toggles::Terrain
                &&  parseSettings._batchFilter == BF::General) {
                CPUProfileEvent pEvnt("TerrainRender", g_cpuProfiler);
                RenderCore::GPUProfilerBlock profilerBlock(context, "TerrainRender");
                CATCH_ASSETS_BEGIN
                    _pimpl->_terrainManager->Render(metalContext.get(), parserContext, preparedPackets, techniqueIndex);
                CATCH_ASSETS_END(parserContext)
            }
        #endif

        if (parseSettings._toggles & Toggles::NonTerrain) {
            if (_pimpl->_characters && (parseSettings._batchFilter == BF::General || parseSettings._batchFilter == BF::PreDepth || parseSettings._batchFilter == BF::DMShadows)) {
                CATCH_ASSETS_BEGIN
                    _pimpl->_characters->Render(context, parserContext, techniqueIndex);
                CATCH_ASSETS_END(parserContext)
            }

            if (_pimpl->_placementsRenderer) {
                auto delaySteps = SceneEngine::AsDelaySteps(parseSettings._batchFilter);
                auto* name = PlacementsRenderName(parseSettings._batchFilter);
                CPUProfileEvent pEvnt(name, g_cpuProfiler);
				RenderCore::GPUProfilerBlock profilerBlock(context, name);
                CATCH_ASSETS_BEGIN
                    for (auto i:delaySteps)
                        if (i != RenderCore::Assets::DelayStep::OpaqueRender) {
                            _pimpl->_placementsRenderer->CommitTransparent(*metalContext, parserContext, techniqueIndex, i);
                        } else {
                            _pimpl->_placementsRenderer->Render(*metalContext, parserContext, techniqueIndex, *_pimpl->_placementsCells);
                        }
                CATCH_ASSETS_END(parserContext)
            }
        }

        for (const auto&p:_pimpl->_scenePlugins)
            p->ExecuteScene(context, parserContext, parseSettings, techniqueIndex);
    }

    bool EnvironmentSceneParser::HasContent(const SceneParseSettings& parseSettings) const
    {
        using BF = RenderCore::Techniques::BatchFilter;
        auto batchFilter = parseSettings._batchFilter;
        if (batchFilter == BF::Transparent || batchFilter == BF::TransparentPreDepth || batchFilter == BF::OITransparent) {
            if (parseSettings._toggles & SceneParseSettings::Toggles::NonTerrain) {
                auto delaySteps = SceneEngine::AsDelaySteps(batchFilter);
                for (auto i:delaySteps)
                    if (_pimpl->_placementsRenderer->HasPrepared(i))
                        return true;
            }

            for (const auto&p:_pimpl->_scenePlugins)
                if (p->HasContent(parseSettings))
                    return true;

            return false;
        }
     
        return true;
    }

    RenderCore::Techniques::CameraDesc EnvironmentSceneParser::GetCameraDesc() const 
    { 
        return *_pimpl->_cameraDesc;
    }

    float EnvironmentSceneParser::GetTimeValue() const      
    { 
            //  The scene parser can also provide a time value, in seconds.
            //  This is used to control rendering effects, such as wind
            //  and waves.
        return _pimpl->_time; 
    }

    void EnvironmentSceneParser::Update(float deltaTime)    
    {
        _pimpl->_characters->Update(deltaTime);
        _pimpl->_time += deltaTime; 
    }

    std::shared_ptr<IPlayerCharacter>  EnvironmentSceneParser::GetPlayerCharacter()
    {
        return _pimpl->_characters->GetPlayerCharacter();
    }

    std::shared_ptr<SceneEngine::TerrainManager> EnvironmentSceneParser::GetTerrainManager()
    {
        return _pimpl->_terrainManager;
    }

    std::shared_ptr<SceneEngine::PlacementsManager> EnvironmentSceneParser::GetPlacementManager()
    {
        return _pimpl->_placementsManager;
    }

    std::shared_ptr<SceneEngine::PlacementCellSet> EnvironmentSceneParser::GetPlacementCells()
    {
        return _pimpl->_placementsCells;
    }

    std::shared_ptr<RenderCore::Techniques::CameraDesc> EnvironmentSceneParser::GetCameraPtr()
    {
        return _pimpl->_cameraDesc;
    }

    std::shared_ptr<SceneEngine::DynamicImposters> EnvironmentSceneParser::GetDynamicImposters()
    {
        return _pimpl->_imposters;
    }

    const SceneEngine::EnvironmentSettings& EnvironmentSceneParser::GetEnvSettings() const 
    { 
        return _pimpl->_envSettings; 
    }

    static const ::Assets::ResChar TerrainCfg[] = "terrain.cfg";
    static const ::Assets::ResChar TerrainMaterialCfg[] = "terrainmaterial.cfg";
    static const ::Assets::ResChar PlacementsCfg[] = "placements.cfg";
    static const ::Assets::ResChar EnvironmentCfg[] = "env.txt:environment";
    static const ::Assets::ResChar GameObjectsCfg[] = "gameobjects.txt";

    // static const Float3 WorldOffset(-11200.f - 7000.f, -11200.f + 700.f, 0.f);
    static const Float3 WorldOffset(0.f, 0.f, 0.f);

    void EnvironmentSceneParser::FlushLoading()
    {
        #if defined(ENABLE_TERRAIN)
            if (!_pimpl->_terrainCfgVal || _pimpl->_terrainCfgVal.GetValidationIndex() != 0) {
				auto terrainCfg = ::Assets::AutoConstructAsset<SceneEngine::TerrainConfig>(MakeStringSection(_pimpl->MakeCfgName(TerrainCfg)));
                _pimpl->_terrainManager->Load(*terrainCfg);
                _pimpl->_terrainCfgVal = terrainCfg->GetDependencyValidation();

                MainTerrainConfig = *terrainCfg;
                MainTerrainCoords = _pimpl->_terrainManager->GetCoords();

                if (_pimpl->_envFeatures)
                    _pimpl->_envFeatures->SetSurfaceHeights(_pimpl->_terrainManager->GetHeightsProvider());
            }

            if (!_pimpl->_terrainTexturesCfgVal || _pimpl->_terrainTexturesCfgVal.GetValidationIndex() != 0) {
                auto terrainMaterialCfg = ::Assets::AutoConstructAsset<SceneEngine::TerrainMaterialConfig>(MakeStringSection(_pimpl->MakeCfgName(TerrainMaterialCfg)));
                _pimpl->_terrainManager->LoadMaterial(*terrainMaterialCfg);
                _pimpl->_terrainTexturesCfgVal = terrainMaterialCfg->GetDependencyValidation();
            }
        #endif

        if (!_pimpl->_placementsCfgVal || _pimpl->_placementsCfgVal.GetValidationIndex() != 0) {
            auto placementsCfg = ::Assets::AutoConstructAsset<SceneEngine::WorldPlacementsConfig>(MakeStringSection(_pimpl->MakeCfgName(PlacementsCfg)));
            _pimpl->_placementsManager = std::make_shared<SceneEngine::PlacementsManager>(_pimpl->_modelCache);
            _pimpl->_placementsRenderer = _pimpl->_placementsManager->GetRenderer();
            _pimpl->_placementsCells = std::make_shared<SceneEngine::PlacementCellSet>(*placementsCfg, WorldOffset);
            _pimpl->_placementsCfgVal = placementsCfg->GetDependencyValidation();

            _pimpl->_imposters = std::make_shared<SceneEngine::DynamicImposters>(
                _pimpl->_modelCache->GetSharedStateSet());
            _pimpl->_placementsRenderer->SetImposters(_pimpl->_imposters);
            _pimpl->_imposters->Load(SceneEngine::DynamicImposters::Config());
        }

        if (!_pimpl->_environmentCfgVal || _pimpl->_environmentCfgVal.GetValidationIndex() != 0) {
            TRY
            {
                auto envSettings = ::Assets::AutoConstructAsset<SceneEngine::EnvironmentSettings>(MakeStringSection(_pimpl->MakeCfgName(EnvironmentCfg)));
                _pimpl->_envSettings = *envSettings;
                _pimpl->_environmentCfgVal = envSettings->GetDependencyValidation();
            } CATCH(...) {
            } CATCH_END
        }

        if (!_pimpl->_gameObjectsCfgVal || _pimpl->_gameObjectsCfgVal.GetValidationIndex() != 0) {
            _pimpl->LoadGameObjects(_pimpl->MakeCfgName(GameObjectsCfg).c_str());
        }
    }

    EnvironmentSceneParser::EnvironmentSceneParser(StringSection<::Assets::ResChar> cfgDir)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_time = 0.f;

        #if 0
            CharacterInputFiles chrFiles = 
                {
                    "game/chr/nu_f/skin/dragon003",
                    "game/chr/nu_f/animation",
                    "game/chr/nu_f/skeleton/all_co_sk_whirlwind_launch_mub",
                };
            AnimationNames animationNames = 
                {
                    "onehand_mo_combat_run_f",
                    "onehand_mo_combat_run_b",
                    "onehand_mo_combat_run_l",
                    "onehand_mo_combat_run_r",

                    "onehand_mo_combat_runtoidle_f",
                    "onehand_mo_combat_runtoidle_b",
                    "onehand_mo_combat_runtoidle_l",
                    "onehand_mo_combat_runtoidle_r",

                    "onehand_ba_combat_idle",
                    "onehand_ba_combat_idle_rand_1",
                    "onehand_ba_combat_idle_rand_2",
                    "onehand_ba_combat_idle_rand_3",
                    "onehand_ba_combat_idle_rand_4",
                    "onehand_ba_combat_idle_rand_5",

                    "Bip01/matrix"
                };
        #endif

        pimpl->_characters = std::make_unique<CharactersScene>();

        SplitPath<::Assets::ResChar> path(cfgDir);
        path.EndsWithSeparator() |= path.GetSectionCount() != 0;
        pimpl->_cfgDir = path.Simplify().Rebuild();

        #if defined(ENABLE_TERRAIN)
            MainTerrainFormat = std::make_shared<SceneEngine::TerrainFormat>();
            pimpl->_terrainManager = std::make_shared<SceneEngine::TerrainManager>(MainTerrainFormat);
            pimpl->_terrainManager->SetWorldSpaceOrigin(WorldOffset);
        #endif

        pimpl->_cameraDesc = std::make_shared<RenderCore::Techniques::CameraDesc>();
        if (pimpl->_characters)
            pimpl->_cameraDesc->_cameraToWorld = pimpl->_characters->DefaultCameraToWorld();
        pimpl->_cameraDesc->_nearClip = 0.5f;
        pimpl->_cameraDesc->_farClip = 6000.f;
        pimpl->_envSettings = PlatformRig::DefaultEnvironmentSettings();
        pimpl->_modelCache = std::make_shared<RenderCore::Assets::ModelCache>();

        pimpl->_retainedEntities = std::make_shared<EntityInterface::RetainedEntities>();
        RegisterEntityInterface(*pimpl->_retainedEntities, pimpl->_characters);

        pimpl->_envFeatures = std::make_shared<ScenePlugin_EnvironmentFeatures>(
            pimpl->_cfgDir, pimpl->_retainedEntities, pimpl->_modelCache);
        pimpl->_scenePlugins.push_back(pimpl->_envFeatures);
        _pimpl = std::move(pimpl);

        FlushLoading();
    }

    EnvironmentSceneParser::~EnvironmentSceneParser()
    {}


}

