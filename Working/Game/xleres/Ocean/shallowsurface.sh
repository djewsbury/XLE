// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OceanLighting.h"
#include "Ocean.h"
#include "OceanShallow.h"
#include "../MainGeometry.h"
#include "../Transform.h"
#include "../Colour.h"
#include "../BasicMaterial.h"

Texture2DArray<float2>		ShallowDerivatives : register(t5);
Texture2DArray<float>		ShallowFoamQuantity : register(t11);

///////////////////////////////////////////////////////////////////////////////////////////////////

Texture2DArray<float>	ShallowWaterHeights : register(t3);

VSOutput vs_main(uint vertexId : SV_VertexId)
{
    VSOutput output;

    uint2 p = uint2(
        vertexId % (SHALLOW_WATER_TILE_DIMENSION+1),
        vertexId / (SHALLOW_WATER_TILE_DIMENSION+1));

    float3 localPosition = float3(
        p.x / float(SHALLOW_WATER_TILE_DIMENSION),
        p.y / float(SHALLOW_WATER_TILE_DIMENSION),
        0.f);

    int3 coord = NormalizeRelativeGridCoord(p);
    if (coord.z >= 0)
        localPosition.z = ShallowWaterHeights.Load(uint4(coord, 0));

    #if GEO_HAS_INSTANCE_ID==1
        float3 worldPosition = InstanceWorldPosition(input, objectCentreWorld);
    #else
        float3 worldPosition = mul(LocalToWorld, float4(localPosition,1)).xyz;
    #endif

    output.position = mul(WorldToClip, float4(worldPosition,1));

    #if (OUTPUT_TEXCOORD==1)
        output.texCoord = localPosition.xy;
    #endif

    #if OUTPUT_WORLD_VIEW_VECTOR==1
        output.worldViewVector = WorldSpaceView.xyz - worldPosition.xyz;
    #endif

    return output;
}

float2 DecompressDerivatives(float2 textureSample, float3 scaleValues)
{
    const float normalizingScale = .5f;
    return 2.f / normalizingScale * (textureSample.xy - 0.5.xx) * scaleValues.xy * scaleValues.z;
}

float3 BuildNormalFromDerivatives(float2 derivativesSample)
{
        //	Rather than storing the normal within the normal map, we
        //	store (dhdx, dhdy). We can reconstruct the normal from
        //	this input with a cross product and normalize.
        //	The derivatives like this will work better with bilinear filtering
        //	and mipmapping.
    float3 u = float3(1.0f, 0.f, derivativesSample.x);
    float3 v = float3(0.f, 1.0f, derivativesSample.y);
    return normalize(cross(u, v));
}

[earlydepthstencil]
    float4 ps_main(VSOutput geo) : SV_Target0
{
    float3 directionToEye = 0.0.xxx;
    #if (OUTPUT_WORLD_VIEW_VECTOR==1)
        directionToEye = normalize(geo.worldViewVector);
    #endif

    float2 texCoord = geo.texCoord;

    float2 surfaceDerivatives = DecompressDerivatives(
        ShallowDerivatives.Sample(ClampingSampler, float3(texCoord.xy, ArrayIndex)).xy,
        1.0.xxx);

    float3 worldSpaceNormal = BuildNormalFromDerivatives(surfaceDerivatives);
    // return float4(worldSpaceNormal, 1.f);

    // float rawDiffuse = dot(worldSpaceNormal, BasicLight[0].NegativeDirection);
    //float specBrightness = CalculateSpecular(
    //    worldSpaceNormal, directionToEye,
    //    BasicLight[0].NegativeDirection,
    //    SpecularParameters_RoughF0(RoughnessMin, Material_SpecularToF0(SpecularMin)), rawDiffuse);

    OceanSurfaceSample oceanSurface = (OceanSurfaceSample)0;
    oceanSurface.worldSpaceNormal = worldSpaceNormal;
    oceanSurface.material.specular = SpecularMin;
    oceanSurface.material.roughness = RoughnessMin;
    oceanSurface.material.metal = 1.f;

    OceanParameters parameters;
    parameters.worldViewVector = geo.worldViewVector;
    parameters.worldViewDirection = normalize(geo.worldViewVector);
    parameters.pixelPosition = uint4(geo.position);

    OceanLightingParts parts = (OceanLightingParts)0;

        //
        //		Calculate all of the lighting effects
        //			- diffuse, specular, reflections, etc..
        //
        //		There's an interesting trick for reflecting
        //		specular -- use a high specular power and jitter
        //		the normal a bit
        //

    const float refractiveIndex = 1.333f;
    float3 opticalThickness	= 0.05f * float3(0.15f, 0.075f, 0.05f);

    CalculateReflectivityAndTransmission2(parts, oceanSurface, parameters, refractiveIndex, true);
    // CalculateFoam(parts, oceanSurface);
    CalculateRefractionValue(parts, geo.position.z, parameters, oceanSurface, refractiveIndex);
    CalculateUpwelling(parts, parameters, opticalThickness);
    CalculateSkyReflection(parts, oceanSurface, parameters);
    CalculateSpecular(parts, oceanSurface, parameters);

    float3 refractedAttenuation = exp(-opticalThickness * min(MaxDistanceToSimulate, parts.refractionAttenuationDepth));

    float3 colour =
          parts.transmission * refractedAttenuation * parts.refracted
        + parts.transmission * parts.upwelling
        + (1.f-parts.foamQuantity) * (parts.specular + parts.skyReflection)
        ;

    float4 result;
    result = float4(colour, 1.f);

    #if MAT_SKIP_LIGHTING_SCALE==0
        result.rgb *= LightingScale;		// (note -- should we scale by this here? when using this shader with a basic lighting pipeline [eg, for material preview], the scale is unwanted)
    #endif
    return result;
}