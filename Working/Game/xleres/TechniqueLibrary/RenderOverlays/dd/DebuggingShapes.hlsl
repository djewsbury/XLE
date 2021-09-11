// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DEBUGGING_SHAPES_H)
#define DEBUGGING_SHAPES_H

#include "../../Framework/CommonResources.hlsl"
#include "Interfaces.hlsl"
#include "CommonShapes.hlsl"
#include "CommonBrushes.hlsl"
#include "BrushUtils.hlsl"

ShapeResult TagShape_Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
{
	float2 texCoord = DebuggingShapesCoords_GetTexCoord0(coords);
	float2 minCoords = shapeDesc._minCoords, maxCoords = shapeDesc._maxCoords;
	float aspectRatio = GetAspectRatio(coords);

	const float roundedProportion = 0.2f;
	float roundedHeight = (maxCoords.y - minCoords.y) * roundedProportion;
	if (	texCoord.x < minCoords.x || texCoord.x > maxCoords.x
		||	texCoord.y < minCoords.y || texCoord.y > maxCoords.y) {
		return ShapeResult_Empty();
	}

	float roundedWidth = roundedHeight * aspectRatio;

	float2 r = texCoord - minCoords;
	if (r.x < roundedWidth) {

		if (r.y < roundedHeight) {
			float2 centre = float2(roundedWidth, roundedHeight);
			float2 o = r - centre; o.x /= aspectRatio;
			return MakeShapeResult(dot(o, o) <= (roundedHeight*roundedHeight), 0.f);
		} else if (r.y > maxCoords.y - minCoords.y - roundedHeight) {
			float2 centre = float2(roundedWidth, maxCoords.y - minCoords.y - roundedHeight);
			float2 o = r - centre; o.x /= aspectRatio;
			return MakeShapeResult(dot(o, o) <= (roundedHeight*roundedHeight), 0.f);
		} else {
			return MakeShapeResult(1.f, 0.f);
		}

	} else {

		float sliceWidth = (maxCoords.y - minCoords.y) * .5f * aspectRatio;
		float sliceStart = maxCoords.x - minCoords.x - sliceWidth;
		if (r.x > sliceStart) {
			float a = (r.x - sliceStart) / sliceWidth;
			if (r.y < (1.f-a) * (maxCoords.y - minCoords.y)) {
				return MakeShapeResult(1.f, 0.f);
			}
		} else {
			return MakeShapeResult(1.f, 0.f);
		}
	}

	return ShapeResult_Empty();
}

float4 RenderTag(float2 minCoords, float2 maxCoords, DebuggingShapesCoords coords)
{
    const float border = 2.f;
    float2 tagMin = minCoords + border * GetUDDS(coords) + border * GetVDDS(coords);
    float2 tagMax = maxCoords - border * GetUDDS(coords) - border * GetVDDS(coords);

	ShapeDesc shapeDesc = MakeShapeDesc(tagMin, tagMax, 0.f, 0.f);
	float2 dhdp = 0.0.xxx;
	float accentuate = 4.f;
	ScreenSpaceDerivatives_Template(TagShape_Calculate, coords, shapeDesc);

	float t = TagShape_Calculate(coords, shapeDesc)._fill;
	if (t > 0.f) {
		// const float3 baseColor = 2.f * float3(0.125f, 0.2f, .25f);
		const float3 baseColor = float3(0.333f, 0.333f, 0.333f);
		// return RaisedRefractiveFill_Calculate(coords, float4(baseColor, 1.f), accentuate*dhdp);
		return RaisedFill_Calculate(coords, float4(baseColor, 1.f), accentuate*dhdp);
	} else {
		const float borderSize = 6.f;
		return float4(0.75.xxx, BorderFromDerivatives(dhdp, t, borderSize));
	}
	return 0.0.xxxx;
}

void RenderScrollBar(   float2 minCoords, float2 maxCoords, float thumbPosition,
                        DebuggingShapesCoords coords, inout float4 result)
{
	ShapeDesc shapeDesc = MakeShapeDesc(0.0.xx, 1.0.xx, 0.f, thumbPosition);
	float2 dhdp = 0.0.xxx;
	float accentuate = 4.f;
	ScreenSpaceDerivatives_Template(ScrollBarShape_Calculate, coords, shapeDesc);

	float t = ScrollBarShape_Calculate(coords, shapeDesc)._fill;
	if (t > 0.f) {
		float3 normal = NormalToSurface(accentuate*dhdp);
		float d = saturate(-dot(BasicShapesLightDirection, normal));
		float A = d; // 12.5f * pow(d, 2.f);

		if (t > 0.75f) {
			// result = float4(A * 2.f * float3(0.125f, 0.2f, .25f) + 0.1.xxx, 1.f);
			result = float4(A * 2.f*float3(0.666f, 0.66f, 0.666f), 1.f);
			// result.rgb += RefractionsBuffer.SampleLevel(ClampingSampler, GetRefractionCoords(coords), 0).rgb;
		} else {
			// result = float4(A * float3(1.1f, .9f, .5f) + 0.1.xxx, 1.f);
			result = float4(A * float3(.33f, .33f, .33f), 1.0f);
			// result.rgb += 0.5f * RefractionsBuffer.SampleLevel(ClampingSampler, GetRefractionCoords(coords), 0).rgb;
		}
	} else {
		/*const float borderSize = 10.f;
		result = float4(1.0.xxx, BorderFromDerivatives(dhdp, t, borderSize));*/
	}
}

#endif
