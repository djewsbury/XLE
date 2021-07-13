// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

Texture2D<float3>	DownSampledNormals 			: register(t6);
Texture2D<float>	DownSampledDepth			: register(t10);

#include "ReflectionUtility.hlsl"
#include "../LightingAlgorithm.hlsl"
#include "../../../Framework/CommonResources.hlsl"
#include "../../../Math/ProjectionMath.hlsl"
#include "../../../Utility/LoadGBuffer.hlsl"
#include "../../../Math/TextureAlgorithm.hlsl"
#include "PixelBasedIteration.hlsl"

Texture2D_MaybeMS<float>	DepthTexture		: register(t3);
Texture2D			SkyReflectionTexture[3]		: register(t7);

Texture2D<float2>	MaskTexture					: register(t5);
Texture2D<float4>	ReflectionsTexture			: register(t11);


float3 CalculateReflectionVector(uint2 pixelCoord, float3 viewFrustumVector, float3 worldSpaceNormal)
{
	const uint msaaSampleIndex = 0;
	const float linear0To1Depth = NDCDepthToLinear0To1(LoadFloat1(DepthTexture, pixelCoord, msaaSampleIndex));
	float3 worldSpacePosition = WorldPositionFromLinear0To1Depth(viewFrustumVector, linear0To1Depth);

	float3 worldSpaceReflection = reflect(normalize(worldSpacePosition - SysUniform_GetWorldSpaceView()), worldSpaceNormal);
	return worldSpaceReflection;
}

float4 TilesDebugging(	float4 position				: SV_Position,
						float2 texCoord				: TEXCOORD0,
						float3 viewFrustumVector	: VIEWFRUSTUMVECTOR) : SV_Target0
{
	// return float4(.5.xxx, MaskTexture.SampleLevel(DefaultSampler, texCoord, 0));
	return float4(MaskTexture.SampleLevel(DefaultSampler, texCoord, 0).rgr, 1.f);
	// return float4(ReflectionsTexture.SampleLevel(DefaultSampler, texCoord, 0).bbb, 0.5f);

	// float2 t	= min(texCoord.xy, 1.f-texCoord.xy);
	// t = saturate(15.f*t);
	// float d = t.x*t.y;
	// return d.xxxx;

#if 1
	float4 reflectionColour = ReflectionsTexture.SampleLevel(ClampingSampler, texCoord, 0);

	uint3 normalDim;
	#if defined(MSAA_SAMPLERS) && (MSAA_SAMPLERS != 0)
		GBuffer_Normals.GetDimensions(normalDim.x, normalDim.y, normalDim.z);
	#else
		GBuffer_Normals.GetDimensions(normalDim.x, normalDim.y);
	#endif

	GBufferValues gbuffer = LoadGBuffer(float4(texCoord.xy*normalDim.xy, 0.f, 1.f), SystemInputs_Default());
	float reflectivity = gbuffer.material.specular;

	float3 reflectionVector = CalculateReflectionVector(
		uint2(texCoord.xy*normalDim.xy), viewFrustumVector, gbuffer.worldSpaceNormal);
	float2 skyReflectionCoord = DirectionToEquirectangularCoord_YUp(reflectionVector);
	float3 skyReflection = SkyReflectionTexture[0].Sample(DefaultSampler, skyReflectionCoord).rgb;
	// return float4(skyReflection, 1.f);
	// reflectivity = 1;
	// return float4(skyReflection, reflectivity);

	#if INTERPOLATE_SAMPLES != 0
		const bool interpolateSamples = true;
	#else
		const bool interpolateSamples = false;
	#endif
	if (!interpolateSamples) {
		// return float4(reflectionColour.rgb, 16.f * reflectionMask);
		return float4(reflectionColour.rgb, 4.f*reflectionColour.a*reflectivity);
	} else {

		float intersectionQuality = reflectionColour.z;
		float pixelReflectivity = reflectionColour.a;

		return float4(pixelReflectivity * intersectionQuality.xxx, 1.f);

		float3 diffuseSample = SampleFloat4(GBuffer_Diffuse, ClampingSampler, reflectionColour.xy, 0).rgb;
		// float3 diffuseSample = float3(reflectionColour.xy, 0.f);

		// if (skyReflectionCoord.y > 0.64f) {
		// 	reflectivity *= lerp(0.f, 1.f, reflectionColour.a);	// ignore bottom half of sky reflection hemisphere
		// }

		return float4(diffuseSample.rgb, pixelReflectivity * intersectionQuality);

		//return float4(
		//	lerp(0.33f * skyReflection,
		//		lerp(0.0.xxx, diffuseSample.rgb, reflectionColour.z),
		//		reflectionColour.a),
		//	2.f * reflectivity);
	}
#endif

	// return float4(reflectionColour.rgb*reflectionColour.a, 1);

	if (texCoord.y < 0.5f) {
		if (texCoord.x < 0.5f) {
			return float4(
				MaskTexture.SampleLevel(DefaultSampler, texCoord * 2.f, 0).xxx, 1.f);
		} else {
			return float4(
				DownSampledNormals.SampleLevel(DefaultSampler,
					float2((texCoord.x - 0.5f) * 2.f, texCoord.y * 2.f), 0), 1.f);
		}
	} else {
		if (texCoord.x < 0.5f) {
			return float4(
				DownSampledDepth.SampleLevel(DefaultSampler,
					float2(texCoord.x * 2.f, (texCoord.y - 0.5f) * 2.f), 0).xxx, 1.f);
		} else {
			return float4(
				ReflectionsTexture.SampleLevel(
					DefaultSampler,
					float2((texCoord.x - 0.5f) * 2.f, (texCoord.y - 0.5f) * 2.f), 0).rgb, 1.f);
		}
	}
}

static const uint SamplesPerBlock = 64;
static const uint BlockDimension = 64;

cbuffer BasicGlobals
{
	const uint2 ViewportDimensions;
	const int2 MousePosition;
}

cbuffer SamplingPattern
{
	uint2 SamplePoint[SamplesPerBlock];
	uint4 ClosestSamples[BlockDimension][BlockDimension/4];
	uint4 ClosestSamples2[BlockDimension][BlockDimension/4];
}

float4 MaskSamplePattern(	float4 position		: SV_Position,
							float2 texCoord		: TEXCOORD0) : SV_Target0
{
//	uint2 pixelCoords = uint2(	ViewportDimensions.x * (-0.5f + 0.5f * position.x),
//								ViewportDimensions.y * ( 0.5f - 0.5f * position.y));
	int2 pixelCoords = int2(position.xy);

	int2 gridMin = int2(32, 32);
	int2 gridCellSize = int2(10, 10);
	int2 gridMax = gridMin + int2(BlockDimension, BlockDimension) * gridCellSize;

	float4 result = 0.0.xxxx;

	int2 hoveringCellIndex = (MousePosition - gridMin) / gridCellSize;
	uint closestSamples = 0, closestSamples2 = 0;
	if (	hoveringCellIndex.x >= 0 && hoveringCellIndex.x < BlockDimension
		&&	hoveringCellIndex.y >= 0 && hoveringCellIndex.y < BlockDimension) {

		closestSamples = ClosestSamples[hoveringCellIndex.y][hoveringCellIndex.x/4][hoveringCellIndex.x%4];
		closestSamples2 = ClosestSamples2[hoveringCellIndex.y][hoveringCellIndex.x/4][hoveringCellIndex.x%4];
	}

	if (	pixelCoords.x >= gridMin.x && pixelCoords.y >= gridMin.y
		&&	pixelCoords.x <= gridMax.x && pixelCoords.y <= gridMax.y) {
		if (	((pixelCoords.x - gridMin.x) % gridCellSize.x) == 0
			||	((pixelCoords.y - gridMin.y) % gridCellSize.y) == 0) {
			result = float4(0.125.xxx, 1.f);
		} else {
			result = float4(0.25.xxx, .5f);
		}

		int2 cellIndex = (pixelCoords - gridMin) / gridCellSize;
		if (cellIndex.x == hoveringCellIndex.x && cellIndex.y == hoveringCellIndex.y) {
			result = float4(0.25f, 0, 0, .5f);
		}
	}

	float2 cellCoords = int2(BlockDimension, BlockDimension) * (pixelCoords - gridMin) / float2(gridMax - gridMin);
	for (uint c=0; c<SamplesPerBlock; ++c) {
		uint2 sampleOffset = SamplePoint[c].xy;
		// sampleOffset = uint2(BlockDimension.xx * float2(frac(c/8.f), frac((c/8)/8.f))) + uint2(4,4);
		float2 diff = int2(sampleOffset) - cellCoords;

		float circleDistance = dot(diff, diff);

		if (circleDistance < 35.f) {

			if (	c == (closestSamples & 0xff)
				||	c == ((closestSamples >> 8) & 0xff)
				||	c == ((closestSamples >> 16) & 0xff)
				||	c == ((closestSamples >> 24) & 0xff)
				||	c == (closestSamples2 & 0xff)
				||	c == ((closestSamples2 >> 8) & 0xff)
				||	c == ((closestSamples2 >> 16) & 0xff)
				||	c == ((closestSamples2 >> 24) & 0xff)) {
				result += float4(.25f, 0, 0, .5f);
			} else {
				if (circleDistance > 32.f) {
					result += float4(0.75.xxx, .5f);
				}
			}
		}
	}

	return result;
}

uint IterationOperator(int2 pixelCapCoord, float entryNDCDepth, float exitNDCDepth, inout float ilastQueryDepth)
{
	float queryDepth = DownSampledDepth[pixelCapCoord];
	if (queryDepth < min(entryNDCDepth, exitNDCDepth))
		return 1;
	return 0;
}

float2 FindIntersectionPt(ReflectionRay2 ray, float randomizerValue, uint2 outputDimensions)
{
	PBISettings settings;
	settings.pixelStep = 1;
	settings.initialPixelsSkip = 2;

	// Use the accurate pixel based operation to find the perfect intersection pt
	PBI i = PixelBasedIteration(
		ViewToClipSpace(ray.viewStart),
		ViewToClipSpace(ray.viewEnd),
		float2(outputDimensions), settings);
	if (!i._gotIntersection) return 1.0.xx;
	float2 t = i._intersectionCoords / float2(outputDimensions);
	return float2(2.f * t.x - 1.f, 1.f - 2.f * t.y);
}

float PtToLineDist(ReflectionRay2 ray, float2 ndcXY)
{
	float2 start = ViewToClipSpace(ray.viewStart).xy / ViewToClipSpace(ray.viewStart).w;
	float2 end   = ViewToClipSpace(ray.viewEnd).xy   / ViewToClipSpace(ray.viewEnd).w;

	float2 v = end - start;
	float2 w = ndcXY - start;

	float c1 = dot(w,v);
	float c2 = dot(v,v);
	float b = c1 / c2;

	float2 Pb = start + saturate(b) * v;
	return distance(ndcXY, Pb);
}

float4 RayDebugging(	float4 position		: SV_Position,
						float2 texCoord		: TEXCOORD0) : SV_Target0
{
	uint2 outputDimensions;
	DownSampledDepth.GetDimensions(outputDimensions.x, outputDimensions.y);

	uint downSampleValue = (uint2(position.xy / texCoord.xy) / outputDimensions).x;
	position /= downSampleValue;
	uint2 samplePt = MousePosition / downSampleValue;

	const float worldSpaceMaxDist = min(5.f, SysUniform_GetFarClip());
	ReflectionRay2 ray = CalculateReflectionRay2(worldSpaceMaxDist, samplePt, outputDimensions, 0);

	float2 ndcXY = float2(
		-1.f + 2.0f * position.x / float(outputDimensions.x),
		 1.f - 2.0f * position.y / float(outputDimensions.y));

	float2 intersectionPtNDC = FindIntersectionPt(ray, GetRandomizerValue(samplePt), outputDimensions);
	if (length(intersectionPtNDC - ndcXY) <= (1.f/256.f))
		return float4(0,1,0,1);

	const uint stepCount = 8;
	[unroll] for (uint c=0; c<stepCount; ++c) {
		float d = GetStepDistance(c, stepCount, GetRandomizerValue(samplePt));
		float3 ndcTest = TestPtAsNDC(GetTestPt(ray, d));
		if (length(ndcTest - ndcXY) <= (1.f/256.f))
			return 1.0.xxxx;
	}

	if (PtToLineDist(ray, ndcXY) <= (.5f/256.f))
		return float4(1,0,0,1);
	return 0.0.xxxx;
}

float4 main(	float4 position		: SV_Position,
				float2 texCoord		: TEXCOORD0,
				float3 viewFrustumVector : VIEWFRUSTUMVECTOR) : SV_Target0
{
	if ((uint(position.x)%128 == 127) || (uint(position.y)%128) == 127) return float4(1,0,0,1);

	float4 ray = RayDebugging(position, texCoord);
	if (ray.a > 0.f) return ray;

	return TilesDebugging(position, texCoord, viewFrustumVector);
	// return MaskSamplePattern(position, texCoord);
}
