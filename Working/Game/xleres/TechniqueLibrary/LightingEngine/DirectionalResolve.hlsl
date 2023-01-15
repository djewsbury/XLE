// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DIRECTIONAL_RESOLVE_H)
#define DIRECTIONAL_RESOLVE_H

#include "SpecularMethods.hlsl"
#include "DiffuseMethods.hlsl"
#include "LightDesc.hlsl"

#include "../Framework/gbuffer.hlsl"
#include "../Math/MathConstants.hlsl"

float GetRoughness(GBufferValues sample) { return sample.material.roughness; }
float GetMetallicness(GBufferValues sample) { return sample.material.metal; }
float GetF0_0(GBufferValues sample) { return SpecularParameterToF0(sample.material.specular); }

float3 DirectionalLightResolve_Diffuse_NdotL(
	GBufferValues sample,
	float3 directionToEye,
	float3 negativeLightDirection,
	float NdotL,
	LightDesc light)
{
		// If we use specular light from both sides, we must also use diffuse from both sides
		// Otherwise we can get situations where a pixel gets specular light, but no diffuse
		//		-- which ends up appearing gray for diaeletrics
	#if MAT_DOUBLE_SIDED_LIGHTING
		NdotL = abs(NdotL);
	#else
		NdotL = saturate(NdotL);
	#endif

	float rawDiffuse = CalculateDiffuse(
		sample.worldSpaceNormal, directionToEye, negativeLightDirection,
		DiffuseParameters_Roughness(GetRoughness(sample), 0.5f, 2.5f));			// light.DiffuseWideningMin, light.DiffuseWideningMax));

    float metal = GetMetallicness(sample);
	float result = rawDiffuse * (1.0f - metal);
	result *= sample.cookedLightOcclusion;
	result *= NdotL;
	return result * light.Brightness.rgb * sample.diffuseAlbedo.rgb;
}

float3 DirectionalLightResolve_Diffuse(
	GBufferValues sample,
	float3 directionToEye,
	float3 negativeLightDirection,
	LightDesc light)
{
	float NdotL = dot(sample.worldSpaceNormal, negativeLightDirection);
	return DirectionalLightResolve_Diffuse_NdotL(sample, directionToEye, negativeLightDirection, NdotL, light);
}


float3 DirectionalLightResolve_Specular(
	GBufferValues sample,
	float3 directionToEye,
	float3 negativeLightDirection,
	LightDesc light,
	float screenSpaceOcclusion = 1.f)
{
		// HACK! preventing problems at very low roughness values
	float roughnessValue = max(0.03f, GetRoughness(sample));

		////////////////////////////////////////////////

		// In our "metal" lighting model, sample.diffuseAlbedo actually contains
		// per-wavelength F0 values.
	float3 metalF0 = sample.diffuseAlbedo;
	float3 F0_0 = lerp(GetF0_0(sample).xxx, metalF0, GetMetallicness(sample));

	SpecularParameters param0 = SpecularParameters_RoughF0Transmission(
		roughnessValue, F0_0, sample.transmission);

	float3 spec0 = CalculateSpecular(
		sample.worldSpaceNormal, directionToEye,
		negativeLightDirection, normalize(negativeLightDirection + directionToEye),
		param0);

	float specularOcclusion = screenSpaceOcclusion * sample.cookedLightOcclusion;
	const bool viewDependentOcclusion = true;
	if (viewDependentOcclusion) {
		float NdotV = dot(sample.worldSpaceNormal, directionToEye);
		#if MAT_DOUBLE_SIDED_LIGHTING
			NdotV *= sign(dot(sample.worldSpaceNormal, negativeLightDirection));
		#endif
		specularOcclusion = TriAceSpecularOcclusion(NdotV, specularOcclusion);
	}

	// note -- specular occlusion is going to apply to both reflected and transmitted specular
	return spec0 * specularOcclusion * light.Brightness;
}

#endif
