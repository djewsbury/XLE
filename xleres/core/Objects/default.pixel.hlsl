// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DEFAULT_PER_PIXEL)
#define DEFAULT_PER_PIXEL

#include "default.material.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../TechniqueLibrary/Framework/VSOUT.hlsl"
#include "../TechniqueLibrary/Framework/LegacySurface.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"
#include "../TechniqueLibrary/Framework/Binding.hlsl"

Texture2D       ParametersTexture       BIND_MAT_T3;
Texture2D       SpecularColorTexture    BIND_MAT_T4;

PerPixelMaterialParam DefaultMaterialValues();

///////////////////////////////////////////////////////////////////////////////////////////////////
    //          M A I N   P E R   P I X E L   W O R K
///////////////////////////////////////////////////////////////////////////////////////////////////

GBufferValues IllumShader_PerPixel(VSOUT geo)
{
    GBufferValues result = GBufferValues_Default();

    float4 diffuseTextureSample = 1.0.xxxx;
    #if VSOUT_HAS_TEXCOORD && (RES_HAS_DiffuseTexture!=0)
        #if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
            diffuseTextureSample = DiffuseTexture.Sample(ClampingSampler, geo.texCoord);
        #else
            diffuseTextureSample = DiffuseTexture.Sample(MaybeAnisotropicSampler, geo.texCoord);
        #endif
        result.diffuseAlbedo = diffuseTextureSample.rgb;
        result.blendingAlpha = diffuseTextureSample.a;
    #endif

    #if VSOUT_HAS_TEXCOORD && (RES_HAS_OpacityTexture!=0)
        #if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
            result.blendingAlpha = OpacityTexture.Sample(ClampingSampler, geo.texCoord).r;
        #else
            result.blendingAlpha = OpacityTexture.Sample(MaybeAnisotropicSampler, geo.texCoord).r;
        #endif
    #endif

    #if VSOUT_HAS_VERTEX_ALPHA && MAT_MODULATE_VERTEX_ALPHA
        result.blendingAlpha *= VSOUT_GetColor0(geo).a;
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

    #if VSOUT_HAS_TEXCOORD && RES_HAS_ParametersTexture!=0
            //	Just using a trilinear sample for this. Anisotropy disabled.
        result.material = DecodeParametersTexture_RMS(
            ParametersTexture.Sample(DefaultSampler, geo.texCoord));
    #endif

    #if (SKIP_MATERIAL_DIFFUSE!=1)
        result.diffuseAlbedo *= SRGBToLinear(MaterialDiffuse);
    #endif

    #if VSOUT_HAS_COLOR_LINEAR
        result.diffuseAlbedo.rgb *= geo.color.rgb;
    #endif

    #if VSOUT_HAS_PER_VERTEX_AO
        result.cookedAmbientOcclusion *= geo.ambientOcclusion;
        result.cookedLightOcclusion *= geo.ambientOcclusion;
    #endif

    result.worldSpaceNormal = VSOUT_GetNormal(geo);

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
