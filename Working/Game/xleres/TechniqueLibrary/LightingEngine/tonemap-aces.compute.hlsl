// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Utility/Colour.hlsl"

cbuffer Params
{
	row_major float3x4 PreToneScale;
	row_major float3x4 PostToneScale;
	float ExposureControl;
}

cbuffer LookupTable
{
	float4 LookupTable_Values[256/4];
}
// #define USE_LOOKUP_TABLE 1

float c5c9CurveEsimate_LogY4(float x)
{
	// this is what we're estimating:
	// 	std::log(ACES::segmented_spline_c9_fwd(ACES::segmented_spline_c5_fwd(x)));

	const float highA = 4.83502402e+03;
	const float highB = -7.01383142e+02;
	const float highC = -1.00362418e+00;
	const float highD = 1.24881353e+03;
	const float highE = -2.87320733e+01;

	const float lowA = 26014.79951605;
	const float lowB = -1722.49722912;
	const float lowC = -193.28455719;
	const float lowD = 6500.52389364;
	const float lowE = 723.32825656;

	float alpha = saturate(x*3-1);
	// (expressed without the dot products, etc)
	// float top = lerp(lowA, highA, alpha) * x + lerp(lowB, highB, alpha);
	// float bottom = lerp(lowC, highC, alpha) * x*x + lerp(lowD, highD, alpha) * x + lerp(lowE, highE, alpha);

	float4 P = float4((1-alpha) * x, alpha * x, (1-alpha), alpha);
	float top = dot(float4(lowA, highA, lowB, highB), P);
	float bottom = dot(float4(lowD, highD, lowE, highE), P);
	bottom += dot(float2(lowC, highC), P.xy*x);
	return top / bottom;
}

float LookupFromCBuffer(float x)
{
	// For verification & comparison purposes -- we'll just lookup the curve from an array
	const float left = 1.0/4096.0;
	const float right = 2.0;
	if (x >= right) {
		// f(2) = 37.9866, f(8) = 46.2867
		return lerp(37.9866, 46.2867, (x-2.0)/(8.0-2.0));
	}
	x = max(x, left);
	float A = 256.f * (x-left) / (right-left);
	uint i = A;
	float a = LookupTable_Values[i>>2][i&3], b = LookupTable_Values[(i+1)>>2][(i+1)&3];
	return lerp(a, b, A-i);
}

float3 ToneMapAces(float3 x)
{
	x = mul(PreToneScale, float4(x, 0));
	#if !USE_LOOKUP_TABLE
		x = exp(float3(c5c9CurveEsimate_LogY4(x.x), c5c9CurveEsimate_LogY4(x.y), c5c9CurveEsimate_LogY4(x.z)));
	#else
		x = float3(LookupFromCBuffer(x.x), LookupFromCBuffer(x.y), LookupFromCBuffer(x.z));
	#endif
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
RWTexture2D<float4> LDROutput;		// output could be >8 bit depth, of course, but we're expecting smaller range than the input

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

		hdrInput *= ExposureControl;

		float3 linearColour = ToneMapAces(hdrInput);
		#if HAS_BRIGHT_PASS
			linearColour += BrightPassSample(pixelId.xy / float2(textureDims.xy), brightPassTexelSize);
		#endif
		LDROutput[pixelId] = float4(LinearToSRGB_Formal(saturate(linearColour)), 1);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// reference

float c5c9CurveEsimate_LogY3(float x)
{
	// older estimate of
	// 	std::log(ACES::segmented_spline_c9_fwd(ACES::segmented_spline_c5_fwd(x)));

	const float A = 8.4253088654086;
	const float B = -0.643530249560736;
	const float C = 0.193493559682001;
	const float D = 2.1643734969300756;
	const float E = 0.10995295285941213;
	const float F = 0.07545273828171664;
	const float G = 0.002939671363902812;
	const float H = -0.012186783602340929;

	float top    = ((A*x + B)*x + C)*x + H;
	float bottom = ((D*x + E)*x + F)*x + G;
	return top / bottom;
}

