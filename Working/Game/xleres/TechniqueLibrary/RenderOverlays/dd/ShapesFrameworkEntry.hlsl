// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Interfaces.hlsl"
#include "BrushUtils.hlsl"
#include "../../Framework/SystemUniforms.hlsl"

float4 frameworkEntry(
    float4 position	    : SV_Position,
    float4 color0		: COLOR0,
    float4 color1		: COLOR1,
    float2 texCoord0	: TEXCOORD0,
    float2 texCoord1	: TEXCOORD1) : SV_Target0
{
    float2 outputDimensions = 1.0f / SysUniform_ReciprocalViewportDimensions().xy;

    DebuggingShapesCoords coords =
        DebuggingShapesCoords_Make(position, texCoord0, outputDimensions);

    ShapeDesc shapeDesc = MakeShapeDesc(0.0.xx, 1.0.xx, texCoord1.x, texCoord1.y);

    float2 dhdp = ScreenSpaceDerivatives(coords, shapeDesc);

    ShapeResult shape = IShape2D_Calculate(coords, shapeDesc);
    float4 fill = IFill_Calculate(coords, color0, dhdp); fill.a *= shape._fill;
    float4 outline = IOutline_Calculate(coords, color1, dhdp); outline.a *= shape._border;

    float3 A = fill.rgb * fill.a;
    float a = 1.f - fill.a;
    A = A * (1.f - outline.a) + outline.rgb * outline.a;
    a = a * (1.f - outline.a);
    return float4(A, 1.f - a);
}
