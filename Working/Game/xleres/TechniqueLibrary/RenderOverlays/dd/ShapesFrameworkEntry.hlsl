// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Interfaces.hlsl"
#include "BrushUtils.hlsl"
#include "../../Framework/SystemUniforms.hlsl"

cbuffer ShapesFramework : register(b0, space0)
{
    float BorderSizePix;
}

float4 frameworkEntry(
    float4 position	            : SV_Position,
    float4 color0		        : COLOR0,
    float4 color1		        : COLOR1,
    float2 texCoord0	        : TEXCOORD0,
    float2 shapeRelativeCoords	: TEXCOORD1) : SV_Target0
{
    float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;

    DebuggingShapesCoords coords =
        DebuggingShapesCoords_Make(position, shapeRelativeCoords, outputDimensions);

    ShapeDesc shapeDesc = MakeShapeDesc(0.0.xx, 1.0.xx, BorderSizePix);

    float2 dhdp = ScreenSpaceDerivatives(coords, shapeDesc);

    ShapeResult shape = IShape2D_Calculate(coords, shapeDesc);
    float4 fill = IFill_Calculate(coords, texCoord0, color0, dhdp); fill.a *= shape._fill;
    float4 outline = IOutline_Calculate(coords, texCoord0, color1, dhdp); outline.a *= shape._border;

    float3 A = fill.rgb * fill.a;
    float a = 1.f - fill.a;
    A = A * (1.f - outline.a) + outline.rgb * outline.a;
    a = a * (1.f - outline.a);
    return float4(A, 1.f - a);
}

float4 frameworkEntryForTwoLayersShader(
    float4 position	    : SV_Position,
    float4 color0		: COLOR0,
    float4 color1		: COLOR1,
    float2 texCoord0	: TEXCOORD0,
    float2 texCoord1	: TEXCOORD1) : SV_Target0
{
    return TwoLayersShader(position, color0, color1, texCoord0, texCoord1);
}

float4 frameworkEntryJustFill(
    float4 position	    : SV_Position,
    float4 color0		: COLOR0,
    float2 texCoord0	: TEXCOORD0) : SV_Target0
{
    float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;

    DebuggingShapesCoords coords =
        DebuggingShapesCoords_Make(position, texCoord0, outputDimensions);

    return IFill_Calculate(coords, texCoord0, color0, 1.0.xx);
}
