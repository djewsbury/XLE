// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OceanLighting.hlsl"
#include "Ocean.hlsl"
#include "OceanShallow.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Utility/Colour.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "xleres/BasicMaterial.hlsl"
#include "../Lighting/resolvefog.hlsl"

Texture2D					Foam_Diffuse : register(t4);
Texture2DArray<float2>		ShallowDerivatives : register(t5);
Texture2DArray<float>		ShallowFoamQuantity : register(t14);

Texture2DArray<float>       ShallowWaterHeights : register(t3);

cbuffer ShallowWaterLighting
{
    float3 OpticalThickness	= 0.2f * float3(0.45f, 0.175f, 0.05f);
    float3 FoamColor = float3(0.5f, 0.5f, .5f);
    float Specular = .22f;
    float Roughness = .06f;
    float RefractiveIndex = 1.333f;
    float UpwellingScale = 0.33f;
    float SkyReflectionScale = 0.75f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

VSOUT vs_main(uint vertexId : SV_VertexId)
{
    VSOUT output;

    uint2 p = uint2(
        vertexId % (SHALLOW_WATER_TILE_DIMENSION+1),
        vertexId / (SHALLOW_WATER_TILE_DIMENSION+1));

    float3 localPosition = float3(
        p.x / float(SHALLOW_WATER_TILE_DIMENSION),
        p.y / float(SHALLOW_WATER_TILE_DIMENSION),
        0.f);

    #if SHALLOW_WATER_IS_SIMULATED==1
        int3 coord = NormalizeRelativeGridCoord(p);
        if (coord.z >= 0)
            localPosition.z = ShallowWaterHeights.Load(uint4(coord, 0)) - 3e-1f;
    #endif

    #if GEO_HAS_INSTANCE_ID
        float3 worldNormal;
        float3 worldPosition = InstanceWorldPosition(input, worldNormal, objectCentreWorld);
    #else
        float3 worldPosition = mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;
    #endif

    output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

    #if VSOUT_HAS_TEXCOORD
        output.texCoord = localPosition.xy;
    #endif

    #if VSOUT_HAS_WORLD_VIEW_VECTOR
        output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
    #endif

    #if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = ResolveOutputFogColor(worldPosition.xyz, SysUniform_GetWorldSpaceView().xyz);
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

float CalculateFoamFromFoamQuantity(float2 texCoord, float foamQuantity)
{
    if (foamQuantity > 0.f) {

            //	simplier, but nicer method.
            //	first tap is used as texture coordinate offset for
            //	second tap

        float4 foamFirstTap = Foam_Diffuse.Sample(DefaultSampler, 1.33f*texCoord + SysUniform_GetGlobalTime() * 0.001f * float2(0.0078f, 0.0046f));
        float foamSecondTap = Foam_Diffuse.Sample(DefaultSampler,
            23.7f*-texCoord
            + 0.027f * (-1.0.xx + 2.f * foamFirstTap.xy)
            + SysUniform_GetGlobalTime() * float2(0.023f, -0.015f));

        return smoothstep(0.f, foamSecondTap, foamQuantity);
    }

    return 0.f;
}

[earlydepthstencil]
    float4 ps_main(VSOUT geo) : SV_Target0
{
    float3 directionToEye = 0.0.xxx;
    #if VSOUT_HAS_WORLD_VIEW_VECTOR
        directionToEye = normalize(geo.worldViewVector);
    #endif

    float2 texCoord = geo.texCoord;

    #if SHALLOW_WATER_IS_SIMULATED==1
        float2 surfaceDerivatives = DecompressDerivatives(
            ShallowDerivatives.Sample(ClampingSampler, float3(texCoord.xy, ArrayIndex)).xy,
            1.0.xxx);
    #else
        float2 surfaceDerivatives = 0.0.xx;
    #endif

    float cameraDistance = length(geo.worldViewVector);
    float detailStrength = lerp(1.25f, .25f, saturate((cameraDistance - 150.f)/200.f));

    surfaceDerivatives += DecompressDerivatives(
        NormalsTexture.Sample(DefaultSampler, 1.f * texCoord.xy).xy,
        detailStrength.xxx);

    float3 worldSpaceNormal = BuildNormalFromDerivatives(surfaceDerivatives);

    OceanSurfaceSample oceanSurface = (OceanSurfaceSample)0;
    oceanSurface.worldSpaceNormal = worldSpaceNormal;
    oceanSurface.material.specular = Specular;
    oceanSurface.material.roughness = Roughness;
    oceanSurface.material.metal = 0.f;

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

    const float refractiveIndex = RefractiveIndex;
    float3 opticalThickness	= OpticalThickness;

    CalculateReflectivityAndTransmission2(parts, oceanSurface, parameters, refractiveIndex, true);
    // CalculateFoam(parts, oceanSurface);
    CalculateRefractionValue(parts, geo.position.z, parameters, oceanSurface, refractiveIndex);
    CalculateUpwelling(parts, parameters, opticalThickness);
    CalculateSkyReflection(parts, oceanSurface, parameters);
    CalculateSpecular(parts, oceanSurface, parameters);

    // return float4(LightingScale * parts.refracted, 1.f);

    float3 refractedAttenuation = exp(-opticalThickness * min(MaxDistanceToSimulate, parts.refractionAttenuationDepth));
    parts.upwelling *= UpwellingScale;
    parts.skyReflection *= SkyReflectionScale;

    parts.foamQuantity += 1.f-saturate(parts.forwardDistanceThroughWater*.75f);
    float foamTex = CalculateFoamFromFoamQuantity(0.05f * geo.texCoord, 0.5f * parts.foamQuantity);

    float3 color =
          parts.transmission * refractedAttenuation * parts.refracted
        + parts.transmission * parts.upwelling
        + (1.f-parts.foamQuantity) * (parts.specular + parts.skyReflection)
        + FoamColor * foamTex
        ;

    #if VSOUT_HAS_FOG_COLOR == 1
		color.rgb = geo.fogColor.rgb + color.rgb * geo.fogColor.a;
	#endif

    float4 result;
    result = float4(color, 1.f);

    #if MAT_SKIP_LIGHTING_SCALE==0
        result.rgb *= LightingScale;		// (note -- should we scale by this here? when using this shader with a basic lighting pipeline [eg, for material preview], the scale is unwanted)
    #endif
    return result;
}
