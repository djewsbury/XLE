// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)


    //
    //  Simple utility functions for rendering debugging panels
    //  (often used in debugging shaders for displaying textures)
    //

#if !defined(DEBUGGING_PANELS_H)
#define DEBUGGING_PANELS_H

#include "../Math/TextureAlgorithm.hlsl"

bool GetTileCoords(float2 minCoords, float2 maxCoords, float2 texCoord, inout float2 tc)
{
	const float border = 0.0125f;
	if (texCoord.x >= minCoords.x + border && texCoord.x <= maxCoords.x - border.x) {
		if (texCoord.y >= minCoords.y + border && texCoord.y <= maxCoords.y - border) {
			tc = float2(
				(texCoord.x - (minCoords.x + border)) / (maxCoords.x-minCoords.x-2*border),
				(texCoord.y - (minCoords.y + border)) / (maxCoords.y-minCoords.y-2*border));
			return true;
		}
	}
	return false;
}

void RenderTileBorder(float2 minCoords, float2 maxCoords, float2 texCoord, inout float4 result)
{
	const float border = 0.0125f;
	const float edge = 0.001f;
	if (	(texCoord.x >= minCoords.x + border - edge)
		&&	(texCoord.x <= maxCoords.x - border + edge)
		&&  (texCoord.y >= minCoords.y + border - edge)
		&&	(texCoord.y <= maxCoords.y - border + edge)) {

		if (	texCoord.x < minCoords.x + border
			||	texCoord.x > maxCoords.x - border
			||	texCoord.y < minCoords.y + border
			||	texCoord.y > maxCoords.y - border) {

			result = float4(1.f, 0.5f, 0.25f, 1.f);
		}
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2D<float4> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions;
		tex.GetDimensions(dimensions.x, dimensions.y);
		const int sampleIndex = 0;
		result = float4(LoadFloat4(tex, tc*dimensions, sampleIndex).rgb, 1.f);
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2D<float> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions;
		tex.GetDimensions(dimensions.x, dimensions.y);
		const int sampleIndex = 0;
		result = float4(LoadFloat1(tex, tc*dimensions, sampleIndex).rrr, 1.f);
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2DMS<float4> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions; uint samples;
		tex.GetDimensions(dimensions.x, dimensions.y, samples);
		const int sampleIndex = 0;
		result = float4(LoadFloat4(tex, tc*dimensions, sampleIndex).rgb, 1.f);
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2DMS<float> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions; uint samples;
		tex.GetDimensions(dimensions.x, dimensions.y, samples);
		const int sampleIndex = 0;
		result = float4(LoadFloat1(tex, tc*dimensions, sampleIndex).rrr, 1.f);
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2D<uint> tex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint2 dimensions;
		tex.GetDimensions(dimensions.x, dimensions.y);
		const int sampleIndex = 0;
		result = float4(asfloat(tex.Load(int3(tc*dimensions, 0))).rrr, 1.f);
	}
}

void RenderTile(float2 minCoords, float2 maxCoords, float2 texCoord, Texture2DArray<float> tex, uint arrayIndex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc)) {
		uint3 dimensions;
		tex.GetDimensions(dimensions.x, dimensions.y, dimensions.z);
		const int sampleIndex = 0;
		result = float4(tex.Load(int4(tc*dimensions.xy, arrayIndex, 0)).rrr, 1.f);
	}
}

interface ITextureLoader
{
	float4 Load(float2 coords, uint arrayIndex, uint sampleIndex);
};

void RenderTile(
	float2 minCoords, float2 maxCoords, float2 texCoord,
	ITextureLoader loader, uint arrayIndex, inout float4 result)
{
	RenderTileBorder(minCoords, maxCoords, texCoord, result);
	float2 tc;
	const int sampleIndex = 0;
	if (GetTileCoords(minCoords, maxCoords, texCoord, tc))
		result = loader.Load(tc, arrayIndex, sampleIndex);
}


#endif
