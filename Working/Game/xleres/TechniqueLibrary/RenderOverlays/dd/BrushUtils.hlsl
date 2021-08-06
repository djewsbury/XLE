// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BRUSH_UTILS_H)
#define BRUSH_UTILS_H

#include "../../Math/EdgeDetection.hlsl"

float2 ScreenSpaceDerivatives(IShape2D shape, DebuggingShapesCoords coords, ShapeDesc shapeDesc)
{
        //
        //		Using "sharr" filter to find image gradient. We can use
        //		this to create a 3D effect for any basic shape.
        //		See:
        //			http://www.hlevkin.com/articles/SobelScharrGradients5x5.pdf
        //
    float2 dhdp = 0.0.xx;
    [unroll] for (uint y=0; y<5; ++y) {
        [unroll] for (uint x=0; x<5; ++x) {
                //  Note that we want the sharr offsets to be in units of screen space pixels
                //  So, we can use screen space derivatives to calculate the correct offsets
                //  in texture coordinate space
            float2 texCoordOffset = ((x-2.f) * GetUDDS(coords)) + ((y-2.f) * GetVDDS(coords));
            DebuggingShapesCoords offsetCoords = coords;
            offsetCoords.texCoord += texCoordOffset;
            float t = shape.Calculate(offsetCoords, shapeDesc)._fill;
            dhdp.x += ScharrHoriz5x5[x][y] * t;
            dhdp.y += ScharrVert5x5[x][y] * t;
        }
    }
    return dhdp;
}

float BorderFromDerivatives(float2 dhdp, float borderSize)
{
    float b = max(abs(dhdp.x), abs(dhdp.y));
    if (b >= borderSize) {
        return b/borderSize;
    }
    return 0.f;
}

#endif
