// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VSOUT_HAS_TEXCOORD 1
#define VSOUT_HAS_TANGENT_FRAME 1
#define VSOUT_HAS_NORMAL 1
#define GEO_HAS_TEXCOORD 1
#define VSINPUT_EXTRA uint spriteIndex : SPRITEINDEX;
#define VSOUTPUT_EXTRA uint spriteIndex : SPRITEINDEX;

#include "../Lighting/LightingAlgorithm.hlsl"  // for CalculateMipmapLevel
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Framework/gbuffer.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../Lighting/resolvefog.hlsl"

Texture2D ImposterAltas[2] : register(t0);

struct VSSprite
{
    float3 position : POSITION0;
    float3 xAxis : XAXIS;
    float3 yAxis : YAXIS;
    float2 size : SIZE;
    uint spriteIndex : SPRITEINDEX;
};

VSSprite vs_main(VSSprite input)
{
    return input;
}

[maxvertexcount(4)]
    void gs_main(point VSSprite input[1], inout TriangleStream<VSOUT> outputStream)
{
    VSOUT output;
    #if VSOUT_HAS_FOG_COLOR == 1
        output.fogColor = ResolveOutputFogColor(input[0].position.xyz, SysUniform_GetWorldSpaceView().xyz);
    #endif

    output.spriteIndex = input[0].spriteIndex;

    float2 texCoord[4] =
    {
        float2(0.f, 0.f),
        float2(0.f, 1.f),
        float2(1.f, 0.f),
        float2(1.f, 1.f)
    };

    float3 tangent = input[0].xAxis;
    float3 bitangent = input[0].yAxis;
    float3 normal = NormalFromTangents(tangent, bitangent, -1.f);    // (assuming orthogonal tangent & bitangent)

    #if VSOUT_HAS_TANGENT_FRAME
        output.tangent = tangent;
        output.bitangent = bitangent;
    #endif

    #if VSOUT_HAS_NORMAL
        output.normal = normal;
    #endif

    float3 xAxis = input[0].xAxis * input[0].size.x;
    float3 yAxis = input[0].yAxis * -input[0].size.y;
    for (uint c=0; c<4; ++c) {
        float2 o = texCoord[c] * 2.f - 1.0.xx;
        float3 localPosition = input[0].position + o.x * xAxis + o.y * yAxis;
        float3 worldPosition = localPosition; // mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;

        #if VSOUT_HAS_TEXCOORD
            output.texCoord = texCoord[c];
        #endif

        output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

        #if VSOUT_HAS_LOCAL_VIEW_VECTOR
            output.localViewVector = SysUniform_GetLocalSpaceView().xyz - localPosition.xyz;
        #endif

        #if VSOUT_HAS_WORLD_VIEW_VECTOR
            output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
        #endif

        #if VSOUT_HAS_WORLD_POSITION
            output.worldPosition = worldPosition.xyz;
        #endif

        outputStream.Append(output);
    }
}

void ps_depthonly(float4 pos : SV_Position) {}

#if !(MAT_ALPHA_TEST==1) && (VULKAN!=1)
    [earlydepthstencil]
#endif
float4 main(VSOUT geo) : SV_Target0
{
    return 1.0.xxxx;
}

static const uint MipMapCount = 5;
cbuffer SpriteTable
{
    uint4 SpriteCoords[2048];
}

float4 LoadImposterAltas(uint atlasIndex, uint4 coords, float2 tc)
{
    float2 ftc;
    ftc.x = lerp((float)coords[0], (float)coords[2], tc.x);
    ftc.y = lerp((float)coords[1], (float)coords[3], tc.y);

    // return ImposterAltas[atlasIndex].Load(uint3(ftc, 0));

        // Custom implemented bilinear -- so we can clamp against the
        // limited window
        // Actually, just using a point filter seems to work fine
        // most of the time -- mipmapping should keep us close to 1:1 ratio
        // (because the sprites are roughly aligned to the screen)
    uint2 A = (uint2)floor(ftc);
    uint2 B = uint2(min(A.x+1, coords[2]-1), min(A.y+1, coords[3]-1));
    float2 w = ftc.xy - A.xy;

        // we can use an alpha weighted blend here to counteract
        // blending to uninitialized surrounding pixels. We can also
        // pre-burn this weighting into the texture by blurring out
        // to surrounding pixels.
        //
        // Note that we weight by the complement of alpha (1.f-alpha)
        // because of the way we write alpha to the imposter texture
    const bool doAlphaWeight = true;
    if (doAlphaWeight) {
        float4 s0 = ImposterAltas[atlasIndex].Load(uint3(A.x, A.y, 0));
        float4 s1 = ImposterAltas[atlasIndex].Load(uint3(B.x, A.y, 0));
        float4 s2 = ImposterAltas[atlasIndex].Load(uint3(A.x, B.y, 0));
        float4 s3 = ImposterAltas[atlasIndex].Load(uint3(B.x, B.y, 0));
        float4 fw = float4(
            (1.0f - w.x) * (1.0f - w.y) * (1.f-s0.a),
            (       w.x) * (1.0f - w.y) * (1.f-s1.a),
            (1.0f - w.x) * (       w.y) * (1.f-s2.a),
            (       w.x) * (       w.y) * (1.f-s3.a));
        float4 c = fw.x * s0 + fw.y * s1 + fw.z * s2 + fw.w * s3;
        float aw = fw.x + fw.y + fw.z + fw.w;
        float a =
              (1.0f - w.x) * (1.0f - w.y) * s0.a
            + (       w.x) * (1.0f - w.y) * s1.a
            + (1.0f - w.x) * (       w.y) * s2.a
            + (       w.x) * (       w.y) * s3.a
            ;
        return float4(c.rgb / aw, a);   // note -- possibility of a divide by zero here
    } else {
        return
              (1.0f - w.x) * (1.0f - w.y) * ImposterAltas[atlasIndex].Load(uint3(A.x, A.y, 0))
            + (       w.x) * (1.0f - w.y) * ImposterAltas[atlasIndex].Load(uint3(B.x, A.y, 0))
            + (1.0f - w.x) * (       w.y) * ImposterAltas[atlasIndex].Load(uint3(A.x, B.y, 0))
            + (       w.x) * (       w.y) * ImposterAltas[atlasIndex].Load(uint3(B.x, B.y, 0))
            ;
    }
}

// [earlydepthstencil]
GBufferEncoded ps_deferred(VSOUT geo)
{
    if (0) {
        float3 color0 = float3(1.0f, 0.f, 0.f);
        float3 color1 = float3(0.0f, 0.f, 1.f);
        uint flag = (uint(geo.position.x/4.f) + uint(geo.position.y/4.f))&1;
        GBufferValues result = GBufferValues_Default();
        result.diffuseAlbedo = flag?color0:color1;
        return Encode(result);
    }

    uint4 coordsTopMip = SpriteCoords[geo.spriteIndex*MipMapCount];

        // We must do custom interpolation, for two reasons:
        //  1) our mip-maps are scattered about the atlas
        //  2) we want to use an interior clamping region
        //      (ie, bilinear should not blur into neighbouring sprites)

        // Note that since the sprite is close to screen-aligned, the
        // mip-map index is probably fixed across the entire surface of
        // the sprite. So we could probably calculate it in the vertex shader
        // (or even in a geometry shader)
    float2 tc = geo.texCoord;
    float mipf = CalculateMipmapLevel(tc, uint2(coordsTopMip[2] - coordsTopMip[0], coordsTopMip[3] - coordsTopMip[1]));

        // seem to get a much better result if we bias the mip-map a bit
        // the mip-maps seem to bleed out the detail a lot...
        // maybe we need a better filter?
    const bool mipBias = -.5f;
    mipf += mipBias;
    mipf = clamp(mipf, 0, MipMapCount-1);
    uint mip = (uint)mipf;

    uint4 coords = SpriteCoords[geo.spriteIndex*MipMapCount+mip];
    float4 diffuse = LoadImposterAltas(0, coords, tc);
    float4 normal = LoadImposterAltas(1, coords, tc);

    // if (diffuse.a > 254.f/255.f) discard;    // prevent depth write
    if (diffuse.a > 164.f/255.f) discard;    // prevent depth write

        // Normals are stored in the view space that was originally
        // used to render the sprite.
        // It's equivalent to the tangent space of the sprite.
    TangentFrame tangentFrame = VSOUT_GetWorldTangentFrame(geo);
    float3x3 normalsTextureToWorld = float3x3(tangentFrame.tangent.xyz, tangentFrame.bitangent, tangentFrame.normal);

        // note that we can skip the decompression step here if we use
        // a higher precision output format for the normal buffer (ie, writing
        // to a float buffer instead of a compressed 8 bit buffer)
        // But we always have to do the final compression step
        //      -- the only way to avoid that would be if the normals where
        //          in view space in the gbuffer
    float3 tempNormal = DecompressGBufferNormal(normal.xyz);
    normal.xyz = mul(tempNormal.xyz, normalsTextureToWorld);
    normal.xyz = CompressGBufferNormal(normal.xyz);
    // normal.xyz = lerp(0.5.xxx, normal.xyz, normal.a);

    #if 0
        float3 mipColors[MipMapCount] =
        {
            float3(1, 1, 1),
            float3(0, 1, 1),
            float3(1, 1, 0),
            float3(0, 0, 1),
            float3(0, 1, 0)
        };
        diffuse.xyz = mipColors[mip];
    #endif

    // Blending normals just cause too many problems here.
    // Using decal-style might be ok if the depth doesn't change too
    // much... But with a significant depth change, it creates too
    // many artefacts
    normal.a = 0.f;

        // we can't use the normal Encode() call for the gbuffer
        // because the normal is already compressed into it's 8 bit format
    GBufferEncoded result;
    result.diffuseBuffer = diffuse;
    result.normalBuffer = float4(normal.xyz * (1.0f - normal.a), normal.a);
    // result.normalBuffer = float4(normal.xyz * normal.a, normal.a);
    // result.normalBuffer = float4(normal.xyz, 0.f); // normal.a);
    #if HAS_PROPERTIES_BUFFER == 1
        result.propertiesBuffer = float4(float3(0, 1, 1) * (1.f-diffuse.a), diffuse.a);
    #endif
    return result;
}
