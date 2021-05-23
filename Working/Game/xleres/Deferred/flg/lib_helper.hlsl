// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "resolveutil.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl" // for LightingScale

#if HAS_SCREENSPACE_AO==1
    Texture2D<float>	AmbientOcclusion : register(t5);
#endif

#if MSAA_SAMPLES > 1
    #define MAYBE_SAMPLE_INDEX , uint sampleIndex
#else
    #define MAYBE_SAMPLE_INDEX
    static const uint sampleIndex = 0;
#endif

export void Setup(
    float4 position,
    float3 viewFrustumVector,
    out float3 worldPosition,
    out float worldSpaceDepth,
    out float screenSpaceOcclusion
    MAYBE_SAMPLE_INDEX
    )
{
    int2 pixelCoords = position.xy;
    worldSpaceDepth = GetWorldSpaceDepth(pixelCoords, sampleIndex);
    worldPosition = SysUniform_GetWorldSpaceView() + (worldSpaceDepth / SysUniform_GetFarClip()) * viewFrustumVector;

    #if HAS_SCREENSPACE_AO==1
        screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, pixelCoords, sampleIndex);
    #else
        screenSpaceOcclusion = 1.f;
    #endif
}

export float4 FinalizeResolve(float3 resolvedLight, float shadow)
{
    return float4((LightingScale*shadow)*resolvedLight, 1.f);
}
