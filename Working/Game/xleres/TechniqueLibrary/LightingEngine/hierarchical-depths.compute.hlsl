
// This file is very closely based on DepthDownsample.hlsl from the GPUOpen samples; so reproducing the 
// copywrite notice here. We include libraries from AMD, which are included via git submodules
// that contain their licensing statements

/**********************************************************************
Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/

#pragma selector_filtering(push_disable)
#define A_GPU
#define A_HLSL
#include "xleres/Foreign/ffx-spd/ffx_a.h"

//////////////////////////////////////////////////////////////////////////////////////////

Texture2D<float> InputDepths            : register(t0, space1);
RWTexture2D<float> DownsampleDepths[13] : register(u1, space1);
RWBuffer<uint> AtomicBuffer             : register(u2, space1);

groupshared float GroupDepthValues[16][16];
groupshared uint GroupAtomicCounter;

#define DS_FALLBACK

// input/output interface for the ffx_spd library
AF4 SpdLoadSourceImage(ASU2 index)                  { return InputDepths[index].xxxx; }
AF4 SpdLoad(ASU2 index)                             { return DownsampleDepths[6][index].xxxx; }
void SpdStore(ASU2 pixel, AF4 outValue, AU1 index)  { DownsampleDepths[index + 1][pixel] = outValue.x; }
void SpdIncreaseAtomicCounter()                     { InterlockedAdd(AtomicBuffer[0], 1, GroupAtomicCounter); }
AU1 SpdGetAtomicCounter()                           { return GroupAtomicCounter; }
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value)  { GroupDepthValues[x][y] = value.x; }

// For AMD screenspace reflections, the reduction operator must return the closest depth of the for
// so for ReverseZ depth mode, we must use a max operator
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3)      { return max(max(v0, v1), max(v2,v3)); }

AF4 SpdLoadIntermediate(AU1 x, AU1 y) 
{
	float f = GroupDepthValues[x][y];
	return f.xxxx; 
}

#include "xleres/Foreign/ffx-spd/ffx_spd.h"

//////////////////////////////////////////////////////////////////////////////////////////

uint GetThreadgroupCount(uint2 imageSize)
{
	return ((imageSize.x + 63) / 64) * ((imageSize.y + 63) / 64);
}

float GetMipsCount(float2 imageSize)
{
	float max_dim = max(imageSize.x, imageSize.y);
	return 1.0 + floor(log2(max_dim));
}

[numthreads(32, 8, 1)]
	void GenerateDownsampleDepths(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	float2 depth_image_size = 0;
	InputDepths.GetDimensions(depth_image_size.x, depth_image_size.y);

	// Copy most detailed level into the hierarchy and transform it.
	uint2 u_depth_image_size = uint2(depth_image_size);
	for (int i = 0; i < 2; ++i)
		for (int j = 0; j < 8; ++j) {
			uint2 idx = uint2(2 * dispatchThreadId.x + i, 8 * dispatchThreadId.y + j);
			if (idx.x < u_depth_image_size.x && idx.y < u_depth_image_size.y)
				DownsampleDepths[0][idx] = InputDepths[idx];
		}

	float2 image_size = 0;
	DownsampleDepths[0].GetDimensions(image_size.x, image_size.y);
	float mipsCount = GetMipsCount(image_size);
	uint threadgroupCount = GetThreadgroupCount(image_size);

	SpdDownsample(
		AU2(groupId.xy),
		AU1(groupIndex),
		AU1(mipsCount),
		AU1(threadgroupCount));
}

#pragma selector_filtering(pop)
