// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Cubemap.hlsl"
#include "sampling-shader-helper.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/SphericalHarmonics.hlsl"
#include "../TechniqueLibrary/LightingEngine/IBL/IBLAlgorithm.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"

Texture2D Input;
RWTexture2DArray<float4> OutputArray;
RWTexture2D<float4> Output;
SamplerState EquirectangularBilinearSampler;
RWTexture2D<float> MarginalHorizontalCDF;
RWTexture1D<float> MarginalVerticalCDF;
Texture2D<uint> SampleIndexLookup;

cbuffer SampleIndexUniforms
{
    float HaltonSamplerJ, HaltonSamplerK;
    uint HaltonSamplerRepeatingStride;
}

struct ControlUniformsStruct
{
    SamplingShaderUniforms _samplingShaderUniforms;
    uint MipIndex;
};
[[vk::push_constant]] ControlUniformsStruct ControlUniforms;

float3 IBLPrecalc_SampleInputTextureUV(float2 uv)
{
    return Input.SampleLevel(EquirectangularBilinearSampler, uv, 0).rgb;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "../Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_common.h"

float LoadMarginalVerticalCDF(uint coord) { return MarginalVerticalCDF[coord]; }
float LoadMarginalHorizontalCDF(uint2 coord) { return MarginalHorizontalCDF[coord]; }

float2 SampleUV(out float pdf, float2 xi, uint2 marginalCDFDims)
{
    // binary search not great for GPUs, but we will deal
    // note -- coordinate conversion not really correct when the input texture isn't an exactly mutliple
    // of the block size
    // Search for the v coordinate
    uint l=0, h=marginalCDFDims.y;
    while (true) {
        uint m = (l+h)>>1;
        float midCDF = LoadMarginalVerticalCDF(m);
        if (xi.y < midCDF) {
            h = m;
        } else {
            l = m;
        }
        if ((l+3)>=h) break;
    }

    float v;
    uint y;
    float range;
    float t1 = LoadMarginalVerticalCDF(l+1);
    float t2 = (((l+2) == marginalCDFDims.y) ? 1.0 : LoadMarginalVerticalCDF(l+2));
    float t3 = (((l+3) >= marginalCDFDims.y) ? 1.0 : LoadMarginalVerticalCDF(l+3));
    if (xi.y < t1) {
        range = t1 - LoadMarginalVerticalCDF(l);
        v = l + (xi.y - LoadMarginalVerticalCDF(l)) / range;
        y = l;
    } else if (xi.y < t2) {
        range = t2 - t1;
        v = l+1 + (xi.y - t1) / range;
        y = l+1;
    } else {
        // xi.y must be smaller than MarginalVerticalCDF[l+3], though l+3 might be off the end
        range = max(t3 - t2, 1e-5);     // tiny ranges here can cause a nans later
        v = l+2 + (xi.y - t2) / range;
        y = l+2;
    }
    v /= marginalCDFDims.y;
    pdf = range * float(marginalCDFDims.y);

    // search for the u coordinate
    l=0; h=marginalCDFDims.x;
    while (true) {
        uint m = (l+h)>>1;
        float midCDF = LoadMarginalHorizontalCDF(uint2(m, y));
        if (xi.x < midCDF) {
            h = m;
        } else {
            l = m;
        }
        if ((l+3)>=h) break;
    }

    float u;
    t1 = LoadMarginalHorizontalCDF(uint2(l+1, y));
    t2 = (((l+2) == marginalCDFDims.x) ? 1.0 : LoadMarginalHorizontalCDF(uint2(l+2, y)));
    t3 = (((l+3) >= marginalCDFDims.x) ? 1.0 : LoadMarginalHorizontalCDF(uint2(l+3, y)));
    if (xi.x < t1) {
        range = t1 - LoadMarginalHorizontalCDF(uint2(l, y));
        u = l + (xi.x - t1) / range;
    } else if (xi.x < t2) {
        range = t2 - t1;
        u = l+1 + (xi.x - t1) / range;
    } else {
        // xi.x must be smaller than MarginalHorizontalCDF[uint2(l+3,y)], though l+3 might be off the end
        range = max(t3 - t2, 1e-5);     // tiny ranges here can cause a nans later
        u = l+2 + (xi.x - t2) / range;
    }
    u /= marginalCDFDims.x;
    pdf *= range * float(marginalCDFDims.x);

    return float2(u, v);
}

float LookupPDF(float2 uv, uint2 marginalCDFDims)
{
    // Lookup this coordinate, and try to figure out what we would have returned as a pdf if
    // we had selected the coordinate from SampleUV

    uint2 xy = min(uv * marginalCDFDims, marginalCDFDims-1);

    float t0 = LoadMarginalVerticalCDF(xy.y);
    float t1 = (xy.y+1) == marginalCDFDims.y ? 1 : LoadMarginalVerticalCDF(xy.y);
    float pdf = (t1-t0) * float(marginalCDFDims.y);

    t0 = LoadMarginalHorizontalCDF(xy);
    t1 = (xy.x+1) == marginalCDFDims.x ? 1 : LoadMarginalHorizontalCDF(xy+uint2(1,0));
    pdf *= (t1-t0) * float(marginalCDFDims.x);

    return pdf;
}

/// <summary>Filtering equation used by FilterGlossySpecular_BRDF_costheta</summary>
/// With the "split-sum" approach to IBL prefiltering, we can select the filtering equation
/// to use somewhat arbitrarily, since we can't capture the full effect of the BRDF using
/// this approximation.
///
/// However, it seems logical that the ideal filtering is based on the BRDF equation itself,
/// and particularly on the "D" term. This will give approximately the right level and shape
/// to the filtering for a given alpha value, when looking straight on at a reflective surface.
///
/// We should also bring in the cosTheta term from the light transport integral. G here doesn't
/// play a huge role, but is convenient for the sampling process.
///
/// Divide by 4 so the integral across the hemisphere is 1 -- this will mean the output retains
/// the brightness levels of the original texture, which therefore means the other part of the 
/// split-sum equation will have full control over the brightness of the final estimate.
float FilteringEquation(float NdotL, float NdotM, float alpha)
{
    float D = TrowReitzD(NdotM, alpha);
    float G = SmithG(NdotL, alpha);     // (only half of the G term from Torrence-Sparrow)
    return NdotL * D * G / 4;
}

static const uint s_VNDFSampling = 0;
static const uint s_GGXSampling = 1;
static const uint s_CosineHemisphereSampling = 2;

/// <summary>For a given light direction, calculate our filtering weight as well as the pdf if we had happened to sample this using FilterGlossySpecular_SampleBRDF</summary>
/// The pdf here is intended for multiple importance sampling; since with that method we take a sample from the light source and we need to know 
/// the pdf if we had picked that sample from the brdf sampling distribution.
///
/// the pdf returned is intended for the light transport equation, so is w.r.t solid angle and describing the distribution of 
/// light directions (ie, not half vectors)
float FilterGlossySpecular_BRDF_costheta(
    out float pdf,
    float3 N, float3 L, 
    float roughness,
    uint samplingMethod)
{
    float alpha = RoughnessToDAlpha(max(roughness, MinSamplingRoughness));
    float NdotL = dot(N, L);
    float NdotV = 1;

    // since V = N, if VdotL = cos(theta), then VdotM = NdotM = cos(theta/2)
    // cos(theta/2) = +/- sqrt((1+cos(theta))/2)
    float NdotM = sqrt((1.0+NdotL)/2.0);

    if (samplingMethod == s_VNDFSampling) {

        //
        // Partial BRDF used for filtering the glossy specular
        // We can't replicate the effect that a true full brdf would have (given this is only single-directional)
        // But we want to try to just get an approximation of the expected blurriness
        // Our filtering is selected partially to be convenient with the half vector selection algorithm
        // we're using for importance sampling
        //
        // filtering equation = NdotL * D * excid_G / (4 * NdotV * NdotL)
        // (note only half of the G term from Torrence-Sparrow)
        //

        float D = TrowReitzD(NdotM, alpha);
        float G = SmithG(NdotV, alpha);
        float filteringWeight = D * G / 4;

        // pdf associated with the half vector selection method we're using for glossy filtering
        // used for multiple importance sampling weighting
        //
        // pdf = D * excid_G * VdotM / (4 * NdotV * VdotM)
        // (above includes the change-of-variables term requires to convert this to be w.r.t. solid angle)
        //
        pdf = D * G / 4;

        return filteringWeight;

    } else if (samplingMethod == s_GGXSampling) {

        pdf = SamplerGGXHalfVector_PDF(float3(0,0,NdotM), float3(0,0,1), alpha);
        return FilteringEquation(NdotL, NdotM, alpha);

    } else {

        // cosine weighted hemisphere
        pdf = SamplerCosineHemisphere_PDF(NdotL);
        return FilteringEquation(NdotL, NdotM, alpha);

    }
}

/// <summary>Sample the brdf to generate a filtering weight and pdf</summary>
/// There are 3 different sampling methods:
///     * s_VNDFSampling
///     * s_GGXSampling
///     * s_CosineHemisphereSampling
///
/// s_VNDFSampling is the preferred approach, though in this application there's little difference between
/// it and s_GGXSampling. Both of these 2 sample half vectors from the BRDF equation itself.
///
/// s_CosineHemisphereSampling samples the entire hemisphere with cosine weight. Since the important samples of
/// the brdf in a focused area of the hemiphere, this isn't efficient. However it can be used for verification.
/// 
/// the pdf returned is intended for the light transport equation, so is w.r.t solid angle and describing the distribution of 
/// light directions (ie, not half vectors)
float FilterGlossySpecular_SampleBRDF(
    out float pdf,
    out float3 L,
    float2 xi,
    float3 N,
    float roughness,
    uint samplingMethod)
{
    float alpha = RoughnessToDAlpha(max(roughness, MinSamplingRoughness));

    if (samplingMethod == s_VNDFSampling) {

        // Sample half vector using VDNF approach
        float3 V = N;
        float3 tangentSpaceHalfVector = SamplerHeitzGGXVNDF_Pick(float3(0,0,1), alpha, alpha, xi.x, xi.y);
        float3 H = TransformByArbitraryTangentFrame(tangentSpaceHalfVector, N);
        L = 2.f * tangentSpaceHalfVector.z * H - V;

        // note that the pdf and filtering weight actually mostly factor out. We only need to calculate
        // most of this stuff just for the multiple importance sampling

        float NdotL = dot(N, L);
        float NdotV = 1;
        float NdotM = tangentSpaceHalfVector.z;
        float D = TrowReitzD(NdotM, alpha);
        float G = SmithG(NdotV, alpha);
        float filteringWeight = NdotL * D * G / 4;

        pdf = D * G / 4;

        return filteringWeight;

    } else if (samplingMethod == s_GGXSampling) {

        float3 tangentSpaceHalfVector = SamplerGGXHalfVector_Pick(xi, alpha);
        L = 2.f * tangentSpaceHalfVector.z * tangentSpaceHalfVector - float3(0,0,1);
        if (L.z < 0) {
            pdf = 0;
            return 0;
        }

        float NdotL = L.z;
        float NdotV = 1;
        float NdotM = tangentSpaceHalfVector.z;

        pdf = SamplerGGXHalfVector_PDF(tangentSpaceHalfVector, float3(0,0,1), alpha);
        L = TransformByArbitraryTangentFrame(L, N);

        return FilteringEquation(NdotL, NdotM, alpha);

    } else {

        float hemisphere_pdf;
        L = SamplerCosineHemisphere_Pick(hemisphere_pdf, xi);
        float NdotL = L.z;

        pdf = hemisphere_pdf;
        L = TransformByArbitraryTangentFrame(L, N);
        return FilteringEquation(NdotL, 1, alpha);

    }
}

float BalanceHeuristic(float nf, float fpdf, float ng, float gpdf)
{
    // "balance heuristic" for multiple important sampling
    // note that fpdf tends to get factored out at the usage point
    return (nf * fpdf) / (nf * fpdf + ng * gpdf);
}

float PowerHeuristic2(float nf, float fpdf, float ng, float gpdf)
{
    float f = nf * fpdf, g = ng * gpdf;
    f *= f; g *= g;
    return f/(f+g);
}

[numthreads(8, 8, 1)]
    void EquirectFilterGlossySpecular(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    // This is the second term of the "split-term" solution for IBL glossy specular
    // Here, we prefilter the reflection texture in such a way that the blur matches
    // the GGX equation.
    //
    // This is very similar to calculating the full IBL reflections. However, we're
    // making some simplifications to make it practical to precalculate it.
    // We can choose to use an importance-sampling approach. This will limit the number
    // of samples to some fixed amount. Alternatively, we can try to sample the texture
    // it some regular way (ie, by sampling every texel instead of just the ones suggested
    // by importance sampling).
    //
    // If we sample every pixel we need to weight by the solid angle of the texel we're
    // reading from. But if we're just using the importance sampling approach, we can skip
    // this step (it's just taken care of by the probability density function weighting)
    //
    // The MarginalHorizontalCDF & MarginalVerticalCDF textures must be prepared before
    // calling this function. Typically this is done with CalculateHorizontalMarginalDensities &
    // NormalizeMarginalDensities. These textures are used to fine which parts of the input
    // texture are most important to the filtered output. Since some parts of the input texture
    // can be thousands of times brighter than other parts, this importance distribution can
    // be very uneven
    uint2 textureDims, marginalCDFDims, inputTextureDims, samplingPatternDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
	MarginalHorizontalCDF.GetDimensions(marginalCDFDims.x, marginalCDFDims.y);
	Input.GetDimensions(inputTextureDims.x, inputTextureDims.y);
    SampleIndexLookup.GetDimensions(samplingPatternDims.x, samplingPatternDims.y);

    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 6), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, arrayLayerCount))) return;
    if (helper._firstDispatch) OutputArray[helper._outputPixel] = 0;

    // The features in the filtered map are clearly biased to one direction in mip maps unless we add half a pixel here
    float2 texCoord = (helper._outputPixel.xy + 0.5.xx) / float2(textureDims);
    float3 cubeMapDirection = CalculateCubeMapDirection(helper._outputPixel.z, texCoord);
    int log2dim = firstbithigh(textureDims.x);
    float roughness = MipmapToRoughness(SpecularIBLMipMapCount-log2dim);
    SpecularParameters specParam = SpecularParameters_RoughF0(roughness, float3(1.0f, 1.0f, 1.0f));
    float samplingJScale = pow(2,HaltonSamplerJ), samplingKScale = pow(3,HaltonSamplerK);
    uint samplerIdxBase = SampleIndexLookup[helper._outputPixel.xy % samplingPatternDims.xy];

    const bool sampleLight = true;
    const bool sampleBrdf = true;
    const uint brdfSamplingMethod = s_VNDFSampling;

    float3 value = 0;
    uint t;
    // Sampling with importance based on the brightness of the image
    if (sampleLight) {
        for (t=0; t<helper._thisPassSampleCount; ++t) {
            uint globalTap = t*helper._thisPassSampleStride+helper._thisPassSampleOffset;
            uint samplerIdx = samplerIdxBase + HaltonSamplerRepeatingStride * globalTap;
            // Using the Halton sequence in such a straightforward way as this is not going to be efficient; but we don't require a perfectly
            // optimal solution here
            float2 xi = float2(frac(RadicalInverseBase2(samplerIdx)*samplingJScale), frac(RadicalInverseBase3(samplerIdx)*samplingKScale));
            xi = saturate(xi);      // floating point creep may be resulting in some bad xi values

            float light_pdf=1;
            float2 inputTextureUV = SampleUV(light_pdf, xi, marginalCDFDims);

            float3 L = EquirectangularCoordToDirection_YUp(inputTextureUV);
            float NdotL = dot(cubeMapDirection, L);
            if (NdotL < 0) continue;   // sampling only one hemisphere (pdf adjusted below)

            // change-of-variables for light_pdf
            // Original light_pdf is w.r.t. u,v coords. But we want a light_pdf w.r.t. solid angle on the hemisphere
            // (consider, for example, that a lot of uvs are densly packed around the poles)
            // see pbr-book chapter 14.2.4
            if (inputTextureUV.y == 0) continue;
            float compressionFactor = sin(pi * inputTextureUV.y);
            light_pdf /= 2 * pi * pi * max(compressionFactor, 1e-5);

            float filtering_pdf;
            float brdf_costheta = FilterGlossySpecular_BRDF_costheta(filtering_pdf, cubeMapDirection, L, roughness, brdfSamplingMethod);

            float msWeight = sampleBrdf ? PowerHeuristic2(1, light_pdf, 1, filtering_pdf) : 1;        // (assuming equal count of samples)

            // todo -- consider kahan sum here -- https://en.wikipedia.org/wiki/Kahan_summation_algorithm 
            value += IBLPrecalc_SampleInputTextureUV(inputTextureUV) * brdf_costheta * msWeight / light_pdf;
        }
    }

    // Sampling with importance based on the filtering kernel
    if (sampleBrdf) {
        for (t=0; t<helper._thisPassSampleCount; ++t) {
            uint globalTap = t*helper._thisPassSampleStride+helper._thisPassSampleOffset;
            uint samplerIdx = samplerIdxBase + HaltonSamplerRepeatingStride * globalTap;
            float2 xi = float2(RadicalInverseBase5(samplerIdx), RadicalInverseBase7(samplerIdx));
            xi = saturate(xi);      // floating point creep may be resulting in some bad xi values

            float filtering_pdf;
            float3 L;
            float brdf_costheta = FilterGlossySpecular_SampleBRDF(
                filtering_pdf, L,
                xi, cubeMapDirection, roughness, brdfSamplingMethod);
            if (brdf_costheta <= 0) continue;       // sometimes getting bad samples
            
            float2 inputTextureUV = DirectionToEquirectangularCoord_YUp(L);
            inputTextureUV.x += (inputTextureUV.x < 0)?1:0;      // try to get in (0,1) range
            float light_pdf = LookupPDF(inputTextureUV, marginalCDFDims);
            // change-of-variables for light_pdf...
            float compressionFactor = sin(pi * inputTextureUV.y);
            light_pdf /= 2 * pi * pi * max(compressionFactor, 1e-5);

            float msWeight = sampleLight ? PowerHeuristic2(1, filtering_pdf, 1, light_pdf) : 1;

            // todo -- consider kahan sum here -- https://en.wikipedia.org/wiki/Kahan_summation_algorithm 
            value += IBLPrecalc_SampleInputTextureUV(inputTextureUV) * brdf_costheta * msWeight / filtering_pdf;
        }
    }

    if (helper._thisPassSampleStride != 1) {
        OutputArray[helper._outputPixel].rgb += value / helper._totalSampleCount;
    } else {
        // potential floating point creep issues here (and output values will vary based on # of samples/pass)
        OutputArray[helper._outputPixel].rgb
            = OutputArray[helper._outputPixel].rgb * (helper._thisPassSampleOffset / float(helper._thisPassSampleOffset+helper._thisPassSampleCount))
            + value / float(helper._thisPassSampleOffset+helper._thisPassSampleCount);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float Brightness(float3 rgb) { return SRGBLuminance(rgb); }

[numthreads(8, 8, 1)]
    void CalculateHorizontalMarginalDensities(uint3 dispatchThreadId : SV_DispatchThreadID) : SV_Target0
{
    uint2 inputDims, outputDims;
	Input.GetDimensions(inputDims.x, inputDims.y);
    MarginalHorizontalCDF.GetDimensions(outputDims.x, outputDims.y);
    if (any(dispatchThreadId.xy >= outputDims)) return;

    uint2 blockDims = uint2(inputDims + outputDims - 1) / outputDims;
    float blockBrightness = 0.0;
    for (uint y=0; y<blockDims.y; ++y)
        for (uint x=0; x<blockDims.x; ++x) {
            uint2 pt = dispatchThreadId.xy*blockDims+uint2(x, y);
            if (all(pt < inputDims)) {
                // Scale down the pixels according to how densly packed the uvs are. This reshapes
                // the pdf and causes us to select evenly w.r.t. solid angle, rather then w.r.t. uvs
                float v = pt.y / float(inputDims.y);
                float compressionFactor = sin(pi * v);
                blockBrightness += compressionFactor * Brightness(Input[pt].rgb);
            }
        }

    // Our code assumes no 0 pdf values, so we will accept some sampling inefficiency for textures with large
    // blocks of blackness
    MarginalHorizontalCDF[dispatchThreadId.xy] = max(blockBrightness / float(blockDims.x * blockDims.y), 1.0/255.0);
}

[numthreads(64, 1, 1)]
    void NormalizeMarginalDensities(uint3 groupThreadId : SV_GroupThreadID) : SV_Target0
{
    // We do this in a single thread group to make the scheduling easy
    uint2 outputDims;
    MarginalHorizontalCDF.GetDimensions(outputDims.x, outputDims.y);
    uint rowsPerThread = (outputDims.y + 64 - 1) / 64;
    for (uint y=groupThreadId.x*rowsPerThread; y<(groupThreadId.x+1)*rowsPerThread; ++y) {
        if (y >= outputDims.y) break;

        // pdf is proportional to brightness within each pixel block
        // furthermore, the integral with respect to u should be 1
        // sum(pdf_values) * (1/outDims.x) = 1 => sum(pdf_values) = outputDims.y
        // so, pdf at a y coord is brightness[y] / sum(brightnesses) * outputDims.y
        float norm = 0.f;
        uint x=0;
        for (; x<outputDims.x; ++x)
            norm += MarginalHorizontalCDF[uint2(x, y)];
        float cummulative = 0.f;
        for (x=0; x<outputDims.x; ++x) {
            float v = cummulative;
            cummulative += MarginalHorizontalCDF[uint2(x, y)] / norm;
            MarginalHorizontalCDF[uint2(x, y)] = v;
        }

        MarginalVerticalCDF[y] = norm;
    }

    AllMemoryBarrierWithGroupSync();        // need GroupSync here
    if (groupThreadId.x == 0) {
        // simple approach to normalizing vertical values
        float verticalNorm = 0.f;
        uint y=0;
        for (; y<outputDims.y; ++y)
            verticalNorm += MarginalVerticalCDF[y];
        float cummulative = 0.f;
        for (y=0; y<outputDims.y; ++y) {
            float v = cummulative;
            cummulative += MarginalVerticalCDF[y] / verticalNorm;
            MarginalVerticalCDF[y] = v;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 1)]
    void EquirectFilterGlossySpecular_Reference(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 6), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, arrayLayerCount))) return;
    if (helper._firstDispatch) OutputArray[helper._outputPixel] = 0;

    uint2 inputTextureDims;
	Input.GetDimensions(inputTextureDims.x, inputTextureDims.y);
    float2 reciprocalInputTextureDims = float2(1/float(inputTextureDims.x), 1/float(inputTextureDims.y));

    // The features in the filtered map are clearly biased to one direction in mip maps unless we add half a pixel here
    float2 texCoord = (helper._outputPixel.xy + 0.5.xx) / float2(textureDims);
    float3 cubeMapDirection = CalculateCubeMapDirection(helper._outputPixel.z, texCoord);
    int log2dim = firstbithigh(textureDims.x);
    float roughness = MipmapToRoughness(SpecularIBLMipMapCount-log2dim);
    SpecularParameters specParam = SpecularParameters_RoughF0(roughness, float3(1.0f, 1.0f, 1.0f));

    // Basic sampling for reference purposes. We're going to sample every pixel from the input texture
    const uint brdfSamplingMethod = s_VNDFSampling;

    float3 value = 0;
    for (uint t=0; t<helper._thisPassSampleCount; ++t) {
        uint globalTap = t+helper._thisPassSampleOffset;

        uint2 inputXY = uint2(globalTap%inputTextureDims.x, globalTap/inputTextureDims.x);
        float2 inputTextureUV = inputXY * reciprocalInputTextureDims;

        float3 L = EquirectangularCoordToDirection_YUp(inputTextureUV);
        float NdotL = dot(cubeMapDirection, L);
        if (NdotL <= 0) continue;   // sampling only one hemisphere (pdf adjusted below)

        float filtering_pdf;
        float brdf_costheta = FilterGlossySpecular_BRDF_costheta(filtering_pdf, cubeMapDirection, L, roughness, brdfSamplingMethod);
        if (brdf_costheta <= 0) continue;

        if (inputTextureUV.y == 0) continue;

        // light_pdf here effectively takes into account the different sizes of the texels in the equirectangular texture
        float light_pdf = 1 / (2 * pi * pi * sin(pi * inputTextureUV.y));        
        value += Input[inputXY].rgb * brdf_costheta / light_pdf * reciprocalInputTextureDims.x * reciprocalInputTextureDims.y;
    }

    // potential floating point creep issues here (and output values will vary based on # of samples/pass)
    OutputArray[helper._outputPixel].rgb += value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0

float3 SampleNormal(float3 core, uint s, uint sampleCount)
{
    float theta = 2.f * pi * float(s)/float(sampleCount);
    const float variation = 0.2f;
    float3 H = float3(variation * cos(theta), variation * sin(theta), 0.f);
    H.z = sqrt(1.f - H.x*H.x - H.y*H.y);
    float3 up = abs(core.z) < 0.999f ? float3(0,0,1) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, core));
    float3 tangentY = cross(core, tangentX);
    return tangentX * H.x + tangentY * H.y + core * H.z;
}

float3 CalculateFilteredTextureTrans(
    float3 cubeMapDirection, float roughness,
    float iorIncident, float iorOutgoing,
    uint passSampleCount, uint passIndex, uint passCount)
{
    float3 corei = cubeMapDirection;
    float3 coreNormal = (iorIncident < iorOutgoing) ? corei : -corei;

    // Very small roughness values break this equation, because D converges to infinity
    // when NdotH is 1.f. We must clamp roughness, or this convergence produces bad
    // floating point numbers.
    roughness = max(roughness, MinSamplingRoughness);
    float alphad = RoughnessToDAlpha(roughness);
    float alphag = RoughnessToGAlpha(roughness);

    float totalWeight = 0.f;
    float3 result = float3(0.0f, 0.0f, 0.0f);
    LOOP_DIRECTIVE for (uint s=0u; s<passSampleCount; ++s) {

        // Ok, here's where it gets complex. We have a "corei" direction,
        //  -- "i" direction when H == normal. However, there are many
        // different values for "normal" that we can use; each will give
        // a different "ot" and a different sampling of H values.
        //
        // Let's take a sampling approach for "normal" as well as H. Each
        // time through the loop we'll pick a random normal within a small
        // cone around corei. This is similar to SampleMicrofacetNormalGGX
        // an it will have an indirect effect on the distribution of
        // microfacet normals.
        //
        // It will also effect the value for "ot" that we get... But we want
        // the minimize the effect of "ot" in this calculation.

        float3 normal = SampleNormal(coreNormal, s, passSampleCount);

        // There seems to be a problem with this line...? This refraction step
        // is causing too much blurring, and "normal" doesn't seem to have much
        // effect
        // float3 ot = refract(-corei, -normal, iorIncident/iorOutgoing);

        float3 ot = CalculateTransmissionOutgoing(corei, normal, iorIncident, iorOutgoing);
        if (dot(ot, ot) == 0.0f) continue;      // no refraction solution

        //float3 test = refract(-ot, normal, iorOutgoing/iorIncident);
        //if (length(test - corei) > 0.001f)
        //    return float3(1, 0, 0);

#if 1
        precise float3 H = SampleMicrofacetNormalGGX(
            s*passCount+passIndex, passSampleCount*passCount,
            normal, alphad);

        // float3 ot = CalculateTransmissionOutgoing(i, H, iorIncident, iorOutgoing);
        float3 i;
        if (!CalculateTransmissionIncident(i, ot, H, iorIncident, iorOutgoing))
            continue;
#else
        precise float3 i = SampleMicrofacetNormalGGX(
            s*passCount+passIndex, passSampleCount*passCount,
            corei, alphad);
#endif

        float3 incidentLight = IBLPrecalc_SampleInputTexture(i);

#if 1
        float bsdf;
        bsdf = 1.f;
        bsdf *= SmithG(abs(dot( i,  normal)), RoughnessToGAlpha(roughness));
        bsdf *= SmithG(abs(dot(ot,  normal)), RoughnessToGAlpha(roughness));
        bsdf *= TrowReitzD(abs(dot( H, normal)), alphad);
        // bsdf *= Sq(iorOutgoing) / Sq(iorIncident * dot(i, H) - iorOutgoing * dot(ot, H));
        bsdf /= max(0.005f, 4.f * RefractionIncidentAngleDerivative2(dot(ot, H), iorIncident, iorOutgoing));
        bsdf *= abs(dot(i, H)) * abs(dot(ot, H));
        bsdf /= abs(dot(i, normal)) * abs(dot(ot, normal));
        //bsdf *= GGXTransmissionFresnel(
        //    i, viewDirection, specParam.F0.g,
        //    iorIncident, iorOutgoing);

        bsdf *= 1.0f - SchlickFresnelCore(saturate(dot(ot, H)));
        bsdf *= -dot(i, normal);

        float weight = bsdf * InversePDFWeight(H, coreNormal, float3(0.0f, 0.0f, 0.0f), alphad);
#endif

        result += incidentLight * weight;
        totalWeight += weight;
    }

    return result / (totalWeight + 1e-6f);
}

[numthreads(8, 8, 6)]
    void EquirectFilterGlossySpecularTrans(uint3 dispatchThreadId : SV_DispatchThreadID) : SV_Target0
{
    // Following the simplifications we use for split-sum specular reflections, here
    // is the equivalent sampling for specular transmission
    uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
	if (dispatchThreadId.x < textureDims.x && dispatchThreadId.y < textureDims.y) {
        float2 texCoord = dispatchThreadId.xy / float2(textureDims);
        float3 cubeMapDirection = CalculateCubeMapDirection(dispatchThreadId.z, texCoord);
        float roughness = MipmapToRoughness(FilterPassParams.MipIndex);
        float iorIncident = SpecularTransmissionIndexOfRefraction;
        float iorOutgoing = 1.f;
        const uint PassSampleCount = 1024;
        float3 r = CalculateFilteredTextureTrans(
            cubeMapDirection, roughness,
            iorIncident, iorOutgoing,
            PassSampleCount, FilterPassParams.PassIndex, FilterPassParams.PassCount);
        if (FilterPassParams.PassIndex == 0) OutputArray[dispatchThreadId.xyz] = float4(0,0,0,1);
        OutputArray[dispatchThreadId.xyz].rgb += r / float(FilterPassParams.PassCount);
    }
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

float FilterDiffuse_BRDF_costheta(
    out float pdf,
    float3 N, float3 L)
{
    // This function is patterned after FilterGlossySpecular_BRDF_costheta, see related comments there
    float NdotL = saturate(dot(N, L));
    pdf = 1;
    return NdotL / pi;
}

[numthreads(8, 8, 1)]
    void EquirectFilterDiffuse_Reference(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);
    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 6), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, arrayLayerCount))) return;
    if (helper._firstDispatch) OutputArray[helper._outputPixel] = 0;

    uint2 inputTextureDims;
	Input.GetDimensions(inputTextureDims.x, inputTextureDims.y);
    float2 reciprocalInputTextureDims = float2(1/float(inputTextureDims.x), 1/float(inputTextureDims.y));

    // The features in the filtered map are clearly biased to one direction in mip maps unless we add half a pixel here
    float2 texCoord = (helper._outputPixel.xy + 0.5.xx) / float2(textureDims);
    float3 cubeMapDirection = CalculateCubeMapDirection(helper._outputPixel.z, texCoord);

    // Basic sampling for reference purposes. We're going to sample every pixel from the input texture
    float3 value = 0;
    for (uint t=0; t<helper._thisPassSampleCount; ++t) {
        uint globalTap = t+helper._thisPassSampleOffset;

        uint2 inputXY = uint2(globalTap%inputTextureDims.x, globalTap/inputTextureDims.x);
        float2 inputTextureUV = inputXY * reciprocalInputTextureDims;

        float3 L = EquirectangularCoordToDirection_YUp(inputTextureUV);

        // 2 different mathematical approaches to arrive at the same result
        #if 1
            float filtering_pdf;
            float brdf_costheta = FilterDiffuse_BRDF_costheta(filtering_pdf, cubeMapDirection, L);
            if (brdf_costheta <= 0 || inputTextureUV.y == 0) continue;

            // light_pdf here effectively takes into account the different sizes of the texels in the equirectangular texture
            float light_pdf = 1 / (2 * pi * pi * sin(pi * inputTextureUV.y));        
            value += Input[inputXY].rgb * brdf_costheta / light_pdf * reciprocalInputTextureDims.x * reciprocalInputTextureDims.y;
        #else
            float texelAreaWeight = (4*pi*pi)/(2.f*inputTextureDims.x*inputTextureDims.y);
            float verticalDistortion = sin(pi * (float(inputXY.y)+0.5f) / float(inputTextureDims.y));
            texelAreaWeight *= verticalDistortion;

            float cosFilter = saturate(dot(L, cubeMapDirection)) / pi;
            value += texelAreaWeight * cosFilter * Input[inputXY].rgb;
        #endif
    }

    OutputArray[helper._outputPixel].rgb += value;
}

////////////////////////////////////////////////////////////////////////////////

// Take an input equirectangular input texture and generate the spherical
// harmonic coefficients that best represent it.
groupshared float4 ProjectToSphericalHarmonic_SharedWorking[64];
[numthreads(64, 1, 1)]
    void ProjectToSphericalHarmonic(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) : SV_Target0
{
    uint index = groupId.x;
	float weightAccum = 0.0f;
    float3 result = float3(0, 0, 0);

	// This function will attempt to build the spherical coefficients that best fit the
	// input environment map.
	//
	// We can use this directly as an approximation for the diffuse lighting (ie, by assuming that
	// the blurring that is given by the spherical harmonic equations roughly match the cosine
	// lobe associated with lambert diffuse).
	//
	// Or, alternatively, we can factor in the cosine lobe during the resolve step.

    uint2 inputDims;
    Input.GetDimensions(inputDims.x, inputDims.y);
    for (uint q=0; q<(inputDims.y+63)/64; ++q) {
        uint y=q*64+groupThreadId.x;
        if (y >= inputDims.y) break;

        // Let's weight the texel area based on the solid angle of each texel.
        // The accumulated weight should total 4*pi, which is the total solid angle
        // across a sphere in steradians.
        //
        // The solid angle varies for each row of the input texture. The integral
        // of the "verticalDistortion" equation is 2.
        //

        // float texelAreaWeight = 1.0f/(inputDims.x*inputDims.y); // (2.0f * pi / inputDims.x) * (pi / inputDims.y);
        const float texelAreaWeightBase = (4*pi*pi)/(2.f*inputDims.x*inputDims.y);
        float verticalDistortion = sin(pi * (y+0.5f) / float(inputDims.y));
        float texelAreaWeight = texelAreaWeightBase * verticalDistortion;

        for (uint x=0; x<inputDims.x; ++x) {
            float3 sampleDirection = EquirectangularCoordToDirection_YUp(float2(x, y) / float2(inputDims));

            float value = EvalSHBasis(index, sampleDirection);
            result += texelAreaWeight * value * Input.Load(uint3(x, y, 0)).rgb;

            weightAccum += texelAreaWeight;
        }
    }

    ProjectToSphericalHarmonic_SharedWorking[groupThreadId.x].rgb = result;

    AllMemoryBarrierWithGroupSync();
    if (groupThreadId.x == 0) {
        result = 0;
        for (uint c=0; c<64; ++c)
            result += ProjectToSphericalHarmonic_SharedWorking[c].rgb;
        
        // we should expect weightAccum to be exactly 4*pi here
        // Output[dispatchThreadId.xy] = float4((4*pi)/weightAccum.xxx, 1.0);
        Output[groupId.xy] = float4(result, 1.0f);    // note -- weighted by 4*pi steradians
    }
}

// These are the band factors from Peter-Pike Sloan's paper, via S�bastien Lagarde's modified cubemapgen
// They are a normalized cosine lobe premultiplied by the factor used in modulating by a zonal harmonic
static const float SHBandFactor[] =
{
	1.0,
	2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0,
	1.0 / 4.0, 1.0 / 4.0, 1.0 / 4.0, 1.0 / 4.0, 1.0 / 4.0,
	0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
	- 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0, - 1.0 / 24.0
};

float3 ResolveSH(float3 direction)
{
    const uint coefficientCount = 9;
    const bool useSharedCodePath = false;
    if (!useSharedCodePath) {
    	float3 result = float3(0,0,0);

    	for (uint c=0; c<coefficientCount; ++c) {
            #if 0
    		      result += Input.Load(uint3(c,0,0)).rgb * EvalSHBasis(c, direction) * SHBandFactor[c];
            #elif 0
                result += Input.Load(uint3(c,0,0)).rgb * EvalSHBasis(c, v);
            #else
                // Using Peter-Pike Sloan's formula for rotating a zonal harmonic
                // See the section on Zonal Harmonics in Stupid Spherical Harmonics tricks
                // The coefficients of the zonal harmonic are a normalized cosine lobe
                // Also note constant factor associated with modulating by the rotated zonal harmonic
                // This demonstrates how the critical "SHBandFactor" parameters are de
                float rsqrtPi = rsqrt(pi);
                float z[] = { .5 * rsqrtPi, sqrt(3)/3.0 * rsqrtPi, sqrt(5)/8.0f * rsqrtPi, 0, -1/16.0f * rsqrtPi };
                uint l = (c>=16) ? 4 : ((c>=9) ? 3 : ((c>=4) ? 2 : ((c>=1) ? 1 : 0)));
                float A = sqrt(4 * pi / (2*float(l)+1));
                float f = A * z[l] * EvalSHBasis(c, direction);
                result += Input.Load(uint3(c,0,0)).rgb * f;

                // note -- "B" is "A" evaluated for the first few bands
                //      and C[i] is z[i] * B[i] (which is equal to SHBandFactor)
                float B[] = { 2*sqrt(pi), 2*sqrt(pi)/sqrt(3.0f), 2*sqrt(pi)/sqrt(5.0f), 2*sqrt(pi)/sqrt(7.0), 2*sqrt(pi)/sqrt(9.0) };
                float C[] = { 1.0f, 2/3, 1/4, 0, 1/24 };
            #endif
    	}
        return result;
    } else {
        float3 result = 0;
        for (uint c=0; c<coefficientCount; ++c)
            result += ResolveSH_Reference(Input.Load(uint3(c,0,0)).rgb, c, direction);
        return result;
    }
}

float4 ResolveSphericalHarmonic(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    uint2 dims = uint2(position.xy / texCoord);
    float3 D = EquirectangularCoordToDirection_YUp(float2(position.xy) / float2(dims));
	return float4(ResolveSH(D), 1.0f);
}

float4 ResolveSphericalHarmonicToCubeMap(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    const uint ArrayIndex = 0;

    float3 cubeMapDirection = CalculateCubeMapDirection(ArrayIndex, texCoord);
	return float4(ResolveSH(cubeMapDirection), 1.0f);
}

#if 0
void RotateOrder3SH(float input[9], float output[9], float3x3 rotationMatrix)
{
    // Rotate an order-3 spherical harmonic coefficients through the
    // given rotation matrix.
    // We have to do 3 bands:
    //  1st is unmodified
    //  2nd is just a permutation of the basic rotation matrix
    //  3rd is 5x5 matrix which will requires a few calculations

    output[0] = input[1];   // (first band)

    float3x3 band2Rotation = float3x3(
        float3( rotationMatrix[1][1], -rotationMatrix[1][2],  rotationMatrix[1][0]),
        float3(-rotationMatrix[2][1],  rotationMatrix[2][2], -rotationMatrix[2][0]),
        float3( rotationMatrix[0][1], -rotationMatrix[0][2],  rotationMatrix[0][0]));
    float3 t = mul(band2Rotation, float3(output[1], output[2], output[3]));
    output[1] = t.x;
    output[2] = t.y;
    output[3] = t.z;

    for (uint c=0; c<5; ++c) output[4+c] = input[4+c];
}

float4 ResolveSphericalHarmonic2(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    uint2 dims = uint2(position.xy / texCoord);
    float3 D = EquirectangularCoordToDirection_YUp(uint2(position.xy), dims);
    float3 radial = CartesianToSpherical_YUp(D);

    float3x3 aroundY = float3x3(
        float3(cos(radial.y), 0, sin(radial.y)),
        float3(0, 1, 0),
        float3(-sin(radial.y), 0, cos(radial.y)));
    float3x3 aroundZ = float3x3(
        float3(cos(radial.x), -sin(radial.x), 0),
        float3(sin(radial.x), cos(radial.x), 0),
        float3(0, 0, 1));

    float3x3 rotationMatrix = mul(aroundZ, aroundY);
    D = mul(rotationMatrix, float3(0,0,1));

    float3 result = float3(0,0,0);
    for (uint c=0; c<coefficientCount; ++c) {
		result += Input.Load(uint3(c,0,0)).rgb * EvalSHBasis(c, D) * SHBandFactor[c];
	}

	return float4(result, 1.0f);
}
#endif

