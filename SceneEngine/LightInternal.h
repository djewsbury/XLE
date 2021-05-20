// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/LightingEngine/LightDesc.h"
#include "../RenderCore/LightingEngine/ShadowUniforms.h"
#include "../Core/Types.h"

namespace RenderCore { class SharedPkt; class IResource; class IResourceView; class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IPipelineAcceleratorPool; }}

namespace SceneEngine
{
    using namespace RenderCore::LightingEngine;

    void BuildShadowConstantBuffers(
        CB_ArbitraryShadowProjection& arbitraryCBSource,
        CB_OrthoShadowProjection& orthoCBSource,
        const MultiProjection<MaxShadowTexturesPerLight>& desc);

    RenderCore::SharedPkt BuildScreenToShadowConstants(
        const PreparedShadowFrustum& preparedFrustum, 
        const Float4x4& cameraToWorld, 
        const Float4x4& cameraToProjection);
    RenderCore::SharedPkt BuildScreenToShadowConstants(
        unsigned frustumCount,
        const CB_ArbitraryShadowProjection& arbitraryCB,
        const CB_OrthoShadowProjection& orthoCB,
        const Float4x4& cameraToWorld,
        const Float4x4& cameraToProjection);

    class ICompiledShadowGenerator;
    std::shared_ptr<ICompiledShadowGenerator> CreateCompiledShadowGenerator(const ShadowOperatorDesc&, const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>&);

#if 0
    void BindShadowsForForwardResolve(
        RenderCore::Metal::DeviceContext& metalContext,
        RenderCore::Techniques::ParsingContext& parsingContext,
        const PreparedDMShadowFrustum& dominantLight);

    void UnbindShadowsForForwardResolve(
        RenderCore::Metal::DeviceContext& metalContext,
        RenderCore::Techniques::ParsingContext& parsingContext);
#endif

}