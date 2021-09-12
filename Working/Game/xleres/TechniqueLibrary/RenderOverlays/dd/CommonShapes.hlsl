// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(COMMON_SHAPES_H)
#define COMMON_SHAPES_H

#include "Interfaces.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
ShapeResult RoundedRectShape_Calculate(
    DebuggingShapesCoords coords,
    ShapeDesc shapeDesc)
{
    float2 texCoord = DebuggingShapesCoords_GetTexCoord0(coords);
    float2 minCoords = shapeDesc._minCoords, maxCoords = shapeDesc._maxCoords;
    [branch] if (
            texCoord.x < minCoords.x || texCoord.x > maxCoords.x
        ||	texCoord.y < minCoords.y || texCoord.y > maxCoords.y) {
        return ShapeResult_Empty();
    }

    float borderSizePix = shapeDesc._borderSizePix;
    float roundedProportion = shapeDesc._param0;

    float2 pixelSize = float2(GetUDDS(coords).x, GetVDDS(coords).y);
    float2 borderSize = borderSizePix * pixelSize;

    float roundedPix = min((maxCoords.y - minCoords.y)/GetVDDS(coords).y, (maxCoords.x - minCoords.x)/GetUDDS(coords).x) * roundedProportion;
    float roundedHeight = roundedPix * GetVDDS(coords).y;
    float roundedWidth = roundedPix * GetUDDS(coords).x;

        // mirror coords so we only have to consider the top/left quadrant
    float2 r = float2(
        min(maxCoords.x - texCoord.x, texCoord.x) - minCoords.x,
        min(maxCoords.y - texCoord.y, texCoord.y) - minCoords.y);

    [branch] if (r.x < roundedWidth && r.y < roundedHeight) {
        float2 centre = float2(roundedWidth, roundedHeight);

        ////////////////
            //  To get a anti-aliased look to the edges, we need to make
            //  several samples. Lets just use a simple pattern aligned
            //  to the pixel edges...
        float2 samplePts[4] =
        {
            float2(.5f, .2f), float2(.5f, .8f),
            float2(.2f, .5f), float2(.8f, .5f),
        };

        ShapeResult result = ShapeResult_Empty();
        [unroll] for (uint c=0; c<4; ++c) {
            float2 o = r - centre + samplePts[c] * pixelSize;
            o.x /= GetAspectRatio(coords);
            float dist = roundedHeight - length(o);
            result._border += .25f * (dist >= 0.f && dist < borderSize.y);
            // result._fill = max(result._fill, dist >= borderSize.y);
            result._fill += .25f * (dist >= 0.f);
        }
        return result;
    }
    if (r.x <= borderSize.x || r.y <= borderSize.y) {
        return MakeShapeResult(1.f, 1.f);
    }

    return MakeShapeResult(1.f, 0.f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
float Sq(float i) { return i*i; }

ShapeResult Ellipse_Calculate(
    DebuggingShapesCoords coords,
    ShapeDesc shapeDesc)
{
    float2 texCoord = DebuggingShapesCoords_GetTexCoord0(coords);
    float2 minCoords = shapeDesc._minCoords, maxCoords = shapeDesc._maxCoords;
    float borderSizePix = shapeDesc._borderSizePix;
    minCoords.x += borderSizePix*GetUDDS(coords).x;
    minCoords.y += borderSizePix*GetVDDS(coords).y;
    maxCoords.x -= borderSizePix*GetUDDS(coords).x;
    maxCoords.y -= borderSizePix*GetVDDS(coords).y;

    float2 pixelSize = float2(GetUDDS(coords).x, GetVDDS(coords).y);
    float2 borderSize = borderSizePix * pixelSize;

    float2 center = 0.5f * (minCoords + maxCoords);
    float e = Sq(texCoord.x - center.x) / Sq(0.5f*(maxCoords.x - minCoords.x)) + Sq(texCoord.y - center.y) / Sq(0.5f*(maxCoords.y - minCoords.y));
    float partialDevX = 2.f*abs(texCoord.x - center.x) / Sq(0.5f*(maxCoords.x - minCoords.x)) + Sq(texCoord.y - center.y) / Sq(0.5f*(maxCoords.y - minCoords.y));
    float partialDevY = Sq(texCoord.x - center.x) / Sq(0.5f*(maxCoords.x - minCoords.x)) + 2.f*abs(texCoord.y - center.y) / Sq(0.5f*(maxCoords.y - minCoords.y));
    if (e > 1) {
        float2 pixelsAway = float2((e-1)/partialDevX, (e-1)/partialDevY);
        pixelsAway = (pixelsAway-borderSize)/(0.25f*borderSize);
        float border = saturate(smoothstep(1, 0, min(pixelsAway.x, pixelsAway.y)));
        return MakeShapeResult(e < 1.0f, border);
    } else {
        return MakeShapeResult(e < 1.0f, 0);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
float CircleShape2(float2 centrePoint, float radius, float2 texCoord, float aspectRatio)
{
    float2 o = texCoord - centrePoint;
    o.x /= aspectRatio;
    return dot(o, o) <= (radius*radius);
}

float RectShape2(float2 mins, float2 maxs, float2 texCoord)
{
    return all(texCoord >= mins) && all(texCoord <= maxs);
}

ShapeResult RectShape_Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
{
        // we'll assume pixel-perfect coords, so we don't have handle
        // partially covered pixels on the edges.
    float2 texCoord = DebuggingShapesCoords_GetTexCoord0(coords);
    float2 minCoords = shapeDesc._minCoords, maxCoords = shapeDesc._maxCoords;
    bool fill =
            texCoord.x >= minCoords.x && texCoord.x < maxCoords.x
        && texCoord.y >= minCoords.y && texCoord.y < maxCoords.y;

    float2 r = float2(
        min(maxCoords.x - texCoord.x, texCoord.x) - minCoords.x,
        min(maxCoords.y - texCoord.y, texCoord.y) - minCoords.y);
    bool border = (texCoord.x <= GetUDDS(coords).x) || (texCoord.y <= GetVDDS(coords).y);

    return MakeShapeResult(float(fill), float(border));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ShapeResult ScrollBarShape_Calculate(DebuggingShapesCoords coords, ShapeDesc shapeDesc)
{
    float2 minCoords = shapeDesc._minCoords;
    float2 maxCoords = shapeDesc._maxCoords;
    float thumbPosition = shapeDesc._param0;
    float2 texCoord = DebuggingShapesCoords_GetTexCoord0(coords);
    float aspectRatio = GetAspectRatio(coords);

    const float thumbWidth = coords.udds.x * 10;
    const float markerWidth = coords.udds.x * 4;
    const float baseLineWidth = coords.vdds.y * 6;

    float2 baseLineMin = float2(minCoords.x, lerp(minCoords.y, maxCoords.y, 0.5f) - baseLineWidth/2);
    float2 baseLineMax = float2(maxCoords.x, lerp(minCoords.y, maxCoords.y, 0.5f) + baseLineWidth/2);
    //float result = 0.5f * RoundedRectShape(baseLineMin, baseLineMax, texCoord, aspectRatio, 0.4f);
    float result = 0.25f * RoundedRectShape_Calculate(coords, MakeShapeDesc(baseLineMin, baseLineMax, 0.f, 0.4f))._fill;

        //	Add small markers at fractional positions along the scroll bar
    float markerPositions[7] = { .125f, .25f, .375f, .5f,   .625f, .75f, .875f };
    float markerHeights[7]   = { .5f  , .65f, .5f ,  .65f, .5f,   .65f, .5f   };

    for (uint c=0; c<7; ++c) {
        float x = lerp(minCoords.x, maxCoords.x, markerPositions[c]);
        float2 markerMin = float2(x - markerWidth/2, lerp(minCoords.y, maxCoords.y, 0.5f*(1.f-markerHeights[c])));
        float2 markerMax = float2(x + markerWidth/2, lerp(minCoords.y, maxCoords.y, 0.5f+0.5f*markerHeights[c]));
        // result = max(result, 0.75f*RectShape(markerMin, markerMax, texCoord));
        result = max(result, 0.25f*RectShape_Calculate(coords, MakeShapeDesc(markerMin, markerMax, 0.f, 0.f))._fill);
    }

    float2 thumbCenter = float2(
        lerp(minCoords.x, maxCoords.x, thumbPosition),
        lerp(minCoords.y, maxCoords.y, 0.5f));
    // result = max(result, CircleShape2(thumbCenter, 0.475f * (maxCoords.y - minCoords.y), texCoord, aspectRatio));
    result = max(result, RectShape2(float2(thumbCenter.x-thumbWidth/2, minCoords.y), float2(thumbCenter.x+thumbWidth/2, maxCoords.y), texCoord));
    return MakeShapeResult(result, 0.f);
}

#endif
