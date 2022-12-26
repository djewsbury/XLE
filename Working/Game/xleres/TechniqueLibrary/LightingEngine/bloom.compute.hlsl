// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

Texture2D<float3>		HDRInput;
RWTexture2D<float3>		MipChainUAV[6];
Texture2D<float3>		MipChainSRV;
SamplerState 			BilinearClamp;

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
	const bool usePerceivedBrightness = true;
	if (usePerceivedBrightness) {
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
	} else {
		const float3 componentWeights = float3(0.2126f, 0.7152f, 0.0722f);
		return dot(color, componentWeights);
	}
}

#define BloomThreshold .95
#define BloomDesaturationFactor .5

float3 BrightPassScale(float3 input)
{
	const float threshold = BloomThreshold;
	return saturate(input/threshold - 1.0.xxx);
}

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
	float3 A = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yy, 0)).rgb;
	float3 B = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xy, 0)).rgb;
	float3 C = HDRInput.Load(uint3(pixelId.xy*2+twiddler.yx, 0)).rgb;
	float3 D = HDRInput.Load(uint3(pixelId.xy*2+twiddler.xx, 0)).rgb;
	
	float lA = CalculateLuminance(A);
	float lB = CalculateLuminance(B);
	float lC = CalculateLuminance(C);
	float lD = CalculateLuminance(D);

	// desaturate a bit
	A = lerp(A, lA.xxx, BloomDesaturationFactor);
	B = lerp(B, lB.xxx, BloomDesaturationFactor);
	C = lerp(C, lC.xxx, BloomDesaturationFactor);
	D = lerp(D, lD.xxx, BloomDesaturationFactor);

	float3 outputColor;
	#if !USE_GEOMETRIC_MEAN
		// use weighted average to take the edge of small bright highlights
		float wA = 1.0 / (lA + 1.0);
		float wB = 1.0 / (lB + 1.0);
		float wC = 1.0 / (lC + 1.0);
		float wD = 1.0 / (lD + 1.0);

		outputColor = (
			  BrightPassScale(A) * wA
			+ BrightPassScale(B) * wB
			+ BrightPassScale(C) * wC
			+ BrightPassScale(D) * wD
			) / (wA + wB + wC + wD);
	#else
		const float tinyValue = 1-5f;
		outputColor = (
			  log(max(BrightPassScale(A), tinyValue.xxx))
			+ log(max(BrightPassScale(B), tinyValue.xxx))
			+ log(max(BrightPassScale(C), tinyValue.xxx))
			+ log(max(BrightPassScale(D), tinyValue.xxx))
			) / 4.0;
	#endif

	MipChainUAV[0][pixelId] = outputColor;
}

[[vk::push_constant]] struct ControlUniformsStruct
{
	float4 A;
	uint4 B;
} ControlUniforms;

float2 UpsampleStep_GetOutputReciprocalDims() { return ControlUniforms.A.xy; }
uint UpsampleStep_GetMipIndex() { return asuint(ControlUniforms.B.z); }
uint2 UpsampleStep_GetThreadGroupCount() { return asuint(ControlUniforms.B.xy); }

[numthreads(8, 8, 1)]
	void UpsampleStep(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 threadGroupCounts = UpsampleStep_GetThreadGroupCount();
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	uint mipIndex = UpsampleStep_GetMipIndex();

	float2 baseSourceTC = pixelId.xy * UpsampleStep_GetOutputReciprocalDims();
	const float A = 2.0;
	float4 twiddler = float4(A * UpsampleStep_GetOutputReciprocalDims(), -A * UpsampleStep_GetOutputReciprocalDims());
 	float3 filteredSample =
 		  MipChainSRV.SampleLevel(BilinearClamp, baseSourceTC + twiddler.zw, mipIndex+1).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, baseSourceTC + twiddler.xw, mipIndex+1).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, baseSourceTC + twiddler.zy, mipIndex+1).rgb
 		+ MipChainSRV.SampleLevel(BilinearClamp, baseSourceTC + twiddler.xy, mipIndex+1).rgb
 		;

	MipChainUAV[mipIndex][pixelId] += filteredSample.rgb * 0.25;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 		F A S T   D O W N S A M P L E   W I T H   F F X _ S P D
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SPD_LINEAR_SAMPLER

globallycoherent RWBuffer<uint> AtomicBuffer;		// global atomic counter - MUST be initialized to 0

float2 FastMipChain_GetReciprocalInputDims() { return ControlUniforms.A.xy; }
uint FastMipChain_GetMipCount() { return ControlUniforms.B.z; }
uint FastMipChain_GetThreadGroupCount() { return ControlUniforms.B.x; }

// Setup pre-portability-header defines (sets up GLSL/HLSL path, etc)
#define A_GPU 1
#define A_HLSL 1
#define SPD_PACKED_ONLY
#if defined(SPD_PACKED_ONLY)
	#define A_HALF
#endif

// Include the portability header (or copy it in without an include).
#include "xleres/Foreign/ffx-spd/ffx_a.h"

// if subgroup operations are not supported / can't use SM6.0
// #define SPD_NO_WAVE_OPERATIONS

#if !defined(SPD_PACKED_ONLY)

	groupshared AF4 spd_intermediate[16][16];

	AF4 SpdLoadSourceImage(ASU2 p)
	{
		AF2 textureCoord = p * FastMipChain_GetReciprocalInputDims() + FastMipChain_GetReciprocalInputDims();
		return float4(MipChainSRV.SampleLevel(BilinearClamp, textureCoord, 0).rgb, 1);
	}

	// SpdLoad() takes a 32-bit signed integer 2D coordinate and loads color.
	// Loads the 5th mip level, each value is computed by a different thread group
	// last thread group will access all its elements and compute the subsequent mips
	AF4 SpdLoad(ASU2 tex){return AF4(MipChainUAV[1+5][tex], 1);}
	void SpdStore(ASU2 pix, AF4 value, AU1 index){MipChainUAV[1+index][pix] = value;}

	// Define the LDS load and store functions
	AF4 SpdLoadIntermediate(AU1 x, AU1 y){return spd_intermediate[x][y];}
	void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value){spd_intermediate[x][y] = value;}

	// Define your reduction function: takes as input the four 2x2 values and returns 1 output value
	// todo also consider brightness weighting here
	AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return AF4((v0.rgb+v1.rgb+v2.rgb+v3.rgb)*0.25, 1);}

#else

	groupshared AH4 spd_intermediate[16][16];

	AH4 SpdLoadSourceImageH(ASU2 p){
		AF2 textureCoord = p * FastMipChain_GetReciprocalInputDims() + FastMipChain_GetReciprocalInputDims();
		return AH4(MipChainSRV.SampleLevel(BilinearClamp, textureCoord, 0).rgb, 1);
	}

	// SpdLoadH() takes a 32-bit signed integer 2D coordinate and loads color.
	// Loads the 5th mip level, each value is computed by a different thread group
	// last thread group will access all its elements and compute the subsequent mips
	AH4 SpdLoadH(ASU2 tex){return AH4(MipChainUAV[1+5][tex].rgb, 1);}

	// Define the store function
	void SpdStoreH(ASU2 pix, AH4 value, AU1 index){MipChainUAV[1+index][pix] = AF4(value);}

	// Define the lds load and store functions
	AH4 SpdLoadIntermediateH(AU1 x, AU1 y){return spd_intermediate[x][y];}
	void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value){spd_intermediate[x][y] = value;}

	// Define your reduction function: takes as input the four 2x2 values and returns 1 output value
	// todo also consider brightness weighting here
	AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3){return AH4((v0.rgb+v1.rgb+v2.rgb+v3.rgb)*AH1(0.25), 1);}

#endif

groupshared AU1 spd_counter;

// Define the atomic counter increase function
// Note this is technically not required if we're limiting the number of mips to 5 or less
void SpdIncreaseAtomicCounter(){InterlockedAdd(AtomicBuffer[0], 1, spd_counter);}
AU1 SpdGetAtomicCounter(){return spd_counter;}

#include "xleres/Foreign/ffx-spd/ffx_spd.h"

[numthreads(256, 1, 1)]
	void FastMipChain(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
	#if !defined(SPD_PACKED_ONLY)
		SpdDownsample(
			AU2(WorkGroupId.xy), AU1(LocalThreadIndex),
			AU1(FastMipChain_GetMipCount()), AU1(FastMipChain_GetThreadGroupCount()));
	#else
		SpdDownsampleH(
			AU2(WorkGroupId.xy), AU1(LocalThreadIndex),
			AU1(FastMipChain_GetMipCount()), AU1(FastMipChain_GetThreadGroupCount()));
	#endif
}
