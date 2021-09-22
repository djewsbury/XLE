// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../SceneEngine/SceneParser.h"
#include "../../PlatformRig/BasicSceneParser.h"
#include <memory>

namespace SceneEngine { class TerrainManager; class PlacementsManager; class PlacementCellSet; class DynamicImposters; }

namespace Sample
{
    class IPlayerCharacter;

    class EnvironmentSceneParser : public PlatformRig::BasicSceneParser
    {
    public:
        void PrepareFrame(
            RenderCore::IThreadContext& context, 
            SceneEngine::LightingParserContext& parserContext);
        void Update(float deltaTime);

        typedef SceneEngine::StandardShadowProjection   StandardShadowProjection;
        typedef SceneEngine::LightingParserContext  LightingParserContext;
        typedef SceneEngine::SceneParseSettings     SceneParseSettings;
        typedef SceneEngine::LightDesc              LightDesc;
        typedef SceneEngine::GlobalLightingDesc     GlobalLightingDesc;
        typedef SceneEngine::ToneMapSettings        ToneMapSettings;

        RenderCore::Techniques::CameraDesc GetCameraDesc() const;

        void PrepareScene(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parserContext,
            SceneEngine::PreparedScene& preparedPackets) const;
        void ExecuteScene(
            RenderCore::IThreadContext& context,
            LightingParserContext& parserContext, 
            const SceneParseSettings& parseSettings,
            SceneEngine::PreparedScene& preparedPackets,
            unsigned techniqueIndex) const;
        bool HasContent(const SceneParseSettings& parseSettings) const;

        float GetTimeValue() const;

        std::shared_ptr<IPlayerCharacter> GetPlayerCharacter();
        std::shared_ptr<SceneEngine::TerrainManager> GetTerrainManager();
        std::shared_ptr<SceneEngine::PlacementsManager> GetPlacementManager();
        std::shared_ptr<SceneEngine::PlacementCellSet> GetPlacementCells();
        std::shared_ptr<SceneEngine::DynamicImposters> GetDynamicImposters();
        std::shared_ptr<RenderCore::Techniques::CameraDesc> GetCameraPtr();

        void FlushLoading();

        EnvironmentSceneParser(StringSection<::Assets::ResChar> cfgDir);
        ~EnvironmentSceneParser();

    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;

        const SceneEngine::EnvironmentSettings& GetEnvSettings() const;
    };

}

