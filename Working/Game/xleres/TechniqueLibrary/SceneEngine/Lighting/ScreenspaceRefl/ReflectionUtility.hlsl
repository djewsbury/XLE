// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(REFLECTION_UTILITY_H)
#define REFLECTION_UTILITY_H

#include "SSConstants.hlsl"
#include "../../../Math/ProjectionMath.hlsl"

// #define DEPTH_IN_LINEAR_COORDS

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer ViewProjectionParameters : register(b1)
{
	row_major float4x4	WorldToView;
	float3				ProjScale;
	float				ProjZOffset;
}

float3 CalculateWorldSpacePosition(uint2 samplingPixel, uint2 outputDimensions, uint msaaSampleIndex, out float outputLinearDepth)
{
    #if defined(DEPTH_IN_LINEAR_COORDS)
	    float linear0To1Depth = DownSampledDepth[samplingPixel];
    #else
        float linear0To1Depth = NDCDepthToLinear0To1(DownSampledDepth[samplingPixel]);
    #endif

 	float2 tc = samplingPixel / float2(outputDimensions);
 	float weight0 = (1.f - tc.x) * (1.f - tc.y);
 	float weight1 = (1.f - tc.x) * tc.y;
 	float weight2 = tc.x * (1.f - tc.y);
 	float weight3 = tc.x * tc.y;

 	float3 viewFrustumVector =
 			weight0 * SysUniform_GetFrustumCorners(0).xyz + weight1 * SysUniform_GetFrustumCorners(1).xyz
 		+   weight2 * SysUniform_GetFrustumCorners(2).xyz + weight3 * SysUniform_GetFrustumCorners(3).xyz
 		;

    outputLinearDepth = linear0To1Depth;
	return CalculateWorldPosition(viewFrustumVector, linear0To1Depth, SysUniform_GetWorldSpaceView());
}

float3 NDCToViewSpace(float3 ndc)
{
		// negate here because into the screen is -Z axis
	float Vz = -NDCDepthToWorldSpace(ndc.z);

		// undo projection -- assuming our projection matrix that
		// can fit into our "minimal projection" representation. If
		// there is an offset to XY part of the projection matrix, we
		// will need to apply that also...
	float2 projAdj = (ndc.xy * -Vz) / SysUniform_GetMinimalProjection().xy;
	return float3(projAdj, Vz);
}

float4 ViewToClipSpace(float3 viewSpace)
{
	return float4(
		SysUniform_GetMinimalProjection().x * viewSpace.x,
		SysUniform_GetMinimalProjection().y * viewSpace.y,
		SysUniform_GetMinimalProjection().z * viewSpace.z + SysUniform_GetMinimalProjection().w,
		-viewSpace.z);
}

float3 ViewToNDCSpace(float3 viewSpace)
{
	float4 clip = ViewToClipSpace(viewSpace);
	return clip.xyz/clip.w;
}

float3 ClipToViewSpace(float4 clipSpace)
{
	return float3(
		clipSpace.x / SysUniform_GetMinimalProjection().x,
		clipSpace.y / SysUniform_GetMinimalProjection().y,
		(clipSpace.z - SysUniform_GetMinimalProjection().w) / SysUniform_GetMinimalProjection().z);
}

float3 CalculateViewSpacePosition(uint2 samplingPixel, uint2 outputDimensions, uint msaaSampleIndex)
{
		// Screen coordinates -> view space is cheap if we make some
		// assumptions about the projection matrix!
		// undo viewport transform
	float2 preViewport = float2(
		-1.f + 2.0f * samplingPixel.x / float(outputDimensions.x),
		 1.f - 2.0f * samplingPixel.y / float(outputDimensions.y));

	return NDCToViewSpace(float3(preViewport, DownSampledDepth[samplingPixel]));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float2 AsZeroToOne(float2 ndc) { return float2(.5f + .5f * ndc.x, .5f - .5f * ndc.y); }

float LoadDepth(float2 ndcXY, float2 outputDimensions)
{
	return DownSampledDepth[uint2(AsZeroToOne(ndcXY)*outputDimensions.xy)];
}

float DepthDifference(float ndcDepth, float depthTextureValue)
{
	#if defined(DEPTH_IN_LINEAR_COORDS)
		return NDCDepthToLinear0To1(ndcDepth) - depthTextureValue;
	#else
		return ndcDepth - depthTextureValue;
	#endif
}

bool IsCollision(float ndcDepth, float depthTextureValue)
{
	float depthDifference = DepthDifference(ndcDepth, depthTextureValue);
	return depthDifference > DepthMinThreshold; // && depthDifference < DepthMaxThreshold;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct ReflectionRay
{
	float4 projStartPosition;
	float4 projBasicStep;

    float2 screenSpaceRayDirection;

    float3 worldSpaceReflection;
    bool valid;
};

float3 LoadWorldSpaceNormal(uint2 pixelCoord)
{
	return DownSampledNormals[pixelCoord.xy].rgb;
}

float3 LoadViewSpaceNormal(uint2 pixelCoord)
{
		// hack -- 	we need a smarter way to get
		//			access to the view space normals
	return mul(
		(float3x3)WorldToView,
		DownSampledNormals[pixelCoord.xy].rgb);
}

ReflectionRay ReflectionRay_Invalid()
{
	ReflectionRay result;
	result.projStartPosition = result.projBasicStep = 0.0.xxxx;
	result.valid = false;
	return result;
}

float3 ClipViewSpaceRay(float3 viewStart, float3 viewEnd)
{
		// Given an input ray in view space; clip against the edge of the screen
		// We need to transform into homogeneous clip space to do the clipping
	float4 clipStart = ViewToClipSpace(viewStart);
	float4 clipEnd = ViewToClipSpace(viewEnd);

	if (clipEnd.x > clipEnd.w) {
		float l = (clipStart.w - clipStart.x) / ((clipStart.w - clipStart.x) - (clipEnd.w - clipEnd.x));
		clipEnd = lerp(clipStart, clipEnd, l);
	}

	if (clipEnd.y > clipEnd.w) {
		float l = (clipStart.w - clipStart.y) / ((clipStart.w - clipStart.y) - (clipEnd.w - clipEnd.y));
		clipEnd = lerp(clipStart, clipEnd, l);
	}

	if (clipEnd.x < -clipEnd.w) {
		float l = (clipStart.w - -clipStart.x) / ((clipStart.w - -clipStart.x) - (clipEnd.w - -clipEnd.x));
		clipEnd = lerp(clipStart, clipEnd, l);
	}

	if (clipEnd.y < -clipEnd.w) {
		float l = (clipStart.w - -clipStart.y) / ((clipStart.w - -clipStart.y) - (clipEnd.w - -clipEnd.y));
		clipEnd = lerp(clipStart, clipEnd, l);
	}

	return ClipToViewSpace(clipEnd);
}

struct ReflectionRay2
{
	float3 viewStart;
	float3 viewEnd;
	bool valid;
};

ReflectionRay2 ReflectionRay2_Invalid()
{
	ReflectionRay2 result;
	result.valid = false;
	return result;
}

bool IsValid(ReflectionRay2 ray) { return ray.valid; }

float GetStepDistance(uint index, uint stepCount, float randomizerValue)
{
	return (index+.5f+.5f*randomizerValue) / float(stepCount);
}

float3 GetTestPt(ReflectionRay2 ray, float distance)
{
		// note that we're returning points that are linear in view space
		// this might not be as ideal as points that are linear in screen
		// space... But the difference seems to be minor so long as we're
		// skipping over pixels each time.
		// It might be ideal to have more samples close the start...?
	return lerp(ray.viewStart, ray.viewEnd, distance);
}

float3 TestPtAsNDC(float3 pt) { return ViewToNDCSpace(pt); }

ReflectionRay2 CreateRay(float3 viewStart, float3 viewEnd)
{
	ReflectionRay2 result;
	result.valid = true;
	result.viewStart = viewStart;
	result.viewEnd = viewEnd;
	return result;
}

ReflectionRay2 CalculateReflectionRay2(float worldSpaceMaxDist, uint2 pixelCoord, uint2 outputDimensions, uint msaaSampleIndex)
{
	float3 rayStartView = CalculateViewSpacePosition(pixelCoord.xy, outputDimensions, msaaSampleIndex);
	float3 viewSpaceNormal = LoadViewSpaceNormal(pixelCoord);
	if (dot(viewSpaceNormal, viewSpaceNormal) < 0.25f)
		return ReflectionRay2_Invalid();
	float3 reflection = reflect(normalize(rayStartView), viewSpaceNormal);

		// We want to find the point where the reflection vector intersects the edge of the
		// view frustum. We can do this just by transforming the ray into NDC coordinates,
		// and doing a clip against the +-W box.
	float3 rayEndView = rayStartView + worldSpaceMaxDist * reflection;
	rayEndView = ClipViewSpaceRay(rayStartView, rayEndView);
	return CreateRay(rayStartView, rayEndView);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float GetRandomizerValue(uint2 dispatchThreadId)
{
	int ditherArray[16] =
	{
		4, 12,  0,  8,
		10,  2, 14,  6,
		15,  7, 11,  3,
		1,  9,  5, 13
	};
	uint2 t = dispatchThreadId.xy & 0x3;
	float base = float(ditherArray[t.x+t.y*4]) / 15.f;
	return lerp(
		SysUniform_GetGlobalSamplingPassIndex()/float(SysUniform_GetGlobalSamplingPassCount()),
		(SysUniform_GetGlobalSamplingPassIndex()+1)/float(SysUniform_GetGlobalSamplingPassCount()),
		base);
}

#endif
