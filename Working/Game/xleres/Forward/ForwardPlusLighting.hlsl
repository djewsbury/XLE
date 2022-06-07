// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LIGHTING_FORWARD_H)
#define LIGHTING_FORWARD_H

#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightShapes.hlsl"
#include "../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowsResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowProbes.hlsl"
#include "../TechniqueLibrary/LightingEngine/SphericalHarmonics.hlsl"
#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"

cbuffer EnvironmentProps : register (b0, space3)
{
	LightDesc DominantLight;
	uint LightCount;
	bool EnableSSR;
	float4 DiffuseSHCoefficients[25];			// todo -- require premultiplied coefficients instead of reference coefficients
};

StructuredBuffer<LightDesc> LightList : register (t1, space3);
StructuredBuffer<uint> LightDepthTable : register(t2, space3);
Texture3D<uint> TiledLightBitField : register(t3, space3);

Texture2D<float3> SSR : register(t4, space3);
Texture2D<float> SSRConfidence : register(t5, space3);

static const uint TiledLights_DepthGradiations = 1024;
static const uint TiledLights_GridDims = 16;

float3 CalculateSkyReflectionFresnel(GBufferValues sample, float3 viewDirection)
{
	float3 F0 = lerp(SpecularParameterToF0(sample.material.specular).xxx, sample.diffuseAlbedo, sample.material.metal);
	return SchlickFresnelF0(viewDirection, sample.worldSpaceNormal, F0);
}

float3 LightResolve_Ambient(GBufferValues sample, float3 directionToEye, LightScreenDest lsd)
{
	float metal = sample.material.metal;
	float3 diffuseSHRef = 0;
	for (uint c=0; c<25; ++c)
		diffuseSHRef += ResolveSH_Reference(DiffuseSHCoefficients[c].rgb, c, sample.worldSpaceNormal);
	float3 result = diffuseSHRef*(1.0f - metal)*sample.diffuseAlbedo.rgb;

	#if !defined(PROBE_PREPARE)
		float3 fresnel = CalculateSkyReflectionFresnel(sample, directionToEye);
		fresnel *= float(EnableSSR); 
		result += fresnel * SSRConfidence.Load(uint3(lsd.pixelCoords, 0)) * SSR.Load(uint3(lsd.pixelCoords, 0)).rgb;
	#endif

	return result; 
}

uint MaskBitsUntil(uint bitIdx) { return (1u<<bitIdx)-1u; }

float3 CalculateIllumination(
	GBufferValues sample, float3 directionToEye,
	float3 worldPosition, float3 worldGeometryNormal, float linear0To1Depth,
	LightScreenDest screenDest, bool hasNormal)
{
	float3 result = 0.0.xxx;

	LightSampleExtra sampleExtra;
	sampleExtra.screenSpaceOcclusion = 1;

	if (hasNormal) {

			// Calculate the shadowing of light sources (where we can)

		#if defined(DOMINANT_LIGHT_SHAPE)
			{
				float shadowing = 1.f;

				#if (DOMINANT_LIGHT_SHAPE & 0x20) == 0x20
					CascadeAddress cascade = ResolveCascade_FromWorldPosition(worldPosition, worldGeometryNormal);
					if (cascade.cascadeIndex >= 0)
						shadowing = ResolveShadows_Cascade(cascade, screenDest.pixelCoords, screenDest.sampleIndex, ShadowResolveConfig_Default());
				#endif

				#if (DOMINANT_LIGHT_SHAPE & 0x7) == 0
					result += shadowing * DirectionalLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#elif (DOMINANT_LIGHT_SHAPE & 0x7) == 1
					result += shadowing * SphereLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#endif
			}
		#endif

		#if !defined(PROBE_PREPARE)

			uint encodedDepthTable = LightDepthTable[linear0To1Depth*TiledLights_DepthGradiations];
			uint minIdx = encodedDepthTable & 0xff;
			uint maxIdx = encodedDepthTable >> 16;

			uint3 tileCoord = uint3(screenDest.pixelCoords.xy/TiledLights_GridDims, 0);

			[branch] if (minIdx != maxIdx) {
				// minIdx = WaveActiveMin(minIdx);
				// maxIdx = WaveActiveMax(maxIdx);
				uint firstPlane=minIdx/32, lastPlane=maxIdx/32; 
				for (uint planeIdx=firstPlane; planeIdx<=lastPlane; ++planeIdx) {
					uint bitField = TiledLightBitField.Load(uint4(tileCoord.xy, planeIdx, 0));
					if (planeIdx == firstPlane )
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
								shadowing = SampleStaticDatabase(l.StaticDatabaseLightId-1, worldPosition-l.Position, screenDest);
						#endif

						if (l.Shape == 0) {
							result += shadowing * DirectionalLightResolve(sample, sampleExtra, l, worldPosition, directionToEye, screenDest);
						} else if (l.Shape == 1) {
							result += shadowing * SphereLightResolve(sample, sampleExtra, l, worldPosition, directionToEye, screenDest);
						}
					}
				}
			}

		#endif
	}

	result += LightResolve_Ambient(sample, directionToEye, screenDest);
	result += sample.emissive;

	return result;
}

#endif
