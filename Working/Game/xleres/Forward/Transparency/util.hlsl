// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../../TechniqueLibrary/Framework/MainGeometry.hlsl"
#include "../../TechniqueLibrary/Math/TextureAlgorithm.hlsl"     // (for SystemInputs)
#include "../../TechniqueLibrary/Framework/Surface.hlsl"
#include "../../TechniqueLibrary/SceneEngine/Lighting/LightDesc.hlsl"
#include "../../TechniqueLibrary/SceneEngine/Lighting/Forward.hlsl"

struct FragmentListNode
{
	uint	next;
	float	depth;
	uint	color;
};

uint FragmentListNode_PackColor(float4 color)
{
	color = saturate(color);
	return uint(color.a*255.f)<<24
		|  uint(color.r*255.f)<<16
		|  uint(color.g*255.f)<< 8
		|  uint(color.b*255.f)
		;
}

RWTexture2D<uint>						FragmentIds : register(u1);
RWStructuredBuffer<FragmentListNode>	NodesList	: register(u2);

Texture2D<float>						DuplicateOfDepthBuffer : register(t17);

void OutputFragmentNode(uint2 position, float4 color, float depth)
{
		//
		//	todo -- we could output a self balancing binary search tree...
		//			just might reduce the workload while resolving the sort
		//			order (particularly if we have a lot of elements, and need
		//			to do a partial sort)
		//				-- but it would require read/write access to NodesList,
		//					which might not be thread safe.
		//
	uint newNodeId = NodesList.IncrementCounter();
	uint oldNodeId;
	InterlockedExchange(FragmentIds[position], newNodeId, oldNodeId);

	FragmentListNode newNode;
	newNode.next = oldNodeId;
	newNode.depth = depth;
	newNode.color = FragmentListNode_PackColor(color);
	NodesList[newNodeId] = newNode;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
		// Do the full lighting here --
		// 	actually, it might be better to do some of the lighting after resolve
		//	If we do a shadow lookup here (for example), it will mean doing a
		//	separate shadow lookup on each layer. But we might only really need
		//	that for the top-most few layers.
		//	One way to deal with this is to do an early depth pass that would set
		//	the depth buffer depth for fully opaque parts; or just after a fixed
		//	number of layers...?
float4 LightSample(GBufferValues sample, VSOUT geo, SystemInputs sys)
{
	float3 directionToEye = 0.0.xxx;
	#if (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
		directionToEye = normalize(geo.worldViewVector);
	#endif

	float4 result = float4(
		CalculateIllumination(
			sample, directionToEye, VSOUT_GetWorldPosition(geo),
			LightScreenDest_Create(int2(geo.position.xy), GetSampleIndex(sys))), 1.f);

	#if VSOUT_HAS_FOG_COLOR == 1
		result.rgb = geo.fogColor.rgb + result.rgb * geo.fogColor.a;
	#endif

	result.a = sample.blendingAlpha;
	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

Texture2DMS<float> StochasticOcclusionDepths : register(t9);

#include "../../TechniqueLibrary/Profiling/metrics.hlsl"
#if defined(_METRICS)
	RWStructuredBuffer<MetricsStructure> MetricsObject : register(u1);
	RWTexture2D<uint> LitSamplesMetrics : register(u2);
#endif

#if !defined(STOCHASTIC_SAMPLE_COUNT)
	#define STOCHASTIC_SAMPLE_COUNT 8
#endif

#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
	Texture2DMS<uint> StochasticPrimitiveIds : register(t8);
	uint LoadPrimitiveId(uint2 coord, int sample) { return StochasticPrimitiveIds.Load(uint2(coord), sample); }
#else
	uint LoadPrimitiveId(uint2 coord, int sample) { return 0; }
#endif

#if (STOCHASTIC_TRANS_OPACITY==1)
	Texture2DMS<float> StochasticOpacities : register(t7);
	float LoadSampleOpacity(uint2 coord, int sample) { return StochasticOpacities.Load(uint2(coord), sample); }
#else
	float LoadSampleOpacity(uint2 coord, int sample) { return 0.f; }
#endif

uint CalculateStochasticPixelType(float4 position, out float occlusion
	#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
		, uint primitiveID
	#endif
	)
{
	// This is the main resolve step when using stochastic blending
	// The brightness of the color we want to write should be modulated
	// by the alpha values of all of the layers on top of this one.
	//
	// But how to we know the alpha contribution of all of the layers
	// above this one?
	// Well, we can use the depth and alpha values written to the stochastic
	// buffers to create an estimate. It's not perfectly accurate, and it
	// is noisy, but it will give us a rough idea.

	occlusion = 1.f;
	float ndcComparison = position.z;

	uint occCount = 0;
	float sampleDepth = StochasticOcclusionDepths.Load(uint2(position.xy), 0);
	float minSampleDepth = sampleDepth, maxSampleDepth = sampleDepth;
	uint primId = LoadPrimitiveId(uint2(position.xy), 0);
	uint firstPrimId = primId;
	bool splitPixel = false;

	[unroll] for (uint s=0;;) {
			// Maybe getting self-occlusion here?
			// The depth values written to the MSAA sample seem to be the depth
			// at the sampling point (not the center of the pixel). So they won't
			// exactly match the pixel center depth we have here.
			// To prevent some self occlusion, we can use the primitive id
		if (	(sampleDepth < ndcComparison)
				#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
					&& (primId != primitiveID)
				#endif
			) {

				// special case -- check if occluded by opaque pixel
			if (LoadSampleOpacity(uint2(position.xy), 0)==1.f)
				return 2;

			++occCount;
		}

		++s;
		if (s>=STOCHASTIC_SAMPLE_COUNT) break;
		sampleDepth = StochasticOcclusionDepths.Load(uint2(position.xy), s);
		minSampleDepth = min(sampleDepth, minSampleDepth);
		maxSampleDepth = max(sampleDepth, maxSampleDepth);

		primId = LoadPrimitiveId(uint2(position.xy), s);

			// note that the equation here isn't perfectly exact... Because we're
			// using a per-draw call "primitiveId", primitives in different draw calls
			// can end up with the same id. In these cases, the result will be incorrect.
			// We can try to deal with this by integrating a draw call id into the primitive
			// id... But that requires a little bit of constant buffer thrashing.
		splitPixel = splitPixel | (firstPrimId != primId);
	}

	if (occCount == STOCHASTIC_SAMPLE_COUNT) {
		// Discard would be much cheaper here. But sometimes all of the fragments on
		// the top have a very low alpha values -- which means that the final blending
		// alpha value becomes incorrect
		//
		// Note that we can sometimes get more fragments falling through this path than
		// the other path. This is especially true for geometry is that is partially opaque
		// and partially transparent.
		//
		// We can reduce the cases that fall through here using a depth pre-pass. That helps
		// in edge cases (such as when a large opaque part is filling a lot of the screen),
		// but in common cases it seems that benefit is not significant.
		//
		// Never the less, it would be nice if we could somehow know when it's not necessary
		// to fall take this path -- and when we can just discard, instead.

			// Here, we can attempt to check if the samples belong to the same pixel by
			// looking at their depth values. Unfortunately it's not very accurate. Up close it
			// is not too bad. But at a certain distance from the camera there is not enough
			// precision in the depth buffer to reliably distinguish one pixel from another.
		// float threshold = 5e-4f;
		// bool singlePixel = (NDCDepthToWorldSpace(maxSampleDepth) - NDCDepthToWorldSpace(minSampleDepth)) < threshold;

			// if all samples have the sample primitive id, then it should mean that the
			// top most pixel is opaque. Note that we can't use this method to detect
			// opaque pixels below partially transparent pixels
		#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
			if (!splitPixel) return 2;
		#endif

		#if defined(_METRICS)
			uint buffer;
			InterlockedAdd(MetricsObject[0].StocasticTransPartialLitFragmentCount, 1, buffer);
			InterlockedAdd(LitSamplesMetrics[uint2(position.xy)], 1, buffer);
		#endif

		return 1;
	} else {
		occlusion = float(occCount) / float(STOCHASTIC_SAMPLE_COUNT);

		#if defined(_METRICS)
			uint buffer;
			InterlockedAdd(MetricsObject[0].StocasticTransLitFragmentCount, 1, buffer);
			// InterlockedAdd(LitSamplesMetrics[uint2(geo.position.xy)], 1, buffer);
		#endif

		return 0;
	}
}
