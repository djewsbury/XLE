// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(OCEAN_LIGHTING_H)
#define OCEAN_LIGHTING_H

#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../Lighting/BasicLightingEnvironment.hlsl"
#include "../Lighting/DirectionalResolve.hlsl"
#include "../Lighting/AmbientResolve.hlsl"
#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../TechniqueLibrary/Math/Misc.hlsl"

    //
    //  Special case lighting for ocean & water surface
    //  We want to pay special attention to specular & reflections,
    //  as well as water specific features like upwelling, refraction
    //  and surface foam.
    //

Texture2D					RefractionsTexture : register(t9);
Texture2D_MaybeMS<float>	DepthBuffer : register(t10);
Texture2D					DynamicReflectionTexture;

static const bool			Specular_JitterNormal = true;
static const float			Specular_JitterNormalStrength = 0.075f; // .115f; // 0.075f;
static const uint			Specular_JitterNormalCount = 3;

static const float			MaxDistanceToSimulate = 100.f;

struct OceanSurfaceSample
{
    float3 worldSpaceNormal;
    float4 compressedNormal;
    float foamQuantity;
    float3 foamAlbedo;
    float normalMapAccuracy;
    float4 dynamicReflectionTexCoord;
    PerPixelMaterialParam material;
};

struct OceanLightingParts
{
    float3 upwelling;
    float3 refracted;
    float3 foamAlbedo;
    float3 specular;
    float3 skyReflection;

    float refractionAttenuationDepth;
    float forwardDistanceThroughWater;		// distance through the water from the viewport to the refraction point
    float reflectivity;
    float transmission;
    float foamQuantity;
};

struct OceanParameters
{
    float3 worldViewVector;
    float3 worldViewDirection;
    uint4 pixelPosition;
};

struct ReflectionAndTransmission
{
    float reflectivity;
    float transmission;
};

GBufferValues BuildGBufferValues(OceanSurfaceSample oceanSample)
{
    GBufferValues sample = GBufferValues_Default();
    sample.diffuseAlbedo = 0.0.xxx;
    sample.worldSpaceNormal = oceanSample.worldSpaceNormal;
    sample.material = oceanSample.material;
    return sample;
}

ReflectionAndTransmission CalculateReflectionAndTransmission(float3 incidentDirection, float3 normal, float refractiveIndex)
{
    float thetai = acos(dot(-normal, incidentDirection));
    float sinthetat = sin(thetai)/refractiveIndex;
    float thetat = asin(sinthetat);

        //	Using full expansion of snell's law
        //	(can be approximated with schlick fresnel)
    float A = sin(thetat - thetai) / sin(thetat + thetai);
    float B = tan(thetat - thetai) / tan(thetat + thetai);

    ReflectionAndTransmission result;
    result.reflectivity = .5f * (A*A + B*B);
    result.transmission = 1.f - result.reflectivity;
    return result;
}

float3 CalculateUpwelling(float angle, OceanLightingParts parts, float3 opticalThickness)
{
        //
        //	Calculate an approximate upwelling value. This is
        //	a basic single scattering simulation
        //
        //		We want to find the brightness of a line passing
        //		beneath the ocean surface. The brightness of a
        //		point beneath the ocean surface is related to the
        //		exponential of the depth. We need to find the
        //		integral of the graph of these changing values.
        //
    const float distanceToSimulate = min(MaxDistanceToSimulate, parts.forwardDistanceThroughWater); // 25.f;
    const uint steps = 8;
    float hDistance = distanceToSimulate;
    float xDistance = cos(angle) * distanceToSimulate;
    float dDistance = sin(angle) * distanceToSimulate;

    float3 result = 0.f;
    const float surfaceBrightness = 1.f;
    for (uint c=0; c<steps; ++c) {

            // step linearly through
        float depth = (c+1)/float(steps) * dDistance;
        float width = hDistance / float(steps);
        float postScatterDistance = (c+1)/float(steps) * hDistance;

            //	simulate some light that travels down from the surface,
            //	reflects in the view direction and then travels towards the
            //	viewer.
        float3 brightness = (surfaceBrightness * width) * exp(-opticalThickness * (depth+postScatterDistance));
        result += brightness;
    }

    return result;
}

void CalculateRefractionValue(	inout OceanLightingParts parts,
                                float oceanDepth,
                                OceanParameters parameters,
                                OceanSurfaceSample oceanSurface,
                                float refractiveIndex)
{
#if MAT_DO_REFRACTION==1
    float oceanBottomDepthValue = LoadFloat1(DepthBuffer, parameters.pixelPosition.xy, 0);

    float oceanBottomLinearDepth = NDCDepthToWorldSpace(oceanBottomDepthValue);
    float oceanLinearDepth = NDCDepthToWorldSpace(oceanDepth);
    float linearDepthThroughWater = oceanBottomLinearDepth - oceanLinearDepth;

    if (linearDepthThroughWater > (.25f * SysUniform_GetFarClip()) || oceanBottomLinearDepth > (.75f * SysUniform_GetFarClip())) {
        parts.refractionAttenuationDepth = 100000.f;
        parts.forwardDistanceThroughWater = 100000.f;
        parts.refracted = 0.0.xxx;
        return;
    }

    const float airRefractiveIndex = 1.f;
    float3 incidentRay = -parameters.worldViewDirection;
    float3 refractedVector = refract(incidentRay, oceanSurface.worldSpaceNormal, airRefractiveIndex/refractiveIndex);
    // refractedVector = normalize(refractedVector);

    float3 worldSpaceOceanSurface = SysUniform_GetWorldSpaceView() - parameters.worldViewVector;

        //	Using equal triangles, we can find the vertical depth through the
        //	water by multiplying the depth along the worldViewDirection by
        //	the z component of the worldViewDirection. Here, we're assuming
        //	"parameters.worldViewDirection" is unit length.
    float zDistanceThroughWater = parameters.worldViewDirection.z * linearDepthThroughWater;

    zDistanceThroughWater = min(zDistanceThroughWater, MaxDistanceToSimulate);
    float3 refractedOceanBottom = worldSpaceOceanSurface + refractedVector * (-zDistanceThroughWater/refractedVector.z);

    float4 t = mul(SysUniform_GetWorldToClip(), float4(refractedOceanBottom.xyz,1));
    float2 refractTexCoord = .5f + .5f * t.xy / t.w;
    refractTexCoord.y = 1.0f - refractTexCoord.y;

        //	imagine a V pattern -- light travels straight down
        //	to the point on the bottom of the ocean, then back in
        //	the direction to the viewer. We can cause attenuation
        //	all along this line.
        //		(Separate optical thickness values for R, G, B?)
    parts.refractionAttenuationDepth = linearDepthThroughWater + zDistanceThroughWater;
    parts.forwardDistanceThroughWater = linearDepthThroughWater;

    float edgeFactor = 1.f;
    if (false) {	// fade off edges of refaction
        float2 edgeTest = min(refractTexCoord.xy, 1.0f-refractTexCoord.xy);
        edgeFactor = saturate(15.f * min(edgeTest.x, edgeTest.y));
    }

    float3 refractionTextureSample = (1.0f/LightingScale) * RefractionsTexture.SampleLevel(ClampingSampler, refractTexCoord, 0).rgb;
    parts.refracted = lerp(0.25.xxx, refractionTextureSample, edgeFactor);
#endif
}

void CalculateUpwelling(inout OceanLightingParts parts,
                        OceanParameters parameters,
                        float3 opticalThickness)
{
    float angleToFlatSurface = acos(dot(float3(0.f, 0.f, 1.f), -parameters.worldViewDirection));
    parts.upwelling = CalculateUpwelling(angleToFlatSurface, parts, opticalThickness);
}

void CalculateFoam(	inout OceanLightingParts parts,
                    OceanSurfaceSample oceanSurface,
                    float foamBrightness)
{
    // add basic lambert diffuse to the foam to give it a bit of punch...
    float diffuseAmount = saturate(dot(oceanSurface.worldSpaceNormal, BasicLight[0].Position));
    parts.foamAlbedo = ((0.1f + diffuseAmount) * foamBrightness) * oceanSurface.foamAlbedo;
    parts.foamQuantity = oceanSurface.foamQuantity;
}

float CalculateFresnel(float3 worldSpaceNormal, float3 worldViewDirection, float refractiveIndex)
{
    float3 worldSpaceReflection = reflect(-worldViewDirection, worldSpaceNormal);
    float3 halfVector = normalize(worldSpaceReflection + worldViewDirection);
    float fresnel = SchlickFresnel(worldViewDirection, halfVector, refractiveIndex);
    return fresnel;
}

void CalculateReflectivityAndTransmission2(	inout OceanLightingParts parts,
                                            OceanSurfaceSample oceanSurface,
                                            OceanParameters parameters,
                                            float refractiveIndex, bool doFresnel)
{
    parts.reflectivity = 1.f;
    parts.transmission = 1.f;

    if (doFresnel) {
        parts.reflectivity = CalculateFresnel(
            oceanSurface.worldSpaceNormal, parameters.worldViewDirection, refractiveIndex);
        parts.transmission = 1.f - parts.reflectivity;

        const bool useShlickApprox = true;
        if (!useShlickApprox) {
            // float3 worldSpaceReflection = reflect(worldViewVectorDirection, oceanSurface.worldSpaceNormal);
            // ReflectionAndTransmission r = CalculateReflectionAndTransmission(worldSpaceReflection, oceanSurface.worldSpaceNormal);
            ReflectionAndTransmission r = CalculateReflectionAndTransmission(
                -parameters.worldViewDirection, oceanSurface.worldSpaceNormal, refractiveIndex);
            parts.reflectivity = r.reflectivity;
            parts.transmission = r.transmission;
        }
    }

    // parts.reflectivity *= .4f + .6f * oceanSurface.specularity;
}

void CalculateSkyReflection(inout OceanLightingParts parts,
                            OceanSurfaceSample oceanSurface,
                            OceanParameters parameters)
{
    float3 reflectionVector = reflect(-parameters.worldViewDirection, oceanSurface.worldSpaceNormal);

    #if MAT_DYNAMIC_REFLECTION == 1

        #if DO_REFLECTION_IN_VS==1
            float2 reflTC = oceanSurface.dynamicReflectionTexCoord.xy / oceanSurface.dynamicReflectionTexCoord.w;
            reflTC += oceanSurface.worldSpaceNormal.xy * ReflectionBumpScale;
            parts.skyReflection = DynamicReflectionTexture.Sample(
                ClampingSampler, reflTC);
        #else
        #endif
        parts.skyReflection /= LightingScale;

    #else

            // todo --  we should consider using the normal instead of the half-vector
            //          for fresnel calculations here. Since water should have close to
            //          mirror reflections (as least some of the time)
        parts.skyReflection = LightResolve_Ambient(
            BuildGBufferValues(oceanSurface),
            parameters.worldViewDirection, BasicAmbient,
            LightScreenDest_Create(parameters.pixelPosition.xy, 0),
            AmbientResolveHelpers_Default(),
            true);

    #endif

}

float3 DoSingleSpecular(OceanSurfaceSample oceanSample, float3 worldViewDirection)
{
        // todo --  we should consider using the normal instead of the half-vector
        //          for fresnel calculations here. Since water should have close to
        //          mirror reflections (as least some of the time)
    return DirectionalLightResolve_Specular(BuildGBufferValues(oceanSample), worldViewDirection, BasicLight[0].Position, BasicLight[0], 1.f, true);
}

void CalculateSpecular(	inout OceanLightingParts parts,
                        OceanSurfaceSample oceanSurface,
                        OceanParameters parameters)
{
        //
        //			Here in an interesting hack for ocean specular...
        //			let's jitter the normal a bit. This has a different
        //			affect on the shape of the highlight than changing
        //			the specular power -- and can make the highlight feel
        //			longer when the sun is low. If the jittering is too
        //			much, however -- it will break apart into a U shape.
        //
        // todo --	maybe only do the normal jittering around the specular
        //			highlight
    float3 baseSpecular = DoSingleSpecular(oceanSurface, parameters.worldViewDirection);

    if (Specular_JitterNormal) {
        float dither = DitherPatternValue(parameters.pixelPosition.xy);

        float3 result = 0.0.xxx;
        const float scale = 2.f * pi / float(Specular_JitterNormalCount);
        for (uint c=0; c<Specular_JitterNormalCount; ++c) {
            float angle = (float(c)+dither) * scale;
            float2 sinecosine;
            sincos(angle, sinecosine.x, sinecosine.y);

            float3 jitteredNormal =
                oceanSurface.worldSpaceNormal + float3(Specular_JitterNormalStrength * sinecosine.yx, 0.f);
            const bool doNormalize = true;
            if (doNormalize) {
                jitteredNormal = normalize(jitteredNormal);
            } else {
                    // rough approximation of normalize (it seems too inaccurate, and makes the strangely result unbalanced)
                jitteredNormal *= 1.f/(1.f + .075f*Specular_JitterNormalStrength);
            }

            OceanSurfaceSample jitteredSample = oceanSurface;
            jitteredSample.worldSpaceNormal = jitteredNormal;
            result += DoSingleSpecular(jitteredSample, parameters.worldViewDirection);
        }
        parts.specular = (2.f*baseSpecular + result) * (1.f/float(2+Specular_JitterNormalCount));
    } else {
        parts.specular = baseSpecular;
    }
}

#endif
