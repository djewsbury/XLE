// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(COMMON_BRUSHES_H)
#define COMMON_BRUSHES_H

#include "Interfaces.hlsl"
#include "../../Framework/CommonResources.hlsl"

float4 SolidFill_Calculate(DebuggingShapesCoords coords, float4 baseColor, float2 dhdp) { return baseColor; }
float4 NoFill_Calculate(DebuggingShapesCoords coords, float4 baseColor, float2 dhdp) { return 0.0.xxxx; }

float4 CrossHatchFill_Calculate(
    DebuggingShapesCoords coords,
    float4 baseColor,
    float2 dhdp)
{
    float4 color = baseColor;

        // cross hatch pattern -- (bright:dark == 1:1)
    uint p = uint(coords.position.x) + uint(coords.position.y);
    if (((p/4) % 2) != 0) {
        color.rgb *= 0.66f;
    }

    return color;
}

Texture2D RefractionsBuffer : register(t12);

static const float SqrtHalf = 0.70710678f;
static const float3 BasicShapesLightDirection = normalize(float3(4.f*SqrtHalf, SqrtHalf, -1.25f));
// static const float3 BasicShapesLightDirection = normalize(float3(0,0, -1.f));

float3 NormalToSurface(float2 dhdp)
{
    float3 u = float3(1.f, 0.f, dhdp.x);
    float3 v = float3(0.f, 1.f, dhdp.y);
    // return normalize(cross(u, v));
    // cross product:
    // float3(u.y*v.z - u.z*v.y, u.z*v.x - u.x*v.z, u.x*v.y - u.y*v.x)
    return normalize(float3(-dhdp.x, -dhdp.y, 1.f));
}

float4 RaisedRefractiveFill_Calculate(
    DebuggingShapesCoords coords,
    float4 baseColor,
    float2 dhdp)
{
    float3 normal = NormalToSurface(dhdp);

    float d = saturate(-dot(BasicShapesLightDirection, normal));
    float A = 7.5f * pow(d, 2.f);

    float3 result = A * baseColor.rgb + 0.1.xxx;

    result.rgb += RefractionsBuffer.SampleLevel(ClampingSampler, GetRefractionCoords(coords), 0).rgb;
    return float4(result.rgb, 1.f);
}

float4 RaisedFill_Calculate(
    DebuggingShapesCoords coords,
    float4 baseColor,
    float2 dhdp)
{
    float3 normal = NormalToSurface(dhdp);
    float d = saturate(-dot(BasicShapesLightDirection, normal));
    return float4(d * baseColor.rgb, 1.f);
}

#endif
