// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DD_INTERFACES_H)
#define DD_INTERFACES_H

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   D E B U G G I N G   S H A P E S   C O O R D S
struct DebuggingShapesCoords
{
    float4 position;
    float2 outputDimensions;
    float2 texCoord;

    float aspectRatio;
    // float2 refractionCoords;
    float2 udds;
    float2 vdds;
};

float4 GetPosition(DebuggingShapesCoords coords) { return coords.position; }
float2 GetOutputDimensions(DebuggingShapesCoords coords) { return coords.outputDimensions; }
float2 DebuggingShapesCoords_GetTexCoord0(DebuggingShapesCoords coords) { return coords.texCoord; }

#if 0
    float2 GetUDDS(DebuggingShapesCoords coords) { return float2(ddx(VSOUT_GetTexCoord0(coords).x), ddy(VSOUT_GetTexCoord0(coords).x)); }
    float2 GetVDDS(DebuggingShapesCoords coords) { return float2(ddx(VSOUT_GetTexCoord0(coords).y), ddy(VSOUT_GetTexCoord0(coords).y)); }
    float GetAspectRatio(DebuggingShapesCoords coords)
    {
        float a = length(GetUDDS(coords));
        if (a == 0.f) return 1.f;       //right on the edge we're not getting an accurate result for this...?
        float texCoordAspect = length(GetVDDS(coords))/a;
        return 1.0f / texCoordAspect;
    }
#else
    float2 GetUDDS(DebuggingShapesCoords coords)        { return coords.udds; }
    float2 GetVDDS(DebuggingShapesCoords coords)        { return coords.vdds; }
    float GetAspectRatio(DebuggingShapesCoords coords)  { return coords.aspectRatio; }
#endif

float2 GetRefractionCoords(DebuggingShapesCoords coords) { return coords.position.xy/coords.outputDimensions.xy; }

DebuggingShapesCoords DebuggingShapesCoords_Make(float4 position, float2 texCoord, float2 outputDimensions)
{
    DebuggingShapesCoords result;
    result.outputDimensions = outputDimensions;
    // result.refractionCoords = position.xy/outputDimensions.xy;
    result.texCoord = texCoord;

    result.udds = float2(ddx(texCoord.x), ddy(texCoord.x));
    result.vdds = float2(ddx(texCoord.y), ddy(texCoord.y));

    result.position = position;

        //  We can calculate the aspect ratio of tex coordinate mapping
        //  by looking at the screen space derivatives
    float a = length(GetUDDS(result));
    if (a == 0.f) {
        result.aspectRatio = 1.f;
    } else {
        float texCoordAspect = length(GetVDDS(result))/a;
        result.aspectRatio = 1.f/texCoordAspect;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   S H A P E   D E S C

struct ShapeDesc
{
    float2 _minCoords, _maxCoords;
    float _borderSizePix;
    float _param0;
};

ShapeDesc MakeShapeDesc(float2 minCoords, float2 maxCoords, float borderSizePix, float param0)
{
    ShapeDesc result;
    result._minCoords = minCoords;
    result._maxCoords = maxCoords;
    result._borderSizePix = borderSizePix;
    result._param0 = param0;
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
    //   I N T E R F A C E S

ShapeResult IShape2D_Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc);
float4 IOutline_Calculate(DebuggingShapesCoords coords, float4 baseColor, float2 dhdp);
float4 IFill_Calculate(DebuggingShapesCoords coords, float4 baseColor, float2 dhdp);

#endif
