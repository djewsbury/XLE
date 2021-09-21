
RWTexture3D<uint> TiledLightBitField : register(u0, space1);
Texture2D<float> DownsampleDepths : register(t2, space1);

static const uint GridDims = 16;

// #define MINIMIZE_ATOMIC_OPS 1

uint LaneIndexWithKey(uint key)
{
	// Inspired by Michal Drobot, "Improved Culling for Tiled and Clustered Rendering"
	// find which other lanes in the current wavegroup have the same key and return an
	// index representing where we fall within that set
	//
	// This isn't working corectly for me on my (nvidia pascal) hardware. Current theory is there
	// are lanes considered "active" by WavePrefixCountBits, but are implicitly disabled along
	// the edges of triangles (ie, probably to support pixel footprint operations). These disabled
	// lanes cannot write to the UAV, but they are active, so they can be selected as the first lane
	// for a given key. We sometimes attempt to write to the UAV from that lane, but the driver
	// blocks it
	uint laneIndex;
	for (;;) {
		uint firstLaneKey = WaveReadLaneFirst(key);
		laneIndex = WavePrefixCountBits(key == firstLaneKey);
		if (firstLaneKey == key) break;
	}
	return laneIndex;
}

[earlydepthstencil]
	void main(float4 position : SV_Position, nointerpolation uint objectIdx : OBJECT_INDEX)
{
	uint bitIdx = objectIdx%32;
	uint planeIdx = objectIdx/32;
	uint3 outputCoord = uint3(position.xy, planeIdx);
	bool write = position.z >= DownsampleDepths[outputCoord.xy];
	[branch] if (write) {
		uint outputKey = outputCoord.z+32*(outputCoord.x+(1920/GridDims)*outputCoord.y);
		#if defined(MINIMIZE_ATOMIC_OPS)
			uint laneIdx = LaneIndexWithKey(outputKey);
			[branch] if (laneIdx == 0) {
				InterlockedOr(TiledLightBitField[outputCoord], 1u<<bitIdx);
			}
		#else
			InterlockedOr(TiledLightBitField[outputCoord], 1u<<bitIdx);
		#endif
	}
}

#if 0

#include "xleres/Deferred/operator-util.hlsl"
#include "xleres/TechniqueLibrary/Math/TextureAlgorithm.hlsl"
#include "xleres/TechniqueLibrary/LightingEngine/LightDesc.hlsl"

StructuredBuffer<LightDesc> CombinedLightBuffer : register (t1, space1);
StructuredBuffer<uint> LightDepthTable : register(t3, space1);

float DistanceAttenuation(float distanceSq, float power) { return power / (distanceSq+1); }

float RadiusAttenuation(float distanceSq, float radius)
{
	float D = distanceSq; D *= D; D *= D;
	float R = radius; R *= R; R *= R; R *= R;
	return 1.f - saturate(3.f * D / R);
}

float4 visualize(
	float4 position : SV_Position, float2 texCoord : TEXCOORD0, 
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR, 
	SystemInputs sys) : SV_Target0
{
	// float4 col = float4(position.x/1920, position.y/1080, 0, 1);
	// return float4(col.xy-WaveReadLaneFirst(col).xy, 0, 1);

	uint3 tileCoord = uint3(position.xy/GridDims, 0);
#if 0
	float4 output = float4(0,0,0,1);
	for (;;) {
		uint linearCoord = (position.y*(1920/GridDims)+position.x);
		uint firstLinearCoord = WaveReadLaneFirst(linearCoord);
		bool isFirst = WavePrefixCountBits(linearCoord == firstLinearCoord) == 0;
		[branch] if (linearCoord == firstLinearCoord) {
			if (isFirst)
				output = float4(
					(TiledLightBitField[tileCoord]&0xff)/256.f,
					((TiledLightBitField[tileCoord]>>8)&0xff)/256.f,
					((TiledLightBitField[tileCoord]>>16)&0xff)/256.f,
					1);
			break;
		}
	}
	return output;
#elif 0
	uint outputKey = (tileCoord.x+(1920/GridDims)*tileCoord.y);
	uint laneIndex = LaneIndexWithKey(outputKey);
	if (laneIndex == 0) return 1;
	if ((uint(position.x)%32) == 0 || (uint(position.y)%32) == 0)
		return float4(0.5, 0.5, 0.5, 1);
	return 0;
	return float4(saturate(laneIndex/255.f), saturate(laneIndex/(255.f*255.f)), 0, 1);
#endif

#if 0
	return float4(
		(TiledLightBitField[tileCoord]&0xff)/256.f,
		((TiledLightBitField[tileCoord]>>8)&0xff)/256.f,
		((TiledLightBitField[tileCoord]>>16)&0xff)/256.f,
		1);
#endif

	LightOperatorInputs resolvePixel = LightOperatorInputs_Create(position, viewFrustumVector, sys);
	if (resolvePixel.ndcDepth == 1) return 0;
	// if (((uint(position.x)^uint(position.y))&3)!=0) return 0;

	uint lightCount = 512;
	float3 lightQuantity = 0;
#if 0
	for (uint planeIdx=0; planeIdx<(lightCount+31)/32; ++planeIdx) {
		uint bitField = TiledLightBitField[uint3(tileCoord.xy, planeIdx)];
		if (bitField) {
			uint s=firstbitlow(bitField), e=firstbithigh(bitField);
			for (uint bitIdx=s; bitIdx<=e; ++bitIdx) { 
				[branch] if (!(bitField & (1u<<bitIdx))) continue;
				LightDesc l = CombinedLightBuffer[planeIdx*32+bitIdx];

				float3 lightVector	 = l.Position - resolvePixel.worldPosition;
				float distanceSq	 = dot(lightVector, lightVector);
				if (distanceSq > l.CutoffRange*l.CutoffRange) {
					lightQuantity.b += 0.01f;
				}
			}
		}

		for (uint lightIdx=planeIdx*32; lightIdx<(planeIdx+1)*32; ++lightIdx) {
			uint bitIdx = lightIdx%32;
			if (bitField & (1u<<bitIdx)) continue;

			LightDesc l = CombinedLightBuffer[planeIdx*32+bitIdx];
			float3 lightVector	 = l.Position - resolvePixel.worldPosition;
			float distanceSq	 = dot(lightVector, lightVector);
			if (distanceSq < (l.CutoffRange*l.CutoffRange)) {
				lightQuantity.r += 1.0f;
			}
		}
	}
#elif 0
	for (uint planeIdx=0; planeIdx<(lightCount+31)/32; ++planeIdx) {
		uint bitField = TiledLightBitField[uint3(tileCoord.xy, planeIdx)];
		// bitField = WaveActiveBitOr(bitField);
		while (bitField != 0) {
			uint bitIdx = firstbitlow(bitField);
			bitField ^= (1u << bitIdx);

			LightDesc l = CombinedLightBuffer[planeIdx*32+bitIdx];
			float3 lightVector	 = l.Position - resolvePixel.worldPosition;
			float distanceSq	 = dot(lightVector, lightVector);
			float attenuation	 = DistanceAttenuation(distanceSq, 1);
			float radiusDropOff  = RadiusAttenuation(distanceSq, l.CutoffRange);
			lightQuantity += 0.01f * l.Brightness * attenuation * radiusDropOff;
		}
	}
#else
	float linearizedDepth = NDCDepthToLinear0To1(resolvePixel.ndcDepth);
	uint encodedDepthTable = LightDepthTable[linearizedDepth*1024];
	uint minIdx = encodedDepthTable & 0xff;
	uint maxIdx = encodedDepthTable >> 16;

	[branch] if (minIdx != maxIdx) {
		// minIdx = WaveActiveMin(minIdx);
		// maxIdx = WaveActiveMax(maxIdx);
		for (uint planeIdx=minIdx/32; planeIdx<=maxIdx/32; ++planeIdx) {
			uint bitField = TiledLightBitField[uint3(tileCoord.xy, planeIdx)];
			// bitField = WaveActiveBitOr(bitField);
			while (bitField != 0) {
				uint bitIdx = firstbitlow(bitField);
				bitField ^= (1u << bitIdx);

				LightDesc l = CombinedLightBuffer[planeIdx*32+bitIdx];
				float3 lightVector	 = l.Position - resolvePixel.worldPosition;
				float distanceSq	 = dot(lightVector, lightVector);
				float attenuation	 = DistanceAttenuation(distanceSq, 1);
				float radiusDropOff  = RadiusAttenuation(distanceSq, l.CutoffRange);
				lightQuantity += 0.01f * l.Brightness * attenuation * radiusDropOff;
			}
		}
	}
#endif

	return float4(lightQuantity, 1);
}

#endif
