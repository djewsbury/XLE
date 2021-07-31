// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LIGHTING_FORWARD_H)
#define LIGHTING_FORWARD_H

#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightShapes.hlsl"
// #include "../TechniqueLibrary/SceneEngine/Lighting/AmbientResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowsResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"

cbuffer EnvironmentProps : register (b0, space3)
{
	LightDesc DominantLight;
	uint LightCount;
};

StructuredBuffer<LightDesc> LightList : register (t1, space3);
StructuredBuffer<uint> LightDepthTable : register(t2, space3);
Texture3D<uint> TiledLightBitField : register(t3, space3);

static const uint TiledLights_DepthGradiations = 1024;
static const uint TiledLights_GridDims = 16;

float3 CalculateIllumination(
	GBufferValues sample, float3 directionToEye,
	float3 worldPosition, float linear0To1Depth,
	LightScreenDest screenDest)
{
	float3 result = 0.0.xxx;

	LightSampleExtra sampleExtra;
	sampleExtra.screenSpaceOcclusion = 1;

	#if (VSOUT_HAS_NORMAL==1)

			// Calculate the shadowing of light sources (where we can)

		#if defined(DOMINANT_LIGHT_SHAPE)
			{
				float shadowing = 1.f;
				bool enableNearCascade = false;
				#if SHADOW_ENABLE_NEAR_CASCADE != 0
					enableNearCascade = true;
				#endif

				CascadeAddress cascade = ResolveCascade_FromWorldPosition(worldPosition, sample.worldSpaceNormal);
				if (cascade.cascadeIndex >= 0) {
					shadowing = ResolveShadows_Cascade(
						cascade.cascadeIndex, cascade.frustumCoordinates, cascade.frustumSpaceNormal, cascade.miniProjection,
						screenDest.pixelCoords, screenDest.sampleIndex, ShadowResolveConfig_NoFilter());
				}

				#if DOMINANT_LIGHT_SHAPE == 0
					result += shadowing * DirectionalLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#elif DOMINANT_LIGHT_SHAPE == 1
					result += shadowing * SphereLightResolve(sample, sampleExtra, DominantLight, worldPosition, directionToEye, screenDest);
				#endif
			}
		#endif

			// note -- 	This loop must be unrolled when using GGX specular...
			//			This is because there are texture look-ups in the GGX
			//			implementation, and those can cause won't function
			//			correctly inside a dynamic loop

		uint encodedDepthTable = LightDepthTable[linear0To1Depth*TiledLights_DepthGradiations];
		uint minIdx = encodedDepthTable & 0xff;
		uint maxIdx = encodedDepthTable >> 16;

		uint3 tileCoord = uint3(screenDest.pixelCoords.xy/TiledLights_GridDims, 0);

		[branch] if (minIdx != maxIdx) {
			// minIdx = WaveActiveMin(minIdx);
			// maxIdx = WaveActiveMax(maxIdx);
			for (uint planeIdx=minIdx/32; planeIdx<=maxIdx/32; ++planeIdx) {
				uint bitField = TiledLightBitField.Load(uint4(tileCoord.xy, planeIdx, 0));
				// bitField = WaveActiveBitOr(bitField);
				while (bitField != 0) {
					uint bitIdx = firstbitlow(bitField);
					bitField ^= (1u << bitIdx);

					LightDesc l = LightList[planeIdx*32+bitIdx];
					float shadowing = 1.0f;
					if (l.Shape == 0) {
						result += shadowing * DirectionalLightResolve(sample, sampleExtra, l, worldPosition, directionToEye, screenDest);
					} else if (l.Shape == 1) {
						result += shadowing * SphereLightResolve(sample, sampleExtra, l, worldPosition, directionToEye, screenDest);
					}
				}
			}
		}
	#endif

	/*result += LightResolve_Ambient(
		sample, directionToEye, BasicAmbient, screenDest,
		AmbientResolveHelpers_Default());*/

	return result;
}

#endif
