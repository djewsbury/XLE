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
};

#define SortingElement float
bool Less(SortingElement lhs, SortingElement rhs) { return abs(lhs) < abs(rhs); }

#include "resolve.hlsl"

Texture2D<uint>							FragmentIds		: register(t0);
StructuredBuffer<FragmentListNode>		NodesList		: register(t1);
Texture2D<float>						DepthTexture	: register(t2);
RWStructuredBuffer<MetricsStructure>	MetricsObject	;

void BlendSamples(	SortingElement sortingBuffer[FixedSampleCount], uint sampleCount, 
					inout float lastEntry, inout float4 combinedColor)
{
	float totalDepth = 0.f;

		// find the how much of the eye ray was within the volume
	const bool ignoreWindingOrder = false;
	if (ignoreWindingOrder) {
		[loop] for (int c=0; c<(sampleCount&(~1)); c+=2) {
			float depth0 = sortingBuffer[c];
			float depth1 = sortingBuffer[c+1];
			totalDepth += NDCDepthToWorldSpace(depth0) - NDCDepthToWorldSpace(depth1);
		}
	} else {
		[loop] for (int c=0; c<sampleCount; c++) {
			float depth = sortingBuffer[c];
			if (depth > 0.f) {
					// the "max" is here so that when lastEntry is "0", we add "0" to totalDepth
				totalDepth += NDCDepthToWorldSpace(max(depth, lastEntry)) - NDCDepthToWorldSpace(depth);
				lastEntry = 0.f;
			} else {
				lastEntry = -depth;
			}
		}
	}

		// standard "optical depth" calculation.
	const float opticalDepth = -0.15f;
	float shadowing = exp(opticalDepth * totalDepth);
	combinedColor.rgb *= shadowing;
	combinedColor.a *= shadowing;
	combinedColor.rgb += 1.0.xxx * (1.f-shadowing);
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0, SystemInputs sys) : SV_Target0
{
	uint firstId = FragmentIds[uint2(position.xy)];
	[branch] if (firstId == 0xffffffff) {
		return float4(0.0.xxx, 0);
	} else {

		SortingElement sortingBuffer[FixedSampleCount];
		float baseLineDepth = DepthTexture[uint2(position.xy)];
		float lastEntry = baseLineDepth;

		uint sampleCount = 0;
		uint nodeId = firstId;
		[loop] do {
			FragmentListNode node = NodesList[nodeId];
			//if (node.depth < baseLineDepth) {
				sortingBuffer[sampleCount++] = node.depth;
			//}
			nodeId = NodesList[nodeId].next;
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
				//if (node.depth < baseLineDepth) {
					SortedInsert_LimitedBuffer(sortingBuffer, node.depth);
				//}
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

			BlendSamples(sortingBuffer, sampleCount, lastEntry, combinedColor);

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
							SortedInsert(sortingBuffer, sampleCount, node.depth);
							sampleCountMetric++;
						}
						nodeId = node.next;
					} while(nodeId != 0xffffffff);

					BlendSamples(sortingBuffer, sampleCount, lastEntry, combinedColor);
					if (sampleCount < FixedSampleCount) {
						break;
					}

					nodeId = firstId;
					minDepth = sortingBuffer[FixedSampleCount-1];
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

