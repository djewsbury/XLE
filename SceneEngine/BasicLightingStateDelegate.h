// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IScene.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetsCore.h"

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

    struct SwirlingLightsOperatorDesc
    {
        unsigned _lightCount = 0u;
        float _swirlingRadius = 15.f;
        float _cutoffRadius = 7.5f;
    };

    void InitializeLight(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        const Utility::ParameterBox& parameters,
        const Float3& offsetLocalToWorld);

    bool SetProperty(
        RenderCore::LightingEngine::ILightScene& lightScene, RenderCore::LightingEngine::ILightScene::LightSourceId sourceId,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::IBloom& bloom,
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

    bool SetProperty(
        RenderCore::LightingEngine::ForwardLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::DeferredLightingTechniqueDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);
        
    bool SetProperty(
        RenderCore::LightingEngine::ToneMapAcesOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::MultiSampleOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::SkyOperatorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);

    bool SetProperty(
        RenderCore::LightingEngine::SkyTextureProcessorDesc& desc,
        uint64_t propertyNameHash, IteratorRange<const void*> data, const Utility::ImpliedTyping::TypeDesc& type);
}

