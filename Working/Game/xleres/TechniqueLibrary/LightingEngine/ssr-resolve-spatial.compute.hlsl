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

#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"

Texture2D<float> DownsampleDepths;
Texture2D GBufferNormal;

Texture2D<min16float3> g_intersection_result_read;
StructuredBuffer<uint> g_tile_meta_data_mask_read;
RWTexture2D<float3> g_spatially_denoised_reflections;

cbuffer FrameIdBuffer
{
    uint FrameId;
};

static const float g_depth_sigma = 0.02f;

groupshared uint g_shared_0[16][16];
groupshared uint g_shared_1[16][16];
groupshared uint g_shared_2[16][16];
groupshared uint g_shared_3[16][16];
groupshared float g_shared_depth[16][16];

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

min16float3 FFX_DNSR_Reflections_LoadRadianceFromGroupSharedMemory(int2 idx)
{
    uint2 tmp;
    tmp.x = g_shared_0[idx.y][idx.x];
    tmp.y = g_shared_1[idx.y][idx.x];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y)).xyz;
}

min16float3 FFX_DNSR_Reflections_LoadNormalFromGroupSharedMemory(int2 idx)
{
    uint2 tmp;
    tmp.x = g_shared_2[idx.y][idx.x];
    tmp.y = g_shared_3[idx.y][idx.x];
    return min16float4(UnpackFloat16(tmp.x), UnpackFloat16(tmp.y)).xyz;
}

float FFX_DNSR_Reflections_LoadDepthFromGroupSharedMemory(int2 idx)
{
    return g_shared_depth[idx.y][idx.x];
}

void FFX_DNSR_Reflections_StoreInGroupSharedMemory(int2 idx, min16float3 radiance, min16float3 normal, float depth) {
    g_shared_0[idx.y][idx.x] = PackFloat16(radiance.xy);
    g_shared_1[idx.y][idx.x] = PackFloat16(min16float2(radiance.z, 0));
    g_shared_2[idx.y][idx.x] = PackFloat16(normal.xy);
    g_shared_3[idx.y][idx.x] = PackFloat16(min16float2(normal.z, 0));
    g_shared_depth[idx.y][idx.x] = depth;
}

float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate)
{
    return GBufferNormal.Load(int3(pixel_coordinate, 0)).a;
}

min16float3 FFX_DNSR_Reflections_LoadRadianceFP16(int2 pixel_coordinate)
{
    return g_intersection_result_read.Load(int3(pixel_coordinate, 0)).xyz;
}

min16float3 FFX_DNSR_Reflections_LoadNormalFP16(int2 pixel_coordinate)
{
    return (min16float3) DecompressGBufferNormal(GBufferNormal.Load(int3(pixel_coordinate, 0)).xyz);
}

float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate)
{
    return DownsampleDepths.Load(int3(pixel_coordinate, 0));
}

void FFX_DNSR_Reflections_StoreSpatiallyDenoisedReflections(int2 pixel_coordinate, min16float3 value)
{
    g_spatially_denoised_reflections[pixel_coordinate] = value;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index)
{
    return g_tile_meta_data_mask_read[index];
}

bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) { return roughness < 0.5f; }
bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) { return false; }

#include "xleres/Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_resolve_spatial.h"

[numthreads(8, 8, 1)]
    void ResolveSpatial(uint group_index : SV_GroupIndex, uint2 group_id : SV_GroupID)
{
    uint2 screen_dimensions;
    DownsampleDepths.GetDimensions(screen_dimensions.x, screen_dimensions.y);

    uint2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index);
    uint2 dispatch_thread_id = group_id * 8 + group_thread_id;
    FFX_DNSR_Reflections_ResolveSpatial((int2)dispatch_thread_id, (int2)group_thread_id, screen_dimensions);
}