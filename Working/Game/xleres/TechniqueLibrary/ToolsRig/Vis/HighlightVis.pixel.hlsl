// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/EdgeDetection.hlsl"
#include "../../Utility/DistinctColors.hlsl"

cbuffer Settings BIND_NUMERIC_B3
{
	float3 OutlineColour;
	uint HighlightedMarker;
    uint BackgroundMarker;
}

#if !defined(INPUT_MODE)
	#if !defined(VULKAN)
		#define INPUT_MODE 1
	#else
		#define INPUT_MODE 2
	#endif
#endif

#if INPUT_MODE == 0
		// In this mode, we're binding a depth buffer/stencil combo to
		// "StencilInput"
		// Oddly, on some hardware Texture2D<uint> works fine to access
		// the stencil parts (assuming the view was created with X24_TYPELESS_G8_UINT
		// or some similar stencil-only format).
		// But, on other hardware we must explicitly access the "G" channel.
	Texture2D<uint2>	StencilInput BIND_NUMERIC_T0;
#elif INPUT_MODE == 1
 	Texture2D			StencilInput BIND_NUMERIC_T0;
#elif INPUT_MODE == 2
	[[vk::input_attachment_index(0)]] SubpassInput<uint>	StencilInput	 	BIND_NUMERIC_T2;
#endif

static const uint DummyMarker = 0xffffffff;

uint GetHighlightMarker()
{
	return HighlightedMarker;
}

uint Marker(uint2 pos)
{
	#if INPUT_MODE == 0
		#if defined(VULKAN)
			uint result = StencilInput.Load(uint3(pos, 0)).r;
		#else
			uint result = StencilInput.Load(uint3(pos, 0)).g;
		#endif
	#elif INPUT_MODE == 1
		uint result = uint(255.f * StencilInput.Load(uint3(pos, 0)).a);
	#elif INPUT_MODE == 2
		uint result = StencilInput.SubpassLoad();
	#endif
	if (result == BackgroundMarker) { return DummyMarker; }

	#if ONLY_HIGHLIGHTED!=0
			// in this mode, we ignore every material except the highlighted one
		if (result != GetHighlightMarker()) {
			result = DummyMarker;
		}
	#endif
	return result;
}

bool HatchFilter(uint2 position)
{
	uint p = uint(position.x) + uint(position.y);
	return ((p/4) % 3) == 0;
}

float4 HighlightByStencil(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{	
	if (!HatchFilter(position.xy)) { discard; }

	uint marker = Marker(uint2(position.xy));
	if (marker == DummyMarker) { discard; }

	return float4(.5f*GetDistinctFloatColour(marker), .5f*1.f);
}

float4 OutlineByStencil(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	int2 basePos = int2(position.xy);
	uint testMarker = Marker(basePos);

	if (testMarker != DummyMarker) discard;

	float2 dhdp = 0.0.xx;
	[unroll] for (int y=0; y<5; ++y) {
		[unroll] for (int x=0; x<5; ++x) {
			uint marker = Marker(int2(basePos) + 2 * int2(x-2, y-2));
			float value = (marker == testMarker) * 2.0 - 1.0;
			dhdp.x += ScharrHoriz5x5[x][y] * value;
			dhdp.y += ScharrVert5x5[x][y] * value;
		}
	}

	float alpha = max(abs(dhdp.x), abs(dhdp.y));
	alpha = 1.0f - pow(1.0f-alpha, 8.f);
	return float4(alpha * OutlineColour, alpha);
}
