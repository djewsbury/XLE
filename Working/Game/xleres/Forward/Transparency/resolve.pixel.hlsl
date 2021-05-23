// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../../TechniqueLibrary/Math/TextureAlgorithm.hlsl"
#include "../../TechniqueLibrary/Profiling/Metrics.hlsl"
#include "../../TechniqueLibrary/Utility/Colour.hlsl"

#define LIMIT_LAYER_COUNT 0

struct FragmentListNode
{
	uint	next;
	float	depth;
	uint	color;
};

float4 FragmentListNode_UnpackColor(uint packedColor)
{
	uint4 c;
	c.r = (packedColor>>16)&0xff;
	c.g = (packedColor>>8)&0xff;
	c.b = packedColor&0xff;
	c.a = packedColor>>24;
	return float4(c) / 255.0.xxxx;
}

#define SortingElement uint2
bool Less(uint2 lhs, uint2 rhs) { return lhs.y < rhs.y; }

#include "resolve.hlsl"

Texture2D<uint>							FragmentIds	 : register(t0);
StructuredBuffer<FragmentListNode>		NodesList	 : register(t1);
Texture2D<float>						DepthTexture : register(t2);
RWStructuredBuffer<MetricsStructure>	MetricsObject;

#if (DETECT_INFINITE_LISTS==1)
	Texture2D<uint>		InfiniteListTexture :register(t3);
#endif

void BlendSamples(	SortingElement sortingBuffer[FixedSampleCount], uint sampleCount,
					inout float4 combinedColor)
{
	[loop] for (uint c=0; c<sampleCount; c++) {
		FragmentListNode node = NodesList[sortingBuffer[c].x];

		float4 color = FragmentListNode_UnpackColor(node.color);
		combinedColor.rgb *= 1.f-color.a;
		combinedColor.a *= 1.f-color.a;
		combinedColor.rgb += color.rgb;
	}
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0, SystemInputs sys) : SV_Target0
{
	#if (DETECT_INFINITE_LISTS==1)
		uint isInfinite = InfiniteListTexture[uint2(position.xy)];
		if (isInfinite) return float4(LightingScale, 0, 0, 1);
	#endif

	uint firstId = FragmentIds[uint2(position.xy)];
	[branch] if (firstId == 0xffffffff) {
		return float4(0.0.xxx, 0);
	} else {
		// return float4(1.0.xxx, 1);

			//
			//		Follow the linked list of
			//		nodes. Sort into a final array, and then combine in
			//		back to front order...
			//
			//			What's the best sort for this?
			//				*	An insertion sort while travelling through
			//					the linked list?
			//				*	Quick sort?
			//
			//		Most methods impose a limit on the maximum
			//		number of combined samples we can support.
			//
			//		Maybe the best method depends on how many blended
			//		layers there are...  Perhaps we should store
			//		the number of layers in a full screen texture.
			//

			//		Note; having a large array declared here may
			//		limit the number of pixel shader instances that can
			//		be scheduled synchronously.

		uint2 sortingBuffer[FixedSampleCount];
		float baseLineDepth = DepthTexture[uint2(position.xy)];

		uint sampleCount = 0;
		uint nodeId = firstId;
		[loop] do {
			FragmentListNode node = NodesList[nodeId];
			if (node.depth < baseLineDepth) {
				sortingBuffer[sampleCount++] = uint2(nodeId, asuint(node.depth));
			}
			// if (node.next == nodeId) return float4(0, LightingScale, 0, 1);
			nodeId = node.next;
		} while(nodeId != 0xffffffff && sampleCount < FixedSampleCount);

			//
			//		If "FixedSampleCount" is very small, perhaps we should
			//		consider alternatives to Quicksort. Maybe some basic
			//		sorting methods would work better for very small buffers.
			//
		bool valid = true;
		const bool useQuicksort = true;
		if (useQuicksort) {
			valid = Quicksort(sortingBuffer, 0, sampleCount-1);
		} else {
			BubbleSort(sortingBuffer, 0, sampleCount-1);
		}

		#if LIMIT_LAYER_COUNT==1

				//	This insertion sort method will merge in
				//	more samples, but keep the final number of samples
				//	limited into a single buffer. So some samples will
				//	be excluded and ignored. Works best with when
				//	"FixedSampleCount" is small (eg, 4 or 6)

			[loop] while (nodeId != 0xffffffff) {
				FragmentListNode node = NodesList[nodeId];
				if (node.depth < baseLineDepth) {
					SortedInsert_LimitedBuffer(sortingBuffer, uint2(nodeId, asuint(node.depth)));
				}
				// if (node.next == nodeId) return float4(0, 0, LightingScale, 1);
				nodeId = node.next;
			}

		#endif

		uint sampleCountMetric = sampleCount;

		float4 combinedColor = float4(0.0.xxx,1);
		[branch] if (nodeId == 0xffffffff) {

			#if defined(_DEBUG)
				if (!valid) {
					return float4(1,0,0,.5);
				}
			#endif

			BlendSamples(sortingBuffer, sampleCount, combinedColor);

		} else {

			// return float4(1,0,0,.5);

				//
				//		We need to do a partial sort... Only
				//		sort the first "FixedSampleCount" items
				//
				//		There are 2 possible ways to do this:
				//			 - using an insertion sort
				//			 - use a select algorithm to
				//				find item at position 'k' first,
				//				and then sort all of the items
				//				smaller than that.
				//
				//		Insertion sort method...
				//

			#if LIMIT_LAYER_COUNT!=1

				float minDepth = baseLineDepth;
				while (true) {
					[loop] do {
						FragmentListNode node = NodesList[nodeId];
						if (node.depth < minDepth) {
							SortedInsert(sortingBuffer, sampleCount, uint2(nodeId, asuint(node.depth)));
							sampleCountMetric++;
						}
						nodeId = node.next;
					} while(nodeId != 0xffffffff);

					BlendSamples(sortingBuffer, sampleCount, combinedColor);
					if (sampleCount < FixedSampleCount) {
						break;
					}

					nodeId = firstId;
					minDepth = asfloat(sortingBuffer[FixedSampleCount-1].y);
					sampleCount = 0;
				}

			#endif
		}

		#if defined(_METRICS)
			uint buffer;
			InterlockedAdd(MetricsObject[0].TranslucentSampleCount, sampleCountMetric, buffer);
			InterlockedAdd(MetricsObject[0].PixelsWithTranslucentSamples, 1, buffer);
			InterlockedMax(MetricsObject[0].MaxTranslucentSampleCount, sampleCountMetric, buffer);
		#endif

		return float4(LightingScale * combinedColor.rgb, 1.f-combinedColor.a);
	}
}

uint FindInfiniteLoops(float4 position : SV_Position, float2 texCoord : TEXCOORD0, SystemInputs sys) : SV_Target0
{
		// Check the input textures to look for infinite loops!
		// if we hit an infinite loop in the main() function it will lock up the GPU
		// To protect against this case, we can run this shader over the texture
		// first to find any pixels that have an infinite loop attached.
		// Ideally, if the writing shader is working correctly we should never get any
		// infinite loops... But it's so hard to debug when we get them we just need
		// some way to detect and prevent them

	uint nodeId = FragmentIds[uint2(position.xy)];
	[loop] while (nodeId != 0xffffffff) {
		FragmentListNode node = NodesList[nodeId];
		if (node.next == nodeId)
			return 1;	// this is an infinite list -- because one link points to itself
		nodeId = node.next;
	}

	return 0;
}
