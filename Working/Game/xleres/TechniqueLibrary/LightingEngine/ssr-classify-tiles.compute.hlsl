
// This file is very closely based on samples from GPUOpen; so reproducing the 
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

uint PackRayCoords(uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal)
{
    uint ray_x_15bit = ray_coord.x & 0b111111111111111;
    uint ray_y_14bit = ray_coord.y & 0b11111111111111;
    uint copy_horizontal_1bit = copy_horizontal ? 1 : 0;
    uint copy_vertical_1bit = copy_vertical ? 1 : 0;
    uint copy_diagonal_1bit = copy_diagonal ? 1 : 0;

    uint packed = (copy_diagonal_1bit << 31) | (copy_vertical_1bit << 30) | (copy_horizontal_1bit << 29) | (ray_y_14bit << 15) | (ray_x_15bit << 0);
    return packed;
}

bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) { return roughness < 0.5f; }
bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) { return roughness < 0.0001; }

#define g_temporal_variance_guided_tracing_enabled 1
#define g_samples_per_quad 1

StructuredBuffer<uint> g_temporal_variance_mask_read       : register(t0, space1);

RWBuffer<uint> g_ray_list                             : register(u1, space1);
globallycoherent RWBuffer<uint> g_ray_counter         : register(u2, space1);
RWTexture2D<float3> g_intersection_result             : register(u3, space1);
RWStructuredBuffer<uint> g_tile_meta_data_mask        : register(u4, space1);
RWBuffer<uint> g_intersect_args                       : register(u5, space1);

Texture2D<float> DownsampleDepths            : register(t6, space1);


uint FFX_DNSR_Reflections_LoadTemporalVarianceMask(uint index)
{
    return g_temporal_variance_mask_read[index];
}

void FFX_DNSR_Reflections_IncrementRayCounter(uint value, out uint original_value)
{
    InterlockedAdd(g_ray_counter[0], value, original_value);
}

void FFX_DNSR_Reflections_StoreRay(int index, uint2 ray_coord, bool copy_horizontal, bool copy_vertical, bool copy_diagonal)
{
    g_ray_list[index] = PackRayCoords(ray_coord, copy_horizontal, copy_vertical, copy_diagonal); // Store out pixel to trace
}

void FFX_DNSR_Reflections_StoreTileMetaDataMask(uint index, uint mask)
{
    g_tile_meta_data_mask[index] = mask;
}

#include "xleres/Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_classify_tiles.h"

[numthreads(8, 8, 1)]
    void ClassifyTiles(uint2 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    uint2 screen_size;
    // g_roughness.GetDimensions(screen_size.x, screen_size.y);
    screen_size = uint2(1920, 1080);

    uint2 group_thread_id = FFX_DNSR_Reflections_RemapLane8x8(group_index); // Remap lanes to ensure four neighboring lanes are arranged in a quad pattern
    uint2 dispatch_thread_id = group_id * 8 + group_thread_id;

    // float roughness = g_roughness.Load(int3(dispatch_thread_id, 0)).w;
    float roughness = (DownsampleDepths.Load(uint3(dispatch_thread_id.xy, 0)) == 1) ? 1.0f : 0.15f;

    if (WaveActiveCountBits(FFX_DNSR_Reflections_IsGlossyReflection(roughness))) {
        FFX_DNSR_Reflections_ClassifyTiles(dispatch_thread_id, group_thread_id, roughness, screen_size, g_samples_per_quad, g_temporal_variance_guided_tracing_enabled);
    } else {
        if (all(group_thread_id == 0)) {
            uint tile_meta_data_index = FFX_DNSR_Reflections_GetTileMetaDataIndex(WaveReadLaneFirst(dispatch_thread_id), screen_size.x);
            FFX_DNSR_Reflections_StoreTileMetaDataMask(tile_meta_data_index, 0);
        }
    }

    // Clear intersection results as there wont be any ray that overwrites them
    g_intersection_result[dispatch_thread_id] = 0;

    // Extract only the channel containing the roughness to avoid loading all 4 channels in the follow up passes.
    // g_extracted_roughness[dispatch_thread_id] = roughness;
}

[numthreads(1, 1, 1)]
    void PrepareIndirectArgs() 
{
    uint ray_count = g_ray_counter[0];
    
    g_intersect_args[0] = (ray_count + 63) / 64;
    g_intersect_args[1] = 1;
    g_intersect_args[2] = 1;

    g_ray_counter[0] = 0;
    g_ray_counter[1] = ray_count;
}
