// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Cubemap.h"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

Texture2D Input;
SamplerState DefaultSampler;

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

        tc.x = 2.0f * (tc.x - tcMins.x) / (tcMaxs.x - tcMins.x) - 1.0f;
        tc.y = 2.0f * (tc.y - tcMins.y) / (tcMaxs.y - tcMins.y) - 1.0f;

        float3 finalDirection = center + plusX * tc.x + plusY * tc.y;
		finalDirection = normalize(finalDirection);
        float2 finalCoord = DirectionToEquirectangularCoord(finalDirection, hemi);

		// note -- 	There isn't a 1:1 relationship between input pixel and output
		// 			pixel. The solid angle of the cubemap pixel and the solid
		//			angle of the equirectangular input pixel changes...!
		//			We can a better result by sampling in a regular pattern over
		//			the full range of the output pixel...
		//			Note that we could build mipmaps in this way as well. it
		//			should give a correctly balanced result, and it would mean
		//			that the mip maps are built from the original texture, not
		//			some derived texture
        result.rgb = Input.SampleLevel(DefaultSampler, finalCoord, 0);
        result.a = 1.f;
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
