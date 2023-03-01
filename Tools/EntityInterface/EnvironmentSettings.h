// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/UTFUtils.h"

namespace SceneEngine { class VolumetricFogManager; class ShallowSurfaceManager; class EnvironmentSettings; }

namespace Utility 
{ 
    class TextOutputFormatter; 
    template<typename CharType> class TextInputFormatter;
}

namespace EntityInterface
{
    class RetainedEntities;
    class RetainedEntity;

///////////////////////////////////////////////////////////////////////////////////////////////////

    SceneEngine::EnvironmentSettings
        BuildEnvironmentSettings(
            const RetainedEntities& flexGobInterface,
            const RetainedEntity& obj);

    using EnvSettingsVector = 
        std::vector<std::pair<std::string, SceneEngine::EnvironmentSettings>>;
    EnvSettingsVector BuildEnvironmentSettings(
        const RetainedEntities& flexGobInterface);

    void ExportEnvSettings(
        TextOutputFormatter& formatter,
        const RetainedEntities& flexGobInterface,
        DocumentId docId);

///////////////////////////////////////////////////////////////////////////////////////////////////

    class EnvEntitiesManager : public std::enable_shared_from_this<EnvEntitiesManager>
    {
    public:
        void RegisterEnvironmentFlexObjects();
        void RegisterVolumetricFogFlexObjects(
            std::shared_ptr<SceneEngine::VolumetricFogManager> manager);
        void RegisterShallowSurfaceFlexObjects(
            std::shared_ptr<SceneEngine::ShallowSurfaceManager> manager);

        void FlushUpdates();

        EnvEntitiesManager(std::shared_ptr<RetainedEntities> sys);
        ~EnvEntitiesManager();

    protected:
        std::shared_ptr<RetainedEntities> _flexSys;
        std::weak_ptr<SceneEngine::ShallowSurfaceManager> _shallowWaterManager;
        bool _pendingShallowSurfaceUpdate;
    };

}

