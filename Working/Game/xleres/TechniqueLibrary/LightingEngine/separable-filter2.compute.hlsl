// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Math/MathConstants.hlsl"

Texture2D InputTexture;
RWTexture2D<float3> OutputTexture;

#define BLOCK_CENTER 16
#define BLOCK_BORDER 5
#define BLOCK_DIMS (BLOCK_CENTER+BLOCK_BORDER+BLOCK_BORDER)

// if we can support half-floats, probably a little more ideal here
groupshared float IntermediateR0[BLOCK_DIMS][BLOCK_DIMS];
groupshared float IntermediateG0[BLOCK_DIMS][BLOCK_DIMS];
groupshared float IntermediateB0[BLOCK_DIMS][BLOCK_DIMS];

groupshared float IntermediateR1[BLOCK_CENTER][BLOCK_DIMS];
groupshared float IntermediateG1[BLOCK_CENTER][BLOCK_DIMS];
groupshared float IntermediateB1[BLOCK_CENTER][BLOCK_DIMS];

uint ExtractEvenBits(uint x)
{
	// see https://stackoverflow.com/questions/4909263/how-to-efficiently-de-interleave-bits-inverse-morton
	// extract even bits
	x = x & 0x55555555;
	x = (x | (x >> 1)) & 0x33333333;
	x = (x | (x >> 2)) & 0x0F0F0F0F;
	x = (x | (x >> 4)) & 0x00FF00FF;
	x = (x | (x >> 8)) & 0x0000FFFF;
	return x;
}

float GaussianWeight1D(float offset, float stdDevSq)
{
	// See https://en.wikipedia.org/wiki/Gaussian_blur
	const float twiceStdDevSq = 2.0 * stdDevSq;
	const float C = 1.0 / sqrt(pi * twiceStdDevSq);		// can done later, because it's constant for all weights
	return C * exp(-offset*offset / twiceStdDevSq);
}

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

	for (cy=threadY; cy<24; cy+=8) {
		for (cx=threadX; cx<24; cx+=8) {
			int2 A = srcTextureOffset + int2(cx, cy);
			if (A.x >= 0 && A.x < inputTextureDims.x && A.y >= 0 && A.y < inputTextureDims.y) {
				float3 c = InputTexture[A].rgb;
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
	cx = 24+(linearThreadGroupIdx >> 5);
	cy = linearThreadGroupIdx & 0x1f;
	if (cy < BLOCK_DIMS) {
		int2 A = srcTextureOffset + int2(cx, cy);
		if (A.x >= 0 && A.x < inputTextureDims.x && A.y >= 0 && A.y < inputTextureDims.y) {
			float3 c = InputTexture[A].rgb;
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

	const float stdDevSq = 1.32 * 1.32;
	const float w0 = GaussianWeight1D(0.0, stdDevSq);
	const float w1 = GaussianWeight1D(1.0, stdDevSq);
	const float w2 = GaussianWeight1D(2.0, stdDevSq);
	const float w3 = GaussianWeight1D(3.0, stdDevSq);
	const float w4 = GaussianWeight1D(4.0, stdDevSq);
	const float w5 = GaussianWeight1D(5.0, stdDevSq);

	for (uint y=threadY; y<BLOCK_DIMS; y+=8) {
		for (cx=0; cx<16; cx+=8) {
			uint x = threadX + cx + BLOCK_BORDER;

			float3 v;
			v  = float3(IntermediateR0[x-5][y], IntermediateG0[x-5][y], IntermediateB0[x-5][y]) * w5;
			v += float3(IntermediateR0[x-4][y], IntermediateG0[x-4][y], IntermediateB0[x-4][y]) * w4;
			v += float3(IntermediateR0[x-3][y], IntermediateG0[x-3][y], IntermediateB0[x-3][y]) * w3;
			v += float3(IntermediateR0[x-2][y], IntermediateG0[x-2][y], IntermediateB0[x-2][y]) * w2;
			v += float3(IntermediateR0[x-1][y], IntermediateG0[x-1][y], IntermediateB0[x-1][y]) * w1;
			v += float3(IntermediateR0[x  ][y], IntermediateG0[x  ][y], IntermediateB0[x  ][y]) * w0;
			v += float3(IntermediateR0[x+1][y], IntermediateG0[x+1][y], IntermediateB0[x+1][y]) * w1;
			v += float3(IntermediateR0[x+2][y], IntermediateG0[x+2][y], IntermediateB0[x+2][y]) * w2;
			v += float3(IntermediateR0[x+3][y], IntermediateG0[x+3][y], IntermediateB0[x+3][y]) * w3;
			v += float3(IntermediateR0[x+4][y], IntermediateG0[x+4][y], IntermediateB0[x+4][y]) * w4;
			v += float3(IntermediateR0[x+5][y], IntermediateG0[x+5][y], IntermediateB0[x+5][y]) * w5;

			IntermediateR1[x-BLOCK_BORDER][y] = v.x;
			IntermediateG1[x-BLOCK_BORDER][y] = v.y;
			IntermediateB1[x-BLOCK_BORDER][y] = v.z;
		}
	}

	// wait until every thread has finished loads, then begin
	// vertical part
	GroupMemoryBarrierWithGroupSync();

	for (cy=0; cy<16; cy+=8) {
		for (cx=0; cx<16; cx+=8) {
			uint x = threadX + cx;
			uint y = threadY + cy + BLOCK_BORDER;

			float3 v;
			v  = float3(IntermediateR1[x][y-5], IntermediateG1[x][y-5], IntermediateB1[x][y-5]) * w5;
			v += float3(IntermediateR1[x][y-4], IntermediateG1[x][y-4], IntermediateB1[x][y-4]) * w4;
			v += float3(IntermediateR1[x][y-3], IntermediateG1[x][y-3], IntermediateB1[x][y-3]) * w3;
			v += float3(IntermediateR1[x][y-2], IntermediateG1[x][y-2], IntermediateB1[x][y-2]) * w2;
			v += float3(IntermediateR1[x][y-1], IntermediateG1[x][y-1], IntermediateB1[x][y-1]) * w1;
			v += float3(IntermediateR1[x][y  ], IntermediateG1[x][y  ], IntermediateB1[x][y  ]) * w0;
			v += float3(IntermediateR1[x][y+1], IntermediateG1[x][y+1], IntermediateB1[x][y+1]) * w1;
			v += float3(IntermediateR1[x][y+2], IntermediateG1[x][y+2], IntermediateB1[x][y+2]) * w2;
			v += float3(IntermediateR1[x][y+3], IntermediateG1[x][y+3], IntermediateB1[x][y+3]) * w3;
			v += float3(IntermediateR1[x][y+4], IntermediateG1[x][y+4], IntermediateB1[x][y+4]) * w4;
			v += float3(IntermediateR1[x][y+5], IntermediateG1[x][y+5], IntermediateB1[x][y+5]) * w5;

			// vertical & horizontal parts done -- just write out
			OutputTexture[srcTextureOffset + uint2(x+BLOCK_BORDER, y)] = v;
		}
	}
}

