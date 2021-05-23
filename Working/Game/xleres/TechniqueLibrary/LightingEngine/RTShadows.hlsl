// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_RT_SHADOWS_H)
#define RESOLVE_RT_SHADOWS_H

#define OPTIMISED_TRI 1

struct RTSListNode
{
    uint    next;
    uint	triIndex;
};

struct RTSTriangle
{
    #if (OPTIMISED_TRI==1)
        float2 a, v0, v1;
        float d00, d01, d11, invDenom;
        float3 depths;
    #else
        float4 corners[3];
    #endif
};

Texture2D<uint>	                RTSListsHead;
StructuredBuffer<RTSListNode>   RTSLinkedLists;
ByteAddressBuffer               RTSTriangles;

RTSTriangle GetTriangle(uint index)
{
        // Our "RTSTriangles" buffer is written by a
        // stream output shader (so it is really a vertex buffer)
        // Because it is a vertex buffer, we can't bind it as
        // a structured buffer -- it can only be a byte address buffer.
        // As a result, we need a bit of decompression work...
    #if (OPTIMISED_TRI==1)
        uint base = index * 52;
        RTSTriangle result;
        result.a.x  = asfloat(RTSTriangles.Load(base+ 0));
        result.a.y  = asfloat(RTSTriangles.Load(base+ 4));
        result.v0.x = asfloat(RTSTriangles.Load(base+ 8));
        result.v0.y = asfloat(RTSTriangles.Load(base+12));
        result.v1.x = asfloat(RTSTriangles.Load(base+16));
        result.v1.y = asfloat(RTSTriangles.Load(base+20));
        result.d00  = asfloat(RTSTriangles.Load(base+24));
        result.d01  = asfloat(RTSTriangles.Load(base+28));
        result.d11  = asfloat(RTSTriangles.Load(base+32));
        result.invDenom = asfloat(RTSTriangles.Load(base+36));
        result.depths.x = asfloat(RTSTriangles.Load(base+40));
        result.depths.y = asfloat(RTSTriangles.Load(base+44));
        result.depths.z = asfloat(RTSTriangles.Load(base+48));
    #else
        uint base = index * 4 * 3 * 4;
        RTSTriangle result;
        result.corners[0].x = asfloat(RTSTriangles.Load(base+ 0));
        result.corners[0].y = asfloat(RTSTriangles.Load(base+ 4));
        result.corners[0].z = asfloat(RTSTriangles.Load(base+ 8));
        result.corners[0].w = asfloat(RTSTriangles.Load(base+12));
        result.corners[1].x = asfloat(RTSTriangles.Load(base+16));
        result.corners[1].y = asfloat(RTSTriangles.Load(base+20));
        result.corners[1].z = asfloat(RTSTriangles.Load(base+24));
        result.corners[1].w = asfloat(RTSTriangles.Load(base+28));
        result.corners[2].x = asfloat(RTSTriangles.Load(base+32));
        result.corners[2].y = asfloat(RTSTriangles.Load(base+36));
        result.corners[2].z = asfloat(RTSTriangles.Load(base+40));
        result.corners[2].w = asfloat(RTSTriangles.Load(base+44));
    #endif
    return result;
}

float DirSign2D(float2 p1, float2 p2, float2 p3)
{
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

bool IsPointInTri2D(float2 pt, float2 a, float2 b, float2 c)
{
    bool t0 = DirSign2D(pt, a, b) < 0.0f;
    bool t1 = DirSign2D(pt, b, c) < 0.0f;
    bool t2 = DirSign2D(pt, c, a) < 0.0f;
    return ((t0 == t1) && (t1 == t2));
}

float3 Barycentric2D(float2 pt, float2 a, float2 b, float2 c)
{
        // precalculate {
    float2 v0 = b - a, v1 = c - a;
    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float invDenom = 1.0 / (d00 * d11 - d01 * d01);
        // }

    float2 v2 = pt - a;
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);

    float3 result;
    result.x = (d11 * d20 - d01 * d21) * invDenom;
    result.y = (d00 * d21 - d01 * d20) * invDenom;
    result.z = 1.0f - result.x - result.y;
    return result;
}

float3 Barycentric2D(float2 pt, RTSTriangle tri)
{
    float2 v2 = pt - tri.a;
    float d20 = dot(v2, tri.v0);
    float d21 = dot(v2, tri.v1);

    float3 result;
    result.x = (tri.d11 * d20 - tri.d01 * d21) * tri.invDenom;
    result.y = (tri.d00 * d21 - tri.d01 * d20) * tri.invDenom;
    result.z = 1.0f - result.x - result.y;
    return result;
}

bool IsShadowedByTriangle(float3 postDivideCoord, uint triIndex)
{
        // note -- we could maybe do a rejection with IsPointInTri2D
        //          before calculating the barycentric coords... that
        //          rejection maybe slightly faster... But would still
        //          have to do the full barycentric calculation anyway.
        //          It's hard to guess what would be the most efficient.

    #if (OPTIMISED_TRI==1)
        RTSTriangle tri = GetTriangle(triIndex);
        float3 bary = Barycentric2D(postDivideCoord.xy, tri);
        bool baryTest = max(max(1.f-bary.x, 1.f-bary.y), 1.f-bary.z) <= 1.f;
    #else
        RTSTriangle tri = GetTriangle(triIndex);
        float2 A = tri.corners[0].xy / tri.corners[0].w;
        float2 B = tri.corners[1].xy / tri.corners[1].w;
        float2 C = tri.corners[2].xy / tri.corners[2].w;
        float3 bary = Barycentric2D(postDivideCoord.xy, A, B, C);
        bool baryTest = max(max(1.f-bary.x, 1.f-bary.y), 1.f-bary.z) <= 1.f;
    #endif

    if (baryTest) {
        #if (OPTIMISED_TRI==1)
            float d = dot(bary.zxy, tri.depths);
        #else
            float d = dot(bary.zxy,
                float3(
                    tri.corners[0].z/tri.corners[0].w,
                    tri.corners[1].z/tri.corners[1].w,
                    tri.corners[2].z/tri.corners[2].w));
        #endif
        return d < (postDivideCoord.z - 2e-5f);
    }

    return false;
}

float SampleRTShadows(float3 postDivideCoord, int2 randomizerValue)
{
    uint2 dims;
    RTSListsHead.GetDimensions(dims.x, dims.y);
    float2 texCoords = float2(0.5f + 0.5f * postDivideCoord.x, 0.5f - 0.5f * postDivideCoord.y);
    uint2 gridIndex = uint2(texCoords.xy * dims);

        // todo -- check min/max here

    uint i = RTSListsHead[gridIndex];
    while (i!=0) {
        if (IsShadowedByTriangle(postDivideCoord, RTSLinkedLists[i-1].triIndex))
            return 0.f;
        i = RTSLinkedLists[i-1].next;
    }

    return 1.f;
}

#endif
