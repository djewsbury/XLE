// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../RenderOverlays/DebuggingPanels.hlsl"

Texture2D<uint> InputTexture0 : register(t0);
Texture2D<uint> InputTexture1 : register(t1);
Texture2D<uint> InitialsTexture0 : register(t2);
Texture2D<uint> InitialsTexture1 : register(t3);

float4 main( float4 position : SV_Position, float2 texCoord : TEXCOORD0 ) : SV_Target0
{
	//uint2 pixelCoord = uint2(position.xy);
	//
	//if (	pixelCoord.x >= 64 && pixelCoord.y >= 64
	//	&&	pixelCoord.x < (512+64) && pixelCoord.y < (512+64)) {
	//	return float4(
	//		asfloat(InitialsTexture0[uint2(pixelCoord.x-64, pixelCoord.y-64)]), 
	//		asfloat(InitialsTexture1[uint2(pixelCoord.x-64, pixelCoord.y-64)]), 
	//		0.f, 1.f);
	//}


	float4 result = float4(0.0.xxx, 0.f);
	RenderTile(float2( 0,  0), float2(.5, .5), texCoord, InitialsTexture0, result);
	RenderTile(float2(.5,  0), float2( 1, .5), texCoord, InitialsTexture1, result);
	return result;
}

Texture2D<float4> InputTextureRGBA;

void copy(	float2 texCoord			: TEXCOORD0		,
			float4 position			: SV_Position	,
			out uint oRealPart		: SV_Target0	,
			out uint oImaginaryPart : SV_Target1	)
{
	oRealPart		 = asuint(InputTextureRGBA[uint2(position.xy)].r);
	oImaginaryPart	 = asuint(float(0.f));
}

Texture1D<float> WaterHeights;
Texture1D<float> FixedSurfaceHeights;

float4 ShallowWaterDebugging(  float4 position : SV_Position, float2 texCoord : TEXCOORD0 ) : SV_Target0
{
	float waterHeight	= WaterHeights.SampleLevel(ClampingSampler, texCoord.x, 0);
	float surfaceHeight = FixedSurfaceHeights.SampleLevel(ClampingSampler, texCoord.x, 0);

	float s = 1.f - surfaceHeight / 30.f;
	float w = 1.f - waterHeight / 30.f;

	if (texCoord.y > s) {
		return float4(1.f, 1.f, 1.f, 0.5f);
	} else if (texCoord.y > w) {
		return float4(0.15f, 0.25f, 0.75f, 0.5f);
	} else {
		return 0.0.xxxx;
	}
}

