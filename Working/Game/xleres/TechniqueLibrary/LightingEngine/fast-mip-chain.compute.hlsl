// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 		F A S T   D O W N S A M P L E   W I T H   F F X _ S P D
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// uniform bindings are so it can be used with the tone map operator, which uses a single descriptor set, shared by many shaders
RWTexture2D<float4>		MipChainUAV[8] : register(u2, space0);
Texture2D<float3>		InputTexture : register(t3, space0);
SamplerState 			BilinearClamp : register(s6, space0);

#define SPD_LINEAR_SAMPLER

[[vk::push_constant]] struct ControlUniformsStruct
{
	float4 A;
	uint4 B;
} ControlUniforms;

globallycoherent RWBuffer<uint> AtomicBuffer : register(u4, space0);		// global atomic counter - MUST be initialized to 0

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

#if !defined(MIP_OFFSET)
	#define MIP_OFFSET 0
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
		return float4(InputTexture.SampleLevel(BilinearClamp, textureCoord, 0).rgb, 1);
	}

	// SpdLoad() takes a 32-bit signed integer 2D coordinate and loads color.
	// Loads the 5th mip level, each value is computed by a different thread group
	// last thread group will access all its elements and compute the subsequent mips
	AF4 SpdLoad(ASU2 tex){return AF4(MipChainUAV[MIP_OFFSET+5][tex].rgb, 1);}
	void SpdStore(ASU2 pix, AF4 value, AU1 index){MipChainUAV[MIP_OFFSET+index][pix] = float4(value.xyz, 1);}

	// Define the LDS load and store functions
	AF4 SpdLoadIntermediate(AU1 x, AU1 y){return spd_intermediate[x][y];}
	void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value){spd_intermediate[x][y] = value;}

	// Define your reduction function: takes as input the four 2x2 values and returns 1 output value
	AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return AF4((v0.rgb+v1.rgb+v2.rgb+v3.rgb)*0.25, 1);}

#else

	groupshared AH4 spd_intermediate[16][16];

	AH4 SpdLoadSourceImageH(ASU2 p){
		AF2 textureCoord = p * FastMipChain_GetReciprocalInputDims() + FastMipChain_GetReciprocalInputDims();
		return AH4(InputTexture.SampleLevel(BilinearClamp, textureCoord, 0).rgb, 1);
	}

	// SpdLoadH() takes a 32-bit signed integer 2D coordinate and loads color.
	// Loads the 5th mip level, each value is computed by a different thread group
	// last thread group will access all its elements and compute the subsequent mips
	AH4 SpdLoadH(ASU2 tex){return AH4(MipChainUAV[MIP_OFFSET+5][tex].rgb, 1);}

	// Define the store function
	void SpdStoreH(ASU2 pix, AH4 value, AU1 index){MipChainUAV[MIP_OFFSET+index][pix] = AF4(value.xyz, 1);}

	// Define the lds load and store functions
	AH4 SpdLoadIntermediateH(AU1 x, AU1 y){return spd_intermediate[x][y];}
	void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value){spd_intermediate[x][y] = value;}

	// Define your reduction function: takes as input the four 2x2 values and returns 1 output value
	AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3)
	{
		const bool brightnessWeighted = false;
		if (brightnessWeighted) {
			float w0 = 1.0 / (1.0 + max(v0.x, max(v0.y, v0.z)));
			float w1 = 1.0 / (1.0 + max(v1.x, max(v1.y, v1.z)));
			float w2 = 1.0 / (1.0 + max(v2.x, max(v2.y, v2.z)));
			float w3 = 1.0 / (1.0 + max(v3.x, max(v3.y, v3.z)));
			return AH4((v0.rgb*w0 + v1.rgb*w1 + v2.rgb*w2 + v3.rgb*w3) / (w0+w1+w2+w3), 1);
		} else {
			return AH4((v0.rgb+v1.rgb+v2.rgb+v3.rgb)*AH1(0.25), 1);
		}
	}

#endif

groupshared AU1 spd_counter;

// Define the atomic counter increase function
// Note this is technically not required if we're limiting the number of mips to 5 or less
void SpdIncreaseAtomicCounter(){InterlockedAdd(AtomicBuffer[0], 1, spd_counter);}
AU1 SpdGetAtomicCounter(){return spd_counter;}

#include "xleres/Foreign/ffx-spd/ffx_spd.h"

[numthreads(256, 1, 1)]
	void main(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
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

