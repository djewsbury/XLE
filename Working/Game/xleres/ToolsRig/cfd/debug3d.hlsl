// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/DistinctColors.hlsl"
#include "../../Framework/CommonResources.hlsl"

Texture3D<float>	Field0 : register(t0);
Texture3D<float>	Field1 : register(t1);
Texture3D<float>	Field2 : register(t2);

cbuffer Constants
{
    float MinValue, MaxValue;
}

float3 GradColor(float input)
{
    input = saturate(input);
    const uint terraceCount = 15;
    float A = input * float(terraceCount);
    uint t = min(uint(floor(A)), terraceCount-1);
    return lerp(GetDistinctFloatColour(t), GetDistinctFloatColour(t+1), frac(A));
}

float3 As3DCoords(float2 input)
{
    const uint tilesX = 4;
    const uint tilesY = 4;
    input *= float2(tilesX, tilesY);
    float tileIndex = floor(input.y)*float(tilesX)+floor(input.x);
    uint3 dims;
    Density.GetDimensions(dims.x, dims.y, dims.z);
    return float3(frac(input), tileIndex/float(dims.z));
}

float4 ps_scalarfield(float4 position : SV_Position, float2 coords : TEXCOORD0) : SV_Target0
{
    float value = Field0.SampleLevel(ClampingSampler, As3DCoords(coords), 0);
    value = (value - MinValue) / (MaxValue - MinValue);
    return float4(GradColor(value), 1.f);
}

float4 ps_vectorfield(float4 position : SV_Position, float2 coords : TEXCOORD0) : SV_Target0
{
    float3 velocity = float3(
        Field0.SampleLevel(ClampingSampler, As3DCoords(coords), 0),
        Field1.SampleLevel(ClampingSampler, As3DCoords(coords), 0),
        Field2.SampleLevel(ClampingSampler, As3DCoords(coords), 0));
    float3 v = saturate((velocity/1.0f + 1.0.xxx) * 0.5.xxx);

    return float4(v, 1.f);
}
