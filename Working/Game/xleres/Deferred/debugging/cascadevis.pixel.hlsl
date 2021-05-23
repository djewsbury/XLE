// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../resolveutil.hlsl"
#include "../../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../../TechniqueLibrary/LightingEngine/ShadowProjection.hlsl"
#include "../../TechniqueLibrary/Utility/Colour.hlsl"
#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"

float4 main(
    float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys : TEXCOORD0) : SV_Target0
{
    int2 pixelCoords = position.xy;

    float opacity = 1.f;
    bool A = ((pixelCoords.x + pixelCoords.y)/1)%8==0;
    bool B = ((pixelCoords.x - pixelCoords.y)/1)%8==0;
    if (!(A||B)) { opacity = 0.125f; }

    bool enableNearCascade = false;
    #if SHADOW_ENABLE_NEAR_CASCADE != 0
        enableNearCascade = true;
    #endif

    CascadeAddress cascade;
    const bool resolveByWorldPosition = false;
    if (resolveByWorldPosition) {
        float3 worldPosition = CalculateWorldPosition(pixelCoords, GetSampleIndex(sys), viewFrustumVector);
        cascade = ResolveCascade_FromWorldPosition(worldPosition);
    } else {
        cascade = ResolveCascade_CameraToShadowMethod(
            texCoord, GetWorldSpaceDepth(pixelCoords, GetSampleIndex(sys)));
    }

    if (cascade.cascadeIndex >= 0) {
        float4 cols[6]= {
            ByteColor(196, 230, 230, 0xff),
            ByteColor(255, 128, 128, 0xff),
            ByteColor(128, 255, 128, 0xff),
            ByteColor(128, 128, 255, 0xff),
            ByteColor(255, 255, 128, 0xff),
            ByteColor(128, 255, 255, 0xff)
        };
        return float4(opacity * cols[min(6, cascade.cascadeIndex)].rgb, opacity);
    }

    return 0.0.xxxx;
}
