// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"

RWTexture2D<float4>		MipChainUAV;
Texture2D<float4>		MipChainSRV;
SamplerState 			BilinearClamp;

[[vk::push_constant]] struct ControlUniformsStruct
{
	float4 A;
	uint4 B;
} ControlUniforms;

float2 UpsampleStep_GetOutputReciprocalDims() { return ControlUniforms.A.xy; }
float Blur_GetFilteringWeight() { return ControlUniforms.A.z; }
uint UpsampleStep_GetMipIndex() { return asuint(ControlUniforms.B.z); }
uint2 UpsampleStep_GetThreadGroupCount() { return asuint(ControlUniforms.B.xy); }
bool UpsampleStep_CopyHighResBlur() { return (bool)asuint(ControlUniforms.B.w); }

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

float GaussianWeight1D(float offset, float stdDevSq)
{
	// See https://en.wikipedia.org/wiki/Gaussian_blur
	// Note that this is equivalent to the product of 1d weight of x and y
	const float twiceStdDevSq = 2.0 * stdDevSq;
	const float C = 1.0 / (pi * twiceStdDevSq);		// can done later, because it's constant for all weights
	return C * exp(-dot(offset, offset) / twiceStdDevSq);
}

float GaussianWeight2D(float2 offset, float stdDevSq)
{
	// See https://en.wikipedia.org/wiki/Gaussian_blur
	// Note that this is equivalent to the product of 1d weight of x and y
	const float twiceStdDevSq = 2.0 * stdDevSq;
	const float C = 1.0 / (pi * twiceStdDevSq);		// can done later, because it's constant for all weights
	return C * exp(-dot(offset, offset) / twiceStdDevSq);
}

float3 UpsampleFilter_ComplexGaussianPrototype(uint2 pixelId, uint mipIndex, float2 dstTexelSize)
{
	// Prototype -- very low performance code
	// let's use actual gaussian weights -- calculated precisely to take into account the different
	// sizes of the src and dst pixels
	// This should give us an upper bound in terms of quality
	// Sample a 5x5 area from the src mip
	int2 srcMipDims; uint mipCount;
	MipChainSRV.GetDimensions(mipIndex, srcMipDims.x, srcMipDims.y, mipCount);
	float3 srcData[5][5];
	float srcWeight[5][5];
	const float stdDevSq = 1.5;
	for (int i=-2; i<=2; ++i)
		for (int j=-2; j<=2; ++j) {
			int2 srcMipPixelId = int2(pixelId) / 2 + int2(i, j);
			srcData[2+i][2+j] = MipChainSRV.mips[mipIndex][clamp(srcMipPixelId, int2(0,0), srcMipDims-1.xx)].rgb;

			// Calculate the weight for this pixel
			// we're begin super particular, so we'll do an integration approximation
			// ... though maybe the true integral here isn't so hard to calculate
			float weight = 0.f;
			[unroll] for (uint x=0; x<16; ++x)
				[unroll] for (uint y=0; y<16; ++y) {
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
	[unroll] for (uint x=0; x<5; ++x)
		[unroll] for (uint y=0; y<5; ++y)
			result += srcData[x][y] * srcWeight[x][y];		// weights already normalized
	return result;
}

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 threadGroupCounts = UpsampleStep_GetThreadGroupCount();
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	uint mipIndex = UpsampleStep_GetMipIndex();

	const float2 srcTexelSize = 2.0 * UpsampleStep_GetOutputReciprocalDims();
	const float2 offset = 0.5.xx;		// offset to sample in the middle of the dst pixel
	const float2 baseSourceTC = (float2(pixelId.xy) + offset) * UpsampleStep_GetOutputReciprocalDims();

	// Using the gaussian approach here does give a slightly nicer blur
	float3 filteredSample = UpsampleFilter_TentCircularBias(baseSourceTC, mipIndex+1, srcTexelSize);
	// float3 filteredSample = UpsampleFilter_ComplexGaussianPrototype(pixelId, mipIndex+1, UpsampleStep_GetOutputReciprocalDims());

	// use Blur_GetFilteringWeight() to adjust the strength of the blur (it's not perfect control, but it can give the sense of increasing/decreasing blur strength)
	float w = Blur_GetFilteringWeight(); // 
	filteredSample = lerp(SRGBToLinear_Formal(MipChainUAV[pixelId].rgb), filteredSample, w);

	MipChainUAV[pixelId] = float4(LinearToSRGB_Formal(filteredSample), 1);
}

