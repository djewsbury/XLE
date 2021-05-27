// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../resolveutil.hlsl"
#include "../standardlighttypes.hlsl"
#include "../../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../../TechniqueLibrary/LightingEngine/ShadowProjection.hlsl"
#include "../../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../../TechniqueLibrary/Utility/Colour.hlsl"

void color_visualisation(
    float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys : TEXCOORD0,
    out float4 outCascadeColor : SV_Target0)
{
    int2 pixelCoords = position.xy;

    float opacity = 1.f;
    bool A = ((pixelCoords.x + pixelCoords.y)/1)%8==0;
    bool B = ((pixelCoords.x - pixelCoords.y)/1)%8==0;
    if (!(A||B)) { opacity = 0.125f; }

    GBufferValues sample = LoadGBuffer(position.xy, sys);
    ResolvePixelProperties resolvePixel = ResolvePixelProperties_Create(position, viewFrustumVector, sys);
    CascadeAddress cascade = ResolveShadowsCascade(resolvePixel.worldPosition, sample.worldSpaceNormal, texCoord, resolvePixel.worldSpaceDepth);
    if (cascade.cascadeIndex >= 0) {
        float4 cols[6]= {
            ByteColor(196, 230, 230, 0xff),
            ByteColor(255, 128, 128, 0xff),
            ByteColor(128, 255, 128, 0xff),
            ByteColor(128, 128, 255, 0xff),
            ByteColor(255, 255, 128, 0xff),
            ByteColor(128, 255, 255, 0xff)
        };
        outCascadeColor = float4(opacity * cols[min(6, cascade.cascadeIndex)].rgb, opacity);
    } else {
        outCascadeColor = 0.0.xxxx;
    }
}

void detailed_visualisation(
    float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys : TEXCOORD0,
    out uint outCascadeIndex : SV_Target0,
    out float4 outSampleDensity : SV_Target1)
{
    GBufferValues sample = LoadGBuffer(position.xy, sys);
    ResolvePixelProperties resolvePixel = ResolvePixelProperties_Create(position, viewFrustumVector, sys);
    CascadeAddress cascade = ResolveShadowsCascade(resolvePixel.worldPosition, sample.worldSpaceNormal, texCoord, resolvePixel.worldSpaceDepth);
    outCascadeIndex = cascade.cascadeIndex;
    outSampleDensity.x = 2048 * 0.5 * (ddx_fine(cascade.frustumCoordinates.x) + ddy_fine(cascade.frustumCoordinates.x));
    outSampleDensity.y = 2048 * 0.5 * (ddx_fine(cascade.frustumCoordinates.y) + ddy_fine(cascade.frustumCoordinates.y));
    outSampleDensity.z = 16384 * 0.5 * (ddx_fine(cascade.frustumCoordinates.z) + ddy_fine(cascade.frustumCoordinates.z));
    outSampleDensity.w = 1.0f;
}

Texture2D<uint> PrebuildCascadeIndexTexture;

float4 col_vis_pass(float4 position : SV_Position) : SV_Target0
{
    uint cascadeIdx = PrebuildCascadeIndexTexture.Load(uint3(position.xy, 0));
    if (cascadeIdx < 6) {

        int2 pixelCoords = position.xy;
        float opacity = 1.f;
        bool A = ((pixelCoords.x + pixelCoords.y)/1)%8==0;
        bool B = ((pixelCoords.x - pixelCoords.y)/1)%8==0;
        if (!(A||B)) { opacity = 0.125f; }

        float4 cols[6]= {
            ByteColor(196, 230, 230, 0xff),
            ByteColor(255, 128, 128, 0xff),
            ByteColor(128, 255, 128, 0xff),
            ByteColor(128, 128, 255, 0xff),
            ByteColor(255, 255, 128, 0xff),
            ByteColor(128, 255, 255, 0xff)
        };
        return float4(cols[cascadeIdx].xyz, opacity);
    } else {
        return 0.0.xxxx;
    }
}
