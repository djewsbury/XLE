// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(ILLUM_PER_PIXEL)
#define ILLUM_PER_PIXEL

#include "../../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../../TechniqueLibrary/Framework/VSOUT.hlsl"
#include "../../TechniqueLibrary/Framework/LegacySurface.hlsl"
#include "../../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../../BasicMaterial.hlsl"
#include "../../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
// #include "../../TechniqueLibrary/Math/perlinnoise.hlsl"
#include "../../TechniqueLibrary/Utility/Colour.hlsl"
#include "../../TechniqueLibrary/Framework/Binding.hlsl"

Texture2D       ParametersTexture       BIND_MAT_T5;
Texture2D       SpecularColorTexture    BIND_MAT_T6;
// Texture2D<float>	CustomTexture;
// Texture2D<float2>	ScratchMap : register(t19);		// high res procedural scratches
// Texture2D<float>	ScratchOccl : register(t20);

Texture2D<float>	Occlusion;

PerPixelMaterialParam DecodeParametersTexture_RMS(float4 paramTextureSample);
PerPixelMaterialParam DecodeParametersTexture_ColoredSpecular(inout float3 diffuseSample, float3 specColorSample);
PerPixelMaterialParam DefaultMaterialValues();

///////////////////////////////////////////////////////////////////////////////////////////////////
    //          M A I N   P E R   P I X E L   W O R K
///////////////////////////////////////////////////////////////////////////////////////////////////

GBufferValues IllumShader_PerPixel(VSOUT geo)
{
    GBufferValues result = GBufferValues_Default();

    //#if (VSOUT_HAS_PER_VERTEX_AO==1)
    //    result.diffuseAlbedo = geo.ambientOcclusion.xxx;
    //    result.worldSpaceNormal = VSOUT_GetNormal(geo);
    //    return result;
    //#endif

    float4 diffuseTextureSample = 1.0.xxxx;
    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_DiffuseTexture!=0)
        #if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
            diffuseTextureSample = DiffuseTexture.Sample(ClampingSampler, geo.texCoord);
        #else
            diffuseTextureSample = DiffuseTexture.Sample(MaybeAnisotropicSampler, geo.texCoord);
        #endif
        result.diffuseAlbedo = diffuseTextureSample.rgb;
        result.blendingAlpha = diffuseTextureSample.a;
    #endif

    #if (VSOUT_HAS_COLOR>=1) && MAT_MODULATE_VERTEX_ALPHA
        result.blendingAlpha *= geo.color.a;
    #endif

    #if (SKIP_MATERIAL_DIFFUSE!=1)
        result.blendingAlpha *= Opacity;
    #endif

    #if (MAT_ALPHA_TEST==1)
            // note -- 	Should the alpha threshold effect take into
            //			account material "Opacity" and vertex alpha?
            //			Compatibility with legacy DX9 thinking might
            //			require it to only use the texture alpha?
        if (result.blendingAlpha < AlphaThreshold) discard;
    #endif

    result.material = DefaultMaterialValues();

    #if (VSOUT_HAS_TEXCOORD>=1)
        #if (RES_HAS_ParametersTexture!=0)
                //	Just using a trilinear sample for this. Anisotropy disabled.
            result.material = DecodeParametersTexture_RMS(
                ParametersTexture.Sample(DefaultSampler, geo.texCoord));
        #elif (RES_HAS_SpecularColorTexture!=0)
            result.material = DecodeParametersTexture_ColoredSpecular(
                result.diffuseAlbedo.rgb,
                SpecularColorTexture.Sample(DefaultSampler, geo.texCoord).rgb);
        #endif
    #endif

    #if (SKIP_MATERIAL_DIFFUSE!=1)
        result.diffuseAlbedo *= SRGBToLinear(MaterialDiffuse);
    #endif

    #if (VSOUT_HAS_COLOR>=1)
        result.diffuseAlbedo.rgb *= geo.color.rgb;
    #endif

    #if (VSOUT_HAS_TEXCOORD>=1) && (RES_HAS_Occlusion==1)
            // use the "custom map" slot for a parameters texture (ambient occlusion, gloss, etc)
        result.cookedAmbientOcclusion = Occlusion.Sample(DefaultSampler, geo.texCoord).r;

            // use perlin noise to generate a random gloss pattern (for debugging)
            // (add #include "../Utility/Equations.h")
        // float3 worldPosition = SysUniform_GetWorldSpaceView() - geo.worldViewVector;
        // float gloss = fbmNoise3D(worldPosition, 1.f/9.23f, .5f, 2.1042, 3);
        // const int method = 0;
        // if (method == 0) {
        // 	result.reflectivity = .5f + .5f * gloss;
        // 	result.reflectivity = BlinnWyvillCosineApproximation(result.reflectivity);
        // } else {
        // 	result.reflectivity = abs(gloss);
        // 	result.reflectivity = DoubleCubicSeatWithLinearBlend(result.reflectivity, 0.347f, 0.887f);
        // 	if (gloss < 0.f)
        // 		result.reflectivity *= 0.25f;
        // 	result.reflectivity *= 5.f;
        // }
    #endif

    #if (VSOUT_HAS_PER_VERTEX_AO==1)
        result.cookedAmbientOcclusion *= geo.ambientOcclusion;
        result.cookedLightOcclusion *= geo.ambientOcclusion;
    #endif

    #if (VSOUT_HAS_PER_VERTEX_MLO==1)
        result.cookedLightOcclusion *= geo.mainLightOcclusion;
    #endif

    #if (MAT_AO_IN_NORMAL_BLUE!=0) && (RES_HAS_NormalsTexture!=0) && (RES_HAS_NormalsTexture_DXT==0) && (VSOUT_HAS_TEXCOORD>=1)
            // some pipelines put a AO term in the blue channel of the normal map
            // we can factor it in here...
        float cookedAO = NormalsTexture.Sample(DefaultSampler, geo.texCoord).z;
        // cookedAO *= cookedAO; // testing with the Nyra model, it's too weak... giving a little extra punch
        result.cookedAmbientOcclusion *= cookedAO;
    #endif

    #if (VSOUT_HAS_TANGENT_FRAME==1) && (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
        // const bool scratchMapTest = false;
        // if (scratchMapTest) {
        //
        //     float3 worldPosition = SysUniform_GetWorldSpaceView() - geo.worldViewVector;
        //     float noiseSample = fbmNoise3D(worldPosition, 1.f/11.63f, .5f, 2.1042, 1);
        //     float scratchiness = saturate(noiseSample + .25f); // saturate(lerp(0.0f, 1.f, noiseSample));
        //
        //         // blend the normal map with a high res scratches map
        //         // use the anisotropic sampler, because we want to emphasize detail
        //         //
        //         //		blending normal maps is tricky... But let's just take the average of the xy components
        //         //		of each normal map. Then the z component can be extrapolated from the others
        //     float2 mainNormals = NormalsTexture.Sample(MaybeAnisotropicSampler, geo.texCoord).xy * 2.f - 1.0.xx;
        //     float2 scratchTexCoords = 6.13f * geo.texCoord;
        //     float2 scratchNormals = ScratchMap.Sample(MaybeAnisotropicSampler, scratchTexCoords).xy * 2.f - 1.0.xx;
        //     float2 blendedNormals = mainNormals + (scratchiness) * scratchNormals;
        //     float3 finalNormal = float3(blendedNormals, sqrt(saturate(1.f + dot(blendedNormals.xy, -blendedNormals.xy))));
        //
        //     TangentFrame tangentFrame = VSOUT_GetWorldTangentFrame(geo);
        //     float3x3 normalsTextureToWorld = float3x3(tangentFrame.tangent, tangentFrame.bitangent, tangentFrame.normal);
        //     result.worldSpaceNormal = mul(finalNormal, normalsTextureToWorld);
        //
        //     result.material.roughness = 1.f - scratchiness;
        //
        //     float scratchOcc = ScratchOccl.Sample(MaybeAnisotropicSampler, scratchTexCoords).r;
        //     result.cookedAmbientOcclusion *= lerp(1.f, scratchOcc, .5f * scratchiness);
        //
        // } else
    #endif
    {
        result.worldSpaceNormal = VSOUT_GetNormal(geo);
    }

    // result.diffuseAlbedo.xyz = 0.5f + 0.5f * result.worldSpaceNormal.xyz;
    // result.diffuseAlbedo.xyz = geo.localTangent.www;

    #if RES_HAS_NormalsTexture && CLASSIFY_NORMAL_MAP
        #if (RES_HAS_NormalsTexture_DXT==1)
            result.diffuseAlbedo = float3(1,0,0);
        #else
            result.diffuseAlbedo = float3(0,1,0);
        #endif
    #endif

        //
        //		The length of the normal from the normal map can be used as
        //		an approximation of the variation in the normals used to
        //		generate each mipmap cell.
        //
    #if (RES_HAS_NormalsTexture_DXT==1)		// only really valid when using DXT textures (3DX format normals are always unit length)
        result.normalMapAccuracy = dot(result.worldSpaceNormal, result.worldSpaceNormal);
    #endif

        //	Scale back the material roughness when there's some doubt
        //	about the normal map accuracy. We want to try to avoid some
        //	aliasing in the normal map. We can also just fade off the
        //  roughness values as the mip map value increases.
    result.material.roughness *= result.normalMapAccuracy;

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //          D E C O D E   P A R A M E T E R S   T E X T U R E
///////////////////////////////////////////////////////////////////////////////////////////////////

PerPixelMaterialParam DecodeParametersTexture_RMS(float4 paramTextureSample)
{
		//	We're just storing roughness, specular & material
		//	pixel pixel in this texture. Another option is to
		//	have a per-pixel material index, and get these values
		//	from a small table of material values.
	PerPixelMaterialParam result = PerPixelMaterialParam_Default();
	result.roughness = paramTextureSample.r;
	result.specular = paramTextureSample.g;
	result.metal = paramTextureSample.b;
	return result;
}

PerPixelMaterialParam DecodeParametersTexture_ColoredSpecular(inout float3 diffuseSample, float3 specColorSample)
{
		// 	Some old textures just have a specular color in the parameters
		//	texture. We could do some conversion in a pre-processing step. But
		//	let's just do a prototype conversion here.
		//
		//	If the specular color is not similar to the diffuse (and particularly if
		//	the diffuse is dark), then we can take that as a hint of metallicness.
		//	But this is very rough.
    // PerPixelMaterialParam result = PerPixelMaterialParam_Default();
    // float specLum = saturate(SRGBLuminance(paramTexSample.rgb));
    // result.roughness = RoughnessMin;
    // result.specular = lerp(SpecularMin, SpecularMax, specLum);

    // float dH = RGB2HSV(diffuseSample.rgb).x;
    // float sH = RGB2HSV(paramTexSample.rgb).x;
    // float diff = abs(dH - sH);
    // if (diff > 180) diff = 360 - diff;
    // result.metal = diff / 180.f;
    // result.metal *= specLum;
    // result.metal *= result.metal;
    // result.metal = lerp(MetalMin, MetalMax, saturate(result.metal));

    PerPixelMaterialParam result = PerPixelMaterialParam_Default();
    float diffuseLum = saturate(SRGBLuminance(diffuseSample));
    float specLum = saturate(SRGBLuminance(specColorSample));
    float ratio = specLum / (diffuseLum + 0.00001f);
    result.metal = saturate((ratio - 1.1f) / 0.9f);
    result.roughness = 1.0f - specLum;
    // result.specular = 0.f;
    // result.roughness = 0.f;
    result.specular = specLum;

    result.metal        = lerp(MetalMin, MetalMax, result.metal);
    result.roughness    = lerp(RoughnessMin, RoughnessMax, result.roughness);
    result.specular     = lerp(SpecularMin, SpecularMax, result.specular);

    diffuseSample       = lerp(diffuseSample, specColorSample, result.metal);

	return result;
}

PerPixelMaterialParam DefaultMaterialValues()
{
	PerPixelMaterialParam result;
	result.roughness = RoughnessMin;
	result.specular = SpecularMin;
	result.metal = MetalMin;
	return result;
}


float GetAlphaThreshold() { return AlphaThreshold; }

#endif
