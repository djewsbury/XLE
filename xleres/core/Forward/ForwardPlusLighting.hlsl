// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(FORWARD_PLUS_LIGHTING_H)
#define FORWARD_PLUS_LIGHTING_H

#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightShapes.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowProjection.hlsl"
#include "../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowsResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowProbes.hlsl"
#include "../TechniqueLibrary/LightingEngine/SphericalHarmonics.hlsl"
#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"

#if SPECULAR_IBL
	#include "../TechniqueLibrary/LightingEngine/ImageBased.hlsl"
#endif

cbuffer EnvironmentProps : register (b0, space2)
{
	LightDesc DominantLight;
	uint LightCount;
	bool EnableSSR;
	float4 DiffuseSHCoefficients[25];			// todo -- require premultiplied coefficients instead of reference coefficients
};

StructuredBuffer<LightDesc> LightList : register (t1, space2);
StructuredBuffer<uint> LightDepthTable : register(t2, space2);
Texture2DArray<uint> TiledLightBitField : register(t3, space2);

Texture2D<float3> SSRTexture : register(t4, space2);
Texture2D<float> SSRConfidence : register(t5, space2);
#if SSAO
	Texture2D<float> SSAOTexture : register(t6, space2);
#endif

#if SPECULAR_IBL
	TextureCube SpecularIBL : register(t14, space2);
#endif
Texture2D<float2> GlossLUT : register(t15, space2);			// this is the look up table used in the split-sum IBL glossy reflections

Texture2D<float>			FPNoiseTexture 			: register(t13, space2);
SamplerComparisonState		ShadowSampler           : register(s16, space2);
SamplerState				ShadowDepthSampler      : register(s17, space2);
Texture2DArray<float> 		PriorityLightShadowTextures BIND_SHADOW_T3;

static const uint TiledLights_DepthGradiations = 1024;
static const uint TiledLights_GridDims = 16;

float3 CalculateSkyReflectionFresnel(GBufferValues sample, float3 viewDirection)
{
	float3 F0 = lerp(SpecularParameterToF0(sample.material.specular).xxx, sample.diffuseAlbedo, sample.material.metal);
	return SchlickFresnelF0(viewDirection, sample.worldSpaceNormal, F0);
}

float3 CalculateDistantReflections(GBufferValues sample, float3 directionToEye, LightScreenDest lsd)
{
	#if SPECULAR_IBL
		float3 F0 = lerp(SpecularParameterToF0(sample.material.specular).xxx, NormalizeMetalColor(sample.diffuseAlbedo), sample.material.metal);
		SpecularParameters specParam = SpecularParameters_RoughF0(sample.material.roughness, F0);
		return SpecularIBLLookup(sample.worldSpaceNormal, directionToEye, specParam, lsd);
	#else
		return 0;
	#endif
}

float3 LightResolve_Ambient(GBufferValues sample, float3 directionToEye, float screenSpaceOcclusion, LightScreenDest lsd)
{
	float metal = sample.material.metal;
	float3 diffuseSHRef = 0;
	for (uint c=0; c<25; ++c)
		diffuseSHRef += ResolveSH_Reference(DiffuseSHCoefficients[c].rgb, c, sample.worldSpaceNormal);
	float3 result = diffuseSHRef*(1.0f - metal)*sample.diffuseAlbedo.rgb;

	#if !defined(PROBE_PREPARE)
		float3 distanceReflections = CalculateDistantReflections(sample, directionToEye, lsd);
		#if SSR
			if (EnableSSR) {
				float3 fresnel = CalculateSkyReflectionFresnel(sample, directionToEye);
				float ssrConfidence = SSRConfidence.Load(uint3(lsd.pixelCoords, 0));
				distanceReflections = lerp(distanceReflections, fresnel * SSRTexture.Load(uint3(lsd.pixelCoords, 0)).rgb, ssrConfidence);
			}
		#endif
		result += distanceReflections;
	#endif

	result *= screenSpaceOcclusion;
	return result; 
}

uint MaskBitsUntil(uint bitIdx) { return (1u<<bitIdx)-1u; }

float3 CalculateIllumination(
	GBufferValues sample, float3 directionToEye,
	float3 worldPosition, float3 worldGeometryNormal, float linear0To1Depth,
	LightScreenDest screenDest, bool hasNormal)
{
	float3 result = 0.0.xxx;

	LightSampleExtra sampleExtra, sampleExtraNoSSAO;
	sampleExtra.screenSpaceOcclusion = 1;
	sampleExtraNoSSAO.screenSpaceOcclusion = 1;
	#if SSAO
		sampleExtra.screenSpaceOcclusion = SSAOTexture.Load(uint3(screenDest.pixelCoords, 0));
	#endif

	if (hasNormal) {

			// Calculate the shadowing of light sources (where we can)

		#if defined(DOMINANT_LIGHT_SHAPE)
			{
				float shadowing = 1.f;
				ShadowResolveContext shadowResolveContext;
				shadowResolveContext.ShadowTextures = PriorityLightShadowTextures;

				#if (DOMINANT_LIGHT_SHAPE & 0x20) == 0x20
					CascadeAddress cascade = ResolveCascade_FromWorldPosition(worldPosition, worldGeometryNormal);
					if (cascade.cascadeIndex >= 0)
						shadowing = ResolveShadows_Cascade(shadowResolveContext, cascade, screenDest.pixelCoords, screenDest.sampleIndex, ShadowResolveConfig_Default());
				#endif

				shadowing = min(shadowing, sample.cookedLightOcclusion);

				#if (DOMINANT_LIGHT_SHAPE & 0x7) == LIGHT_SHAPE_DIRECTIONAL
					result += shadowing * DirectionalLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#elif (DOMINANT_LIGHT_SHAPE & 0x7) == LIGHT_SHAPE_SPHERE
					result += shadowing * SphereLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#elif (DOMINANT_LIGHT_SHAPE & 0x7) == LIGHT_SHAPE_CONE
					result += shadowing * ConeLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#endif
			}
		#endif

		#if !defined(PROBE_PREPARE)

			uint encodedDepthTable = LightDepthTable[linear0To1Depth*TiledLights_DepthGradiations];
			uint minIdx = encodedDepthTable & 0xffffu;
			uint maxIdx = encodedDepthTable >> 16u;

			uint3 tileCoord = uint3(screenDest.pixelCoords.xy/TiledLights_GridDims, 0);

			[branch] if (minIdx != maxIdx) {
				// minIdx = WaveActiveMin(minIdx);
				// maxIdx = WaveActiveMax(maxIdx);
				uint firstPlane=minIdx/32, lastPlane=maxIdx/32; 
				for (uint planeIdx=firstPlane; planeIdx<=lastPlane; ++planeIdx) {
					uint bitField = TiledLightBitField.Load(uint4(tileCoord.xy, planeIdx, 0));
					if (planeIdx == firstPlane)
						bitField &= ~MaskBitsUntil(minIdx%32u);
					if (planeIdx == lastPlane)
						bitField &= MaskBitsUntil(maxIdx%32u) | (1u<<(maxIdx%32u));
					// bitField = WaveActiveBitOr(bitField);
					while (bitField != 0) {
						uint bitIdx = firstbitlow(bitField);
						bitField ^= (1u << bitIdx);

						uint idx = planeIdx*32+bitIdx;
						LightDesc l = LightList[idx];

						float shadowing = 1.0f;
						#if SHADOW_PROBE
							if (l.StaticDatabaseLightId != 0)
								shadowing = SampleStaticShadowDatabase(l.StaticDatabaseLightId-1, worldPosition-l.Position, screenDest);
						#endif

						#if DYNAMIC_SHADOW_PROBE
							if (l.DynamicCubeDatabaseLightId != 0)
								shadowing *= SampleDynamicCubeShadowDatabase(l.DynamicCubeDatabaseLightId-1, worldPosition-l.Position, screenDest);
						#endif

						[branch] if (l.Shape == LIGHT_SHAPE_DIRECTIONAL) {
							result += shadowing * DirectionalLightResolve(sample, sampleExtraNoSSAO, l, worldPosition, directionToEye, screenDest);
						} else if ((l.Shape|2) == LIGHT_SHAPE_CONE) {
							float3 light = SphereLightResolve(sample, sampleExtraNoSSAO, l, worldPosition, directionToEye, screenDest);
							[branch] if (l.Shape == LIGHT_SHAPE_CONE) light *= ConeLightShapeMultipler(l, worldPosition, screenDest);
							result += shadowing * light;
						}
						#if defined(_DEBUG)
							else result += float3(1,0,0);
						#endif
					}
				}
			}

		#endif
	}

	result += LightResolve_Ambient(sample, directionToEye, sampleExtra.screenSpaceOcclusion, screenDest);
	result += sample.emissive;

	return result;
}

float2 GlossLUT_Sample(float2 tc) { return GlossLUT.SampleLevel(ClampingSampler, tc, 0).xy; }
#if SPECULAR_IBL
	float3 SpecularIBL_Sample(float3 cubemapCoords, float mipmapLevel) { return SpecularIBL.SampleLevel(DefaultSampler, cubemapCoords, mipmapLevel).rgb; }
#endif
Texture2D<float> GetNoiseTexture() { return FPNoiseTexture; }
SamplerComparisonState GetShadowSampler() { return ShadowSampler; }
SamplerState GetShadowDepthSampler() { return ShadowDepthSampler; }

#endif
