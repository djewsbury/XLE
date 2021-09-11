// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BRUSH_UTILS_H)
#define BRUSH_UTILS_H

#if !defined(DD_INTERFACES_H)
    #error Include Interfaces.hlsl before this file
#endif

#include "../../Math/EdgeDetection.hlsl"

#define ScreenSpaceDerivatives_Template(ShapeFn, coords, shapeDesc)                                 \
    [unroll] for (uint y=0; y<5; ++y) {                                                             \
        [unroll] for (uint x=0; x<5; ++x) {                                                         \
            float2 texCoordOffset = ((x-2.f) * GetUDDS(coords)) + ((y-2.f) * GetVDDS(coords));      \
            DebuggingShapesCoords offsetCoords = coords;                                            \
            offsetCoords.texCoord += texCoordOffset;                                                \
            float t = ShapeFn(offsetCoords, shapeDesc)._fill;                                       \
            dhdp.x += ScharrHoriz5x5[x][y] * t;                                                     \
            dhdp.y += ScharrVert5x5[x][y] * t;                                                      \
        }                                                                                           \
    }                                                                                               \
    /**/

float2 ScreenSpaceDerivatives(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
{
        //
        //		Using "sharr" filter to find image gradient. We can use
        //		this to create a 3D effect for any basic shape.
        //		See:
        //			http://www.hlevkin.com/articles/SobelScharrGradients5x5.pdf
        //
    float2 dhdp = 0.0.xx;
    ScreenSpaceDerivatives_Template(IShape2D_Calculate, coords, shapeDesc);
    return dhdp;
}

float BorderFromDerivatives(float2 dhdp, float value, float borderSize)
{
    // inside is 1.0 and outside is 0.0
    // so the border is actually at 0.5, and we need to detect the distance
    // to there 
    float b = max(abs(dhdp.x), abs(dhdp.y));
    float pixelsAway = abs(value - 0.5f)/b;
    // return (pixelsAway <= borderSize) ? 1 : 0;
    // returns 1.0f to the distance "borderSize" and then falls off to zero to borderSize*2.0f
    // return saturate(1.0f - (pixelsAway-borderSize)/borderSize);
    return saturate(smoothstep(1, 0, (pixelsAway-borderSize)/(0.25f*borderSize)));
}

#endif
