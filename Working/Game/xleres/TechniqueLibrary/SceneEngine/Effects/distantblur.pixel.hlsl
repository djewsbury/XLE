// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Framework/CommonResources.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"

cbuffer Constants
{
	float4	FilteringWeights0;		// weights for gaussian filter
	float4	FilteringWeights1;
	float4	FilteringWeights2;
}

Texture2D			BlurredBufferInput;
Texture2D<float>	DepthsInput;

cbuffer BlurConstants
{
	float DepthStart;
	float DepthEnd;
}

float DistanceToBlurring(float linearDepth)
{
	float blurring = saturate((linearDepth - DepthStart) / (DepthEnd - DepthStart));
	blurring = blurring*blurring;
	return blurring;
}

float4 integrate(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	// float linearDepth = NDCDepthToLinear0To1(DepthsInput.Load(int3(position.xy, 0)));
	float4 blurSample = BlurredBufferInput.Sample(ClampingSampler, texCoord);
	blurSample.rgb /= max(1e-5f, blurSample.a);
	return float4(blurSample.rgb, blurSample.a); // * DistanceToBlurring(linearDepth));
}

Texture2D_MaybeMS<float4>	InputTexture;

////////////////////////////////////////////////////////////////////////////////////
// Blurring with weight values for distance
// 	Note that when we do this, it isn't truly mathematically "separable"
// 	But the results turn out ok. It's possible that some shapes may produce
//	artifacts, because the weighting is smeared incorrectly due to the separation
//	of horizontal and vertical.

float LoadDepth(uint2 p)
{
	// note -- assuming 2:2 downsample between depths texture and blurring texture
	float A = LoadFloat1(DepthsInput, p + uint2(0,0), 0);
	float B = LoadFloat1(DepthsInput, p + uint2(1,0), 0);
	float C = LoadFloat1(DepthsInput, p + uint2(0,1), 0);
	float D = LoadFloat1(DepthsInput, p + uint2(1,1), 0);
	return NDCDepthToLinear0To1(min(min(min(A, B), C), D));

	//float A = LoadFloat1(DepthsInput, p + uint2( 0, 0), 0);
	//float B = LoadFloat1(DepthsInput, p + uint2( 1, 0), 0);
	//float C = LoadFloat1(DepthsInput, p + uint2( 0, 1), 0);
	//float D = LoadFloat1(DepthsInput, p + uint2(-1, 0), 0);
	//float E = LoadFloat1(DepthsInput, p + uint2( 0,-1), 0);
	//return NDCDepthToLinear0To1(min(min(min(min(A, B), C), D), E));
}

float4 HorizontalBlur_DistanceWeighted(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	const int offset[] = { -3, -2, -1, 0, 1, 2, 3 };
	const uint sampleCount = 7;

	float FixedWeights[7];
	FixedWeights[0] = FilteringWeights0.x;
	FixedWeights[1] = FilteringWeights0.y;
	FixedWeights[2] = FilteringWeights0.z;
	FixedWeights[3] = FilteringWeights0.w;
	FixedWeights[4] = FilteringWeights1.x;
	FixedWeights[5] = FilteringWeights1.y;
	FixedWeights[6] = FilteringWeights1.z;

	int2 inputDims;
	#if MSAA_SAMPLERS != 0
		int ignore;
		InputTexture.GetDimensions(inputDims.x, inputDims.y, ignore);
	#else
		InputTexture.GetDimensions(inputDims.x, inputDims.y);
	#endif

	float2 outputDims = position.xy / texCoord.xy;
	float2 coordScale = float2(inputDims.xy) / outputDims.xy;

	float weightSum = 0.f;
	float3 result = 0.0.xxx;

	uint2 pixelCoord = uint2(position.xy);
	[unroll] for (uint c=0; c<sampleCount; c++) {
		uint2 p;
		p.x = uint(clamp(
			coordScale.x * (pixelCoord.x + offset[c]) + .5f,
			0, inputDims.x-1));
		p.y = uint(coordScale.y * pixelCoord.y + .5f);

		float3 texSample = LoadFloat4(InputTexture, p, 0).rgb;
		float distantWeight = DistanceToBlurring(LoadDepth(p));
		result += texSample * FixedWeights[c] * distantWeight;
		weightSum += FixedWeights[c] * distantWeight;
	}

	float alpha = weightSum;
	return float4(result, alpha);
}

float4 VerticalBlur_DistanceWeighted(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	const int offset[]		= { -3, -2, -1, 0, 1, 2, 3 };
	const uint sampleCount	= 7;

	float FixedWeights[7];
	FixedWeights[0] = FilteringWeights0.x;
	FixedWeights[1] = FilteringWeights0.y;
	FixedWeights[2] = FilteringWeights0.z;
	FixedWeights[3] = FilteringWeights0.w;
	FixedWeights[4] = FilteringWeights1.x;
	FixedWeights[5] = FilteringWeights1.y;
	FixedWeights[6] = FilteringWeights1.z;

	int2 inputDims;
	#if MSAA_SAMPLERS != 0
		int ignore;
		InputTexture.GetDimensions(inputDims.x, inputDims.y, ignore);
	#else
		InputTexture.GetDimensions(inputDims.x, inputDims.y);
	#endif

	float2 outputDims = position.xy / texCoord.xy;
	float2 coordScale = float2(inputDims.xy) / outputDims.xy;

	float weightSum = 0.f;
	float3 result = 0.0.xxx;

	int2 pixelCoord = uint2(position.xy);
	[unroll] for (uint c=0; c<sampleCount; c++) {
		uint2 p;
		p.x = uint(coordScale.x * pixelCoord.x); // + .5f);
		p.y = uint(clamp(
			coordScale.y * (pixelCoord.y+offset[c]), // + .5f,
			0, inputDims.y-1));

		float4 texSample = LoadFloat4(InputTexture, p, 0).rgba;
		float distantWeight = texSample.a;
		result += texSample.rgb * FixedWeights[c];
		weightSum += distantWeight * FixedWeights[c];
	}

	float alpha = max(1e-5f, weightSum);
	return float4(result, alpha);
}
