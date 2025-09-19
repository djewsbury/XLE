// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Interfaces.hlsl"
#include "BrushUtils.hlsl"
#include "CommonShapes.hlsl"		// for RectShape_Calculate
#include "../TechniqueLibrary/Framework/SystemUniforms.hlsl"

cbuffer ShapesFramework
{
	float BorderSizePix;
}

float4 ResolveShapeFramework_ShapeFillAndOutline(
	ShapeResult shape : ShapeResult,
	float4 fill : Fill,
	float4 outline : Outline) : SV_Target0
{
	// Both outline and fill
	shape._border *= outline.a; // when outline alpha is zero, we want that space to be considered "fill"
	float4 A = lerp(fill, outline, shape._border);
	return float4(A.rgb, A.a * max(shape._fill, shape._border));
}

float4 ResolveShapeFramework_ShapeAndFill(
	ShapeResult shape : ShapeResult,
	float4 fill : Fill) : SV_Target0
{
	// Just fill
	return float4(fill.rgb, fill.a * max(shape._fill, shape._border));
}

float4 ResolveShapeFramework_Fill(
	float4 fill : Fill) : SV_Target0
{
	// Just fill, no shape
	return fill;
}

float4 ResolveShapeFramework_ShapeAndOutline(
	ShapeResult shape : ShapeResult,
	float4 outline : Outline) : SV_Target0
{
	// Just outline
	return float4(outline.rgb, outline.a * shape._border);
}

void BuildShapeFrameworkInputs(
	float2 shapeRelativeCoords : TEXCOORD1,		// todo -- use a more specific semantic for this?
	out float2 outputDimensions : OutputDimensions,
	out float aspectRatio : AspectRatio,
	out DebuggingShapesCoords shapeCoords : DebuggingShapesCoords,
	out ShapeDesc shapeDesc : ShapeDesc)
{
	outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;
	shapeCoords = DebuggingShapesCoords_Make(shapeRelativeCoords);
	aspectRatio = CalculateAspectRatio(shapeCoords);
	shapeDesc = MakeShapeDesc(0.0.xx, 1.0.xx, BorderSizePix);
}

void BuildShapeDHDP(
	out float2 dhdp : DHDP, 
	DebuggingShapesCoords coords : DebuggingShapesCoords,
	ShapeDesc shapeDesc : ShapeDesc,
	float aspectRatio : AspectRatio)
{
	dhdp = ScreenSpaceDerivatives(coords, shapeDesc, aspectRatio);
}

void BuildShapeFromInterface(
	out ShapeResult shape : ShapeResult,
	DebuggingShapesCoords coords : DebuggingShapesCoords,
	ShapeDesc shapeDesc : ShapeDesc,
	float aspectRatio : AspectRatio)
{
	#if HAS_INSTANTIATION_IShape2D_Calculate
		shape = IShape2D_Calculate(coords, shapeDesc, aspectRatio);
	#else
		shape = RectShape_Calculate(coords, shapeDesc);
	#endif
}
