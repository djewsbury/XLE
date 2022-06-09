// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/LightingEngine/ShadowProjection.hlsl"
#include "../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowsResolve.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////

#if MSAA_SAMPLES > 1
    #define MAYBE_SAMPLE_INDEX , uint sampleIndex
#else
    #define MAYBE_SAMPLE_INDEX
    static const uint sampleIndex = 0;
#endif

export float DoResolve_ShadowResolver_PoissonDisc(
    float4  frustumCoordinates,
    int     cascadeIndex,
    float4  miniProjection,
    float2  pixelCoords
    MAYBE_SAMPLE_INDEX
    )
{
    ShadowResolveConfig config = ShadowResolveConfig_Default();
    config._filteringMode = SHADOW_FILTER_MODEL_POISSONDISC;
    return ResolveShadows_Cascade(
        cascadeIndex, frustumCoordinates, miniProjection,
        int2(pixelCoords.xy), sampleIndex,
        config);
}

export float DoResolve_ShadowResolver_Smooth(
    float4  frustumCoordinates,
    int     cascadeIndex,
    float4  miniProjection,
    float2  pixelCoords
    MAYBE_SAMPLE_INDEX
    )
{
    ShadowResolveConfig config = ShadowResolveConfig_Default();
    config._filteringMode = SHADOW_FILTER_MODEL_SMOOTH;
    return ResolveShadows_Cascade(
        cascadeIndex, frustumCoordinates, miniProjection,
        int2(pixelCoords.xy), sampleIndex,
        config);
}

export float DoResolve_ShadowResolver_None(
    float4  frustumCoordinates,
    int     cascadeIndex,
    float4  miniProjection,
    float2  pixelCoords
    MAYBE_SAMPLE_INDEX
    )
{
    return 1.f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

static const bool ResolveCascadeByWorldPosition = false;

void InternalCascadeResolve(
    float3 worldPosition, float2 camXY, float worldSpaceDepth,
    out float4  frustumCoordinates,
    out int     cascadeIndex,
    out float4  miniProjection,
    uint cascadeMode, bool enableNearCascade)
{
    CascadeAddress adr;
    if (ResolveCascadeByWorldPosition == true) {
        adr = ResolveCascade_FromWorldPosition(worldPosition);
    } else {
        adr = ResolveCascade_CameraToShadowMethod(camXY, worldSpaceDepth);
    }
    frustumCoordinates = adr.frustumCoordinates;
    cascadeIndex = adr.cascadeIndex;
    miniProjection = adr.miniProjection;
}

export void DoResolve_CascadeResolver_Orthogonal(
    float3 worldPosition, float2 camXY, float worldSpaceDepth,
    out float4  frustumCoordinates,
    out int     cascadeIndex,
    out float4  miniProjection)
{
    InternalCascadeResolve(
        worldPosition, camXY, worldSpaceDepth,
        frustumCoordinates, cascadeIndex, miniProjection,
        SHADOW_CASCADE_MODE_ORTHOGONAL, false);
}

export void DoResolve_CascadeResolver_OrthogonalWithNear(
    float3 worldPosition, float2 camXY, float worldSpaceDepth,
    out float4  frustumCoordinates,
    out int     cascadeIndex,
    out float4  miniProjection)
{
    InternalCascadeResolve(
        worldPosition, camXY, worldSpaceDepth,
        frustumCoordinates, cascadeIndex, miniProjection,
        SHADOW_CASCADE_MODE_ORTHOGONAL, true);
}

export void DoResolve_CascadeResolver_Arbitrary(
    float3 worldPosition, float2 camXY, float worldSpaceDepth,
    out float4  frustumCoordinates,
    out int     cascadeIndex,
    out float4  miniProjection)
{
    InternalCascadeResolve(
        worldPosition, camXY, worldSpaceDepth,
        frustumCoordinates, cascadeIndex, miniProjection,
        SHADOW_CASCADE_MODE_ARBITRARY, false);
}

export void DoResolve_CascadeResolver_None(
    float3 worldPosition, float2 camXY, float worldSpaceDepth,
    out float4  frustumCoordinates,
    out int     cascadeIndex,
    out float4  miniProjection)
{
    CascadeAddress invalid = CascadeAddress_Invalid();
    frustumCoordinates = invalid.frustumCoordinates;
    cascadeIndex = invalid.cascadeIndex;
    miniProjection = invalid.miniProjection;
}
