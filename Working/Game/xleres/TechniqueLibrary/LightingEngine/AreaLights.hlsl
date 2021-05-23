// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(AREA_LIGHTS_H)
#define AREA_LIGHTS_H

#include "SpecularMethods.hlsl"        // for RoughnessToDAlpha
#include "../Math/MathConstants.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   Utility functions for calculating rectangle area lights
///////////////////////////////////////////////////////////////////////////////////////////////////

float RectRectIntersectionArea(
    out float2 representativePoint,
    float2 R0min, float2 R0max, float2 R1min, float2 R1max)
{
    float2 intersectionMin = max(R0min, R1min);
    float2 intersectionMax = min(R0max, R1max);
    float2 A = intersectionMax - intersectionMin;
    representativePoint = .5f * (intersectionMin + intersectionMax);
    return max(0, A.x) * max(0, A.y);
}

float2 RectangleDiffuseRepPoint(float3 samplePt, float3 sampleNormal, float2 lightHalfSize)
{
        // This method based on Drobot's research in GPU Gems 5. It should be
        // fairly general, and it may be possible to adapt this method to
        // many different shapes.
        // For diffuse, we're going to use a kind of importance sampling
        // technique... Except that there will be only a single sample. We want
        // to find the single that sample that best represents the integral of
        // diffuse lighting over the whole shape.
        //
        // First, we need to find 2 points. The first point is the point on the
        // light plane that intersects the sample point normal. (p' or p0)
        // The second point is the projection of the sample point onto the light
        // plane along the plane normal (p'' or p1)

    // if (abs(sampleNormal.z) < 1e-3f) sampleNormal += float3(0.03f, 0.03f, 0.03f);

        // We need to skew in these cases to find a collision point.
    bool isFacingAway = sampleNormal.z >= -1e-3f && sampleNormal.z <= 1e-3f;
    float3 skewedNormal = sampleNormal;
        // we give an imaginary value to skewedNormal.z. This has to be fairly small... If it's too large,
        // it creates a discontinuity near the equator of the light
    if (isFacingAway) skewedNormal = /*normalize*/(float3(sampleNormal.xy, sign(samplePt.z) * -0.01f)); // (without normalization, it should be incorrect -- but maybe no major visual artefacts)

        // Triple product type operation becomes simplier...
        // note -- obvious problems with sampleNormal.z is near 0
    float2 p0 = samplePt.xy + skewedNormal.xy * (-samplePt.z/skewedNormal.z);
    float2 p1 = samplePt.xy;

    if (isFacingAway) {
            // We need to prevent p1 from falling beneath the horizon...
            // Let's find the direction along the horizon plane to the light
            // We can do this by removing the part of the direction that is
            // perpendicular to the normal (thereby creating a vector that is
            // tangent to the horizon plane)
        float3 directionAlongHorizon = float3(0.0.xx, -samplePt.z);
        directionAlongHorizon -= dot(directionAlongHorizon, sampleNormal) * sampleNormal;
        p1 = samplePt.xy + directionAlongHorizon.xy * (-samplePt.z / directionAlongHorizon.z);
    }

        // In our coordinate system, pc and pt are easy to find
        // (though is can be more complex if one of the points falls under the sample horizon)
        // Drobot didn't actually calculate pc and pt -- he just used a half way point
        // between p0 and p1. Actually, the difference does seem to be quite minor.
        // But in light space, it's easy to clamp pc and pt.
    float2 pc = clamp(p0.xy, -lightHalfSize.xy, lightHalfSize.xy);
    float2 pt = clamp(p1.xy, -lightHalfSize.xy, lightHalfSize.xy);

    float2 r = 0.5f * (pc+pt);
    // r = float2(clamp(r.x, -lightHalfSize.x, lightHalfSize.x), clamp(r.y, -lightHalfSize.y, lightHalfSize.y));
    return r;
}

void CalculateEllipseApproximation(
    out float2 S0A, out float2 S0B,
    out float2 S1A, out float2 S1B,
    out float ellipseArea,
    out float squareWeighting,
    float3 reflectedDirLight, float distance, float sinConeAngle, float cosConeAngle, float2 projectedCircleCenter)
{
    float dBySinConeAngle = distance * sinConeAngle;
    float minorAxis = dBySinConeAngle / cosConeAngle;

    float cosTiltAngle = -reflectedDirLight.z;
    float sinTiltAngle = sqrt(1.f-cosTiltAngle*cosTiltAngle);

    #if 0
        //  This is the slow way of doing the calculation...
        float tiltAngle = acos(saturate(cosTiltAngle));
        float phi = .5f*pi + tiltAngle;
        float c = pi - (asin(sinConeAngle) + phi);
        float A = distance * sinConeAngle / sin(c);     // from the law of sines

        float phi2 = .5f*pi - tiltAngle;
        float c2 = pi - (asin(sinConeAngle) + phi2);
        float B = distance * sinConeAngle / sin(c2);

    #else
        // Trig optimizes out completely. But we need both sine and cosine for
        // cone angle and the tilt angle. That means we need to use a sqrt to
        // get the sine from the cosine

        // float c = cos(asin(sinConeAngle) + tiltAngle);
        // cos(a+b) = cosa.cosb - sina.sinb (trig addition)
        // float c = cosConeAngle * cosTiltAngle - sinConeAngle * sinTiltAngle;
        float Q = cosConeAngle * cosTiltAngle;
        float W = sinConeAngle * sinTiltAngle;
        float A = dBySinConeAngle / (Q-W);

        // float c2 = sin(pi - asin(sinConeAngle) - (.5f*pi - tiltAngle))
        //          = sin(.5f*pi - (asin(sinConeAngle) - tiltAngle))
        // float c2 = cos(asin(sinConeAngle) - tiltAngle)
        // cos(a-b) = cosa.cosb + sina.sinb (trig addition)
        // float c2 = cosConeAngle * cosTiltAngle + sinConeAngle * sinTiltAngle;
        float B = dBySinConeAngle / (Q+W);

    #endif

    float majorAxis = .5f * (A + B);
    float2 ellipseLongAxis = normalize(reflectedDirLight.xy);
    float2 ellipseCenter = projectedCircleCenter + ellipseLongAxis * .5f * (A-B);

        // "ellipseC" is used to define the "vertices" of the ellipse.
        // These are critical points for defining the ellipse.
    // float ellipseC = sqrt(majorAxis*majorAxis - minorAxis*minorAxis);
    // float2 focusA = ellipseCenter + ellipseC * ellipseLongAxis;
    // float2 focusB = ellipseCenter - ellipseC * ellipseLongAxis;

    ellipseArea = pi * majorAxis * minorAxis;
    ellipseArea = max(0.f, ellipseArea);
    float squareRadiusForEllipse = sqrt(.125f * ellipseArea);  // (half to convert from edge length to "radius" value)

    // We could put the center of the squared exactly on the ellipse vertices.
    // But it seems to make more sense just to position them so that the
    // edge tends to touch the edge of the ellipse.
    // Some artifacts can be caused by over-weighting the edges of the ellipse.
    // We adjust the squares in a bit to try to reduce the effect of these artifacts.
    const float fudgeFactor = 1.33f;
    float2 vertexAxis = (majorAxis - fudgeFactor * squareRadiusForEllipse) * ellipseLongAxis;
    float2 focusA = ellipseCenter + vertexAxis;
    float2 focusB = ellipseCenter - vertexAxis;

    S0A = focusA - float2(squareRadiusForEllipse, squareRadiusForEllipse);
    S0B = focusA + float2(squareRadiusForEllipse, squareRadiusForEllipse);

    S1A = focusB - float2(squareRadiusForEllipse, squareRadiusForEllipse);
    S1B = focusB + float2(squareRadiusForEllipse, squareRadiusForEllipse);

    squareWeighting = A / (A+B);
}

float2 RectangleSpecularRepPoint(out float intersectionArea, float3 samplePt, float3 sampleNormal, float3 viewDirection, float2 lightHalfSize, float roughness)
{
        // To calculate specular, we need a different equation. We're going to
        // create a cone centered around the reflection angle. The angle of the cone
        // should be based on the specular equation -- in particular, the normal
        // distribution term of the micro-facet equation.
        // We will project the cone onto the light plane, and we want to find the area
        // of the intersection of the cone and the light.
        // We're going to use the ratio of the intersection area with the full light area
        // to modulate the specular equation. This is an approximation of the integral
        // incoming light weighted by the normal distribution function.
        //
        // A projected cone is a complex shape. So we're not going to use it directly.
        // Instead, we approximate the projected cone as just a square on the light
        // plane. First we find a disk approximating the projected cone. Then we find
        // a square that is adjusted to have the same area.
        //
        // Let's try doing this in light space, as well.

    float3 reflectedDirLight = reflect(-viewDirection, sampleNormal);

        // We can calculate an estimate for the cone angle by using the inverse of the
        // normal distribution function. The NDF gives us an intensity value for an input
        // cosTheta (where theta is the angle between N and H). We can go backwards by
        // finding the cosTheta that gives us a certain intensity value. The inverse can
        // give us a cone on which the D value is a given intensity. So let's pick some
        // cut-off value, and use that. 0.312 is convenient because of the definition of
        // our NDF.
        // The full inverse for GGX Trowbridge Reitz is a little too expensive. And the
        // end result is fairly subtle... So we get by with a rough estimate.
        //
        // The clearest artefact we get is problems on grazing angles when the light
        // source is near a surface (ie, a wall length light source gets distorted
        // in the floor). The effect is increased with smaller cutoff values here...
        // Very small cutoff angles almost seem to give the best result.
    // float cosConeAngle = TrowReitzDInverse(0.32f, RoughnessToDAlpha(roughness));
    // float cosConeAngle = TrowReitzDInverse(0.97f, RoughnessToDAlpha(roughness));
    float cosConeAngle = TrowReitzDInverseApprox(RoughnessToDAlpha(roughness));

        // note --  we could incorporate this tanConeAngle calculation into the
        //          TrowReitzDInverseApprox function...?
    float sinConeAngle = sqrt(1.f - cosConeAngle*cosConeAngle);
    float tanConeAngle = sinConeAngle/cosConeAngle;

        //  Drobot's phong based specular cone angle...
        //  Maybe performance is similar to our method, but
        //  it's more difficult to balance it against our "roughness" parameter
    // float ta = 512.f * (1.f-roughness);
    // float phongAngle = 2.f / pi * sqrt(2/(ta+2));
    // tanConeAngle = pi * phongAngle; // tan(phongAngle);

        // The projected circle radius value here is only accurate when the direction
        // to the light is along the reflection dir (but that's also when the highlight is
        // brightest)
    float distToProjectedConeCenter = (-samplePt.z/reflectedDirLight.z);
    float2 projectedCircleCenter = samplePt.xy + reflectedDirLight.xy * distToProjectedConeCenter;

    const bool useDoubleSquareMethod = true;
    if (!useDoubleSquareMethod) {
            // Beyond a certain width, the inaccuracies in the intersection detection
            // become too great. The shape of the light become very distorted.
            // Can try to it an upper range, and clamp it off.
            // However, this is a double-edged sword. Because with this clamp, sometimes
            // light sources that are far away will appear too sharp. It's unfortunate,
            // because the distortion is really only a problem in some cases.
        // const float maxProjectedCircleRadius = 0.33f * min(lightHalfSize.x, lightHalfSize.y);
        // float projectedCircleRadius = clamp(distToProjectedConeCenter * tanConeAngle, 0.f, maxProjectedCircleRadius);
        float projectedCircleRadius = distToProjectedConeCenter * tanConeAngle;
        // return float4(projectedCircleRadius.xxx, 1.f);

            // This is squaring the circle...?!
            // float circleArea = pi * projectedCircleRadius * projectedCircleRadius;
            // float squareSide = sqrt(circleArea);
        float squareRadius = halfSqrtPi * projectedCircleRadius;

        float2 representativePt;
        intersectionArea = RectRectIntersectionArea(
            representativePt,
            -lightHalfSize, lightHalfSize,
            projectedCircleCenter - float2(squareRadius, squareRadius),
            projectedCircleCenter + float2(squareRadius, squareRadius));

        // representativePt = projectedCircleCenter;

        // note --  zero area lights will cause nans here
        //          we should skip zero area lights on the CPU side
        intersectionArea /= pi * projectedCircleRadius * projectedCircleRadius;
        return representativePt;

    } else {

    ///////////////////////////////////////////////////////////////////////////////////////////////////
            // Ellipse approximation experiment...

        float2 S0A, S0B, S1A, S1B;
        float ellipseArea;
        float squareWeighting;
        CalculateEllipseApproximation(
            S0A, S0B, S1A, S1B, ellipseArea, squareWeighting,
            reflectedDirLight,
            distToProjectedConeCenter, sinConeAngle, cosConeAngle, projectedCircleCenter);

        float2 representativePtA, representativePtB;
        float intersectionAreaA = RectRectIntersectionArea(representativePtA, -lightHalfSize, lightHalfSize, S0A, S0B);
        float intersectionAreaB = RectRectIntersectionArea(representativePtB, -lightHalfSize, lightHalfSize, S1A, S1B);

            // We have to be careful when the intersection area of one square is zero. In this
            // case the square is completely clipped, and the representativePt may be invalid.
        float aTotal = (intersectionAreaA + intersectionAreaB);
        intersectionArea = aTotal / (ellipseArea + 1e-7f);
        // return lerp(representativePtA, representativePtB, intersectionAreaB / (aTotal + 1e-7f));

        // float w0 = (1.f - squareWeighting) * intersectionAreaA;
        // float w1 = squareWeighting * intersectionAreaB;
        // return lerp(representativePtA, representativePtB, w1/(w0+w1+1e-7f));
        float2 repPtFromSquares = lerp(representativePtA, representativePtB, squareWeighting);

            // When we adjust the representative pt for square clipping,
            // we end up moving the point away from the reflection ray.
            // With GGX, this has a very quick and significant effect on the
            // brightness of the reflection. Even when part of the specular
            // cone is clipped, the very center has a dominant effect on the
            // brightness.
            // This means that adjusting the representative point to the intersection
            // center is having too extreme an effect. We have to tone it down a
            // little bit some how.
            // The easiest way is just to linearly blend from the reflection ray to
            // the intersection center...
        return lerp(repPtFromSquares, projectedCircleCenter, 0.5f);

    ///////////////////////////////////////////////////////////////////////////////////////////////////
    }
}


#endif
