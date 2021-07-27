// Parts of this file is based on samples from GPUOpen; so reproducing the 
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

RWTexture2D<float3> g_denoised_reflections                	: register(u0, space1); 
Texture2D<float3> g_temporally_denoised_reflections_read	: register(t1, space1); 
StructuredBuffer<uint> g_tile_meta_data_mask_read         	: register(t2, space1);

Texture2D<float> DownsampleDepths            : register(t3, space1);

static const float g_roughness_sigma_min = 0.001f;
static const float g_roughness_sigma_max = 0.01f;

groupshared uint g_shared_0[12][12];
groupshared uint g_shared_1[12][12];

uint PackFloat16(min16float2 v)
{
    uint2 p = f32tof16(float2(v));
    return p.x | (p.y << 16);
}

min16float2 UnpackFloat16(uint a)
{
    float2 tmp = f16tof32(
        uint2(a & 0xFFFF, a >> 16));
    return min16float2(tmp);
}

void FFX_DNSR_Reflections_LoadFromGroupSharedMemory(int2 idx, out min16float3 radiance, out min16float roughness)
{
	uint2 tmp;
	tmp.x = g_shared_0[idx.x][idx.y];
	tmp.y = g_shared_1[idx.x][idx.y];

	min16float4 min16tmp = min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y));
	radiance = min16tmp.xyz;
	roughness = min16tmp.w;
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 idx, min16float3 radiance, min16float roughness)
{
	min16float4 tmp = min16float4(radiance, roughness);
	g_shared_0[idx.x][idx.y] = PackFloat16(tmp.xy);
	g_shared_1[idx.x][idx.y] = PackFloat16(tmp.zw);
}

min16float3 FFX_DNSR_Reflections_LoadRadianceFP16(int2 pixel_coordinate)
{
	return g_temporally_denoised_reflections_read.Load(int3(pixel_coordinate, 0)).xyz;
}

min16float FFX_DNSR_Reflections_LoadRoughnessFP16(int2 pixel_coordinate)
{
	return (min16float) ((DownsampleDepths.Load(uint3(pixel_coordinate.xy, 0)) == 1) ? 1.0f : 0.15f);
}

void FFX_DNSR_Reflections_StoreDenoisedReflectionResult(int2 pixel_coordinate, min16float3 value)
{
	g_denoised_reflections[pixel_coordinate] = value;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index)
{
	return g_tile_meta_data_mask_read[index];
}

bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) { return roughness < 0.5f; }
bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) { return roughness < 0.0001; }

#include "xleres/Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_blur.h"

[numthreads(8, 8, 1)]
    void ReflectionsBlur(int2 dispatch_thread_id : SV_DispatchThreadID, int2 group_thread_id : SV_GroupThreadID) 
{
	uint2 screen_dimensions;
	g_temporally_denoised_reflections_read.GetDimensions(screen_dimensions.x, screen_dimensions.y);

	// The blurring plus here seems redundant, unless we're also upsampling at this point
	// the blurring should be coming from the distribution of rays in the "intersect" shader; and 
	// we then to a very similar filter in "resolve-spatial". This filter is also not super cheap; so
	// we might actually be better off with just more rays earlier or more samples in "resolve-spatial"
	FFX_DNSR_Reflections_Blur(dispatch_thread_id, group_thread_id, screen_dimensions);
	// g_denoised_reflections[dispatch_thread_id] = g_temporally_denoised_reflections_read[dispatch_thread_id];
}
