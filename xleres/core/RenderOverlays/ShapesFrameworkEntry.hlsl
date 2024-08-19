// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Interfaces.hlsl"
#include "BrushUtils.hlsl"
#include "../TechniqueLibrary/Framework/SystemUniforms.hlsl"

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
    float4 fill = IFill_Calculate(coords, texCoord0, color0, dhdp);
    float4 outline = IOutline_Calculate(coords, texCoord0, color1, dhdp);

    shape._border *= outline.a; // when outline alpha is zero, we want that space to be considered "fill"
    float4 A = lerp(fill, outline, shape._border);
    return float4(A.rgb, A.a * max(shape._fill, shape._border));
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

float4 frameworkEntryPC3D(
    float4 position	    : SV_Position,
    float4 color0		: COLOR0,
    float3 worldPosition : WORLDPOSITION) : SV_Target0
{
    return PC3D(position, color0, worldPosition);
}


float4 frameworkEntryPCT3D(
    float4 position	    : SV_Position,
    float4 color0		: COLOR0,
    float2 texCoord0	: TEXCOORD0,
    float3 worldPosition : WORLDPOSITION) : SV_Target0
{
    return PCT3D(position, color0, texCoord0, worldPosition);
}


