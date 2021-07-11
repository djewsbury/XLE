// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Cubemap.h"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

Texture2D Input : register(t0, space0);
RWTexture2DArray<float4> Output : register(u1, space0);
SamplerState FixedPointSampler : register(s2, space0);

float2 DirectionToEquirectangularCoord(float3 direction, bool hemi)
{
	if (hemi) return DirectionToHemiEquirectangularCoord_YUp(direction);
	return DirectionToEquirectangularCoord_YUp(direction);
}

void Panel(inout float4 result, float2 tc, float2 tcMins, float2 tcMaxs, float3 panel[3], bool hemi)
{
    float3 plusX = panel[0];
    float3 plusY = panel[1];
    float3 center = panel[2];

    if (    tc.x >= tcMins.x && tc.y >= tcMins.y
        &&  tc.x <  tcMaxs.x && tc.y <  tcMaxs.y) {

		result.rgb = 0.0.xxx;
		result.a = 1.f;
		// brute-force filtering! It's kind of silly, but it should be reasonably close to correct
		for (uint y=0; y<32; y++) {
			for (uint x=0; x<32; x++) {
				float2 face;
				face.x = 2.0f * (tc.x + x/32.0f - tcMins.x) / (tcMaxs.x - tcMins.x) - 1.0f;
        		face.y = 2.0f * (tc.y + y/32.0f - tcMins.y) / (tcMaxs.y - tcMins.y) - 1.0f;
				float3 finalDirection = center + plusX * face.x + plusY * face.y;
				finalDirection = normalize(finalDirection);
				float2 finalCoord = DirectionToEquirectangularCoord(finalDirection, hemi);
				result.rgb += Input.SampleLevel(FixedPointSampler, finalCoord, 0).rgb;
			}
		}
		result.rgb /= (32*32);
    }
}

float4 Horizontal(float2 texCoord, bool hemi)
{
	return 0.0.xxxx;
}

float4 Vertical(float2 texCoord, bool hemi)
{
	float4 result = 0.0.xxxx;
	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 0.0f), float2(2.0f/3.0f, 1.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[0], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 1.0f/4.0f), float2(2.0f/3.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[1], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 2.0f/4.0f), float2(2.0f/3.0f, 3.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[2], hemi);

	Panel(
		result,
		texCoord,
		float2(1.0f/3.0f, 3.0f/4.0f), float2(2.0f/3.0f, 1.0f),
		VerticalCrossPanels_CubeMapGen[3], hemi);

	Panel(
		result,
		texCoord,
		float2(0.0f, 1.0f/4.0f), float2(1.0f/3.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[4], hemi);

	Panel(
		result,
		texCoord,
		float2(2.0f/3.0f, 1.0f/4.0f), float2(1.0f, 2.0f/4.0f),
		VerticalCrossPanels_CubeMapGen[5], hemi);
	return result;
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    uint2 dims = uint2(position.xy / texCoord);
    if (dims.x >= dims.y) {
		return Horizontal(texCoord, false);
    } else {
		return Vertical(texCoord, false);
    }
}

float4 hemi(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	uint2 dims = uint2(position.xy / texCoord);
	if (dims.x >= dims.y) {
		return Horizontal(texCoord, true);
	} else {
		return Vertical(texCoord, true);
	}
}

[numthreads(8, 8, 6)]
	void EquRectToCube(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 textureDims; uint arrayLayerCount;
	Output.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
	if (dispatchThreadId.x < textureDims.x && dispatchThreadId.y < textureDims.y) {
		float4 color;
		Panel(
			color,
			dispatchThreadId.xy, 0.0.xx, textureDims.xy,
			CubeMapFaces[dispatchThreadId.z],
			false);
		Output[dispatchThreadId.xyz] = color;
	}
}
