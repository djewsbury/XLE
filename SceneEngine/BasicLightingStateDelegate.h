// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../SceneEngine/Tonemap.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetsCore.h"
#if 0
#include "../SceneEngine/VolumetricFog.h"
#include "../SceneEngine/Ocean.h"
#include "../SceneEngine/DeepOceanSim.h"
#endif

namespace RenderCore { namespace LightingEngine { class SunSourceFrustumSettings; }}
namespace Utility { class ParameterBox; }

namespace SceneEngine
{
    using SunSourceFrustumSettings = RenderCore::LightingEngine::SunSourceFrustumSettings;

    /// <summary>Simple & partial implementation of the ILightingStateDelegate interface<summary>
    /// This provides implementations of the basic lighting related interfaces of
    /// ISceneParser that will hook into an EnvironmentSettings object.
    /// Derived classes should implement the accessor GetEnvSettings().
    ::Assets::PtrToFuturePtr<ILightingStateDelegate> CreateBasicLightingStateDelegate(StringSection<> envSettings);

    EnvironmentalLightingDesc   DefaultEnvironmentalLightingDesc();
    SunSourceFrustumSettings    DefaultSunSourceFrustumSettings();

    EnvironmentalLightingDesc MakeEnvironmentalLightingDesc(const ParameterBox& props);
    LightDesc MakeLightDesc(const Utility::ParameterBox& props);

    void InitializeLight(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        const ParameterBox& parameters,
        const Float3& offsetLocalToWorld);
}

