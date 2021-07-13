// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


#include "../../Math/MathConstants.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Framework/CommonResources.hlsl"

cbuffer CircleHighlightParameters
{
	float3 HighlightCenter;
	float Radius;
}

Texture2D_MaybeMS<float>	DepthTexture;
Texture2D					HighlightResource;

float GetLinear0To1Depth(int2 pixelCoords, uint sampleIndex)
{
	float depth = LoadFloat1(DepthTexture, pixelCoords.xy, sampleIndex);
	return NDCDepthToLinear0To1(depth);
}

float3 WorldPositionFromLinear0To1Depth(int2 pixelCoords, uint sampleIndex, float3 viewFrustumVector)
{
	float linear0To1Depth = GetLinear0To1Depth(pixelCoords, sampleIndex);
	return WorldPositionFromLinear0To1Depth(viewFrustumVector, linear0To1Depth);
}

float4 ps_circlehighlight(	float4 position : SV_Position,
							float2 texCoord : TEXCOORD0,
							float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
							SystemInputs sys) : SV_Target0
{
	int2 pixelCoords	= position.xy;

		//	draw a cylindrical highlight around the given position, with
		//	the given radius. The cylinder is always arranged so +Z is up
	float3 worldPosition = LoadWorldPosition(pixelCoords, GetSampleIndex(sys), viewFrustumVector);

	float2 xyOffset = worldPosition.xy - HighlightCenter.xy;
	float r = length(xyOffset);
	if (r > Radius)
		discard;

	//float theta = atan2(xyOffset.y, xyOffset.x);
	//const float wrappingCount = 128;

	//float a = HighlightResource.SampleLevel(DefaultSampler, float2(theta/(2.f*pi)*wrappingCount, lerp(1.f/128.f, 127/128.f, r/Radius)), 0).r;
	//return float4(1.0.xxx*a*.25f, .25f*a);

	float d = fwidth(r/Radius);
	float a3 = 1.f - smoothstep(0., d*2.f, (1.f - r/Radius));
	// float a3 = 1.f - saturate((1.f - r/Radius) / (4.f * d));

	float2 B = float2(worldPosition.xy - HighlightCenter.xy);
	bool hatch = frac((B.x + B.y) / 2.f) < .5f;

	return float4(hatch * 1.0.xxx*a3, hatch?0.5f:0.25f);
}


cbuffer RectangleHighlightParameters
{
	float3 Mins, Maxs;
}

float4 ps_rectanglehighlight(	float4 position : SV_Position,
								float2 texCoord : TEXCOORD0,
								float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
								SystemInputs sys) : SV_Target0
{
	int2 pixelCoords	= position.xy;

	float3 worldPosition = LoadWorldPosition(pixelCoords, GetSampleIndex(sys), viewFrustumVector);

		//	find how this worldPosition relates to the min/max rectangle we're
		//	rendering. The texture we're using was build for circles. But we
		//	can try to map it around the edges of the rectangle, as well.
		//		texCoord.y should be a distance from the edge
		//		texCoord.x should be a parameter for the dotted line

	float distances[4] =
	{
			// (note -- ignoring z)
		worldPosition.x - Mins.x,
		worldPosition.y - Mins.y,
		Maxs.x - worldPosition.x,
		Maxs.y - worldPosition.y
	};
	float minDist = min(min(min(distances[0], distances[1]), distances[2]), distances[3]);
	if (minDist < 0.f)
		discard;

	float2 b2 = float2(
		(worldPosition.x - Mins.x) / (Maxs.x - Mins.x),
		(worldPosition.y - Mins.y) / (Maxs.y - Mins.y));

	float2 d = fwidth(b2);
	float4 A = float4(b2, 1.0.xx-b2);
	float4 a3 = 1.f - smoothstep(0.0.xxxx, d.xyxy*2.f, A);

	float2 B = float2(worldPosition.xy - Mins.xy);
	bool hatch = frac((B.x + B.y) / 4.f) < .5f;

	return float4(hatch * 1.0.xxx*max(max(max(a3.x, a3.y), a3.z), a3.w), hatch?0.5f:0.25f);
}


float4 ps_lockedareahighlight(	float4 position : SV_Position,
								float2 texCoord : TEXCOORD0,
								float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
								SystemInputs sys) : SV_Target0
{
	int2 pixelCoords = position.xy;

	float linear0To1Depth = GetLinear0To1Depth(pixelCoords, GetSampleIndex(sys));
	if (linear0To1Depth >= 0.99999f) discard;
	float3 worldPosition = WorldPositionFromLinear0To1Depth(viewFrustumVector, linear0To1Depth);

	float distances[4] =
		{ 	worldPosition.x - Mins.x, worldPosition.y - Mins.y,
			Maxs.x - worldPosition.x, Maxs.y - worldPosition.y };
	float minDist = min(min(min(distances[0], distances[1]), distances[2]), distances[3]);

	float2 b2 = float2(
		(worldPosition.x - Mins.x) / (Maxs.x - Mins.x),
		(worldPosition.y - Mins.y) / (Maxs.y - Mins.y));

	float2 d = fwidth(b2);
	d = clamp(d, -0.001.xx, 0.001.xx);
	float4 A = float4(b2, 1.0.xx-b2);

	float2 B = float2(worldPosition.xy - Mins.xy);
	bool hatch = frac((B.x + B.y) / 2.f) < .5f;

	float4 c = A / d.xyxy;
	float m = min(min(min(c.x, c.y), c.z), c.w);
	if (m < 0.f && m > -3.f) {
		if (frac(worldPosition.x) < 0.5f && frac(worldPosition.y) < 0.5f)
			return float4(0.f, .25, 1.f, 0.f);
	}

	if (minDist > 0.f)
		discard;

	return float4(0.0.xxx, hatch ? 0.35f : 0.0f);
}
