// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSConstants.hlsl"
#include "../LightingAlgorithm.hlsl"
#include "../../../Math/ProjectionMath.hlsl"
#include "../../../Math/TextureAlgorithm.hlsl"
#include "../../../Framework/gbuffer.hlsl"

RWTexture2D<float2>			ReflectionsMask;
Texture2D<float4>			DownSampledNormals	: register(t1);
Texture2D<float>			DownSampledDepth	: register(t2);

#include "ReflectionUtility.hlsl"
#include "PixelBasedIteration.hlsl"

static const uint SamplesPerBlock = 64;
static const uint BlockDimension = 64;

cbuffer SamplingPattern
{
	uint4 SamplePoint[SamplesPerBlock];
	uint4 ClosestSamples[BlockDimension][BlockDimension/4];
	uint4 ClosestSamples2[BlockDimension][BlockDimension/4];
};

///////////////////////////////////////////////////////////////////////////////////////////

bool LookForCollision(ReflectionRay2 ray, uint stepCount, uint2 outputDimensions, float randomizerValue)
{
	bool result = false;
	[unroll] for (uint c=0; c<stepCount; ++c) {
		float d = GetStepDistance(c, stepCount, randomizerValue);
		float3 ndc = TestPtAsNDC(GetTestPt(ray, d));
		result = result|IsCollision(ndc.z, LoadDepth(ndc.xy, outputDimensions));
	}
	return result;
}

bool LookForCollision_Ref(ReflectionRay2 ray, uint2 outputDimensions, float randomizerValue)
{
	PBISettings settings;
	settings.pixelStep = 8;
	settings.initialPixelsSkip = 2 + int(randomizerValue * (settings.pixelStep+1));

	return PixelBasedIteration(
		ViewToClipSpace(ray.viewStart), ViewToClipSpace(ray.viewEnd),
		float2(outputDimensions), settings)._gotIntersection;
}

uint IterationOperator(int2 pixelCapCoord, float entryNDCDepth, float exitNDCDepth, inout float ilastQueryDepth)
{
	#if defined(DEPTH_IN_LINEAR_COORDS)
		// not implemented
	#else
		float queryDepth = DownSampledDepth[pixelCapCoord];
	#endif
	float lastQueryDepth = ilastQueryDepth;
	ilastQueryDepth = queryDepth;

		//	collide first with the "cap" of the pixel. If the ray
		//	penetrates the pixel cap, we know that there is definitely
		//	an intersection
	const float epsilon = 0.f;
//	if (	queryDepth >= min(ndc0, ndc1) - epsilon
//		&&	queryDepth <= max(ndc0, ndc1) + epsilon) {

		//	try to distinquish front-face and back-face collisions by looking at
		//	the direction we pass through this pixel cap
	if (entryNDCDepth <= queryDepth && exitNDCDepth >= queryDepth) {
		return 2;   // cap intersection
	}

		//	As collide with the edge of the pixel. Sometimes an edge in depth
		//	space represents a continuous edge in world space. But other times,
		//	there is a continiuity there. We can use an epsilon value to try to
		//	distinquish the two.
		//	Note that if the ray intersects a discontinuous edge, we have 2 options:
		//		* finish the ray here, and create a "shadow" in the reflection
		//		* just continue the ray behind the object, and look for a high
		//			quality intersection later.
		//
		//	Also note that there are two types of intersections: back-face and front-face.
		//	We don't really want to stop at a back-face intersection
	const float continuousEpsilon = 0.005f;	// (have to be rough for build mask)
	bool isFrontFace = true; // (exitNDCDepth > entryNDCDepth) != (queryDepth > lastQueryDepth);
	if (abs(lastQueryDepth - queryDepth) < continuousEpsilon && isFrontFace) {
		if (entryNDCDepth >= min(lastQueryDepth, queryDepth) && entryNDCDepth <= max(lastQueryDepth, queryDepth)) {
			return 1;
		}
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////

groupshared bool SampleFoundCollision[SamplesPerBlock];

float ReciprocalLength(float3 v) { return rsqrt(dot(v, v)); }

[numthreads(1, 1, 64)]
	void BuildMask(uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
		//
		//		Split the buffer into 64x64 pixel blocks.
		//		In each block, sample the offsets in "samplePoint"
		//

	uint2 outputDimensions;
	DownSampledDepth.GetDimensions(outputDimensions.x, outputDimensions.y);

	const uint msaaSampleIndex = 0;
	uint rayCastSample = groupIndex;

		//
		//		We do 1 sample ray test per thread. This avoids putting a complex
		//		loop within the shader, and should give us much better parallelism (particularly
		//		at low resolutions)
		//
	uint2 sampleOffset = SamplePoint[rayCastSample].xy;
 	uint2 samplingPixel = dispatchThreadId.xy * BlockDimension.xx + sampleOffset;

	bool foundCollision = false;

		// Iteration:
		//   0: new code
		//   1: reference high resolution iteration
	const uint iterationMethod = 0;
	const float worldSpaceMaxDist = min(MaxReflectionDistanceWorld, SysUniform_GetFarClip());
	if (iterationMethod == 0) {
		ReflectionRay2 ray = CalculateReflectionRay2(worldSpaceMaxDist, samplingPixel.xy, outputDimensions, msaaSampleIndex);
		[branch] if (ray.valid)
			foundCollision = LookForCollision(ray, MaskTestsPerRay, outputDimensions, GetRandomizerValue(dispatchThreadId.xy));
	} else if (iterationMethod == 1) {
		ReflectionRay2 ray = CalculateReflectionRay2(worldSpaceMaxDist, samplingPixel.xy, outputDimensions, msaaSampleIndex);
		[branch] if (ray.valid)
			foundCollision = LookForCollision_Ref(ray, outputDimensions, GetRandomizerValue(dispatchThreadId.xy));
	}

	SampleFoundCollision[rayCastSample] = foundCollision;

		////////// BARRIER! //////////
			//		Wait until every thread in the group has completed the above sample ray test

	GroupMemoryBarrierWithGroupSync();

		//
		//		Assuming BlockDimension == SamplesPerBlock
		//		We can split each "y" iteration through this
		//		loop into a different compute shader thread
		//			(just better for parrallelism than a loop)
		//
		//		We're mostly reading from "SampleFoundCollision" here
		//			... so that must be "groupshared"
		//
	uint y = groupIndex;
	for (uint x=0; x<BlockDimension; ++x) {

			//	We know the 4 closest samples to this pixel.
			//	Find them out and find how many successful samples
		int samplesWithCollisions = 0;
		uint packedSampleIndices = ClosestSamples[y][x/4][x%4];
		uint packedSampleIndices2 = ClosestSamples2[y][x/4][x%4];
		samplesWithCollisions += SampleFoundCollision[ packedSampleIndices        & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices >>  8) & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices >> 16) & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices >> 24) & 0xff];

		samplesWithCollisions += SampleFoundCollision[ packedSampleIndices2        & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices2 >>  8) & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices2 >> 16) & 0xff];
		samplesWithCollisions += SampleFoundCollision[(packedSampleIndices2 >> 24) & 0xff];

		const uint2 samplingPixel = dispatchThreadId.xy * BlockDimension.xx + uint2(x, y);

		float2 maskValue;
		[branch] if (samplesWithCollisions > 0) {

				//	At least one successful collisions means we should consider
				//	this an active pixel.
			const float SampleDivisor = 4.f;
			const float maskFromCollision = saturate(float(samplesWithCollisions)/SampleDivisor);

			float linearDepth;
			float3 worldSpacePosition = CalculateWorldSpacePosition(
				samplingPixel, outputDimensions, msaaSampleIndex, linearDepth);

			float3 worldSpaceNormal = DownSampledNormals[samplingPixel].rgb;
			float F0 = DownSampledNormals[samplingPixel].a;
			if (dot(worldSpaceNormal, worldSpaceNormal) < 0.25f || F0 < F0Threshold) {
				maskValue = 0.0.xx;	// invalid edge pixels have normals with tiny magnitudes
			} else {
				// We're just going to do a quick estimate of the fresnel term by
				// using the angle between the view direction and the normal. This is
				// equivalent to mirror reflections model -- it's not perfectly
				// right for our microfacet reflections model.
				// But should be ok for an estimate here.

				float3 viewDirection = SysUniform_GetWorldSpaceView() - worldSpacePosition;
				float NdotV = dot(viewDirection, worldSpaceNormal) * ReciprocalLength(viewDirection); // (correct so long as worldSpaceNormal is unit length)
				float q = SchlickFresnelCore(saturate(NdotV));
				float F = F0 + (1.f - F0) * q;
				maskValue = float2(maskFromCollision, F);
			}
		} else {
			maskValue = 0.0.xx;
		}

		ReflectionsMask[samplingPixel] = maskValue;
	}

}
