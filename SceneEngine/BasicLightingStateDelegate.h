// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../RenderCore/LightingEngine/SunSourceConfiguration.h"
#include "../SceneEngine/Tonemap.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetsCore.h"
#if 0
#include "../SceneEngine/VolumetricFog.h"
#include "../SceneEngine/Ocean.h"
#include "../SceneEngine/DeepOceanSim.h"
#endif

namespace Utility
{
    template<typename Type> class InputStreamFormatter;
}
namespace Assets 
{ 
    class DirectorySearchRules; 
    template<typename Formatter> class ConfigFileContainer; 
}

namespace EntityInterface { class IDynamicFormatter; }

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
}

