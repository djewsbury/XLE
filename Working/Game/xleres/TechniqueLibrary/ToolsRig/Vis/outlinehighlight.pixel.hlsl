// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/EdgeDetection.hlsl"
#include "../../Framework/Binding.hlsl"

cbuffer Settings BIND_NUMERIC_B0
{
	float3 OutlineColour;
}
Texture2D InputTexture BIND_NUMERIC_T0;

float4 main(float4 pos : SV_Position) : SV_Target0
{
		//	using a sharr convolution to find the edges of the shapes in "InputTexture",
		//	we'll write out an outline highlight
		//
		//	This is not the most efficient way to do this... The sharr filter should be
		//	separable. But this implementation was intended for non-performance critical
		//	purposes, and convenient to do this it this way.

	int2 basePos = int2(pos.xy);
	if (InputTexture.Load(int3(basePos, 0)).a > 0.01f) discard;

	float2 dhdp = 0.0.xx;
	for (int y=0; y<5; ++y) {
		for (int x=0; x<5; ++x) {
				//	We need to convert the input color values to some
				//	kind of scalar. We could use luminance. But if we
				//	just want to find the outline of a rendered area,
				//	then we can use the alpha channel
			float value = InputTexture.Load(int3(basePos + 2*int2(x-2, y-2), 0)).a > 0.01f;
			dhdp.x += SharrHoriz5x5[x][y] * value;
			dhdp.y += SharrVert5x5[x][y] * value;
		}
	}

	if (frac((basePos.x + basePos.y) / 8.f) < 0.5f) discard;

	float alpha = max(abs(dhdp.x), abs(dhdp.y));
	alpha = 1.0f - pow(1.0f-alpha, 8.f);
	return float4(alpha * OutlineColour, alpha);
}

cbuffer ShadowHighlightSettings BIND_NUMERIC_B0
{
	float4 ShadowColor;
}

float4 main_shadow(float4 pos : SV_Position) : SV_Target0
{
		//	using a sharr convolution to find the edges of the shapes in "InputTexture",
		//	we'll write out an outline highlight
		//
		//	This is not the most efficient way to do this... The sharr filter should be
		//	separable. But this implementation was intended for non-performance critical
		//	purposes, and convenient to do this it this way.

	int2 basePos = int2(pos.xy);
	if (frac((basePos.x + basePos.y) / 4.f) < 0.5f) discard;

	float alpha = InputTexture.Load(int3(basePos.xy, 0)).a;
	// return ShadowColor * alpha;
	return ShadowColor * (alpha > 0.01f);
}
