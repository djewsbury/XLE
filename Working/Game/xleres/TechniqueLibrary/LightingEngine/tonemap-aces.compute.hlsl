// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Utility/Colour.hlsl"

cbuffer Params
{
	row_major float3x4 PreToneScale;
	row_major float3x4 PostToneScale;
}

float3 c5c9CurveEsimate_LogY3(float3 x)
{
	// this is what we're estimating:
	// 	std::log(ACES::segmented_spline_c9_fwd(ACES::segmented_spline_c5_fwd(x)));

	const float A = 1.25588544e+03;
	const float B = -1.59161681e+02;
	const float C = 3.10987237e+01;
	const float D = 3.21980642e+02;
	const float E = 2.24585460e+00;
	const float F = 8.39043523e+00;
	const float G = 4.34339192e-01;

	float3 top    = ((A*x + B)*x + C)*x - 1.69897;
	float3 bottom = ((D*x + E)*x + F)*x + G;
	return top / bottom;
}

float3 ToneMapAces(float3 x)
{
	x = mul(PreToneScale, float4(x, 0));
	x = exp(c5c9CurveEsimate_LogY3(x));
	return mul(PostToneScale, float4(x, 0));
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// 			e n t r y   p o i n t
//////////////////////////////////////////////////////////////////////////////////////////////////

#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

#if HDR_INPUT_SAMPLE_COUNT
	Texture2DMS<float3> HDRInput;
#else
	Texture2D<float3> HDRInput;
#endif
RWTexture2D<float3> LDROutput;		// output could be >8 bit depth, of course, but we're expecting smaller range than the input

#if HAS_BRIGHT_PASS
Texture2D<float3> BrightPass;
SamplerState BilinearClamp;

float3 BrightPassSample(float2 tc, float2 brightPassTexelSize)
{
	const uint sampleQuality = 1;
	if (sampleQuality == 0) {
		return BrightPass.SampleLevel(BilinearClamp, tc, 0);
	} else if (sampleQuality == 1) {
		// Using a tent filter with circular bias; ala the upsampling steps
		const float pushApart = 1.5;
		const float4 twiddler = pushApart * float4(brightPassTexelSize.xy, -brightPassTexelSize.x, 0);
		const float circularBias = 0.70710678; // 1.0 / sqrt(2.0);
		float3 filteredSample =
					BrightPass.SampleLevel(BilinearClamp, tc - twiddler.xy * circularBias	, 0).rgb
			+ 2.0 * BrightPass.SampleLevel(BilinearClamp, tc - twiddler.wy					, 0).rgb
			+		BrightPass.SampleLevel(BilinearClamp, tc - twiddler.zy * circularBias	, 0).rgb
			
			+ 2.0 * BrightPass.SampleLevel(BilinearClamp, tc - twiddler.xw					, 0).rgb
			+ 4.0 * BrightPass.SampleLevel(BilinearClamp, tc				 				, 0).rgb
			+ 2.0 * BrightPass.SampleLevel(BilinearClamp, tc + twiddler.xw					, 0).rgb

			+		BrightPass.SampleLevel(BilinearClamp, tc + twiddler.zy * circularBias	, 0).rgb
			+ 2.0 * BrightPass.SampleLevel(BilinearClamp, tc + twiddler.wy					, 0).rgb
			+		BrightPass.SampleLevel(BilinearClamp, tc + twiddler.xy * circularBias	, 0).rgb
			;
		return filteredSample * 1.0 / 16.0;
	} else {
		return 0;
	}
}
#endif

float3 MinimalToneMap(float3 x) 		{ return x/(1+x); }
float3 InvertMinimalToneMap(float3 y) 	{ return y/(1-y); }

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	LDROutput.GetDimensions(textureDims.x, textureDims.y);
	float2 brightPassTexelSize = float2(2.0 / textureDims.x, 2.0 / textureDims.y);		// assuming bright pass load at quarter resolution

	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);

	if (pixelId.x < textureDims.x && pixelId.y < textureDims.y) {

		float3 hdrInput;
		#if HDR_INPUT_SAMPLE_COUNT
			// Integrated resolve for MSAA. We do this at this point if there are no postprocessing effects that need to happen
			// in HDR post-resolve, but pre-tonemap
			// But note that this minimal tonemap becomes extremely flat -- we could be loosing a lot of precision just going through these transforms
			hdrInput = MinimalToneMap(HDRInput.sample[0][pixelId]);
			[unroll] for (uint c=1; c<HDR_INPUT_SAMPLE_COUNT; ++c)
				hdrInput += MinimalToneMap(HDRInput.sample[c][pixelId]);
			hdrInput = InvertMinimalToneMap(hdrInput / HDR_INPUT_SAMPLE_COUNT);
		#else
			hdrInput = HDRInput[pixelId];
		#endif

		float3 linearColour = ToneMapAces(hdrInput);
		#if HAS_BRIGHT_PASS
			linearColour += BrightPassSample(pixelId.xy / float2(textureDims.xy), brightPassTexelSize);
		#endif
		LDROutput[pixelId] = LinearToSRGB_Formal(saturate(linearColour));
	}
}
