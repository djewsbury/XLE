// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)
//
// Parts of this file based on implementations of Capsaicin from AMD
// https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin
// licensed under MIT license https://github.com/GPUOpen-LibrariesAndSDKs/Capsaicin/blob/master/LICENSE.txt
//
// Modified for XLE shader interface & 
//

#include "../Math/TextureAlgorithm.hlsl"

#define RADIUS      1
#define GROUP_SIZE  16
#define TILE_DIM    (2 * RADIUS + GROUP_SIZE)

#if !defined(PLAYDEAD_NEIGHBOURHOOD_SEARCH)
    #define PLAYDEAD_NEIGHBOURHOOD_SEARCH 1
#endif

#if !defined(CATMULL_ROM_SAMPLING)
    #define CATMULL_ROM_SAMPLING 1
#endif

Texture2D<int2> GBufferMotion;
Texture2D<float> Depth;
RWTexture2D<float3> Output;
Texture2D<float3> OutputPrev;
Texture2D<float3> ColorHDR;

cbuffer ControlUniforms
{
    uint2 BufferDimensions;
    bool HistoryValid;
    float BlendingAlpha;
}

SamplerState BilinearClamp;
SamplerState PointClamp;

groupshared float3 Tile[TILE_DIM * TILE_DIM];

// https://www.gdcvault.com/play/1022970/Temporal-Reprojection-Anti-Aliasing-in
int2 GetClosestMotion(in int2 baseCoord, out bool is_sky_pixel)
{
    int2 motion = 0;
    float closestDepth = 0.0f;

    // Explore the local 3x3 neighbourhood for the pixel closes to the camera, and use the
    // motion vector from that, and use that instead of our center motion vector
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 coord = baseCoord + int2(x, y);
            float depth = Depth.Load(uint3(coord, 0));
            if (depth > closestDepth) {     // assuming inverted depth buffer
                motion = GBufferMotion.Load(uint3(coord, 0)).xy;
                closestDepth = depth;
            }
        }
    }

    is_sky_pixel = (closestDepth == 0.0f);
    return motion;
}

int2 SampleMotion(in int2 baseCoord, out bool is_sky_pixel)
{
    #if PLAYDEAD_NEIGHBOURHOOD_SEARCH
        return GetClosestMotion(baseCoord, is_sky_pixel);
    #else
        return GBufferMotion.Load(uint3(baseCoord, 0)).xy;
        is_sky_pixel = Depth.Load(uint3(baseCoord, 0)) > 0;
    #endif
}

float3 SampleHistory(Texture2D<float3> tex, SamplerState bilinearSampler, in float2 uv, in float2 texelSize)
{
    #if CATMULL_ROM_SAMPLING
        return SampleCatmullRomFloat3(tex, bilinearSampler, uv, texelSize);
    #else
        return tex.SampleLevel(bilinearSampler, uv, 0);
    #endif
}

float3 Tap(in float2 pos)
{
    return Tile[int(pos.x) + TILE_DIM * int(pos.y)];
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void ResolveTemporal(in uint2 globalID : SV_DispatchThreadID, in uint2 localID : SV_GroupThreadID, in uint localIndex : SV_GroupIndex, in uint2 groupID : SV_GroupID)
{
    uint2 bufferDimensions = BufferDimensions;

    float2 texelSize = 1.0f / float2(bufferDimensions);
    float2 uv         = (globalID + 0.5f) * texelSize;
    float2 tile_pos   = localID + RADIUS + 0.5f;

    // Populate LDS tile
    if (localIndex < TILE_DIM * TILE_DIM / 4) {
        int2 anchor = groupID.xy * GROUP_SIZE - RADIUS;

        int2 coord1 = anchor + int2(localIndex % TILE_DIM, localIndex / TILE_DIM);
        int2 coord2 = anchor + int2((localIndex + TILE_DIM * TILE_DIM / 4) % TILE_DIM, (localIndex + TILE_DIM * TILE_DIM / 4) / TILE_DIM);
        int2 coord3 = anchor + int2((localIndex + TILE_DIM * TILE_DIM / 2) % TILE_DIM, (localIndex + TILE_DIM * TILE_DIM / 2) / TILE_DIM);
        int2 coord4 = anchor + int2((localIndex + TILE_DIM * TILE_DIM * 3 / 4) % TILE_DIM, (localIndex + TILE_DIM * TILE_DIM * 3 / 4) / TILE_DIM);

        // AMD uses uvs and SampleLevel() with a point sampler instead of Load() a lot here... is there really some benefit?
        // float2 uv1 = (coord1 + 0.5f) * texelSize;
        // float2 uv2 = (coord2 + 0.5f) * texelSize;
        // float2 uv3 = (coord3 + 0.5f) * texelSize;
        // float2 uv4 = (coord4 + 0.5f) * texelSize;

        float3 color0 = ColorHDR.Load(uint3(coord1, 0)).rgb;
        float3 color1 = ColorHDR.Load(uint3(coord2, 0)).rgb;
        float3 color2 = ColorHDR.Load(uint3(coord3, 0)).rgb;
        float3 color3 = ColorHDR.Load(uint3(coord4, 0)).rgb;

        Tile[localIndex]                               = color0;
        Tile[localIndex + TILE_DIM * TILE_DIM / 4]     = color1;
        Tile[localIndex + TILE_DIM * TILE_DIM / 2]     = color2;
        Tile[localIndex + TILE_DIM * TILE_DIM * 3 / 4] = color3;
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(globalID >= bufferDimensions))
        return; // out of bounds

    // Calculate the temporal clipping boundary based on the local spatial neighbourhood
    float  wsum  = 0.0f;
    float3 vsum  = float3(0.0f, 0.0f, 0.0f);
    float3 vsum2 = float3(0.0f, 0.0f, 0.0f);

    for (float y = -RADIUS; y <= RADIUS; ++y) {
        for (float x = -RADIUS; x <= RADIUS; ++x) {
            float3 neigh = Tap(tile_pos + float2(x, y));
            float w = exp(-3.0f * (x * x + y * y) / ((RADIUS + 1.0f) * (RADIUS + 1.0f)));

            vsum2 += neigh * neigh * w;
            vsum  += neigh * w;
            wsum  += w;
        }
    }

    // Calculate mean and standard deviation
    float3 ex  = vsum / wsum;
    float3 ex2 = vsum2 / wsum;
    float3 dev = sqrt(max(ex2 - ex * ex, 0.0f));

    // todo -- incorporate our already calculated history confidence value?
    bool is_sky_pixel;
    int2 motion = SampleMotion(globalID, is_sky_pixel);
    float2 motion_uv = float2(motion) * texelSize;
    float box_size = lerp(0.5f, 2.5f, is_sky_pixel ? 0.0f : smoothstep(0.02f, 0.0f, length(motion_uv)));

    // Reproject and clamp to bounding box
    float3 nmin = ex - dev * box_size;
    float3 nmax = ex + dev * box_size;

    float3 today             = Tap(tile_pos);
    float3 yesterday         = HistoryValid ? SampleHistory(OutputPrev, BilinearClamp, uv + motion_uv, texelSize) : today;
    float3 clamped_yesterday = clamp(yesterday, nmin, nmax);

    float3 result            = lerp(clamped_yesterday, today, BlendingAlpha);      // (AMD use 1.0f / 16.0f, an equivalent of a "N" value of 31)

    Output[globalID] = result;
}

#if 0 ////////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)]
void ResolvePassthru(in uint2 did : SV_DispatchThreadID)
{
    float3 color = g_DirectLightingBuffer.Load(int3(did, 0)).xyz;
    color += g_GlobalIlluminationBuffer.Load(int3(did, 0)).xyz;

    g_ColorBuffer[did] = float4(color, 1.0f);
}

float3 ApplySharpening(in float3 center, in float3 top, in float3 left, in float3 right, in float3 bottom)
{
    float sharpen_amount = 0.25f;

    float accum  = 0.0f;
    float weight = 0.0f;
    float result = sqrt(luminance(center));

    {
        float n0 = sqrt(luminance(top));
        float n1 = sqrt(luminance(bottom));
        float n2 = sqrt(luminance(left));
        float n3 = sqrt(luminance(right));

        float w0 = max(1.0f - 6.0f * (abs(result - n0) + abs(result - n1)), 0.0f);
        float w1 = max(1.0f - 6.0f * (abs(result - n2) + abs(result - n3)), 0.0f);

        w0 = min(1.25f * sharpen_amount * w0, w0);
        w1 = min(1.25f * sharpen_amount * w1, w1);

        accum += n0 * w0;
        accum += n1 * w0;
        accum += n2 * w1;
        accum += n3 * w1;

        weight += 2.0f * w0;
        weight += 2.0f * w1;
    }

    result = max(result * (weight + 1.0f) - accum, 0.0f);
    result = squared(result);

    return min(center * result / max(luminance(center), 1e-5f), 1.0f);
}

[numthreads(8, 8, 1)]
void UpdateHistory(in uint2 did : SV_DispatchThreadID)
{
    if (any(did >= g_BufferDimensions))
    {
        return; // out of bounds
    }

    float2 texelSize = 1.0f / g_BufferDimensions;
    float2 uv         = (did + 0.5f) * texelSize;

    float3 top        = g_HistoryBuffer.SampleLevel(g_NearestSampler, uv + float2( 0.0f,          texelSize.y), 0.0f).xyz;
    float3 left       = g_HistoryBuffer.SampleLevel(g_NearestSampler, uv + float2(-texelSize.x,  0.0f        ), 0.0f).xyz;
    float3 right      = g_HistoryBuffer.SampleLevel(g_NearestSampler, uv + float2( texelSize.x,  0.0f        ), 0.0f).xyz;
    float3 bottom     = g_HistoryBuffer.SampleLevel(g_NearestSampler, uv + float2( 0.0f,         -texelSize.y), 0.0f).xyz;

    float3 center = g_HistoryBuffer.Load(int3(did, 0)).xyz;
    float3 color  = ApplySharpening(center, top, left, right, bottom);

    g_ColorBuffer[did] = float4(color, 1.0f);
    g_OutputBuffer[did] = float4(center, 1.0f);
}

#endif ////////////////////////////////////////////////////////////////////////////////////////////////////