// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebuggingShapes.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Math/MathConstants.hlsl"

float4 HorizTweakerBarShader(
	float4 position,
	float4 color,
	float4 color1,
	float2 texCoord0,
	float2 texCoord1)
{
	float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	float4 result = 0.0.xxxx;
	RenderHorizTweakerBarShader(
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
		return float4(brightness * 0.125.xxx, .5f);
	} else {
		return float4(0.3.xxx, 0.25f);
	}
}

float IntegrateCircleCutout(float x, float a, float k)
{
	// based on an idea from
	// https://stackoverflow.com/questions/622287/area-of-intersection-between-circle-and-rectangle
	// Here we just take a simple integral of a circle equation to find the area
	// under the curve
	// combining this integral in various ways allows us to find the area of an intersection of
	// a circle and rectangle

	// integral of sqrt[a^2 - x^2] + k (w.r.t. x)
	float a1 = sqrt(a*a - x*x);
	return 0.5 * (x*a1 + a*a*atan(x/a1)) + k*x;
}

float CalculateCircleRectangleIntersectionArea(
	float2 topLeft, float2 bottomRight,
	float radius)
{
	// Given a rectangle an circle (circle at the origin), find the area of the intersection
	// between them

	//
	//			0	|	1	|	2
	//		-------------------------
	//			3	|	4	|	5
	//		-------------------------
	//			6	|	7	|	8
	//
	// If the rectangle is region 4 in the area above, we need to know
	// in which of them does our circle center (ie, the origin), appear?

	float k, x1, x2;
	if (topLeft.y > 0) {
		k = -topLeft.y;
		float q = sqrt(radius*radius - k*k);
		x1 = max(topLeft.x, -q);
		x2 = min(bottomRight.x, q);
		if (x1 > bottomRight.x || x2 < topLeft.x) return 0;
	}
	
	else if (bottomRight.y < 0) {
		k = bottomRight.y;
		float q = sqrt(radius*radius - k*k);
		x1 = max(topLeft.x, -q);
		x2 = min(bottomRight.x, q);
		if (x1 > bottomRight.x || x2 < topLeft.x) return 0;
	}
	
	else if (topLeft.x > 0) {
		k = -topLeft.x;
		float q = sqrt(radius*radius - k*k);
		x1 = max(topLeft.y, -q);
		x2 = min(bottomRight.y, q);
	}
	
	else if (bottomRight.x < 0) {
		k = bottomRight.x;
		float q = sqrt(radius*radius - k*k);
		x1 = max(topLeft.y, -q);
		x2 = min(bottomRight.y, q);
	}

	else {
		
		// in the center, we need to check if we're in range of a border. In this case we start with
		// a full circle and cut out segments when we find them

		float area = pi * radius * radius;
		if (topLeft.y > -radius) {
			k = topLeft.y;
			float q = sqrt(radius*radius - k*k);
			x1 = -q;
			x2 = q;
			float i1 = IntegrateCircleCutout(x1, radius, k);
			float i2 = IntegrateCircleCutout(x2, radius, k);
			area -= i2 - i1;
		}
		
		if (bottomRight.y < radius) {
			k = -bottomRight.y;
			float q = sqrt(radius*radius - k*k);
			x1 = -q;
			x2 = q;
			float i1 = IntegrateCircleCutout(x1, radius, k);
			float i2 = IntegrateCircleCutout(x2, radius, k);
			area -= i2 - i1;
		}

		if (topLeft.x > -radius) {
			k = topLeft.x;
			float q = sqrt(radius*radius - k*k);
			x1 = max(topLeft.y, -q);
			x2 = min(bottomRight.y, q);
			float i1 = IntegrateCircleCutout(x1, radius, k);
			float i2 = IntegrateCircleCutout(x2, radius, k);
			area -= i2 - i1;
		}

		if (bottomRight.x < radius) {
			k = -bottomRight.x;
			float q = sqrt(radius*radius - k*k);
			x1 = max(topLeft.y, -q);
			x2 = min(bottomRight.y, q);
			float i1 = IntegrateCircleCutout(x1, radius, k);
			float i2 = IntegrateCircleCutout(x2, radius, k);
			area -= i2 - i1;
		}

		return area;
	}

	float i1 = IntegrateCircleCutout(x1, radius, k);
	float i2 = IntegrateCircleCutout(x2, radius, k);
	float area = i2 - i1;
	return area;
}

float4 SoftShadowRect(
	float4 position,
	float4 color, float4 color1,
	float2 texCoord0, float2 texCoord1)
{
	// Draw a soft shadow for a rectangle
	// The coordinates in rectangle space are in "texCoord0"

	float dudx = ddx(texCoord0.x), dvdy = ddy(texCoord0.y);
	float2 topLeft = float2(-texCoord0.x / dudx, -texCoord0.y / dvdy); 
	float2 bottomRight = float2((1.f - texCoord0.x) / dudx, (1.0f - texCoord0.y) / dvdy);

	// topLeft, bottomRight are the coordinates of the rectangle, in pixel coords, relative to position.xy
	// Shadowing is equal to the proportion of a circle (centered on the sample point) which intersects
	// the rectangle

	const float radius = texCoord1.x;
	float area = CalculateCircleRectangleIntersectionArea(topLeft, bottomRight, radius);
	float A = area / (pi * radius * radius);
	return float4(color.rgb, color.a*A);

}
