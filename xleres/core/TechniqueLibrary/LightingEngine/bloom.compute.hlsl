// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma GCC diagnostic ignored "-Wconversion"		// the DirectXShaderCompiler might be ignoring these, unfortunately

#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"
#include "../Math/MathConstants.hlsl"

#if HDR_INPUT_SAMPLE_COUNT > 1
	Texture2DMS<float3>		HDRInput : register(t0, space0);
#else
	Texture2D<float3>		HDRInput : register(t0, space0);
#endif
RWTexture2D<float4>		HighResBlurTemp : register(u1, space0);
RWTexture2D<float4>		MipChainUAV[8] : register(u2, space0);
Texture2D<float3>		MipChainSRV : register(t3, space0);
SamplerState 			BilinearClamp : register(s6, space0);

cbuffer BloomParameters : register(b5, space0)
{
	float BloomThreshold;
	float BloomDesaturationFactor;
	float Weight0, Weight1, Weight2, Weight3, Weight4, Weight5;
	float4 BloomLargeRadiusBrightness;
	float4 BloomSmallRadiusBrightness;
}

float CalculateLuminance(float3 color)
{
		//
		//	Typical weights for calculating luminance (assuming
		//	linear space input)
		//
		//		See also here, for a description of lightness:
		//			http://www.poynton.com/notes/colour_and_gamma/GammaFAQ.html
		//			--	humans have a non linear response to comparing
		//				luminance values. So perhaps we should use
		//				a curve like this in this calculation.
		//
	const uint method = 2;
	if (method == 1) {
			//
			//		See some interesting results from this "perceived brightness"
			//		algorithm:
			//			http://alienryderflex.com/hsp.html
			//			http://stackoverflow.com/questions/596216/formula-to-determine-brightness-of-rgb-color
			//		scroll down on the stackoverflow page to see how this
			//		method distributes brightness evenly and randomly through
			//		a color cube -- curious property...?
			//
		const float3 componentWeights = float3(0.299f, .587f, .114f);
		return sqrt(max(0,dot(componentWeights, color*color)));		// negatives cause havok... we have to be careful!
	} else if (method == 2) {
		float r = color.r, g = color.g, b = color.b;
		float chroma = sqrt(dot(color, float3(r-b, g-r, b-g)));
		const float ycRadiusWeight = 1.75;
  		return (ycRadiusWeight*chroma + color.b+color.g+color.r) / 3.0;
	} else {
		const float3 componentWeights = float3(0.2126f, 0.7152f, 0.0722f);
		return dot(color, componentWeights);
	}
}

float3 BrightPassScale(float3 input)
{
	// We can calculate the bloom on each channel individually; but it tends to just create far too much
	// saturation. It's better to bloom based on some luminance heuristic.
	// There are more complicated calculations we can make here; but arguably the simple linear effect has a bit of charm to it
	const float threshold = BloomThreshold;
	float a = CalculateLuminance(input) / threshold - 1.0;
	return saturate(input * a);
}

float FastLuminance(float3 c) { return (c.x+c.y+c.z)*0.333333; }

// #define USE_GEOMETRIC_MEAN 1

[numthreads(8, 8, 1)]
	void BrightPassFilter(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	// Before we do any blurring, we need to find and separate the parts of the image
	// that are bright enough to bloom
	// We're expecting HDRInput & OutputTexture to be different resolutions here.
	// we'll assume quartering for simplicity

	uint2 textureDims;
	MipChainUAV[0].GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);

	uint2 twiddler = uint2(1,0);
	#if HDR_INPUT_SAMPLE_COUNT > 1
		float3 A = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yy, 0), 0).rgb;
		float3 B = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xy, 0), 0).rgb;
		float3 C = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yx, 0), 0).rgb;
		float3 D = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xx, 0), 0).rgb;
	#else
		float3 A = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yy, 0)).rgb;
		float3 B = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xy, 0)).rgb;
		float3 C = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yx, 0)).rgb;
		float3 D = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xx, 0)).rgb;
	#endif

	float3 bA = BrightPassScale(A);
	float3 bB = BrightPassScale(B);
	float3 bC = BrightPassScale(C);
	float3 bD = BrightPassScale(D);
	
	float lA = FastLuminance(bA);
	float lB = FastLuminance(bB);
	float lC = FastLuminance(bC);
	float lD = FastLuminance(bD);

	// desaturate a bit
	bA = lerp(bA, lA.xxx, BloomDesaturationFactor);
	bB = lerp(bB, lB.xxx, BloomDesaturationFactor);
	bC = lerp(bC, lC.xxx, BloomDesaturationFactor);
	bD = lerp(bD, lD.xxx, BloomDesaturationFactor);

	float3 outputColor;
	#if !USE_GEOMETRIC_MEAN
		// use weighted average to take the edge of small bright highlights
		float wA = 1.0 / (lA + 1.0);
		float wB = 1.0 / (lB + 1.0);
		float wC = 1.0 / (lC + 1.0);
		float wD = 1.0 / (lD + 1.0);

		outputColor = (
			  bA * wA
			+ bB * wB
			+ bC * wC
			+ bD * wD
			) / (wA + wB + wC + wD);
	#else
		const float tinyValue = 1e-5f;
		outputColor = (
			  log(max(bA, tinyValue.xxx))
			+ log(max(bB, tinyValue.xxx))
			+ log(max(bC, tinyValue.xxx))
			+ log(max(bD, tinyValue.xxx))
			) / 4.0;
	#endif

	MipChainUAV[0][pixelId] = float4(outputColor, 1);
}

[[vk::push_constant]] struct ControlUniformsStruct
{
	float4 A;
	uint4 B;
} ControlUniforms;

float2 UpsampleStep_GetOutputReciprocalDims() { return ControlUniforms.A.xy; }
uint UpsampleStep_GetMipIndex() { return asuint(ControlUniforms.B.z); }
uint2 UpsampleStep_GetThreadGroupCount() { return asuint(ControlUniforms.B.xy); }
bool UpsampleStep_CopyHighResBlur() { return (bool)asuint(ControlUniforms.B.w); }

float3 UpsampleFilter_Square(float2 tc, uint mipIndex, float2 texelSize)
{
	float4 twiddler = float4(texelSize, -texelSize);
 	float3 filteredSample =
 		  MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.zw, mipIndex).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xw, mipIndex).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.zy, mipIndex).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xy, mipIndex).rgb
 		;
	filteredSample.rgb *= 0.25;
	return filteredSample;
}

float3 UpsampleFilter_BasicTent(float2 tc, uint mipIndex, float2 texelSize)
{
	// classic tent filter style
	// 1 2 1
	// 2 4 2
	// 1 2 1
	const float4 twiddler = float4(texelSize.xy, -texelSize.x, 0);
	float3 filteredSample =
				MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.xy, mipIndex).rgb
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.wy, mipIndex).rgb
 		+		MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.zy, mipIndex).rgb
		
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.xw, mipIndex).rgb
		+ 4.0 * MipChainSRV.SampleLevel(BilinearClamp, tc			   , mipIndex).rgb
		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xw, mipIndex).rgb

		+		MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.zy, mipIndex).rgb
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.wy, mipIndex).rgb
 		+		MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xy, mipIndex).rgb
 		;
	filteredSample.rgb *= 1.0 / 16.0;
	return filteredSample;
}

float3 UpsampleFilter_TentCircularBias(float2 tc, uint mipIndex, float2 texelSize)
{
	// Pushing apart the samples here helps improve the stability of the blurred image considerably
	// while also giving us more blur.
	// with pushApart=1.5, we should get approx 3 texels between each sample, which is probably the
	// most we can sustain -- with the bilinear, this should still result in all pixels being weighted
	const float pushApart = 1.5;
	// Tent filter with circular bias (see http://cg.skku.edu/pub/papers/2009-lee-tvcg-mintdof-cam.pdf)
	// The bias here will ultimately just play with the bilinear weights since every SampleLevel here
	// is actually going to become 4 samples.
	// This filtering is in general not particularly mathematical or strict, so small tweaks can help
	// if they improve the look
	const float4 twiddler = pushApart * float4(texelSize.xy, -texelSize.x, 0);
	const float circularBias = 0.70710678; // 1.0 / sqrt(2.0);
	float3 filteredSample =
				MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.xy * circularBias	, mipIndex).rgb
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.wy					, mipIndex).rgb
 		+		MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.zy * circularBias	, mipIndex).rgb
		
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc - twiddler.xw					, mipIndex).rgb
		+ 4.0 * MipChainSRV.SampleLevel(BilinearClamp, tc				 				, mipIndex).rgb
		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xw					, mipIndex).rgb

		+		MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.zy * circularBias	, mipIndex).rgb
 		+ 2.0 * MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.wy					, mipIndex).rgb
 		+		MipChainSRV.SampleLevel(BilinearClamp, tc + twiddler.xy * circularBias	, mipIndex).rgb
 		;
	filteredSample.rgb *= 1.0 / 16.0;
	return filteredSample;
}

float GaussianWeight2D(float2 offset, float stdDevSq)
{
	// See https://en.wikipedia.org/wiki/Gaussian_blur
	// Note that this is equivalent to the product of 1d weight of x and y
	const float twiceStdDevSq = 2.0 * stdDevSq;
	const float C = 1.0 / (pi * twiceStdDevSq);		// can done later, because it's constant for all weights
	return C * exp(-dot(offset, offset) / twiceStdDevSq);
}

float3 UpsampleFilter_ComplexGaussianPrototype(uint2 pixelId, uint dstMipIndex, float2 dstTexelSize)
{
	// Prototype -- very low performance code
	// let's use actual gaussian weights -- calculated precisely to take into account the different
	// sizes of the src and dst pixels
	// This should give us an upper bound in terms of quality
	// Sample a 5x5 area from the src mip
	float3 srcData[5][5];
	float srcWeight[5][5];
	const float stdDevSq = 1;
	for (int i=-2; i<=2; ++i)
		for (int j=-2; j<=2; ++j) {
			uint2 srcMipPixelId = pixelId / 2 + int2(i, j);
			srcData[2+i][2+j] = MipChainSRV.mips[dstMipIndex+1][srcMipPixelId];

			// Calculate the weight for this pixel
			// we're begin super particular, so we'll do an integration approximation
			// ... though maybe the true integral here isn't so hard to calculate
			float weight = 0.f;
			for (uint x=0; x<16; ++x)
				for (uint y=0; y<16; ++y) {
					float2 srcMipPixelCenter = float2(
						srcMipPixelId.x + ((float)x+0.5) / 16.0,
						srcMipPixelId.y + ((float)y+0.5) / 16.0);
					float2 dstMipPixelCenter = 2.0 * srcMipPixelCenter;
					float2 offset = dstMipPixelCenter - (float2(pixelId) + 0.5.xx);
					weight += GaussianWeight2D(offset, stdDevSq) / 64.0;
				}
				
			srcWeight[2+i][2+j] = weight;
		}
	
	float3 result = 0.0;
	for (uint x=0; x<5; ++x)
		for (uint y=0; y<5; ++y)
			result += srcData[x][y] * srcWeight[x][y];		// weights already normalized
	return result;
}

[numthreads(8, 8, 1)]
	void UpsampleStep(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 threadGroupCounts = UpsampleStep_GetThreadGroupCount();
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	uint mipIndex = UpsampleStep_GetMipIndex();

	const float2 srcTexelSize = 2.0 * UpsampleStep_GetOutputReciprocalDims();
	const float2 offset = 0.5.xx;		// offset to sample in the middle of the dst pixel
	const float2 baseSourceTC = (float2(pixelId.xy) + offset) * UpsampleStep_GetOutputReciprocalDims();
	const uint filterType = 2;
	float3 filteredSample;
	if (filterType == 0) 		filteredSample = UpsampleFilter_Square(baseSourceTC, mipIndex+1, srcTexelSize);
	else if (filterType == 1) 	filteredSample = UpsampleFilter_BasicTent(baseSourceTC, mipIndex+1, srcTexelSize);
	else if (filterType == 2) 	filteredSample = UpsampleFilter_TentCircularBias(baseSourceTC, mipIndex+1, srcTexelSize);
	else if (filterType == 3) 	filteredSample = UpsampleFilter_ComplexGaussianPrototype(pixelId, mipIndex, UpsampleStep_GetOutputReciprocalDims());

	if (mipIndex == 0) {
		// high res is an alternative to what's behind
		if (UpsampleStep_CopyHighResBlur()) {
			#if !USE_GEOMETRIC_MEAN
				filteredSample *= BloomLargeRadiusBrightness.rgb;
			#else
				filteredSample = exp(filteredSample) * BloomLargeRadiusBrightness.rgb;
			#endif
			filteredSample += HighResBlurTemp[pixelId].rgb; // in the final upsample we merge in the small radius blur
		} else {
			filteredSample += MipChainUAV[mipIndex][pixelId].rgb;
			filteredSample *= BloomLargeRadiusBrightness.rgb;
		}
		MipChainUAV[mipIndex][pixelId] = float4(filteredSample, 1);
	} else {
		MipChainUAV[mipIndex][pixelId] = float4(MipChainUAV[mipIndex][pixelId].rgb + filteredSample, 1);
	}
}

