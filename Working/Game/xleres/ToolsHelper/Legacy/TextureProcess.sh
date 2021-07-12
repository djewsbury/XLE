// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Utility/Colour.hlsl"

Texture2D	Input : register(t0);

cbuffer Material
{
    float2 RRoughnessRange;
    float2 RSpecularRange;
    float2 RMetalRange;

    float2 GRoughnessRange;
    float2 GSpecularRange;
    float2 GMetalRange;

    float2 BRoughnessRange;
    float2 BSpecularRange;
    float2 BMetalRange;

    float2 ARoughnessRange;
    float2 ASpecularRange;
    float2 AMetalRange;
}

float4 copy(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    return Input.Load(int3(position.xy, 0));
}

Texture2D DiffuseTexture;
Texture2D SpecularColor;
Texture2D MaterialMask;

struct PerPixelMaterialParam
{
    float   roughness;
    float   specular;
    float   metal;
};

PerPixelMaterialParam PerPixelMaterialParam_Default()
{
    PerPixelMaterialParam result;
    result.roughness = 0.5f;
    result.specular = 0.5f;
    result.metal = 0.f;
    return result;
}

PerPixelMaterialParam DecodeParametersTexture_ColoredSpecular(
    inout float3 diffuseSample, float3 specColorSample,
    float2 roughnessRange, float2 specularRange, float2 metalRange)
{
    PerPixelMaterialParam result = PerPixelMaterialParam_Default();
    float diffuseLum    = saturate(SRGBLuminance(diffuseSample));
    float specLum       = saturate(SRGBLuminance(specColorSample));
    float ratio         = specLum / (diffuseLum + 0.00001f);
    result.metal        = saturate((ratio - 1.1f) / 0.9f);
    result.roughness    = 0.f;
    result.specular     = specLum;

    result.metal        = lerp(metalRange.x, metalRange.y, result.metal);
    result.roughness    = lerp(roughnessRange.x, roughnessRange.y, result.roughness);
    result.specular     = lerp(specularRange.x, specularRange.y, result.specular);

    diffuseSample       = lerp(diffuseSample, specColorSample, result.metal);

    return result;
}

PerPixelMaterialParam ScaleByRange(
    PerPixelMaterialParam input,
    float2 rRange, float2 sRange, float2 mRange)
{
    PerPixelMaterialParam result;
    result.roughness = lerp(rRange.x, rRange.y, input.roughness);
    result.specular = lerp(sRange.x, sRange.y, input.specular);
    result.metal = lerp(mRange.x, mRange.y, input.metal);
    return result;
}

PerPixelMaterialParam Scale(PerPixelMaterialParam input, float factor)
{
    PerPixelMaterialParam result = input;
    result.roughness *= factor;
    result.specular *= factor;
    result.metal *= factor;
    return result;
}

PerPixelMaterialParam Add(PerPixelMaterialParam lhs, PerPixelMaterialParam rhs)
{
    PerPixelMaterialParam result;
    result.roughness = lhs.roughness + rhs.roughness;
    result.specular = lhs.specular + rhs.specular;
    result.metal = lhs.metal + rhs.metal;
    return result;
}

float4 AsParametersTexture(PerPixelMaterialParam param)
{
    return float4(param.roughness, param.specular, param.metal, 1.f);
}

float4 SpecularWithMask(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float4 diffuse = DiffuseTexture.Load(int3(position.xy, 0));
    float4 specColor = SpecularColor.Load(int3(position.xy, 0));
    float4 materialMask = MaterialMask.Load(int3(position.xy, 0));
    materialMask.a = 1.0f - materialMask.a;       // (alpha weight is inverted)

    float3 rDiffuse = diffuse.rgb, gDiffuse = diffuse.rgb, bDiffuse = diffuse.rgb, aDiffuse = diffuse.rgb;
    PerPixelMaterialParam rSample = DecodeParametersTexture_ColoredSpecular(rDiffuse, specColor.rgb, RRoughnessRange, RSpecularRange, RMetalRange);
    PerPixelMaterialParam gSample = DecodeParametersTexture_ColoredSpecular(gDiffuse, specColor.rgb, GRoughnessRange, GSpecularRange, GMetalRange);
    PerPixelMaterialParam bSample = DecodeParametersTexture_ColoredSpecular(bDiffuse, specColor.rgb, BRoughnessRange, BSpecularRange, BMetalRange);
    PerPixelMaterialParam aSample = DecodeParametersTexture_ColoredSpecular(aDiffuse, specColor.rgb, ARoughnessRange, ASpecularRange, AMetalRange);

    float weightSum = materialMask.r + materialMask.g + materialMask.b + materialMask.a;
    return
        AsParametersTexture(
            Add(Scale(rSample, materialMask.r/weightSum),
            Add(Scale(gSample, materialMask.g/weightSum),
            Add(Scale(bSample, materialMask.b/weightSum),
                Scale(aSample, materialMask.a/weightSum)))));
}

float4 DiffuseWithMask(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float4 diffuse = DiffuseTexture.Load(int3(position.xy, 0));
    float4 specColor = SpecularColor.Load(int3(position.xy, 0));
    float4 materialMask = MaterialMask.Load(int3(position.xy, 0));
    materialMask.a = 1.0f - materialMask.a;       // (alpha weight is inverted)

    float3 rDiffuse = diffuse.rgb, gDiffuse = diffuse.rgb, bDiffuse = diffuse.rgb, aDiffuse = diffuse.rgb;
    PerPixelMaterialParam rSample = DecodeParametersTexture_ColoredSpecular(rDiffuse, specColor.rgb, RRoughnessRange, RSpecularRange, RMetalRange);
    PerPixelMaterialParam gSample = DecodeParametersTexture_ColoredSpecular(gDiffuse, specColor.rgb, GRoughnessRange, GSpecularRange, GMetalRange);
    PerPixelMaterialParam bSample = DecodeParametersTexture_ColoredSpecular(bDiffuse, specColor.rgb, BRoughnessRange, BSpecularRange, BMetalRange);
    PerPixelMaterialParam aSample = DecodeParametersTexture_ColoredSpecular(aDiffuse, specColor.rgb, ARoughnessRange, ASpecularRange, AMetalRange);

    float weightSum = materialMask.r + materialMask.g + materialMask.b + materialMask.a;
    float4 result = float4(
              (rDiffuse * materialMask.r/weightSum)
            + (gDiffuse * materialMask.g/weightSum)
            + (bDiffuse * materialMask.b/weightSum)
            + (aDiffuse * materialMask.a/weightSum),
            diffuse.a);
    return float4(LinearToSRGB(result.rgb), result.a);
}
