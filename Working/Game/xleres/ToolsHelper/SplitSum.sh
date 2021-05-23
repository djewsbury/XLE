// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define FORCE_GGX_REF

#include "Cubemap.h"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/MaterialQuery.hlsl"

Texture2D Input;

float3 IBLPrecalc_SampleInputTexture(float3 direction)
{
    float2 coord = DirectionToEquirectangularCoord_YUp(direction);
    return Input.SampleLevel(DefaultSampler, coord, 0).rgb;
}

#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

static const uint PassSampleCount = 256;

cbuffer SubResourceId
{
    uint ArrayIndex, MipIndex;
    uint PassIndex, PassCount;
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float2 dims = position.xy / texCoord.xy;
    float NdotV = texCoord.x + (.1/dims.x);  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;
    return float4(GenerateSplitTerm(NdotV, roughness, sampleCount, 0, 1), 0, 1);
}

float4 main_trans(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float NdotV = texCoord.x;
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;

    float specular = saturate(0.05f + ArrayIndex / 32.f);
    float iorIncident = F0ToRefractiveIndex(Material_SpecularToF0(specular));
    float iorOutgoing = 1.f;
    return float4(GenerateSplitTermTrans(NdotV, roughness, iorIncident, iorOutgoing, sampleCount, 0, 1), 0, 0, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float4 EquiRectFilterGlossySpecular(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    // This is the second term of the "split-term" solution for IBL glossy specular
    // Here, we prefilter the reflection texture in such a way that the blur matches
    // the GGX equation.
    //
    // This is very similar to calculating the full IBL reflections. However, we're
    // making some simplifications to make it practical to precalculate it.
    // We can choose to use an importance-sampling approach. This will limit the number
    // of samples to some fixed amount. Alternatively, we can try to sample the texture
    // it some regular way (ie, by sampling every texel instead of just the ones suggested
    // by importance sampling).
    //
    // If we sample every pixel we need to weight by the solid angle of the texel we're
    // reading from. But if we're just using the importance sampling approach, we can skip
    // this step (it's just taken care of by the probability density function weighting)

    float3 cubeMapDirection = CalculateCubeMapDirection(ArrayIndex, texCoord);
    float roughness = MipmapToRoughness(MipIndex);
    float3 r = GenerateFilteredSpecular(cubeMapDirection, roughness, PassSampleCount, PassIndex, PassCount);
    return float4(r / float(PassCount), 1.f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float4 EquiRectFilterGlossySpecularTrans(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    // Following the simplifications we use for split-sum specular reflections, here
    // is the equivalent sampling for specular transmission
    float3 cubeMapDirection = CalculateCubeMapDirection(ArrayIndex, texCoord);
    float roughness = MipmapToRoughness(MipIndex);
    float iorIncident = SpecularTransmissionIndexOfRefraction;
    float iorOutgoing = 1.f;
    float3 r = CalculateFilteredTextureTrans(
        cubeMapDirection, roughness,
        iorIncident, iorOutgoing,
        PassSampleCount, PassIndex, PassCount);
    return float4(r / float(PassCount), 1.f);
}
