// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Framework/gbuffer.hlsl"

Texture2DMS<float>		DepthBuffer : register(t0);
Texture2DMS<float4>		GBuffer_Normals : register(t1);

void main(float4 position : SV_Position, float2 texCoord : TEXCOORD0)
{
	float minDepth = 1e12, maxDepth = -1e12;
	float depthValues[MSAA_SAMPLES];
	#if MSAA_SAMPLES > 1
		for (int c=0; c<MSAA_SAMPLES; ++c) 
	#else
		const int c=0;
	#endif
	{
		depthValues[c] = DepthBuffer.Load(int2(position.xy), c);
		minDepth = min(minDepth, depthValues[c]);
		maxDepth = max(maxDepth, depthValues[c]);
	}

	float minNormalDot = 1.f, baseNormalMagnitudeSq = 1.f;
	#if MSAA_SAMPLES > 1
			//		Some normals in the normal buffer can sometime be unnormalized
			//		(because of blending that occurs for terrain layers)
			//		We can deal with this without normalizes by looking
			//		at the magnitude squared of the base normal. It's not perfect,
			//		but might be ok.
		float3 baseNormal = DecodeGBufferNormal(GBuffer_Normals.Load(int2(position.xy), 0));
		baseNormalMagnitudeSq = dot(baseNormal, baseNormal);
		for (int c2=1; c2<MSAA_SAMPLES; ++c2) {
			float3 sampleNormal = DecodeGBufferNormal(GBuffer_Normals.Load(int2(position.xy), c2));
			float d = dot(sampleNormal, baseNormal);
			minNormalDot = min(d, minNormalDot);
		}
	#endif

	bool isEdgeSample = false;
	if (minNormalDot < 0.8f * baseNormalMagnitudeSq) {

			//	Large discontinuity in the normal means it must be an edge
			//	Sometimes there are large normal discontinuities and small
			//	depth discontinuities.
		isEdgeSample = true;

	} else {

			//
			//		Try to pick out edges where aliasing
			//		can happen, by looking for depth discontinuities
			//
			//		We want to avoid marking entire polygons on shear angles
			//			-- maybe it would be best to look at the derivative 
			//				of depths?
			//

		const bool relationalDifference = true;
		if (relationalDifference) {
			float x = 0.005f * minDepth;
			isEdgeSample = (maxDepth - minDepth) >= x;
		} else {
			isEdgeSample = (maxDepth - minDepth) >= 0.000025f;
		}
	}

	if (!isEdgeSample) {
		discard;
	}
}

