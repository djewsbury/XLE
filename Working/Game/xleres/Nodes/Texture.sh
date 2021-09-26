// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(NODES_TEXTURE_SH)
#define NODES_TEXTURE_SH

#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../TechniqueLibrary/Framework/VSOUT.hlsl"
#include "../TechniqueLibrary/Math/perlinnoise.hlsl"

float4 SampleAnisotropic(Texture2D inputTexture, float2 texCoord : TEXCOORD0)
{
    return inputTexture.Sample(MaybeAnisotropicSampler, texCoord);
}

float4 Sample(Texture2D inputTexture, float2 texCoord : TEXCOORD0)
{
    return inputTexture.Sample(DefaultSampler, texCoord);
}

float4 SampleWithSampler(Texture2D inputTexture, SamplerState inputSampler, float2 texCoord : TEXCOORD0)
{
    return inputTexture.Sample(inputSampler, texCoord);
}

float4 GetPixelCoords(VSOUT geo)
{
    return geo.position;
}

float4 LoadAbsolute(Texture2D inputTexture, uint2 pixelCoords)
{
    return inputTexture.Load(uint3(pixelCoords, 0));
}

void SampleTextureDiffuse(VSOUT geo, out float3 rgb, out float alpha)
{
    float4 diffuseTextureSample = 1.0.xxxx;
    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_DiffuseTexture!=0)
        #if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
            diffuseTextureSample = DiffuseTexture.Sample(ClampingSampler, geo.texCoord);
        #else
            diffuseTextureSample = DiffuseTexture.Sample(MaybeAnisotropicSampler, geo.texCoord);
        #endif
    #endif
    rgb = diffuseTextureSample.rgb;
    alpha = diffuseTextureSample.a;
}

float SampleTextureAO(VSOUT geo)
{
    float result = 1.f;
    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_Occlusion==1)
        result *= Occlusion.Sample(DefaultSampler, geo.texCoord).r;
    #endif

    #if (MAT_AO_IN_NORMAL_BLUE!=0) && (RES_HAS_NormalsTexture!=0) && (RES_HAS_NormalsTexture_DXT==0) && (VSOUT_HAS_TEXCOORD>=1)
        result *= NormalsTexture.Sample(DefaultSampler, geo.texCoord).z;
    #endif
    return result;
}

float3 GetParametersTexture(VSOUT geo)
{
    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_ParametersTexture!=0)
        return ParametersTexture.Sample(DefaultSampler, geo.texCoord).rgb;
    #else
        return float3(1.f, 1.f, 1.f);
    #endif
}

float GetVertexOpacityMultiplier(VSOUT geo)
{
    #if (VSOUT_HAS_COLOR==1) && MAT_MODULATE_VERTEX_ALPHA
        return geo.color.a;
    #else
        return 1.f;
    #endif
}

float GetVertexAOMultiplier(VSOUT geo)
{
    #if (VSOUT_HAS_PER_VERTEX_AO==1)
        return geo.ambientOcclusion;
    #else
        return 1.f;
    #endif
}

float GetVertexMainLightMultiplier(VSOUT geo)
{
    #if (VSOUT_HAS_PER_VERTEX_MLO==1)
        return geo.mainLightOcclusion;
    #else
        return 1.f;
    #endif
}

float3 MaybeMakeDoubleSided(VSOUT geo, float3 normal)
{
    #if (MAT_DOUBLE_SIDED_LIGHTING==1) && (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
            // We can use either the per-pixel normal or the vertex normal
            // in this calculation.
            // Using the vertex normal might add some complication to the shader
            // (because in some cases the vertex normal has to be calculated from
            // shader inputs). But using the vertex normal better preserves detail
            // in the normal map (which otherwise might be factored out by the flipping)
            //
            // Actually, there are 2 ways we can do this flip -- we can just flip the normal
            // directly. Or we can flip the vertex normal that is used in the normal map
            // algorithm. Each way will express a different kind of underlying shape to the
            // geometry.
        if (dot(VSOUT_GetWorldVertexNormal(geo), geo.worldViewVector) < 0.f)
            return -1.f * normal;
    #endif
    return normal;
}

float MaybeDoAlphaTest(float alpha, float alphaThreshold)
{
    #if (VSOUT_HAS_TEXCOORD>=1) && ((MAT_ALPHA_TEST==1)||(MAT_ALPHA_TEST_PREDEPTH==1))
        if (alpha < alphaThreshold) discard;
    #endif
    return alpha;
}

float3 SampleNormalMap(
  Texture2D normalMap,
  float2 texCoord : TEXCOORD0,
  VSOUT geo)
{
    const bool dxtNormalMap = false;
    float3 normalTextureSample = SampleNormalMap(normalMap, DefaultSampler, dxtNormalMap, texCoord);
    return TransformTangentSpaceToWorld(normalTextureSample, geo);
}

float3 BlendNormals(float3 normalA, float3 normalB, float alpha)
{
    return normalize(normalA * (1.0f - alpha) + normalB * alpha);
}

float3 NoiseTexture3(float3 position, float scale, float detail, float distortion, out float fac)
{
    // This function made to emulate the "Noise" noise in Blender
    //      -- this makes it easier for testing

    float3 p = position * scale;
    if (distortion != 0.f) {
        const float distortionConstant = 13.5f;
        p.x += (0.5f + 0.5f * PerlinNoise3D(position + distortionConstant.xxx)) * distortion;
        p.y += (0.5f + 0.5f * PerlinNoise3D(position)) * distortion;
        p.z += (0.5f + 0.5f * PerlinNoise3D(position - distortionConstant.xxx)) * distortion;
    }

    int octaves = int(detail);
    float normFactor = ((float)(1 << octaves) / (float)((1 << (octaves + 1)) - 1));
    fac = fbmNoise3DZeroToOne(p, 1.f, .5f, 2.1042, octaves) * normFactor;
    return float3(
        fac,
        fbmNoise3DZeroToOne(float3(p[1], p[0], p[2]), 1.f, .5f, 2.1042, octaves) * normFactor,
        fbmNoise3DZeroToOne(float3(p[1], p[2], p[0]), 1.f, .5f, 2.1042, octaves) * normFactor);
}

#endif
