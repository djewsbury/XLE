// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebuggingShapes.hlsl"
#include "../../Framework/SystemUniforms.hlsl"

float4 ScrollBarShader(
	float4 position,
	float4 color,
	float4 color1,
	float2 texCoord0,
	float2 texCoord1)
{
	float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	float4 result = 0.0.xxxx;
	RenderScrollBar(
		0.0.xx, 1.0.xx, texCoord1.x,
		DebuggingShapesCoords_Make(position, texCoord0, outputDimensions),
		result);
	return result;
}

float4 TagShader(
	float4 position	    : SV_Position,
	float4 color		: COLOR0,
	float4 color1		: COLOR1,
	float2 texCoord0	: TEXCOORD0,
	float2 texCoord1	: TEXCOORD1) : SV_Target0
{
	float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	return RenderTag(
		0.0.xx, 1.0.xx,
		DebuggingShapesCoords_Make(position, texCoord0, outputDimensions));
}

float4 SmallGridBackground(
	float4 position,
	float4 color,
	float4 color1,
	float2 texCoord0,
	float2 texCoord1)
{
	float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	DebuggingShapesCoords coords = DebuggingShapesCoords_Make(position, texCoord0, outputDimensions);

		// outline rectangle
	if (texCoord0.x <= GetUDDS(coords).x || texCoord0.x >= (1.f-GetUDDS(coords).x) || texCoord0.y <= GetVDDS(coords).y || texCoord0.y >= (1.f-GetVDDS(coords).y)) {
		return float4(.5f*float3(0.35f, 0.5f, 0.35f), 1.f);
	}

	float xPixels = DebuggingShapesCoords_GetTexCoord0(coords).x / GetUDDS(coords).x;
	float yPixels = DebuggingShapesCoords_GetTexCoord0(coords).y / GetVDDS(coords).y;

	uint pixelsFromThumb = uint(abs(texCoord0.x - texCoord1.x) / GetUDDS(coords).x + 0.5f);
	if (pixelsFromThumb < 3) {
		return float4(1.0.xxx, 0.5f);
	}

	float4 gridColor = float4(0.125f, 0.35f, 0.125f, 0.f);

	if (xPixels > 20 && texCoord0.x < (1.0f - 20 * GetUDDS(coords).x)) {

		float pixelsFromCenterY = abs(texCoord0.y - 0.5f) / GetVDDS(coords).y;
		if (pixelsFromCenterY <= 1.f) {
				// middle line
			return gridColor;
		}

		uint pixelsFromCenterX = uint(abs(texCoord0.x - 0.5f) / GetUDDS(coords).x + 0.5f);
		if (pixelsFromCenterX % 4 == 0) {
			uint height = ((pixelsFromCenterX % 32) == 0) ? 7 : 4;
			if (uint(pixelsFromCenterY) < height) {
				return gridColor;
			}
		}
	}

	return float4(0.35f, 0.5f, 0.35f, 0.75f);
}

float4 GridBackgroundShader(
	float4 position,
	float4 color,
	float4 color1,
	float2 texCoord0,
	float2 texCoord1)
{
	float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	DebuggingShapesCoords coords = DebuggingShapesCoords_Make(position, texCoord0, outputDimensions);

	if (texCoord0.x >= (1.f-GetUDDS(coords).x) || texCoord0.y <= GetVDDS(coords).y || texCoord0.y >= (1.f-GetVDDS(coords).y)) {
		return float4(0.0.xxx, 1.f);
	}

	float brightness = 0.f;

	float xPixels = DebuggingShapesCoords_GetTexCoord0(coords).x / GetUDDS(coords).x;
	if (uint(xPixels)%64==63) {
		brightness = 1.f;
	} else if (uint(xPixels)%8==7) {
		brightness = 0.5f;
	}

	float yPixels = DebuggingShapesCoords_GetTexCoord0(coords).y / GetVDDS(coords).y;
	if (uint(yPixels)%64==63) {
		brightness = max(brightness, 1.f);
	} else if (uint(yPixels)%8==7) {
		brightness = max(brightness, 0.5f);
	}

	if (brightness > 0.f) {
		return float4(brightness * 0.25.xxx, .75f);
	} else {
		return float4(1.0.xxx, 0.25f);
	}
}
