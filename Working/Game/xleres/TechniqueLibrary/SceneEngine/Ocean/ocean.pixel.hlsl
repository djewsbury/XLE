// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VSOUT_HAS_TEXCOORD 1
#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
#define DO_REFLECTION_IN_VS 1
#define VSOUT_HAS_FOG_COLOR 1

#if (MAT_DYNAMIC_REFLECTION==1) && (DO_REFLECTION_IN_VS==1)
	#define VSOUTPUT_EXTRA float4 dynamicReflectionTexCoord : DYNREFLTC; float2 specularityTC : SPECTC;
#elif MAT_USE_SHALLOW_WATER==1
	#define VSOUTPUT_EXTRA float4 shallowWaterTexCoord : SHALLOWWATER; float2 specularityTC : SPECTC;
#else
	#define VSOUTPUT_EXTRA float2 specularityTC : SPECTC;
#endif

#include "Ocean.hlsl"
#include "OceanLighting.hlsl"
#include "OceanRenderingConstants.hlsl"

#include "../Effects/Sky.pixel.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../Framework/gbuffer.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"
#include "../../Utility/Colour.hlsl"
#include "../../Math/perlinnoise.hlsl"

Texture2D<float>			FoamQuantity : register(t3);
Texture2D					Foam_Diffuse : register(t4);

#if MAT_USE_SHALLOW_WATER==1
	Texture2DArray<float2>		ShallowDerivatives;
	Texture2DArray<float>		ShallowFoamQuantity;
	Texture2D<uint>				ShallowGridsLookupTable;
#endif

Texture2D<float>			SurfaceSpecularity;

static const bool			DoFresnel = true;

// #define USE_ACCURATE_NORMAL_DECOMPRESSION 1

float4 DecompressGBufferNormal2(float4 gBufferNormalSample)
{
    float3 rangeAdj = -1.0.xxx + 2.f * gBufferNormalSample.xyz;
    return float4(normalize(rangeAdj), gBufferNormalSample.a);
}

float4 CompressedNormalMapSample(Texture2D tex, float2 texCoord)
{
		//
		//	Because of the way we store the normals in the normal
		//	map, we need custom interpolation... We also have to
		//	handle mipmap filtering, because mipmaps are important
		//	here...
		//
	uint2 dims; tex.GetDimensions(dims.x, dims.y);
	float mipMapLevel = max(0.f, CalculateMipmapLevel(texCoord, dims.xy));

	texCoord = frac(texCoord);
    float2 exploded = texCoord*float2(dims.xy);
	exploded -= 0.5.xx;

	uint l = uint(mipMapLevel); uint2 d = dims.xy >> l;
	exploded.xy /= float(1<<l);
	float4 sample000 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy) + uint2(0,0))%d.xy, l)));
	float4 sample010 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy) + uint2(0,1))%d.xy, l)));
	float4 sample100 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy) + uint2(1,0))%d.xy, l)));
	float4 sample110 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy) + uint2(1,1))%d.xy, l)));

	float4 sample001 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy/2.f) + uint2(0,0))%(d.xy/2), l+1)));
	float4 sample011 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy/2.f) + uint2(0,1))%(d.xy/2), l+1)));
	float4 sample101 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy/2.f) + uint2(1,0))%(d.xy/2), l+1)));
	float4 sample111 = DecompressGBufferNormal2(tex.Load(uint3((uint2(exploded.xy/2.f) + uint2(1,1))%(d.xy/2), l+1)));

	float2 f = frac(exploded);
	float m = frac(mipMapLevel);
	float4 result =
		  sample000 * (1.f-f.x) * (1.f-f.y) * (1.f-m)
		+ sample010 * (1.f-f.x) * (    f.y) * (1.f-m)
		+ sample100 * (    f.x) * (1.f-f.y) * (1.f-m)
		+ sample110 * (    f.x) * (    f.y) * (1.f-m)
		+ sample001 * (1.f-f.x) * (1.f-f.y) * (    m)
		+ sample011 * (1.f-f.x) * (    f.y) * (    m)
		+ sample101 * (    f.x) * (1.f-f.y) * (    m)
		+ sample111 * (    f.x) * (    f.y) * (    m)
		;
	// result.rgb = normalize(result.rgb);
	return result;
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

OceanSurfaceSample BuildOceanSurfaceSample(
	float2 texCoord,
	float3 shallowWaterTexCoord, float shallowWaterWeight,
	float4 dynamicReflectionTexCoord, float2 specularityTC,
	float distanceToViewerSq)
{
	OceanSurfaceSample result;

		//	The normals texture is a little unusual, because it's stored in
		//	world space coordinates (not tangent space coordinates).
		//	Also, we use the same normal compression scheme as the gbuffer (so
		//	we have to normalize after lookup)
	#if MAT_USE_DERIVATIVES_MAP
				// blend in multiple samples from the derivatives map
				//		-- so we get some high frequency texture and movement
		float2 derivativesValues = DecompressDerivatives(
			NormalsTexture.Sample(DefaultSampler, texCoord).xy,
			float3(StrengthConstantXY.xx, StrengthConstantZ));

		#if MAT_USE_SHALLOW_WATER==1

			#if 0
				float shallowWaterArrayIndex = shallowWaterTexCoord.z;
			#else
					//	in this case, we need to recalculate the array index (post interpolation)
					//	if shallowWaterTexCoord.z is a whole value, they we can just use that array index...?
				float shallowWaterArrayIndex;
				[branch] if (frac(shallowWaterTexCoord.z) < 0.00001f) {
					shallowWaterArrayIndex = float(shallowWaterTexCoord.z);
				} else {
					int2 a = int2(shallowWaterTexCoord.xy);
					shallowWaterArrayIndex = CalculateShallowWaterArrayIndex(
						ShallowGridsLookupTable, a);
				}
			#endif

				//	we have to use "ClampingSampler" because we're not correctly
				//	interpolating when some of the linear interpolation samples are
				//	on different tiles. We could handle this case, but it seems like
				//	an unecessary hassle (clamping should be a reasonable approximation).
			float2 shallowDerivativesValues = 0.0.xx;
			if (shallowWaterArrayIndex < 128) {

					//	crazy mipmapping problem here... beause of the "frac", here, texture coordinates
					//	are not continuous from pixel to pixel. So, on the edges, the wrong mipmap is selected
					//	by the default mipmap logic. So we need to manually calculate the correct mipmap level
					//	level.

				uint3 derivativesTextureSize;
				ShallowDerivatives.GetDimensions(derivativesTextureSize.x, derivativesTextureSize.y, derivativesTextureSize.z);
				float mipmapLevel = CalculateMipmapLevel(shallowWaterTexCoord.xy, derivativesTextureSize.xy);
				shallowDerivativesValues = DecompressDerivatives(
					ShallowDerivatives.SampleLevel(
						ClampingSampler, float3(frac(shallowWaterTexCoord.xy), shallowWaterArrayIndex), mipmapLevel).xy,
					1.0.xxx);
			}

			// float2 shallowDerivativesValues =
			// 	DecompressDerivatives(ShallowDerivatives.Load(uint4(frac(shallowWaterTexCoord.xy)*64, shallowWaterArrayIndex, 0)).xy);

			derivativesValues = lerp(derivativesValues, shallowDerivativesValues, shallowWaterWeight);
		#endif

		float strengthDistanceScale = saturate(20000.f / distanceToViewerSq);

		derivativesValues +=
			DecompressDerivatives(
				NormalsTexture.Sample(DefaultSampler, /*float2(0.37, 0.59f) +*/ texCoord.xy * -DetailNormalFrequency).xy,
				strengthDistanceScale * DetailNormalsStrength.xxx);

		result.worldSpaceNormal = BuildNormalFromDerivatives(derivativesValues);
		result.compressedNormal = float4(result.worldSpaceNormal.rgb, 1);
	#elif USE_ACCURATE_NORMAL_DECOMPRESSION == 1
		result.compressedNormal = CompressedNormalMapSample(NormalsTexture, texCoord);
		result.worldSpaceNormal = compressedNormal.rgb;
	#else
		result.compressedNormal = NormalsTexture.Sample(DefaultSampler, texCoord);
		result.worldSpaceNormal = DecodeGBufferNormal(compressedNormal);
	#endif

	// float specularity = SurfaceSpecularity.Sample(DefaultSampler, specularityTC);
	result.material.metal = 1.f;
	result.material.specular = 0.22f;	// (has no bearing)
	result.material.roughness = MatRoughness;

	result.foamAlbedo = 1.0.xxx;
	float foamQuantity = FoamQuantity.SampleLevel(DefaultSampler, texCoord, 0);

	#if MAT_USE_SHALLOW_WATER==1
		float shallowFoamQuantity = ShallowFoamQuantity.SampleLevel(DefaultSampler, shallowWaterTexCoord, 0);
		foamQuantity = lerp(foamQuantity, shallowFoamQuantity, shallowWaterWeight);
	#endif

		// build a random quantity of foam to cover the surface.
	float randomFoam = SurfaceSpecularity.Sample(DefaultSampler, float2(0.005f*SysUniform_GetGlobalTime(), 0.f) + 0.1f * specularityTC);
	randomFoam = randomFoam * randomFoam * randomFoam;
	randomFoam *= 0.5f;
	foamQuantity += randomFoam * saturate(20000.f / distanceToViewerSq);

	result.dynamicReflectionTexCoord = dynamicReflectionTexCoord;

	result.foamQuantity = foamQuantity;
	return result;
}

float CalculateFoamFromFoamQuantity(float2 texCoord, float foamQuantity)
{
	if (foamQuantity > 0.f) {

		#if 0

				// 3 taps of the foam texture gives a detailed and interesting result
				//		todo -- foam should fade off with distance, because the tiling becomes too obvious
			float texAmount;
			float4 foamDiffuse = Foam_Diffuse.Sample(DefaultSampler, 23.72f*texCoord + SysUniform_GetGlobalTime() * float2(0.01f, 0.005f));
			if (foamQuantity > .666f)		{ texAmount = lerp(foamDiffuse.g, foamDiffuse.r, (foamQuantity-.666f)/.333f); }
			else if (foamQuantity > .333f)	{ texAmount = lerp(foamDiffuse.b, foamDiffuse.g, (foamQuantity-.333f)/.333f); }
			else							{ texAmount = foamDiffuse.b * foamQuantity/.333f; }

			foamDiffuse = Foam_Diffuse.Sample(DefaultSampler, 6.72f*-texCoord + SysUniform_GetGlobalTime() * float2(0.023f, -0.015f));
			if (foamQuantity > .666f)		{ texAmount += lerp(foamDiffuse.g, foamDiffuse.r, (foamQuantity-.666f)/.333f); }
			else if (foamQuantity > .333f)	{ texAmount += lerp(foamDiffuse.b, foamDiffuse.g, (foamQuantity-.333f)/.333f); }
			else							{ texAmount += foamDiffuse.b * foamQuantity/.333f; }

			foamDiffuse = Foam_Diffuse.Sample(DefaultSampler, 19.12f*-texCoord + SysUniform_GetGlobalTime() * float2(-0.0132f, 0.0094f));
			if (foamQuantity > .666f)		{ texAmount += lerp(foamDiffuse.g, foamDiffuse.r, (foamQuantity-.666f)/.333f); }
			else if (foamQuantity > .333f)	{ texAmount += lerp(foamDiffuse.b, foamDiffuse.g, (foamQuantity-.333f)/.333f); }
			else							{ texAmount += foamDiffuse.b * foamQuantity/.333f; }

			return foamQuantity*foamQuantity*min(1, texAmount);

		#else

				//	simplier, but nicer method.
				//	first tap is used as texture coordinate offset for
				//	second tap

			float4 foamFirstTap = Foam_Diffuse.Sample(DefaultSampler, 1.33f*texCoord + SysUniform_GetGlobalTime() * float2(0.0078f, 0.0046f));
			float foamSecondTap = Foam_Diffuse.Sample(DefaultSampler,
				23.7f*-texCoord
				+ 0.027f * (-1.0.xx + 2.f * foamFirstTap.xy)
				+ SysUniform_GetGlobalTime() * float2(0.023f, -0.015f));

			return smoothstep(0.f, foamSecondTap, foamQuantity);

		#endif

		#if 0
			float3 foamNormal = float3(0,0,1); //SampleNormalMap(Foam_Normal, DefaultSampler, false, foamTexScale*geo.texCoord);
			float foamSpecularity = 1.f; // Foam_Specularity.Sample(DefaultSampler, foamTexScale*geo.texCoord);
			result.worldSpaceNormal = normalize(result.worldSpaceNormal + foamQuantity * foamNormal);
			result.reflectivity += foamSpecularity * foamQuantity;
		#endif
	}

	return 0.f;
}

[earlydepthstencil]
	GBufferEncoded Deferred(VSOUT geo)
{
	GBufferValues result = GBufferValues_Default();

	float3 shallowWaterCoords = 0.0.xxx;
	float shallowWaterWeight = 0.f;
	float4 dynamicReflectionTexCoord = 0.0.xxxx;
	#if MAT_USE_SHALLOW_WATER==1
		shallowWaterCoords = geo.shallowWaterTexCoord.xyz;
		shallowWaterWeight = geo.shallowWaterTexCoord.w;
	#endif
	#if MAT_DYNAMIC_REFLECTION==1
		dynamicReflectionTexCoord = geo.dynamicReflectionTexCoord;
	#endif
	float distanceToViewerSq = dot(geo.worldViewVector, geo.worldViewVector);
	OceanSurfaceSample oceanSurface = BuildOceanSurfaceSample(
		geo.texCoord, shallowWaterCoords, shallowWaterWeight,
		dynamicReflectionTexCoord, geo.specularityTC, distanceToViewerSq);
	result.diffuseAlbedo = lerp(0.0.xxx/*oceanSurface.diffuseAlbedo*/, oceanSurface.foamAlbedo, oceanSurface.foamQuantity);

//	if (DoFresnel) {
//		result.reflectivity = CalculateFresnel(oceanSurface.worldSpaceNormal, geo.worldViewVector);
//	}

	result.normalMapAccuracy = oceanSurface.normalMapAccuracy;
	return Encode(result);
}

[earlydepthstencil]
	float4 Illum(VSOUT geo) : SV_Target0
{
	float3 shallowWaterCoords = 0.0.xxx;
	float shallowWaterWeight = 0.f;
	float4 dynamicReflectionTexCoord = 0.0.xxxx;
	#if MAT_USE_SHALLOW_WATER==1
		shallowWaterCoords = geo.shallowWaterTexCoord.xyz;
		shallowWaterWeight = geo.shallowWaterTexCoord.w;
	#endif
	#if MAT_DYNAMIC_REFLECTION==1
		dynamicReflectionTexCoord = geo.dynamicReflectionTexCoord;
	#endif
	float distanceToViewerSq = dot(geo.worldViewVector, geo.worldViewVector);
	OceanSurfaceSample oceanSurface = BuildOceanSurfaceSample(
		geo.texCoord, shallowWaterCoords, shallowWaterWeight,
		dynamicReflectionTexCoord, geo.specularityTC, distanceToViewerSq);

	// return float4(.5f + .5f * oceanSurface.worldSpaceNormal.xyz, 1.f);

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

	CalculateReflectivityAndTransmission2(parts, oceanSurface, parameters, RefractiveIndex, DoFresnel);
	CalculateFoam(parts, oceanSurface, FoamBrightness);
	CalculateRefractionValue(parts, geo.position.z, parameters, oceanSurface, RefractiveIndex);
	CalculateUpwelling(parts, parameters, OpticalThickness);
	CalculateSkyReflection(parts, oceanSurface, parameters);
	CalculateSpecular(parts, oceanSurface, parameters);

	parts.upwelling *= UpwellingScale;
	parts.foamQuantity += 1.f-saturate(parts.forwardDistanceThroughWater*.2f);
	parts.foamQuantity = saturate(parts.foamQuantity);
	float foamTex = CalculateFoamFromFoamQuantity(geo.texCoord, parts.foamQuantity);
	parts.skyReflection.rgb *= SkyReflectionBrightness;

	float3 refractedAttenuation = exp(-OpticalThickness * min(MaxDistanceToSimulate, parts.refractionAttenuationDepth));

	#if OCEAN_SEPARATE == 1
		return float4(LightingScale * 0.05f * parts.refractionAttenuationDepth.xxx, 1.f);
	#elif OCEAN_SEPARATE == 2
		return float4(LightingScale * parts.upwelling, 1.f);
	#elif OCEAN_SEPARATE == 3
		return float4(LightingScale * parts.refracted, 1.f);
	#elif OCEAN_SEPARATE == 4
		return float4(LightingScale * parts.specular, 1.f);
	#elif OCEAN_SEPARATE == 5
		return float4(LightingScale * parts.skyReflection.rgb, 1.f);
	#elif OCEAN_SEPARATE == 6
		return float4(LightingScale * parts.transmission.xxx, 1.f);
	#elif OCEAN_SEPARATE == 7
		return float4(LightingScale * parts.reflectivity.xxx, 1.f);
	#elif OCEAN_SEPARATE == 8
		return float4(LightingScale * .5.xxx + .5f * oceanSurface.worldSpaceNormal.xyz, 1.f);
	#elif OCEAN_SEPARATE == 9
		return float4(LightingScale * parts.forwardDistanceThroughWater.xxx / 500.f, 1.f);
	#elif OCEAN_SEPARATE == 10
		return float4(LightingScale * parts.refractionAttenuationDepth.xxx / 500.f, 1.f);
	#elif OCEAN_SEPARATE == 11
		return float4(LightingScale * refractedAttenuation.xxx, 1.f);
	#endif

	float3 color =
		  parts.transmission * refractedAttenuation * parts.refracted
		+ parts.transmission * parts.upwelling
		+ (1.f-parts.foamQuantity) * (parts.specular + parts.skyReflection)
		+ (foamTex * parts.foamAlbedo)
		;

	#if VSOUT_HAS_FOG_COLOR == 1
		color.rgb = geo.fogColor.rgb + color.rgb * geo.fogColor.a;
	#endif

	return float4(LightingScale * color, 1.f);
}
