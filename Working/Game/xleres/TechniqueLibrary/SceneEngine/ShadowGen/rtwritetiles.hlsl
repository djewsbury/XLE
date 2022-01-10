// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Lighting/ShadowProjection.hlsl"
#include "../Vegetation/WindAnim.hlsl"
#include "../Vegetation/InstanceVS.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../Framework/WorkingVertex.hlsl"
#include "../../Math/ProjectionMath.hlsl"

#define OPTIMISED_TRI 1

///////////////////////////////////////////////////////////////////////////////////////////////////

struct RTS_VSOutput
{
    float4 position : POSITION;

    #if VSOUT_HAS_TEXCOORD
        float2 texCoord : TEXCOORD;
    #endif

    #if VSOUT_HAS_WORLD_POSITION
        float3 worldPosition : WORLDPOSITION;
    #endif
};

void vs_writetris(VSIN input, out RTS_VSOutput output)
{
    float3 localPosition	= VSIN_GetLocalPosition(input);

    #if GEO_HAS_INSTANCE_ID
        float3 objectCentreWorld;
        float3 worldNormal;
        float3 worldPosition = InstanceWorldPosition(input, worldNormal, objectCentreWorld);
    #else
        float3 worldPosition = mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;
        float3 objectCentreWorld = float3(SysUniform_GetLocalToWorld()[0][3], SysUniform_GetLocalToWorld()[1][3], SysUniform_GetLocalToWorld()[2][3]);
        float3 worldNormal = LocalToWorldUnitVector(DeriveLocalNormal(input));
    #endif

    #if VSOUT_HAS_TEXCOORD
        output.texCoord = VSIN_GetTexCoord0(input);
    #endif

    #if GEO_HAS_NORMAL || GEO_HAS_TEXTANGENT
        #if (GEO_HAS_NORMAL==0) && GEO_HAS_TEXTANGENT
            worldNormal =  DeriveWorldTangentFrame(input).normal;
        #endif

        worldPosition = PerformWindBending(worldPosition, worldNormal, objectCentreWorld, float3(1,0,0), VSIN_GetColor0(input));
    #endif

    #if SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ARBITRARY || SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_CUBEMAP
        output.position = ShadowProjection_GetOutput(worldPosition, 0);
    #elif SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ORTHOGONAL
        float3 basePosition = mul(OrthoShadowWorldToView, float4(worldPosition, 1));
        float3 cascadePos = AdjustForOrthoCascade(basePosition, 0);
        output.position = float4(cascadePos, 1.f);
    #endif

    #if VSOUT_HAS_WORLD_POSITION
        output.worldPosition = worldPosition.xyz;
    #endif
}

struct RTSTriangle
{
    #if (OPTIMISED_TRI==1)
        float4 a_v0 : A;
        float2 v1 : B;
        float4 param : C;
        float3 depths : D;
    #else
        float4 corners[3];
    #endif
};

[maxvertexcount(1)]
    void gs_writetris(
        triangle RTS_VSOutput input[3],
        inout PointStream<RTSTriangle> outputStream)
{
        // Ideally we want the primitive id to be after frustum culling!
        // But we can't get the size of the stream-output buffer from here
        // So, the only way to do that is to use stream-output to collect all
        // of the triangles first, and then rasterize them in a second step.
        // Well, maybe that wouldn't be so bad.
    if (    TriInFrustum(input[0].position, input[1].position, input[2].position)
        &&  BackfaceSign(input[0].position, input[1].position, input[2].position) > 0.f) {

        #if (OPTIMISED_TRI==1)
            RTSTriangle tri;
            float2 a = input[0].position.xy / input[0].position.w;
            float2 b = input[1].position.xy / input[1].position.w;
            float2 c = input[2].position.xy / input[2].position.w;

            float2 v0 = b - a, v1 = c - a;
            float d00 = dot(v0, v0);
            float d01 = dot(v0, v1);
            float d11 = dot(v1, v1);
            float invDenom = 1.0f / (d00 * d11 - d01 * d01);

            tri.a_v0 = float4(a, v0);
            tri.v1 = v1;
            tri.param = float4(d00, d01, d11, invDenom);

            tri.depths = float3(
                input[0].position.z / input[0].position.w,
                input[1].position.z / input[1].position.w,
                input[2].position.z / input[2].position.w);

            outputStream.Append(tri);
        #else
            RTSTriangle tri;
            tri.corners[0] = input[0].position;
            tri.corners[1] = input[1].position;
            tri.corners[2] = input[2].position;
            outputStream.Append(tri);
        #endif
    }
}

#if (OPTIMISED_TRI==1)
    RTSTriangle vs_passthrough(RTSTriangle input) { return input; }
#else
    void vs_passthrough(float4 position : POSITION, out float4 outPos : SV_Position) { outPos = position; }
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

struct RTS_GSInput
{
    float4 position : POSITION;
};

struct RTS_GSOutput
{
    float4 position : SV_Position;
    nointerpolation uint triIndex : TRIINDEX;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

struct ListNode
{
    uint    next;
    uint	triIndex;
};

RWTexture2D<uint>	          ListsHead    : register(u1);
RWStructuredBuffer<ListNode>  LinkedLists  : register(u2);

#if (OUTPUT_PRIM_ID==1)     // (set if we get the primitive id from the geometry shader)
    #define TRI_INDEX_SEMANTIC PRIMID
#else
    #define TRI_INDEX_SEMANTIC SV_PrimitiveID
#endif

uint ps_main(float4 pos : SV_Position, uint triIndex : TRI_INDEX_SEMANTIC) : SV_Target0
{
        // it would be helpful for ListsHead where our bound render target.
        // But we need to both read and write from it... That isn't possible
        // without using a UAV. But it means that the pixel shader output
        // is going to be discarded.

    uint newNodeId = LinkedLists.IncrementCounter();
    uint oldNodeId;
    InterlockedExchange(ListsHead[uint2(pos.xy)], newNodeId+1, oldNodeId);

    LinkedLists[newNodeId].triIndex = triIndex;
    LinkedLists[newNodeId].next = oldNodeId;

    // discard;    // perhaps we can write out min/max here (instead of just discard)
    return newNodeId;
}
