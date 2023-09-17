// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"

RWTexture2D<float4> OutputTexture : register(u1, space0);
Texture2D<float3> InputTexture : register(t3, space0);

// TAP_COUNT must be odd
#if !defined(TAP_COUNT)
	#define TAP_COUNT 11
#endif

#if (TAP_COUNT & 1) != 1
	#error TAP_COUNT must be odd
#endif

#define WING_COUNT ((TAP_COUNT-1)/2)

cbuffer ControlUniforms : register(b5, space0)
{
	bool SRGBConversionOnInput;
	bool SRGBConversionOnOutput;
	float Weight[WING_COUNT+1];
}

#define BLOCK_CENTER 16
#define BLOCK_BORDER WING_COUNT
#define BLOCK_DIMS (BLOCK_CENTER+BLOCK_BORDER+BLOCK_BORDER)

#define F16 min16float			// float16_t
#define F16_3 min16float3		// float16_t3

groupshared F16 Intermediate0[BLOCK_DIMS][BLOCK_DIMS];
groupshared F16 Intermediate1[BLOCK_CENTER][BLOCK_DIMS];

void Gaussian(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint pixelElement)
{
	// We do this on a block-by-block basis in order to maximize our use of groupshared memory
	// one block per thread group
	// Unfortunately we still need separate input/output textures despite the tricks we're doing here
	// More advanced versions of this approach might be able to do all of this in a single dispatch
	// and walk through the texture, updating groupshared memory part by part... that's overkill for
	// the moment, though
	
	int2 srcTextureOffset = groupId.xy * BLOCK_CENTER - int2(BLOCK_BORDER, BLOCK_BORDER);
	uint cx, cy;

	// Initializing BLOCK_DIMS*BLOCK_DIMS (676) pixels
	// using 64 threads
	// -- 10.5625 loads per thread
	uint threadX = groupThreadId.x;
	uint threadY = groupThreadId.y;

	uint2 inputTextureDims;
	InputTexture.GetDimensions(inputTextureDims.x, inputTextureDims.y);

	const uint threadGroupWidth = 8;		// width & height must be the same
	const uint blockDimsMultiple = (BLOCK_DIMS / threadGroupWidth) * threadGroupWidth;

	for (cy=threadY; cy<blockDimsMultiple; cy+=8) {
		for (cx=threadX; cx<blockDimsMultiple; cx+=8) {
			int2 A = srcTextureOffset + int2(cx, cy);
			A = clamp(A, 0.xx, inputTextureDims-1.xx);
			float c = InputTexture[A][pixelElement];
			if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
			Intermediate0[cx][cy] = c;
		}
	}

	// awkward vertical & horizontal stripe to fill in the remainder
	// how much of our cache carefullness is defeated by this?
	uint remainingStripePixels = (BLOCK_DIMS - blockDimsMultiple) * BLOCK_DIMS;
	uint linearThreadGroupIdx = (threadX * threadGroupWidth) + threadY;
	for (uint px=linearThreadGroupIdx; px<remainingStripePixels; px+=threadGroupWidth*threadGroupWidth) {
		cx = blockDimsMultiple + (px / BLOCK_DIMS);
		cy = px % BLOCK_DIMS;

		int2 A = srcTextureOffset + int2(cx, cy);
		A = clamp(A, 0.xx, inputTextureDims-1.xx);
		float c = InputTexture[A][pixelElement];
		if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
		Intermediate0[cx][cy] = c;

		A = srcTextureOffset + int2(cy, cx);
		A = clamp(A, 0.xx, inputTextureDims-1.xx);
		c = InputTexture[A][pixelElement];
		if (SRGBConversionOnInput) c = SRGBToLinear_Formal(c);
		Intermediate0[cy][cx] = c;
	}

	// wait until every thread has finished loads, then begin
	// horizontal part
	GroupMemoryBarrier();

	F16 w[WING_COUNT+1];
	[unroll] for (uint c=0; c<WING_COUNT+1; ++c)
		w[c] = Weight[c];

	for (uint y=threadY; y<BLOCK_DIMS; y+=8) {
		for (cx=0; cx<BLOCK_CENTER; cx+=8) {
			uint x = threadX + cx + BLOCK_BORDER;

			F16 v;
			v = Intermediate0[x  ][y] * w[0];
			[unroll] for (uint c=1; c<WING_COUNT; ++c) {
				v += Intermediate0[x-c][y] * w[c];
				v += Intermediate0[x+c][y] * w[c];
			}

			Intermediate1[x-BLOCK_BORDER][y] = v;
		}
	}

	// wait until every thread has finished loads, then begin
	// vertical part
	GroupMemoryBarrier();

	for (cy=0; cy<BLOCK_CENTER; cy+=8) {
		for (cx=0; cx<BLOCK_CENTER; cx+=8) {
			uint x = threadX + cx;
			uint y = threadY + cy + BLOCK_BORDER;

			F16 v;
			v = Intermediate1[x][y  ] * w[0];
			[unroll] for (uint c=1; c<WING_COUNT; ++c) {
				v += Intermediate1[x][y-c] * w[c];
				v += Intermediate1[x][y+c] * w[c];
			}

			if (SRGBConversionOnOutput)
				v = LinearToSRGB_Formal(v);

			// vertical & horizontal parts done -- just write out
			float4 t = OutputTexture[srcTextureOffset + uint2(x+BLOCK_BORDER, y)];
			t[pixelElement] = v;
			t[3] = 1;
			OutputTexture[srcTextureOffset + uint2(x+BLOCK_BORDER, y)] = t;
		}
	}
}

[numthreads(8, 8, 1)]
	void GaussianRGB(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	Gaussian(groupThreadId, groupId, 0);
	GroupMemoryBarrier();
	Gaussian(groupThreadId, groupId, 1);
	GroupMemoryBarrier();
	Gaussian(groupThreadId, groupId, 2);
}
