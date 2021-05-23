// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "VolumetricFog.hlsl"

Texture2DArray<float> InputShadowTextures : register(t2);

int WorkingSlice;
uint DownsampleScaleFactor;
cbuffer Filtering
{
	float4	FilteringWeights0;		// weights for gaussian filter
	float4	FilteringWeights1;
	float4	FilteringWeights2;
}

float LoadDepth(int2 coords)
{
	return InputShadowTextures.Load(int4(coords.xy, WorkingSlice, 0));
}

float BuildExponentialShadowMap(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float accumulativeLinearDepth = 0.f;

		//		load 16 samples from the source texture
		//		and find the average
		//		Should it just be the average? Or should it
		//		be weighted towards the centre.

		//		todo -- use "GatherRed" to improve performance

	const int DownsampleCount = 4;
	const bool useAverage = false;
	if (useAverage) {
		[unroll] for (int y=0; y<DownsampleCount; ++y) {
			[unroll] for (int x=0; x<DownsampleCount; ++x) {
				int2 coords = int2(position.xy)*(DownsampleCount*DownsampleScaleFactor) + int2(x, y);
				accumulativeLinearDepth += MakeComparisonDistance(LoadDepth(coords.xy), WorkingSlice);
			}
		}

		#if ESM_SHADOW_MAPS==1
			return exp((-ESM_C / float(DownsampleCount*DownsampleCount)) * accumulativeLinearDepth);
		#else
			return accumulativeLinearDepth / float(DownsampleCount*DownsampleCount);
		#endif
	} else {

			//	Collect the maximum depth here. This might make the shadows slightly smaller,
			//	but that's probably better than making the shadows slightly larger
		float depth = 0.f;
		const bool useMax = false;
		if (useMax) {
			[unroll] for (int y=0; y<DownsampleCount; ++y) {
				[unroll] for (int x=0; x<DownsampleCount; ++x) {
					int2 coords = int2(position.xy)*(DownsampleCount*DownsampleScaleFactor) + int2(x, y);
					depth = max(depth, LoadDepth(coords.xy));
				}
			}
		} else {
			depth = 1.f;
			[unroll] for (int y=0; y<DownsampleCount; ++y) {
				[unroll] for (int x=0; x<DownsampleCount; ++x) {
					int2 coords = int2(position.xy)*(DownsampleCount*DownsampleScaleFactor) + int2(x, y);
					depth = min(depth, LoadDepth(coords.xy));
				}
			}
		}

		depth = MakeComparisonDistance(depth, WorkingSlice);

		#if ESM_SHADOW_MAPS==1
			return exp(-ESM_C * depth);
		#else
			return depth;
		#endif
	}
}

Texture2DArray<float> FilteringSource : register(t0);

		//
		//		Separable gaussian filter
		//			(quick blurring of the shadow values)
		//

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const bool DontBlurDistantSamples = true;
static const bool UseFilteringWeights = true;

#if ESM_SHADOW_MAPS==1
	float DistanceSampleThreshold() { return exp(.95f * ESM_C); }
#else
	float DistanceSampleThreshold() { return 0.98f; }
#endif

void AccValue(int2 coord, float weight, inout float accumulatedValue, inout float accWeight)
{
	float value = weight * FilteringSource.Load(int4(coord.xy, WorkingSlice, 0));
	if (!DontBlurDistantSamples || value < DistanceSampleThreshold()) {
		accumulatedValue += value;
		accWeight += weight;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float HorizBlur(int2 mid, int samples, int textureWidth)
{
	float accumulatedValue = 0.f;

	float filteringWeights[11];
	if (UseFilteringWeights) {
		filteringWeights[ 0] = FilteringWeights0.x;
		filteringWeights[ 1] = FilteringWeights0.y;
		filteringWeights[ 2] = FilteringWeights0.z;
		filteringWeights[ 3] = FilteringWeights0.w;
		filteringWeights[ 4] = FilteringWeights1.x;
		filteringWeights[ 5] = FilteringWeights1.y;
		filteringWeights[ 6] = FilteringWeights1.z;
		filteringWeights[ 7] = FilteringWeights1.w;
		filteringWeights[ 8] = FilteringWeights2.x;
		filteringWeights[ 9] = FilteringWeights2.y;
		filteringWeights[10] = FilteringWeights2.z;
	} else {
		for (uint c=0; c<samples; ++c)
			filteringWeights[c] = 1.f / float(samples);
	}

	float accWeight = 0.f;
	[unroll] for (int c=0; c<samples/2; ++c) {
		int x = max(mid.x - samples/2 + c, 0);
		AccValue(int2(x, mid.y), filteringWeights[c], accumulatedValue, accWeight);
	}

	AccValue(mid.xy, filteringWeights[samples/2], accumulatedValue, accWeight);

	[unroll] for (int c=0; c<samples/2; ++c) {
		uint x = min(mid.x + 1 + c, textureWidth-1);
		AccValue(int2(x, mid.y), filteringWeights[samples/2 + 1 + c], accumulatedValue, accWeight);
	}

	if (!accWeight) {
		#if ESM_SHADOW_MAPS==1
			accumulatedValue = 10000.f * DistanceSampleThreshold();
		#else
			accumulatedValue = 1.f;
		#endif
	} else
		accumulatedValue /= accWeight;

	return accumulatedValue;
}

float VertBlur(int2 mid, int samples, int textureHeight)
{
	float accumulatedValue = 0.f;

	float filteringWeights[11];
	if (UseFilteringWeights) {
		filteringWeights[ 0] = FilteringWeights0.x;
		filteringWeights[ 1] = FilteringWeights0.y;
		filteringWeights[ 2] = FilteringWeights0.z;
		filteringWeights[ 3] = FilteringWeights0.w;
		filteringWeights[ 4] = FilteringWeights1.x;
		filteringWeights[ 5] = FilteringWeights1.y;
		filteringWeights[ 6] = FilteringWeights1.z;
		filteringWeights[ 7] = FilteringWeights1.w;
		filteringWeights[ 8] = FilteringWeights2.x;
		filteringWeights[ 9] = FilteringWeights2.y;
		filteringWeights[10] = FilteringWeights2.z;
	} else {
		for (uint c=0; c<samples; ++c)
			filteringWeights[c] = 1.f / float(samples);
	}

	float accWeight = 0.f;
	[unroll] for (int c=0; c<samples/2; ++c) {
		int y = max(mid.y - samples/2 + c, 0);
		AccValue(int2(mid.x, y), filteringWeights[c], accumulatedValue, accWeight);
	}

	AccValue(mid.xy, filteringWeights[samples/2], accumulatedValue, accWeight);

	[unroll] for (int c=0; c<samples/2; ++c) {
		uint y = min(mid.y + 1 + c, textureHeight-1);
		AccValue(int2(mid.x, y), filteringWeights[samples/2 + 1 + c], accumulatedValue, accWeight);
	}

	if (!accWeight) {
		#if ESM_SHADOW_MAPS==1
			accumulatedValue = 10000.f * DistanceSampleThreshold();
		#else
			accumulatedValue = 1.f;
		#endif
	} else
		accumulatedValue /= accWeight;

	return accumulatedValue;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float HorizontalFilter5(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return HorizBlur(int2(position.xy), 5, uint(position.x / texCoord.x));
}

float VerticalFilter5(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return VertBlur(int2(position.xy), 5, uint(position.y / texCoord.y));
}

float HorizontalFilter7(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return HorizBlur(int2(position.xy), 7, uint(position.x / texCoord.x));
}

float VerticalFilter7(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return VertBlur(int2(position.xy), 7, uint(position.y / texCoord.y));
}

float HorizontalBoxFilter11(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return HorizBlur(int2(position.xy), 11, uint(position.x / texCoord.x));
}

float VerticalBoxFilter11(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return VertBlur(int2(position.xy), 11, uint(position.y / texCoord.y));
}
