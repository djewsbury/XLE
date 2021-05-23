// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(AMBIENT_RESOLVE_H)
#define AMBIENT_RESOLVE_H

#include "../../LightingEngine/LightDesc.hlsl"
#include "../../LightingEngine/LightingAlgorithm.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"
#include "../../Utility/Colour.hlsl"	                 // for LightingScale
#include "../../Framework/Binding.hlsl"
#include "../../Framework/gbuffer.hlsl"

#if CALCULATE_SCREENSPACE_REFLECTIONS==1
    #include "../../Utility/LoadGBuffer.hlsl"
#endif

/////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(TILED_LIGHTS_RESOLVE_MS)
    Texture2D_MaybeMS<float4>	TiledLightsResolve	 	BIND_NUMERIC_T6;
#else
    Texture2D<float4>			TiledLightsResolve	 	BIND_NUMERIC_T6;
#endif

#if HAS_SCREENSPACE_AO == 1
    Texture2D<float>			AmbientOcclusion		BIND_NUMERIC_T5;
#endif

#if SKY_PROJECTION == 5
    TextureCube					SkyReflectionTexture BIND_NUMERIC_T6;
#elif SKY_PROJECTION == 1
    Texture2D					SkyReflectionTexture[3] BIND_NUMERIC_T6;
#else
    Texture2D					SkyReflectionTexture BIND_NUMERIC_T6;
#endif

#if CALCULATE_SCREENSPACE_REFLECTIONS==1
    Texture2D<float4>			ScreenSpaceReflResult	BIND_NUMERIC_T7;
    Texture2D<float4>			LightBufferCopy			BIND_NUMERIC_T8;
#endif

#if !defined(SKY_PROJECTION)
    #define SKY_PROJECTION 3
#endif

float3 ReadSkyReflectionTexture(float3 reflectionVector, float mipMap)
{
    #if SKY_PROJECTION == 1

        uint2 reflectionTextureDims;
        SkyReflectionTexture[0].GetDimensions(reflectionTextureDims.x, reflectionTextureDims.y);

        return ReadReflectionHemiBox(
            reflectionVector,
            SkyReflectionTexture[0], SkyReflectionTexture[1], SkyReflectionTexture[2],
            ClampingSampler,
            reflectionTextureDims, uint(mipMap));

    #elif (SKY_PROJECTION == 3) || (SKY_PROJECTION == 4)

        uint2 reflectionTextureDims;
        SkyReflectionTexture.GetDimensions(reflectionTextureDims.x, reflectionTextureDims.y);

        #if (SKY_PROJECTION == 3)
            float2 skyReflectionCoord = DirectionToEquirectangularCoord_YUp(reflectionVector);
        #else
            float2 skyReflectionCoord = DirectionToHemiEquirectangularCoord_YUp(reflectionVector);
        #endif
        mipMap = max(mipMap, CalculateMipmapLevel(skyReflectionCoord.xy, reflectionTextureDims));

        // #define DEBUG_REFLECTION_MIPMAPS 1
        #if DEBUG_REFLECTION_MIPMAPS==1
            if (uint(mipMap)==0) return float3(1, 0, 0);
            if (uint(mipMap)==1) return float3(0, 1, 0);
            if (uint(mipMap)==2) return float3(0, 0, 1);
            if (uint(mipMap)==3) return float3(1, 1, 0);
            if (uint(mipMap)==4) return float3(1, 0, 1);
            if (uint(mipMap)==5) return float3(0, 1, 1);
            if (uint(mipMap)==6) return float3(0, .5, 1);
            if (uint(mipMap)==7) return float3(1, 0, .5);
        #endif

        return SkyReflectionTexture.SampleLevel(ClampingSampler, skyReflectionCoord.xy, mipMap).rgb;

    #elif SKY_PROJECTION==5

            // cheap alternative to proper specular IBL
        return SkyReflectionTexture.SampleLevel(DefaultSampler, AdjSkyCubeMapCoords(reflectionVector), mipMap).rgb;

    #else
        return 0.0.xxx;
    #endif
}

#include "ImageBased.hlsl"

float3 CalculateSkyReflectionFresnel(float3 F0, GBufferValues sample, float3 viewDirection, bool mirrorReflection)
{
        // Note that it might be worth considering a slight hack to
        // the fresnel calculation here... Imagine a reflective sphere --
        // around microfacet shadowing should start to play a bigger effect.
        // That should give us an excuse to ramp down the fresnel a little
        // bit around the edge. This could be handy because very bright halos
        // around objects can tend to highlight inaccuracies in the lighting.
    if (!mirrorReflection) {
        float3 worldSpaceReflection = reflect(-viewDirection, sample.worldSpaceNormal);
        float3 halfVector = normalize(worldSpaceReflection + viewDirection);
        return SchlickFresnelF0(viewDirection, halfVector, F0);
    } else {
        return SchlickFresnelF0(viewDirection, sample.worldSpaceNormal, F0);
    }
}

float3 CalculateSkyReflections(GBufferValues sample, float3 viewDirection, float3 fresnel, float blurriness)
{
    float3 worldSpaceReflection = reflect(-viewDirection, sample.worldSpaceNormal);
    float roughness = sample.material.roughness;

    // note -- this is a primitive alternative to the importance sampling IBL (implemented elsewhere)
    float mipMap = blurriness + saturate(roughness) * ReflectionBlurrinessFromRoughness;
    return fresnel * ReadSkyReflectionTexture(worldSpaceReflection, mipMap);
}

/////////////////////////////////////////

float3 CalcBasicAmbient(LightScreenDest lsd, GBufferValues sample,
                        float3 ambientColor, float iblScale)
{
    #if CALCULATE_TILED_LIGHTS==1
        #if defined(TILED_LIGHTS_RESOLVE_MS)
            float3 tiledLights	= LoadFloat4(TiledLightsResolve, lsd.pixelCoords, lsd.sampleIndex).xyz;
        #else
            float3 tiledLights	= TiledLightsResolve.Load(int3(lsd.pixelCoords, 0)).xyz;
        #endif
    #else
        float3 tiledLights = 0.0.xxx;
    #endif

    float3 diffuseIBL = iblScale * SampleDiffuseIBL(sample.worldSpaceNormal, lsd);

    float metal = sample.material.metal;
    return (1.0f - metal)*(ambientColor + tiledLights + diffuseIBL)*sample.diffuseAlbedo.rgb;
}

/////////////////////////////////////////

struct AmbientResolveHelpers
{
    float2 ReciprocalViewportDims;
};

AmbientResolveHelpers AmbientResolveHelpers_Default()
{
    AmbientResolveHelpers result;
    result.ReciprocalViewportDims = 1.0.xx;
    return result;
}

#if CALCULATE_SCREENSPACE_REFLECTIONS==1
    float4 CalculateScreenSpaceReflections(
        LightScreenDest lsd, AmbientResolveHelpers helpers,
        float3 fresnel, float ambientColor, float iblScale)
    {
            //	the screen space refl sampler should contain a texture coordinate
            //	and an alpha value. We need a copy of the "lighting buffer"
            //	render target for this step -- because we want to read from a texture
            //	that can the direct lighting already calculated.
        float4 reflectionValue = ScreenSpaceReflResult.SampleLevel(
            ClampingSampler, float2(lsd.pixelCoords) * helpers.ReciprocalViewportDims, 0);

        if (reflectionValue.a == 0.0f) return 0.0.xxxx;

        float intersectionQuality = reflectionValue.z;
        float pixelReflectivity = reflectionValue.a;
        float2 reflectionSourceCoords = reflectionValue.xy;
        float3 litSample = LightBufferCopy.SampleLevel(ClampingSampler, reflectionSourceCoords, 0).rgb;

            //	we need to predict the ambient light on this point, also (because integration occurs
            //	in the ambient shader, the amient integrating hasn't happened)
        uint2 dims; LightBufferCopy.GetDimensions(dims.x, dims.y);
        float4 samplePosition = float4(reflectionSourceCoords * float2(dims), 0.f, 1.f);
        GBufferValues sample = LoadGBuffer(samplePosition, SystemInputs_Default());
        litSample = CalcBasicAmbient(LightScreenDest_Create(int2(samplePosition.xy), 0), sample, ambientColor, iblScale);

            // blending with the sky reflections helps hide artefacts...
            //  -- and there are so many artefacts!
        float screenSpaceBlend = .5f;
        return float4(
            litSample.rgb * fresnel / LightingScale,
            screenSpaceBlend * intersectionQuality * pixelReflectivity);
    }
#endif

float3 LightResolve_Ambient(
    GBufferValues sample, float3 directionToEye, AmbientDesc ambient,
    LightScreenDest lsd,
    AmbientResolveHelpers helpers,
    bool mirrorReflection = false)
{
    float3 diffusePart = CalcBasicAmbient(lsd, sample, ambient.Colour, ambient.SkyReflectionScale);

    #if HAS_SCREENSPACE_AO==1
        float occlusion = LoadFloat1(AmbientOcclusion, lsd.pixelCoords, lsd.sampleIndex);
    #else
        float occlusion = 1.f;
    #endif

    occlusion *= sample.cookedAmbientOcclusion;
    diffusePart *= occlusion;

        // In our metal model, we store F0 values per wavelength in sample.diffuseAlbedo.
        // This gives us fantasic freedom to control the metallic reflections.
        // See some good reference here:
        //  https://seblagarde.wordpress.com/2011/08/17/feeding-a-physical-based-lighting-mode/
        // Note that we can calculate the correct F0 values for real-world materials using
        // the methods described on that page.
        // Also consider sRGB/Linear wierdness in this step...
    float3 F0 = lerp(SpecularParameterToF0(sample.material.specular).xxx, sample.diffuseAlbedo, sample.material.metal);
    float3 fresnel = CalculateSkyReflectionFresnel(F0, sample, directionToEye, mirrorReflection);

    #if HAS_SPECULAR_IBL==1
        float3 skyReflections = SampleSpecularIBL(
            sample.worldSpaceNormal, directionToEye,
            SpecularParameters_RoughF0(sample.material.roughness, F0), lsd);

        #if MAT_TRANSMITTED_SPECULAR==1
            float3 transmissionNormal = sign(dot(sample.worldSpaceNormal, directionToEye)) * sample.worldSpaceNormal;
            skyReflections += SampleSpecularIBLTrans(
                transmissionNormal, directionToEye,
                SpecularParameters_RoughF0Transmission(sample.material.roughness, F0, sample.transmission), lsd);
        #endif
    #else
        float blurriness = ambient.SkyReflectionBlurriness;
        float3 skyReflections = CalculateSkyReflections(sample, directionToEye, fresnel, blurriness);
    #endif
    skyReflections *= ambient.SkyReflectionScale;

    #if CALCULATE_SCREENSPACE_REFLECTIONS==1
            //	The "screen space" reflections block out the sky reflections.
            //	If we get a collision with the screen space reflections, then we need
            //	to blend from the sky reflection color into that color.
            // note... if we want fogging on the reflections, we need to perform the fog calculations here, on the
            // reflected pixel
        float4 dynamicReflections = CalculateScreenSpaceReflections(
            lsd, helpers, fresnel, ambient.Colour, ambient.SkyReflectionScale);
        skyReflections = lerp(skyReflections, dynamicReflections.rgb, dynamicReflections.a);
    #endif

    #define SPECULAR_OCCLUSION_EXPERIMENT
    #if defined(SPECULAR_OCCLUSION_EXPERIMENT)
        float NdotV = dot(directionToEye, sample.worldSpaceNormal);
        const bool useHalfVector = false;   // using the half vector here doesn't seem to really help
        if (useHalfVector) {
            float3 worldSpaceReflection = reflect(-directionToEye, sample.worldSpaceNormal);
            float3 halfVector = normalize(worldSpaceReflection + directionToEye);
            NdotV = dot(directionToEye, halfVector);
        }
        float specularOcclusion = TriAceSpecularOcclusion(NdotV, occlusion);
        skyReflections *= specularOcclusion;
    #endif

    return diffusePart + skyReflections;
}

#endif
