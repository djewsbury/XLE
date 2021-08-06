// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(GRADIENT_FLAGS_H)
#define GRADIENT_FLAGS_H

#include "../../TechniqueLibrary/Math/EdgeDetection.hlsl"

float2 CalculateDHDXY(int2 coord)
{
    float centerHeight = GetHeight(coord);
    float2 dhdp = 0.0.xx;
    #if 0
        for (uint y=0; y<5; ++y) {
            for (uint x=0; x<5; ++x) {
                int2 c = coord + int2(x,y) - int2(2,2);
                if (CoordIsValid(c)) {
                    float heightDiff = GetHeight(c) - centerHeight;
                    dhdp.x += ScharrHoriz5x5[x][y] * heightDiff;
                    dhdp.y += ScharrVert5x5[x][y] * heightDiff;
                }
            }
        }
    #else
        for (uint y=0; y<3; ++y) {
            for (uint x=0; x<3; ++x) {
                int2 c = coord + int2(x,y) - int2(1,1);
                if (CoordIsValid(c)) {
                    float heightDiff = GetHeight(c) - centerHeight;
                    dhdp.x += ScharrHoriz3x3[x][y] * heightDiff;
                    dhdp.y += ScharrVert3x3[x][y] * heightDiff;
                }
            }
        }
    #endif
    return dhdp;
}

uint CalculateRawGradientFlags(
    int2 baseCoord, float spacing,
    float slope0Threshold, float slope1Threshold, float slope2Threshold)
{
///////////////////////////////////////////////////////////////////////////////////////////////////

        //  0  1  2
        //  3  4  5
        //  6  7  8
    const int2 Offsets[9] =
    {
        int2(-1, -1), int2( 0, -1), int2( 1, -1),
        int2(-1,  0), int2( 0,  0), int2( 1,  0),
        int2(-1,  1), int2( 0,  1), int2( 1,  1)
    };

    float heights[9];
    float2 dhdxy[9];
    float heightDiff[9];
    for (uint c=0; c<9; c++) {
        heights[c] = GetHeight(baseCoord + Offsets[c]);
        dhdxy[c] = CalculateDHDXY(baseCoord + Offsets[c]) / spacing;
    }
    for (uint c2=0; c2<9; c2++) {
        heightDiff[c2] = heights[c2] - heights[4];
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    // good for finding flat places:
    // dot(dhdxy[1], dhdxy[8]) >= 1.f;
    // dot(dhdxy[3], dhdxy[5]) >= 1.f;

    float slope = max(abs(dhdxy[4].x), abs(dhdxy[4].y));
    if (slope < slope0Threshold) return 0;
    if (slope < slope1Threshold) return 1;
    if (slope < slope2Threshold) return 2;
    return 3;

#if 0
    bool centerIsSlope = max(abs(dhdxy[4].x), abs(dhdxy[4].y)) > slopeThreshold;
    int result = int(centerIsSlope);

    float3 b = float3( 0.f, -spacing, heightDiff[1]);
    float3 t = float3( 0.f,  spacing, heightDiff[7]);
    float3 l = float3(-spacing,  0.f, heightDiff[3]);
    float3 r = float3( spacing,  0.f, heightDiff[5]);
    bool topBottomTrans = dot(normalize(b), -normalize(t)) < transThreshold;
    bool leftRightTrans = dot(normalize(l), -normalize(r)) < transThreshold;
    if (leftRightTrans || topBottomTrans) result |= 2;

    return result;
#endif
}

#endif
