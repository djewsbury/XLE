// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DD_INTERFACES_H)
#define DD_INTERFACES_H

//
// SEMANTICS
// in to patches:
//		SV_Position		(pixel shader position)
//		OutputDimensions
//		AspectRatio
//
//		DHDP
//
//		ShapeDesc (ShapeDesc struct)
//		DebuggingShapesCoords (DebuggingShapesCoords struct)
//
//		COLOR 			(Linear color)
//		COLOR_SRGB 		(SRGB color)
//		TEXCOORD
//
// out from patches:
//		Fill		(float4: color)
//		Outline		(float4: color)
//		ShapeResult (ShapeResult struct)
// 

///////////////////////////////////////////////////////////////////////////////////////////////////
	//   S H A P E   D E S C

struct ShapeDesc
{
	float2 _minCoords, _maxCoords;
	float _borderSizePix;
};

ShapeDesc MakeShapeDesc(float2 minCoords, float2 maxCoords, float borderSizePix)
{
	ShapeDesc result;
	result._minCoords = minCoords;
	result._maxCoords = maxCoords;
	result._borderSizePix = borderSizePix;
	return result;
}

ShapeDesc DefaultValue_ShapeDesc()
{
	ShapeDesc result;
	result._minCoords = 0.0.xx;
	result._maxCoords = 1.0.xx;
	result._borderSizePix = 1.0;
	return result;
}

struct ShapeResult
{
		// strength (0-1) of the "border" and "fill" parts of this shape
	float _fill;
	float _border;
};

ShapeResult ShapeResult_Empty() { ShapeResult temp; temp._border = temp._fill = 0.f; return temp; }
ShapeResult MakeShapeResult(float fill, float border)  { ShapeResult temp; temp._fill = fill; temp._border = border; return temp; }

///////////////////////////////////////////////////////////////////////////////////////////////////
	//   D E B U G G I N G   S H A P E S   C O O R D S
struct DebuggingShapesCoords
{
	float2 shapeRelativeCoords;
	float2 udds, vdds;
};

float2 GetShapeRelativeCoords(DebuggingShapesCoords coords) { return coords.shapeRelativeCoords; }
float2 GetUDDS(DebuggingShapesCoords coords)        { return coords.udds; }
float2 GetVDDS(DebuggingShapesCoords coords)        { return coords.vdds; }

DebuggingShapesCoords DebuggingShapesCoords_Make(float2 shapeRelativeCoords)
{
	DebuggingShapesCoords result;
	result.shapeRelativeCoords = shapeRelativeCoords;
	result.udds = float2(ddx(shapeRelativeCoords.x), ddy(shapeRelativeCoords.x));
	result.vdds = float2(ddx(shapeRelativeCoords.y), ddy(shapeRelativeCoords.y));
	return result;
}

float CalculateAspectRatio(DebuggingShapesCoords coords)
{
		//  We can calculate the aspect ratio of tex coordinate mapping
		//  by looking at the screen space derivatives
	float a = length(GetVDDS(coords));
	return (a == 0.f) ? 1.f : (length(GetUDDS(coords)) / a);
}

#endif
