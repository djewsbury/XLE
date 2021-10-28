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
Texture2D GBufferNormalPrev;
Texture2D<int2> GBufferMotion;

Texture2D<float3> g_temporally_denoised_reflections_history;
Texture2D<float> g_ray_lengths_read;
Texture2D<float3> g_spatially_denoised_reflections_read;
StructuredBuffer<uint> g_tile_meta_data_mask_read;

RWTexture2D<float3> g_temporally_denoised_reflections;
RWStructuredBuffer<uint> g_temporal_variance_mask;

#if SPLIT_CONFIDENCE
    Texture2D<float> g_temporally_denoised_confidence_history;
    RWTexture2D<float> g_temporally_denoised_confidence;
    Texture2D<float> g_spatially_denoised_confidence_read;
#endif

cbuffer ExtendedTransforms
{
    row_major float4x4 ClipToView;      // g_inv_proj
    row_major float4x4 ClipToWorld;     // g_inv_view_proj
    row_major float4x4 WorldToView;     // g_view
    row_major float4x4 ViewToWorld;     // g_inv_view
    row_major float4x4 ViewToProj;      // g_proj
    row_major float4x4 PrevWorldToClip; // g_prev_view_proj
    float2 SSRNegativeReciprocalScreenSize;
};

#define g_temporal_stability_factor 0.66 // 0.975f
#define g_temporal_variance_threshold 0.002f

static const float g_roughness_sigma_min = 0.001f;
static const float g_roughness_sigma_max = 0.01f;

float FFX_DNSR_Reflections_LoadRayLength(int2 pixel_coordinate)
{
    return g_ray_lengths_read.Load(int3(pixel_coordinate, 0));
}

float2 FFX_DNSR_Reflections_LoadMotionVector(int2 pixel_coordinate)
{
    float2 res = GBufferMotion.Load(int3(pixel_coordinate, 0)).xy;
    res *= SSRNegativeReciprocalScreenSize;
    return res;
}

float3 FFX_DNSR_Reflections_LoadNormal(int2 pixel_coordinate)
{
    return DecompressGBufferNormal(GBufferNormal.Load(int3(pixel_coordinate, 0)).xyz);
}

float3 FFX_DNSR_Reflections_LoadNormalHistory(int2 pixel_coordinate)
{
    return DecompressGBufferNormal(GBufferNormalPrev.Load(int3(pixel_coordinate, 0)).xyz);
}

float FFX_DNSR_Reflections_LoadRoughness(int2 pixel_coordinate)
{
    return GBufferNormal.Load(int3(pixel_coordinate, 0)).a;
}

float FFX_DNSR_Reflections_LoadRoughnessHistory(int2 pixel_coordinate)
{
    return GBufferNormalPrev.Load(int3(pixel_coordinate, 0)).a;
}

float3 FFX_DNSR_Reflections_LoadRadianceHistory(int2 pixel_coordinate)
{
    return g_temporally_denoised_reflections_history.Load(int3(pixel_coordinate, 0)).xyz;
}

float FFX_DNSR_Reflections_LoadDepth(int2 pixel_coordinate)
{
    return DownsampleDepths.Load(int3(pixel_coordinate, 0));
}

float3 FFX_DNSR_Reflections_LoadSpatiallyDenoisedReflections(int2 pixel_coordinate)
{
    return g_spatially_denoised_reflections_read.Load(int3(pixel_coordinate, 0)).xyz;
}

uint FFX_DNSR_Reflections_LoadTileMetaDataMask(uint index)
{
    return g_tile_meta_data_mask_read[index];
}

void FFX_DNSR_Reflections_StoreTemporallyDenoisedReflections(int2 pixel_coordinate, float3 value)
{
    g_temporally_denoised_reflections[pixel_coordinate] = value;
}

#if SPLIT_CONFIDENCE
    float FFX_DNSR_Reflections_LoadSpatiallyDenoisedConfidence(int2 pixel_coordinate)
    {
        return g_spatially_denoised_confidence_read.Load(int3(pixel_coordinate, 0));
    }

    float FFX_DNSR_Reflections_LoadConfidenceHistory(int2 pixel_coordinate)
    {
        return g_temporally_denoised_confidence_history.Load(int3(pixel_coordinate, 0));
    }

    void FFX_DNSR_Reflections_StoreTemporallyDenoisedConfidence(int2 pixel_coordinate, float value)
    {
        g_temporally_denoised_confidence[pixel_coordinate] = value;
    }
#endif

void FFX_DNSR_Reflections_StoreTemporalVarianceMask(int index, uint mask)
{
    g_temporal_variance_mask[index] = mask;
}

bool FFX_DNSR_Reflections_IsGlossyReflection(float roughness) { return roughness < 0.5f; }
bool FFX_DNSR_Reflections_IsMirrorReflection(float roughness) { return false; }

float3 FFX_DNSR_Reflections_ScreenSpaceToViewSpace(float3 screen_space_position) 
{
    #if !defined(VULKAN)
        screen_space_position.y = (1 - screen_space_position.y);
    #endif
    screen_space_position.xy = 2 * screen_space_position.xy - 1;
    float4 result = mul(ClipToView, float4(screen_space_position, 1));
    return result.xyz/result.w;
}

float3 FFX_DNSR_Reflections_ViewSpaceToWorldSpace(float4 view_space_coord) 
{
    return mul(ViewToWorld, view_space_coord).xyz;
}

// Transforms origin to uv space
// Mat must be able to transform origin from its current space into clip space.
float3 ProjectPosition(float3 origin, row_major float4x4 mat)
{
    float4 projected = mul(mat, float4(origin, 1));
    projected.xyz /= projected.w;
    projected.xy = 0.5 * projected.xy + 0.5;
    #if !defined(VULKAN)
        projected.y = (1 - projected.y);
    #endif
    return projected.xyz;
}

float3 FFX_DNSR_Reflections_WorldSpaceToScreenSpacePrevious(float3 world_coord) 
{
    return ProjectPosition(world_coord, PrevWorldToClip);
}

#include "xleres/Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_resolve_temporal.h"

[numthreads(8, 8, 1)]
    void ResolveTemporal(int2 dispatch_thread_id : SV_DispatchThreadID, int2 group_thread_id : SV_GroupThreadID)
{
    uint2 image_size;
    g_temporally_denoised_reflections.GetDimensions(image_size.x, image_size.y);
    FFX_DNSR_Reflections_ResolveTemporal(dispatch_thread_id, group_thread_id, image_size, g_temporal_stability_factor, g_temporal_variance_threshold);
    // FFX_DNSR_Reflections_StoreTemporallyDenoisedReflections(dispatch_thread_id.xy, FFX_DNSR_Reflections_LoadSpatiallyDenoisedReflections(dispatch_thread_id.xy));
    // FFX_DNSR_Reflections_StoreTemporallyDenoisedConfidence(dispatch_thread_id.xy, FFX_DNSR_Reflections_LoadSpatiallyDenoisedConfidence(dispatch_thread_id.xy));
}
