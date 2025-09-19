// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(COMMON_BRUSHES_H)
#define COMMON_BRUSHES_H

#include "Interfaces.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"

float4 SolidFill_Calculate(float4 fillColor : COLOR0) : Fill { return fillColor; }
float4 NoFill_Calculate() : Fill { return 0.0.xxxx; }

float4 SolidOutline_Calculate(float4 outlineColor : COLOR1) : Outline { return outlineColor; }
float4 NoOutline_Calculate() : Outline { return 0.0.xxxx; }

float4 CrossHatchFill_Calculate(
    float4 position : SV_Position,
    float4 baseColor : COLOR) : Fill
{
    float4 color = baseColor;

        // cross hatch pattern -- (bright:dark == 1:1)
    uint p = uint(position.x) + uint(position.y);
    if (((p/4) % 2) != 0) {
        color.rgb *= 0.66f;
    }

    return color;
}

Texture2D RefractionsBuffer : register(t12);

static const float SqrtHalf = 0.70710678f;
static const float3 BasicShapesLightDirection = normalize(float3(4.f*SqrtHalf, 2.*SqrtHalf, -1.25f));
static const float3 BasicShapesReverseLightDirection = normalize(float3(-4.f*SqrtHalf, -2.*SqrtHalf, -1.25f));

float3 NormalToSurface(float2 dhdp)
{
    float3 u = float3(1.f, 0.f, dhdp.x);
    float3 v = float3(0.f, 1.f, dhdp.y);
    // return normalize(cross(u, v));
    // cross product:
    // float3(u.y*v.z - u.z*v.y, u.z*v.x - u.x*v.z, u.x*v.y - u.y*v.x)
    return normalize(float3(-dhdp.x, -dhdp.y, 1.f));
}

float2 GetRefractionCoords(float4 position, float2 outputDimensions) { return position.xy/outputDimensions.xy; }

float4 RaisedRefractiveFill_Calculate(
    float4 position : SV_Position,
    float2 outputDimensions : OutputDimensions,
    float4 baseColor : COLOR0,
    float2 dhdp : DHDP) : Fill
{
    float3 normal = NormalToSurface(dhdp);

    float d = saturate(-dot(BasicShapesLightDirection, normal));
    float A = 7.5f * pow(d, 2.f);

    float3 result = A * baseColor.rgb + 0.1.xxx;

    result.rgb += RefractionsBuffer.SampleLevel(ClampingSampler, GetRefractionCoords(position, outputDimensions), 0).rgb;
    return float4(result.rgb, 1.f);
}

float4 RaisedFill_Calculate(
    float4 baseColor : COLOR0,
    float2 dhdp : DHDP) : Fill
{
    float accentuate = 8.f;
    float3 normal = NormalToSurface(accentuate*dhdp);
    float d = saturate(-dot(BasicShapesLightDirection, normal));
    return float4(d * baseColor.rgb, 1.f);
}

float4 ReverseRaisedFill_Calculate(
    float4 baseColor : COLOR0,
    float2 dhdp : DHDP) : Fill
{
    float accentuate = 8.f;
    float3 normal = NormalToSurface(accentuate*dhdp);
    float d = saturate(-dot(BasicShapesReverseLightDirection, normal));
    return float4(d * baseColor.rgb, 1.f);
}

float4 DashLine_Calculate(float2 baseTex : TEXCOORD0, float4 baseColor : COLOR0) : Fill
{
    const float sectionLength = 12;
    float sectionCoord = frac(baseTex.x / sectionLength);
    if (sectionCoord < 0.9f) {
        return baseColor;
    } else {
        return 0;
    }
}

cbuffer ColorAdjustSettings
{
    float SaturationMultiplier;
    float LuminanceOffset;
}

Texture2D DiffuseTexture;

float3 DoColorAdjust3(float3 input)
{
    float luminance = SRGBLuminance(input);
    const float saturationMultiplier = SaturationMultiplier;
    const float luminanceOffset = LuminanceOffset;
    return float3(
        luminanceOffset + ((1-luminanceOffset) * lerp(luminance, input.x, saturationMultiplier)),
        luminanceOffset + ((1-luminanceOffset) * lerp(luminance, input.y, saturationMultiplier)),
        luminanceOffset + ((1-luminanceOffset) * lerp(luminance, input.z, saturationMultiplier)));
}

float4 ColorAdjust_Calculate(float2 baseTex : TEXCOORD0, float4 baseColor : COLOR0) : Fill
{
    float4 texColor = DiffuseTexture.SampleLevel(ClampingSampler, baseTex, 0);
    return float4(DoColorAdjust3(texColor.rgb), texColor.a) * baseColor;
}

#endif

