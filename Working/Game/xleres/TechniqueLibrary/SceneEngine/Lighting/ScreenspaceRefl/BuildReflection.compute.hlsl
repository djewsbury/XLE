// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSConstants.hlsl"
#include "../../../Math/ProjectionMath.hlsl"
#include "../../../Math/TextureAlgorithm.hlsl"
#include "../../../Framework/CommonResources.hlsl"
#include "../../../Framework/gbuffer.hlsl"

Texture2D_MaybeMS<float4>	GBuffer_Diffuse		: register(t0);
Texture2D<float4>			DownSampledNormals	: register(t1);
Texture2D<float>			DownSampledDepth	: register(t2);
Texture2D<float2>			ReflectionsMask		: register(t3);
Texture2D<float>			RandomNoiseTexture	: register(t4);

#include "ReflectionUtility.hlsl"
#include "PixelBasedIteration.hlsl"

RWTexture2D<float4>			OutputTexture;

static const float4 EmptyPixel = 0.0.xxxx;

// #define DEBUG_STEP_COUNT

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float CalculateAlphaValue(float distance, float2 texCoord)
{
		//		We must fade off extreme samples
		//			-	fade off when the sample point is
		//				near the edge of the screen (these
		//				samples may disappear soon as the camera
		//				moves)
		//			-   fade off if we neared the edge of the
		//				ray iteration distance
	float2 t	= min(texCoord.xy, 1.f-texCoord.xy);
	t = saturate(15.f*t);
	float d = lerp(0.25f, 1.f, t.x*t.y);
	const float FadeOffDistance = 0.35f;
	d *= 1.0f - saturate((distance - (1.0f-FadeOffDistance)) * (1.0f/FadeOffDistance));
	return d;
}

float4 BuildResult(float distance, float2 texCoord, bool isGoodIntersection, uint msaaSampleIndex)
{
	const bool debugAlphaValue = false;
	#if INTERPOLATE_SAMPLES != 0
		const bool writeTextureCoordinates = true;
	#else
		const bool writeTextureCoordinates = false;
	#endif

	if (debugAlphaValue) {
		return float4(CalculateAlphaValue(distance, texCoord).xxx, 1.f);
	} else if (writeTextureCoordinates) {
		float intersectionQuality = float(isGoodIntersection) * CalculateAlphaValue(distance, texCoord);
		return float4(texCoord, intersectionQuality, 1.f);
	} else {
		float alpha = CalculateAlphaValue(distance, texCoord);
		float3 colourToReflect = 0.0.xxx;
		if (isGoodIntersection) {
			colourToReflect = SampleFloat4(GBuffer_Diffuse, ClampingSampler, texCoord, msaaSampleIndex).rgb;
		}
		return float4(colourToReflect, alpha);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool DetailStep(
	out float resultDistance, out float2 resultTC, out bool resultIsGoodIntersection,
	ReflectionRay2 ray, uint stepCount,
	float2 outputDimensions)
{
	for (uint step=0; step<stepCount; ++step) {
		float d = GetStepDistance(step, stepCount, 1.f);
		float3 testPtNDC = TestPtAsNDC(GetTestPt(ray, d));
		float testDepth = LoadDepth(testPtNDC.xy, outputDimensions);
		[branch] if (IsCollision(testPtNDC.z, testDepth)) {
			resultDistance = d;
			resultTC = AsZeroToOne(testPtNDC.xy);
			resultIsGoodIntersection = (testDepth - testPtNDC.z) < DepthMaxThreshold;
			return true;
		}
	}

	return false;
}

struct MRStepSettings
{
	uint initialStepCount;
	uint detailStepCount;
};

bool MultiResolutionStep(
	out float4 result, ReflectionRay2 ray, MRStepSettings settings,
	float randomizerValue, float2 outputDimensions, uint msaaSampleIndex)
{
	float3 lastPt = ray.viewStart;
	for (uint step=0; step<settings.initialStepCount; ++step) {
		float d = GetStepDistance(step, settings.initialStepCount, randomizerValue);
		float3 rayPt = GetTestPt(ray, d);
		float3 testPtNDC = TestPtAsNDC(rayPt);
		float testDepth = LoadDepth(testPtNDC.xy, outputDimensions);
		[branch] if (IsCollision(testPtNDC.z, testDepth)) {
			ReflectionRay2 detailRay = CreateRay(lastPt, rayPt);
			float resultDistance; float2 resultTC; bool resultIsGoodIntersection;
			if (DetailStep(resultDistance, resultTC, resultIsGoodIntersection, detailRay, settings.detailStepCount, outputDimensions)) {
				resultDistance = lerp(GetStepDistance(step-1, settings.initialStepCount, randomizerValue), d, resultDistance);
			} else {
				resultDistance = d;
				resultTC = AsZeroToOne(testPtNDC.xy);
				resultIsGoodIntersection = (testDepth - testPtNDC.z) < DepthMaxThreshold;
			}
			result = BuildResult(resultDistance, resultTC, resultIsGoodIntersection, msaaSampleIndex);
			return true;
		}
		lastPt = rayPt;
	}

	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//		iteration on per-pixel basis						//

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
//	if (	queryDepth >= min(entryNDCDepth, exitNDCDepth) - epsilon
//		&&	queryDepth <= max(entryNDCDepth, exitNDCDepth) + epsilon) {

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
	const float continuousEpsilon = 0.001f;
	bool isFrontFace = true; // (exitNDCDepth > entryNDCDepth) != (queryDepth > lastQueryDepth);
	if (abs(lastQueryDepth - queryDepth) < continuousEpsilon && isFrontFace) {
		if (entryNDCDepth >= min(lastQueryDepth, queryDepth) && entryNDCDepth <= max(lastQueryDepth, queryDepth)) {
			return 1;
		}
	}

	return 0;
}

float4 MakeResult(PBI iterator, uint2 outputDimensions)
{
	if (!iterator._gotIntersection) return EmptyPixel;
	// return float4((iterator._testCount / 256.f).xxx, 1.0.x);		// (display step count)
	return BuildResult(
		iterator._distance,
		iterator._intersectionCoords.xy / float2(outputDimensions),
		iterator._gotIntersection, 0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(16, 16, 1)]
	void BuildReflection(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	float2 maskValue = ReflectionsMask[dispatchThreadId.xy];
	[branch] if ((maskValue.x * maskValue.y) == 0.f) {
		OutputTexture[dispatchThreadId.xy] = 0.0.xxxx;
		return;
	}

		// do high resolution ray sample
	const uint msaaSampleIndex = 0;
	uint2 outputDimensions;
	DownSampledDepth.GetDimensions(outputDimensions.x, outputDimensions.y);

	float4 result = EmptyPixel;

		//	when not doing the blur, the jittering actually looks better with
		//	a repeating pattern. For some reason, the regularity of it helps
		//	a completely random pattern just ends up looking messy and distracting
	float randomizerValue = GetRandomizerValue(dispatchThreadId.xy);

		// Iteration:
		//   0: new code
		//   1: reference high resolution iteration
	const uint stepMethod = 0;
	const float worldSpaceMaxDist = min(MaxReflectionDistanceWorld, SysUniform_GetFarClip());
	if (stepMethod == 0) {
		ReflectionRay2 ray = CalculateReflectionRay2(worldSpaceMaxDist, dispatchThreadId.xy, outputDimensions, msaaSampleIndex);
		if (ray.valid) {
			MRStepSettings settings;
			settings.initialStepCount = ReflectionInitialTestsPerRay;
			settings.detailStepCount = ReflectionDetailTestsPerRay;
			MultiResolutionStep(
				result, ray, settings,
				randomizerValue, outputDimensions, msaaSampleIndex);
		}
	} else if (stepMethod == 1) {
		ReflectionRay2 ray = CalculateReflectionRay2(worldSpaceMaxDist, dispatchThreadId.xy, outputDimensions, msaaSampleIndex);
		if (ray.valid) {
			PBISettings settings;
			settings.pixelStep = 8;
			settings.initialPixelsSkip = 2 + int(randomizerValue * (settings.pixelStep+1));
			PBI i = PixelBasedIteration(
				ViewToClipSpace(ray.viewStart), ViewToClipSpace(ray.viewEnd),
				float2(outputDimensions), settings);
			result = MakeResult(i, outputDimensions);
		}
	}

	// We can multiply in our "confidence" value from the mask here...
	// This helps to fade off the edges of the reflection area a bit.
	float confidenceFromMask = maskValue.x;
	#if INTERPOLATE_SAMPLES != 0
		result.b *= confidenceFromMask;
	#else
		result.a *= confidenceFromMask;
	#endif

	OutputTexture[dispatchThreadId.xy] = result;
}
