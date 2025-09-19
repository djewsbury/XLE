// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../RenderOverlays/Legacy/DebuggingPanels.hlsl"

struct LuminanceBufferStruct
{
	float	_currentLuminance;
	float	_prevLuminance;
};


void RenderTile_LogValues(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2D<float> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions;
		tex.GetDimensions(dimensions.x, dimensions.y);
		const int sampleIndex = 0;
		result = float4(exp(LoadFloat1(tex, tc*dimensions, sampleIndex).rrr), 1.f);
	}
}

StructuredBuffer<LuminanceBufferStruct> LuminanceBuffer : register(t0);
Texture2D<float>	LuminanceTexture0	: register(t1);
Texture2D<float>	LuminanceTexture1	: register(t2);
Texture2D<float>	LuminanceTexture2	: register(t3);
Texture2D			BloomTexture0		: register(t4);
Texture2D			BloomTexture1		: register(t5);
Texture2D			BloomTexture2		: register(t6);

float4 HDRDebugging(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float4 result = float4(0.0.xxx, 0.f);
	RenderTile_LogValues(float2( 0,  0), float2(.5f, .5f), texCoord, LuminanceTexture0, result);
	RenderTile_LogValues(float2( .3f,  .05f), float2(.45f, .2f), texCoord, LuminanceTexture1, result);
	RenderTile_LogValues(float2( .3f,  .3f), float2(.45f, .45f), texCoord, LuminanceTexture2, result);

	RenderTile(float2(.5f, 0.f), float2(1.f, .5f), texCoord, BloomTexture0, result);
	RenderTile(float2(.8f, .05f), float2(.95f, .2f), texCoord, BloomTexture1, result);
	RenderTile(float2(.8f, .3f), float2(.95f, .45f), texCoord, BloomTexture2, result);

	return result;
}

struct VSOUT
{
	float2	topLeft : TOPLEFT;
	float2	bottomRight : BOTTOMRIGHT;
	float	value : VALUE;
};

VSOUT LuminanceValue(uint vertexId : SV_VertexId)
{
		//	this is a vertex shader that should feed into
		//	the metric render geometry shader.
	const float lineHeight = 64.f/2.f;
	VSOUT output;
	output.topLeft = float2(64.f, lineHeight*float(2+vertexId));
	output.bottomRight = float2(256.f+64.f, lineHeight*float(5+vertexId));

	output.value = LuminanceBuffer[0]._currentLuminance;

	return output;
}


Texture2D<float>	AmbientOcclusionBuffer	: register(t0);

float4 AODebugging(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float4 result = float4(0.0.xxx, 0.f);
	RenderTile(float2( 0,  0), float2(.5f, .5f), texCoord, AmbientOcclusionBuffer, result);
	return result;
}
