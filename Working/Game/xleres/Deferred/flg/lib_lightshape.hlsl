// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/SceneEngine/Lighting/LightShapes.hlsl"
#include "../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../TechniqueLibrary/Framework/Binding.hlsl"

#if MSAA_SAMPLES > 1
    #define MAYBE_SAMPLE_INDEX , uint sampleIndex
#else
    #define MAYBE_SAMPLE_INDEX
    static const uint sampleIndex = 0;
#endif

// We have to careful about constant buffer assignments
// Our system doesn't support cases where multiple libraries used
// by the same shader have different constant buffers (or resources)
// bound to the same slot.
// For example, another library might have GlobalTransform bound to
// constant buffer slot 0. So we have to have move LightBuffer to
// a free slot.
cbuffer LightBuffer BIND_MAT_B1
{
    LightDesc Light;
}

LightSampleExtra MakeLightSampleExtra(float screenSpaceOcclusion)
{
    LightSampleExtra sampleExtra;
    sampleExtra.screenSpaceOcclusion = screenSpaceOcclusion;
    return sampleExtra;
}

export float3 DoResolve_Directional(
    float4 position, float3 viewFrustumVector,
    float3 worldPosition,
    float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    SystemInputs sys = SystemInputs_SampleIndex(sampleIndex);

    int2 pixelCoords = position.xy;
    GBufferValues sample = LoadGBuffer(position.xy, sys);

    LightSampleExtra sampleExtra = MakeLightSampleExtra(screenSpaceOcclusion);
    LightScreenDest screenDest = LightScreenDest_Create(pixelCoords, GetSampleIndex(sys));

    return DirectionalLightResolve(sample, sampleExtra, Light, worldPosition, normalize(-viewFrustumVector), screenDest);
}

export float3 DoResolve_Sphere(
    float4 position, float3 viewFrustumVector,
    float3 worldPosition,
    float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    SystemInputs sys = SystemInputs_SampleIndex(sampleIndex);

    int2 pixelCoords = position.xy;
    GBufferValues sample = LoadGBuffer(position.xy, sys);

    LightSampleExtra sampleExtra = MakeLightSampleExtra(screenSpaceOcclusion);
    LightScreenDest screenDest = LightScreenDest_Create(pixelCoords, GetSampleIndex(sys));

    return SphereLightResolve(sample, sampleExtra, Light, worldPosition, normalize(-viewFrustumVector), screenDest);
}

export float3 DoResolve_Tube(
    float4 position, float3 viewFrustumVector,
    float3 worldPosition,
    float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    SystemInputs sys = SystemInputs_SampleIndex(sampleIndex);

    int2 pixelCoords = position.xy;
    GBufferValues sample = LoadGBuffer(position.xy, sys);

    LightSampleExtra sampleExtra = MakeLightSampleExtra(screenSpaceOcclusion);
    LightScreenDest screenDest = LightScreenDest_Create(pixelCoords, GetSampleIndex(sys));

    return TubeLightResolve(sample, sampleExtra, Light, worldPosition, normalize(-viewFrustumVector), screenDest);
}

export float3 DoResolve_Rectangle(
    float4 position, float3 viewFrustumVector,
    float3 worldPosition,
    float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    SystemInputs sys = SystemInputs_SampleIndex(sampleIndex);

    int2 pixelCoords = position.xy;
    GBufferValues sample = LoadGBuffer(position.xy, sys);

    LightSampleExtra sampleExtra = MakeLightSampleExtra(screenSpaceOcclusion);
    LightScreenDest screenDest = LightScreenDest_Create(pixelCoords, GetSampleIndex(sys));

    return RectangleLightResolve(sample, sampleExtra, Light, worldPosition, normalize(-viewFrustumVector), screenDest);
}

export float3 DoResolve_Disc(
    float4 position, float3 viewFrustumVector,
    float3 worldPosition,
    float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    return 0.0.xxx;
}
