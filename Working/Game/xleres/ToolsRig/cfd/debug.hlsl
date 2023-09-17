// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/DistinctColors.hlsl"
#include "../../Framework/CommonResources.hlsl"

Texture2D<float>	Field0 : register(t0);
Texture2D<float>	Field1 : register(t1);

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

float4 ps_scalarfield(float4 position : SV_Position, float2 coords : TEXCOORD0) : SV_Target0
{
    float value = Field0.SampleLevel(ClampingSampler, coords, 0);
    value = (value - MinValue) / (MaxValue - MinValue);
    return float4(GradColor(value), 1.f);
}

float4 ps_vectorfield(float4 position : SV_Position, float2 coords : TEXCOORD0) : SV_Target0
{
    float2 velocity = float2(
        Field0.SampleLevel(ClampingSampler, coords, 0),
        Field1.SampleLevel(ClampingSampler, coords, 0));
    float2 v = saturate((velocity/1.0f + 1.0.xx) * 0.5.xx);

    return float4(v, 0.f, 1.f);
}
