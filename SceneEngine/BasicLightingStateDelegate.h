// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetsCore.h"
#if 0
#include "../SceneEngine/VolumetricFog.h"
#include "../SceneEngine/Ocean.h"
#include "../SceneEngine/DeepOceanSim.h"
#endif

namespace RenderCore { namespace LightingEngine { struct SunSourceFrustumSettings; }}
namespace Utility { class ParameterBox; }
namespace Utility { namespace ImpliedTyping { class TypeDesc; }}

namespace SceneEngine
{
    /// <summary>Simple & partial implementation of the ILightingStateDelegate interface<summary>
    /// This provides implementations of the basic lighting related interfaces of
    /// ISceneParser that will hook into an EnvironmentSettings object.
    /// Derived classes should implement the accessor GetEnvSettings().
    ::Assets::PtrToMarkerPtr<ILightingStateDelegate> CreateBasicLightingStateDelegate(StringSection<> envSettings);

    RenderCore::LightingEngine::SunSourceFrustumSettings    DefaultSunSourceFrustumSettings();

    void InitializeLight(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        const Utility::ParameterBox& parameters,
        const Float3& offsetLocalToWorld);

    bool SetProperty(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::LightSourceOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::ShadowOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::AmbientLightOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::SunSourceFrustumSettings& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);
}

