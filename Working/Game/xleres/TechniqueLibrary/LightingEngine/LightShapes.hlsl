// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LIGHT_SHAPES_H)
#define LIGHT_SHAPES_H

#include "LightDesc.hlsl"
#include "DirectionalResolve.hlsl"
#include "AreaLights.hlsl"
#include "LightingAlgorithm.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////

float3 DirectionalLightResolve(
    GBufferValues sample,
    LightSampleExtra sampleExtra,
    LightDesc light,
    float3 worldPosition,
    float3 directionToEye,
    LightScreenDest screenDest)
{
    float3 diffuse = DirectionalLightResolve_Diffuse(sample, directionToEye, light.Position, light);
    float3 specular = DirectionalLightResolve_Specular(sample, directionToEye, light.Position, light, sampleExtra.screenSpaceOcclusion);
    return diffuse + specular;
}

    // note -- is there a performance or accuracy advantage to doing ReciprocalMagnitude this way?
float ReciprocalMagnitude(float3 vec)   { return rsqrt(dot(vec, vec)); }
float MagnitudeSquared(float3 vec)      { return dot(vec, vec); }

float3 RepresentativeVector_Sphere(out float distortionCompensation, float3 vectorToCenter, float lightRadius, float3 reflectionDir)
{
    // We want to find the "representative point" for a spherical light source
    // This is the point on the object that best represents the integral of all
    // incoming light. For a sphere, this is easy.. We just want to find the
    // point on the sphere closest to the reflection ray. This works so long as the
    // sphere is not (partially) below the equator. But we'll ignore the artefacts in
    // these cases.
    // See Brian Karis' 2013 Unreal course notes for more detail.
    // See also GPU Gems 5 for Drobot's research on this topic.
    // See also interesting shadertoy. "Distance Estimated Area Lights"
    //      https://www.shadertoy.com/view/4ss3Ws

    float3 L = vectorToCenter;
    float3 testPt = dot(reflectionDir, L) * reflectionDir;
    float t = lightRadius * ReciprocalMagnitude(testPt - L);

    // We can try to reduce the distortion on extreme angles
    // by reducing the highlight when the representation pt
    // is poor. Note that this adds a linear falloff to the
    // specular equation -- which is not really correct. But
    // we can justify it by saying we are just correcting for
    // the inaccuracy in the representative point.
    distortionCompensation = saturate(1.25f*t);

    return lerp(L, testPt, saturate(t));
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float3 SphereLightResolve(
    GBufferValues sample,
    LightSampleExtra sampleExtra,
    LightDesc light,
    float3 worldPosition,
    float3 directionToEye,
    LightScreenDest screenDest)
{
    float3 reflectionDir = reflect(-directionToEye, sample.worldSpaceNormal);
    float distortionCompensation;
    float3 specLightDir = RepresentativeVector_Sphere(distortionCompensation, light.Position - worldPosition, light.SourceRadiusX, reflectionDir);

    float distanceSq = MagnitudeSquared(light.Position - worldPosition);
    float rDistance = rsqrt(distanceSq);

        // note --  Here we should really be doing some extra work to calculate the
        //          incoming light power. We need to define what the light power really
        //          means...? Is it irradiance? Or power per surface area? Or the power
        //          than an equivalent point light source would have?
        //      Right now we're going to ignore that, and just use trivial implementations.
        //      Also, if we were using a specular equation that is normalizes for energy
        //      conservation, we also need to make special changes here... Again, we'll ignore.

        // note -- on high roughness materials, the specular seems to have very little effect
        //      beyond short radius. We could probably find a cut-off point and disable specular
        //      based on distance, source radius & power, & material roughness

    float3 diffuseLightDir = (light.Position - worldPosition)*rDistance;
    float3 diffuse = DirectionalLightResolve_Diffuse(sample, directionToEye, diffuseLightDir, light);
    float3 specular = DirectionalLightResolve_Specular(sample, directionToEye, normalize(specLightDir), light, sampleExtra.screenSpaceOcclusion);

        // Specular attenuation is a little tricky here... We want the light
        // brightness to drop off relative to the solid angle of the light source.
        // Karis has a rough estimate to an sphere light version of GGX.
        // He suggests using the ratio of the normalization factors for this estimated
        // GGX with a direction light source GGX.
        // It feels like more work could be done here... It seems that the distant specular
        // highlights are still too bright. Probably it should be compared to a reference
        // ray tracer solution.
    float alpha = sample.material.roughness * sample.material.roughness;
    float alphaPrime = saturate(alpha + light.SourceRadiusX * light.SourceRadiusX * .5f * rDistance);
    float specAttenuation = (alpha * alpha) / (alphaPrime * alphaPrime);
    specAttenuation *= distortionCompensation;

    float distanceAttenuation = saturate(DistanceAttenuation(distanceSq, 1.f));
    float radiusDropOff = CalculateRadiusLimitAttenuation(distanceSq, light.CutoffRange);

    return (radiusDropOff*distanceAttenuation)*(diffuse + specAttenuation*specular);
}

float TubeLightDiffuseIntegral(float3 L0, float3 L1, float3 N)
{
    // see the Unreal course notes and
    // http://www.cse.yorku.ca/~amana/research/linearLights.pdf

    float L0rmag = ReciprocalMagnitude(L0);
    float L1rmag = ReciprocalMagnitude(L1);
    float A = saturate(dot(N, L0) * .5f * L0rmag + dot(N, L1) * 0.5f * L1rmag);
    float B = 1.f/(L0rmag*L1rmag);
    return 2.f * A / (B + dot(L0, L1) + 2.f);
}

float3 RepresentativeVector_Tube(float3 L0, float3 L1, float3 reflectionDir)
{
    float3 Ld = L1 - L0;
    float LdmagSq = dot(Ld, Ld);
    float RdotLd = dot(reflectionDir, Ld);
    float t = (dot(reflectionDir, L0) * RdotLd - dot(L0, Ld)) / (LdmagSq - RdotLd*RdotLd);
    // float t =
    //      (dot(L0, Ld) * dot(reflectionDir, L0) - dot(L0, L0) * dot(reflectionDir, Ld))
    //    / (dot(L0, Ld) * dot(reflectionDir, Ld) - dot(Ld, Ld) * dot(reflectionDir, L0));
    return L0 + saturate(t) * Ld;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float3 TubeLightResolve(
    GBufferValues sample,
    LightSampleExtra sampleExtra,
    LightDesc light,
    float3 worldPosition,
    float3 directionToEye,
    LightScreenDest screenDest)
{
    float3 reflectionDir = reflect(-directionToEye, sample.worldSpaceNormal);

        // As per Karis' 2013 Unreal course notes, we can calculate tube lights using
        // different methods for diffuse and specular.
        // For diffuse, we can directly calculate the integral of NdotL against a line
        // For specular, we use a representative point (similar to the sphere implementation)

    float3 L0 = light.Position - light.SourceRadiusY * light.OrientationY;
    float3 L1 = light.Position + light.SourceRadiusY * light.OrientationY;

    float NdotL = TubeLightDiffuseIntegral(L0 - worldPosition, L1 - worldPosition, sample.worldSpaceNormal);

        // note --  After doing "RepresentativeVector_Tube" we could also use
        //          RepresentativeVector_Sphere to estimate a thick tube
    float3 tubePoint = RepresentativeVector_Tube(L0 - worldPosition, L1 - worldPosition, reflectionDir);
    float distortionCompensation;
    float3 specLightDir = RepresentativeVector_Sphere(distortionCompensation, tubePoint, light.SourceRadiusX, reflectionDir);

    // We can choose to use either "specLightDir" or "tubePoint" for our diffuse distance calc
    // If we use tubePoint, the radius will have no effect on the diffuse result (just on specular result)
    // This might not be perfectly correct, but it can avoid artefacts that can occur at certain angles
    float distanceSq = MagnitudeSquared(tubePoint - worldPosition);
    float rDistance = rsqrt(distanceSq);
    float3 diffuseRepDir = (tubePoint - worldPosition) * rDistance;

    float3 diffuse = DirectionalLightResolve_Diffuse_NdotL(sample, directionToEye, diffuseRepDir, NdotL, light);
    float3 specular = DirectionalLightResolve_Specular(sample, directionToEye, normalize(specLightDir), light, sampleExtra.screenSpaceOcclusion);

        // This specular attenuation method is based on Karis. Maybe it needs
        // a little more work...?
    float alpha = sample.material.roughness * sample.material.roughness;
    float alphaPrime0 = saturate(alpha + .5f * light.SourceRadiusY * light.SourceRadiusY * .5f * rDistance);
    float specAttenuation = alpha / alphaPrime0; // in principle the highlight is stretched in only one direction... so no square
    float alphaPrime1 = saturate(alpha + light.SourceRadiusX * light.SourceRadiusX * .5f * rDistance);
    specAttenuation *= (alpha * alpha) / (alphaPrime1 * alphaPrime1);
    specAttenuation *= distortionCompensation;

    float distanceAttenuation = saturate(DistanceAttenuation(distanceSq, 1.f));
    float radiusDropOff = CalculateRadiusLimitAttenuation(distanceSq, light.CutoffRange);

    return radiusDropOff*(diffuse + distanceAttenuation*specAttenuation*specular);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float3 RectangleLightResolve(
    GBufferValues sample,
    LightSampleExtra sampleExtra,
    LightDesc light,
    float3 worldPosition,
    float3 directionToEye,
    LightScreenDest screenDest)
{
        // We can define and light space where the light center is at the origin,
        // light is projected along +Z and +X and +Y lie on the light plane.
        // We can then choose to work either in world space or light space.
        // Let's use light space, because it might make doing boundary test
        // easier later.
        // It might be that working in world space could end up being fewer
        // calculations, but we've have to try it to find out.

    float3 lightCenter = light.Position;
    float2 lightHalfSize = float2(light.SourceRadiusX, light.SourceRadiusY);

        // Here lightToWorld is an orthogonal rotation matrix (ie no scale)
        // so we can use simplified transformation operations.
    float3x3 worldToLight = float3x3(light.OrientationX, light.OrientationY, light.OrientationZ);

    float3 samplePt = mul(worldToLight, worldPosition - lightCenter);
    float3 sampleNormal = mul(worldToLight, sample.worldSpaceNormal);
    float3 viewDirectionLight = mul(worldToLight, directionToEye);

    // if (samplePt.z < 0.f) return float4(0.0.xxx, 1.f);

    float2 repPt = RectangleDiffuseRepPoint(samplePt, sampleNormal, lightHalfSize);
    float3 lightNegDir = float3(repPt - samplePt.xy, -samplePt.z);
    float distanceSq = dot(lightNegDir, lightNegDir);
    float rDistance = rsqrt(distanceSq);
    lightNegDir *= rDistance;

        // we're doing the light calculations in "light space" also, so
        // adjust sample.worldSpaceNormal
    sample.worldSpaceNormal = sampleNormal;

        // We can just do the rest of the diffuse calculation in light space, also...
        // If it's just lambert, it's trivial.
    float NdotL = dot(sampleNormal, lightNegDir);
    float3 diffuse = DirectionalLightResolve_Diffuse_NdotL(sample, viewDirectionLight, lightNegDir, NdotL, light);

        // "cosThetaLightPlane" is the angle between the light normal the
        // direction from the light to the sample point. In this light type, light
        // is generally being emitted in the one direction, and so less light escapes
        // out tangental to the light surface. So, we consider this with cosThetaLightPlane.
        // In 3DS Max, rectangle lights don't work like this.
        //
        // Light escapes equally in all directions. However, our solution works poorly
        // when cosThetaLightPlane is near zero, mostly because of the "skewed normal"
        // calculations in RectangleDiffuseRepPoint. If we had a better solution for this
        // case, we could support the same light shape as max.
    float diffuseAttenuation;
    const uint RectLightType_SingleDir = 0; // cast light forward, along a single direction
    const uint RectLightType_TwoDir = 1;    // cast light forward and backward
    const uint RectLightType_Max = 2;       // cast light in all directions equally, like 3DS Quicksilver
    const uint rectLightType = RectLightType_SingleDir;
    if (rectLightType == RectLightType_SingleDir)   { diffuseAttenuation = saturate(-lightNegDir.z); }
    else if (rectLightType == RectLightType_TwoDir) { diffuseAttenuation = abs(lightNegDir.z); }
    else                                            { diffuseAttenuation = 1.f; }

    float intersectionArea;
    float2 specRepPt = RectangleSpecularRepPoint(intersectionArea, samplePt, sampleNormal, viewDirectionLight, lightHalfSize, sample.material.roughness);

    float3 specLightNegDir = float3(specRepPt - samplePt.xy, -samplePt.z);
    float specDistanceSq = MagnitudeSquared(specLightNegDir);
    specLightNegDir *= rsqrt(specDistanceSq);
    // float area = lightHalfSize.x * lightHalfSize.y * 4.f;
    // float integralApprox = intersectionArea / area;
    float integralApprox = intersectionArea;

        // note --  We can get some interesting results if we use "lightNegDir" here instead of
        //          specLightNegDir -- it's a good way to visualise the representative point we're using
        //          for the diffuse calculation.
    float3 specular = DirectionalLightResolve_Specular(sample, viewDirectionLight, specLightNegDir, light, sampleExtra.screenSpaceOcclusion);
    float specAttenuation = integralApprox;

    if (rectLightType == RectLightType_SingleDir)   { specAttenuation *= saturate(-specLightNegDir.z); }
    else if (rectLightType == RectLightType_TwoDir) { specAttenuation *= abs(specLightNegDir.z); }

        // Let's use the same distance for both diffuse and specular attenuation
        // (both to reduce the calculations, and because it makes sense.)
        // We can choose to use either the diffuse or specular rep point.
    float distanceAttenuation = saturate(DistanceAttenuation(distanceSq, 1.f));

        // We can attempt to normalize the specular calculation in much the
        // same way we do for spherical lights. Let's imagine the that light is
        // a disc with the same area as the rectangle. We can use that in the
        // alpha prime calculation for spherical lights. This is an approximation
        // for spherical lights already
    float alpha = sample.material.roughness * sample.material.roughness;
    float discRadius = sqrt(lightHalfSize.x * lightHalfSize.y * reciprocalPi);
    float alphaPrime = saturate(alpha + discRadius * discRadius * .5f * rDistance);
    specAttenuation *= (alpha * alpha) / (alphaPrime * alphaPrime);

    float radiusDropOff = CalculateRadiusLimitAttenuation(distanceSq, light.CutoffRange);

    //  Note that we can scale by "area" here if we define our light units in
    //  "luminous flux per area" units. But it seems more natural to separate
    //  the "brightness" quantity from area.

    return (distanceAttenuation*radiusDropOff) * (diffuseAttenuation*diffuse + specAttenuation*specular);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#endif
