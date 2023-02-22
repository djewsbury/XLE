// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Math/MathConstants.hlsl"
#include "../../Utility/Colour.hlsl"

RWTexture2D<float4> OutputTexture : register(u1, space0);
Texture2D<float3> InputTexture : register(t3, space0);

cbuffer ControlUniforms : register(b5, space0)
{
	float Weight0, Weight1, Weight2, Weight3, Weight4, Weight5;
	bool SRGBConversionOnInput;
	bool SRGBConversionOnOutput;
}

#define BLOCK_CENTER 16
#define BLOCK_BORDER 5
#define BLOCK_DIMS (BLOCK_CENTER+BLOCK_BORDER+BLOCK_BORDER)

#define F16 min16float			// float16_t
#define F16_3 min16float3		// float16_t3

groupshared F16 IntermediateR0[BLOCK_DIMS][BLOCK_DIMS];
groupshared F16 IntermediateG0[BLOCK_DIMS][BLOCK_DIMS];
groupshared F16 IntermediateB0[BLOCK_DIMS][BLOCK_DIMS];

groupshared F16 IntermediateR1[BLOCK_CENTER][BLOCK_DIMS];
groupshared F16 IntermediateG1[BLOCK_CENTER][BLOCK_DIMS];
groupshared F16 IntermediateB1[BLOCK_CENTER][BLOCK_DIMS];

[numthreads(8, 8, 1)]
	void Gaussian11RGB(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	// We do this on a block-by-block basis in order to maximize our use of groupshared memory
	// one block per thread group
	// Unfortunately we still need separate input/output textures despite the tricks we're doing here
	// More advanced versions of this approach might be able to do all of this in a single dispatch
	// and walk through the texture, updating groupshared memory part by part... that's overkill for
	// the moment, though
	
	const uint borderPixels = 5;
	int2 srcTextureOffset = groupId.xy * BLOCK_CENTER - int2(BLOCK_BORDER, BLOCK_BORDER);
	uint cx, cy;

	// Initializing BLOCK_DIMS*BLOCK_DIMS (676) pixels
	// using 64 threads
	// -- 10.5625 loads per thread
	uint threadX = groupThreadId.x;
	uint threadY = groupThreadId.y;

	uint2 inputTextureDims;
	InputTexture.GetDimensions(inputTextureDims.x, inputTextureDims.y);

	const uint blockDimsMultiple = (BLOCK_DIMS / 8) * 8;

	for (cy=threadY; cy<blockDimsMultiple; cy+=8) {
		for (cx=threadX; cx<blockDimsMultiple; cx+=8) {
			int2 A = srcTextureOffset + int2(cx, cy);
			if (A.x >= 0 && A.x < inputTextureDims.x && A.y >= 0 && A.y < inputTextureDims.y) {
				float3 c = InputTexture[A].rgb;
				if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
				IntermediateR0[cx][cy] = c.r;
				IntermediateG0[cx][cy] = c.g;
				IntermediateB0[cx][cy] = c.b;
			} else {
				IntermediateR0[cx][cy] = 0;
				IntermediateG0[cx][cy] = 0;
				IntermediateB0[cx][cy] = 0;
			}
		}
	}

	// awkward vertical & horizontal stripe to fill in the remainder
	// how much of our cache carefullness is defeated by this?
	uint linearThreadGroupIdx = (threadX * 8) + threadY;
	cx = blockDimsMultiple+(linearThreadGroupIdx >> 5);
	cy = linearThreadGroupIdx & 0x1f;
	if (cy < BLOCK_DIMS) {
		int2 A = srcTextureOffset + int2(cx, cy);
		if (A.x >= 0 && A.x < inputTextureDims.x && A.y >= 0 && A.y < inputTextureDims.y) {
			float3 c = InputTexture[A].rgb;
			if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
			IntermediateR0[cx][cy] = c.r;
			IntermediateG0[cx][cy] = c.g;
			IntermediateB0[cx][cy] = c.b;
		} else {
			IntermediateR0[cx][cy] = 0;
			IntermediateG0[cx][cy] = 0;
			IntermediateB0[cx][cy] = 0;
		}

		A = srcTextureOffset + int2(cy, cx);
		if (A.x >= 0 && A.x < inputTextureDims.x && A.y >= 0 && A.y < inputTextureDims.y) {
			float3 c = InputTexture[A].rgb;
			if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
			IntermediateR0[cy][cx] = c.r;
			IntermediateG0[cy][cx] = c.g;
			IntermediateB0[cy][cx] = c.b;
		} else {
			IntermediateR0[cy][cx] = 0;
			IntermediateG0[cy][cx] = 0;
			IntermediateB0[cy][cx] = 0;
		}
	}

	// wait until every thread has finished loads, then begin
	// horizontal part
	GroupMemoryBarrierWithGroupSync();

	const F16 w0 = Weight0;
	const F16 w1 = Weight1;
	const F16 w2 = Weight2;
	const F16 w3 = Weight3;
	const F16 w4 = Weight4;
	const F16 w5 = Weight5;

	for (uint y=threadY; y<BLOCK_DIMS; y+=8) {
		for (cx=0; cx<BLOCK_CENTER; cx+=8) {
			uint x = threadX + cx + BLOCK_BORDER;

			F16_3 v;
			v  = F16_3(IntermediateR0[x-5][y], IntermediateG0[x-5][y], IntermediateB0[x-5][y]) * w5;
			v += F16_3(IntermediateR0[x-4][y], IntermediateG0[x-4][y], IntermediateB0[x-4][y]) * w4;
			v += F16_3(IntermediateR0[x-3][y], IntermediateG0[x-3][y], IntermediateB0[x-3][y]) * w3;
			v += F16_3(IntermediateR0[x-2][y], IntermediateG0[x-2][y], IntermediateB0[x-2][y]) * w2;
			v += F16_3(IntermediateR0[x-1][y], IntermediateG0[x-1][y], IntermediateB0[x-1][y]) * w1;
			v += F16_3(IntermediateR0[x  ][y], IntermediateG0[x  ][y], IntermediateB0[x  ][y]) * w0;
			v += F16_3(IntermediateR0[x+1][y], IntermediateG0[x+1][y], IntermediateB0[x+1][y]) * w1;
			v += F16_3(IntermediateR0[x+2][y], IntermediateG0[x+2][y], IntermediateB0[x+2][y]) * w2;
			v += F16_3(IntermediateR0[x+3][y], IntermediateG0[x+3][y], IntermediateB0[x+3][y]) * w3;
			v += F16_3(IntermediateR0[x+4][y], IntermediateG0[x+4][y], IntermediateB0[x+4][y]) * w4;
			v += F16_3(IntermediateR0[x+5][y], IntermediateG0[x+5][y], IntermediateB0[x+5][y]) * w5;

			IntermediateR1[x-BLOCK_BORDER][y] = v.x;
			IntermediateG1[x-BLOCK_BORDER][y] = v.y;
			IntermediateB1[x-BLOCK_BORDER][y] = v.z;
		}
	}

	// wait until every thread has finished loads, then begin
	// vertical part
	GroupMemoryBarrierWithGroupSync();

	for (cy=0; cy<BLOCK_CENTER; cy+=8) {
		for (cx=0; cx<BLOCK_CENTER; cx+=8) {
			uint x = threadX + cx;
			uint y = threadY + cy + BLOCK_BORDER;

			F16_3 v;
			v  = F16_3(IntermediateR1[x][y-5], IntermediateG1[x][y-5], IntermediateB1[x][y-5]) * w5;
			v += F16_3(IntermediateR1[x][y-4], IntermediateG1[x][y-4], IntermediateB1[x][y-4]) * w4;
			v += F16_3(IntermediateR1[x][y-3], IntermediateG1[x][y-3], IntermediateB1[x][y-3]) * w3;
			v += F16_3(IntermediateR1[x][y-2], IntermediateG1[x][y-2], IntermediateB1[x][y-2]) * w2;
			v += F16_3(IntermediateR1[x][y-1], IntermediateG1[x][y-1], IntermediateB1[x][y-1]) * w1;
			v += F16_3(IntermediateR1[x][y  ], IntermediateG1[x][y  ], IntermediateB1[x][y  ]) * w0;
			v += F16_3(IntermediateR1[x][y+1], IntermediateG1[x][y+1], IntermediateB1[x][y+1]) * w1;
			v += F16_3(IntermediateR1[x][y+2], IntermediateG1[x][y+2], IntermediateB1[x][y+2]) * w2;
			v += F16_3(IntermediateR1[x][y+3], IntermediateG1[x][y+3], IntermediateB1[x][y+3]) * w3;
			v += F16_3(IntermediateR1[x][y+4], IntermediateG1[x][y+4], IntermediateB1[x][y+4]) * w4;
			v += F16_3(IntermediateR1[x][y+5], IntermediateG1[x][y+5], IntermediateB1[x][y+5]) * w5;

			float4 output = float4(v, 1);
			if (SRGBConversionOnOutput)
				output.rgb = LinearToSRGB_Formal(output.rgb);

			// vertical & horizontal parts done -- just write out
			OutputTexture[srcTextureOffset + uint2(x+BLOCK_BORDER, y)] = output;
		}
	}
}

