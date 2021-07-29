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

cbuffer EnvironmentProps
{
	uint LightCount;
};

StructuredBuffer<LightDesc> LightList : register (t1, space1);
StructuredBuffer<uint> LightDepthTable : register(t3, space1);
RWTexture3D<uint> TiledLightBitField : register(u0, space1);

static const uint TiledLights_DepthGradiations = 1024;
static const uint TiledLights_GridDims = 16;

float3 CalculateIllumination(
	GBufferValues sample, float3 directionToEye,
	float3 worldPosition, float linear0To1Depth,
	LightScreenDest screenDest)
{
	float3 result = 0.0.xxx;

	#if (VSOUT_HAS_NORMAL==1)

			// Calculate the shadowing of light sources (where we can)

		/*
		float shadowing[BASIC_LIGHT_COUNT];
		{
			[unroll] for (uint c=0; c<BASIC_LIGHT_COUNT; ++c)
				shadowing[c] = 1.f;

			#if SHADOW_CASCADE_MODE!=0 && (VULKAN!=1)
				bool enableNearCascade = false;
				#if SHADOW_ENABLE_NEAR_CASCADE != 0
					enableNearCascade = true;
				#endif

				CascadeAddress cascade = ResolveCascade_FromWorldPosition(worldPosition);
				if (cascade.cascadeIndex >= 0) {
					shadowing[0] = ResolveShadows_Cascade(
						cascade.cascadeIndex, cascade.frustumCoordinates, cascade.miniProjection,
						screenDest.pixelCoords, screenDest.sampleIndex, ShadowResolveConfig_NoFilter());
				}
			#endif
		}
		*/

			// note -- 	This loop must be unrolled when using GGX specular...
			//			This is because there are texture look-ups in the GGX
			//			implementation, and those can cause won't function
			//			correctly inside a dynamic loop

		uint encodedDepthTable = LightDepthTable[linear0To1Depth*TiledLights_DepthGradiations];
		uint minIdx = encodedDepthTable & 0xff;
		uint maxIdx = encodedDepthTable >> 16;

		LightSampleExtra sampleExtra;
		sampleExtra.screenSpaceOcclusion = 1;
		uint3 tileCoord = uint3(screenDest.pixelCoords.xy/TiledLights_GridDims, 0);

		[branch] if (minIdx != maxIdx) {
			// minIdx = WaveActiveMin(minIdx);
			// maxIdx = WaveActiveMax(maxIdx);
			for (uint planeIdx=minIdx/32; planeIdx<=maxIdx/32; ++planeIdx) {
				uint bitField = TiledLightBitField[uint3(tileCoord.xy, planeIdx)];
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
