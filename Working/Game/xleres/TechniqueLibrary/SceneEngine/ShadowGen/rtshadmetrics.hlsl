// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "xleres/Deferred/resolvecascade.hlsl"
#include "xleres/Deferred/ResolveUtil.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"

struct RTSListNode
{
    uint    next;
    uint	triIndex;
};

Texture2D<uint>	                RTSListsHead;
StructuredBuffer<RTSListNode>   RTSLinkedLists;
ByteAddressBuffer               RTSTriangles;

float4 ps_main(float4 position : SV_Position, float2 texCoord : TEXCOORD0, SystemInputs sys) : SV_Target0
{
    int2 pixelCoords = position.xy;

    int finalCascadeIndex;
    float4 cascadeNormCoords;
    FindCascade_CameraToShadowMethod(
        finalCascadeIndex, cascadeNormCoords,
        texCoord, LoadWorldSpaceDepth(pixelCoords, GetSampleIndex(sys)));
    if (finalCascadeIndex==0) {
        // Draw a heat-map type color based on the number of triangles that
        // need to be tested for shadowing of this pixel

        float3 postDivideCoord = cascadeNormCoords.xyz/cascadeNormCoords.w;

        uint2 dims;
        RTSListsHead.GetDimensions(dims.x, dims.y);
        float2 texCoords = float2(0.5f + 0.5f * postDivideCoord.x, 0.5f - 0.5f * postDivideCoord.y);
        uint2 gridIndex = uint2(texCoords.xy * dims);

        uint i = RTSListsHead[gridIndex];
        int count = 0;
        while (i!=0) {
            ++count;
            i = RTSLinkedLists[i-1].next;
        }

        float A = saturate(count/100.f);
        return float4(lerp(float3(0,0,1), float3(1,0,0), A), 1);
    }

    return 0.0.xxxx;
}
