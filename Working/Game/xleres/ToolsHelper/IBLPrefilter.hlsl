// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define FORCE_GGX_REF

#include "Cubemap.hlsl"
#include "sampling-shader-helper.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/SphericalHarmonics.hlsl"
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

float3 IBLPrecalc_SampleInputTexture(float3 direction)
{
    float2 coord = DirectionToEquirectangularCoord_YUp(direction);
    return Input.SampleLevel(EquirectangularBilinearSampler, coord, 0).rgb;
}

#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////

#include "../Foreign/ffx-reflection-dnsr/ffx_denoiser_reflections_common.h"

float2 SampleUV(out float pdf, float2 xi, uint2 marginalCDFDims)
{
    // Search for the v coordinate
    uint l=0, h=marginalCDFDims.y;
    while (true) {
        uint m = (l+h)>>1;
        float midCDF = MarginalVerticalCDF[m];
        if (xi.y < midCDF) {
            h = m;
        } else {
            l = m;
        }
        if ((l+3)>=h) break;
    }

    float v;
    uint y;
    float workingPdf;
    if (xi.y < MarginalVerticalCDF[l+1]) {
        float range = MarginalVerticalCDF[l+1] - MarginalVerticalCDF[l];
        v = lerp(l, l+1, (xi.y - MarginalVerticalCDF[l]) / range)/marginalCDFDims.y;
        y = l;
        workingPdf = range;
    } else if (xi.y < MarginalVerticalCDF[l+2]) {
        float range = MarginalVerticalCDF[l+2] - MarginalVerticalCDF[l+1];
        v = lerp(l+1, l+2, (xi.y - MarginalVerticalCDF[l+1]) / range)/marginalCDFDims.y;
        y = l+1;
        workingPdf = range;
    } else {
        // xi.y must be smaller than MarginalVerticalCDF[l+3], though l+3 might be off the end
        float range = (((l+3) == marginalCDFDims.y) ? 1.0 : MarginalVerticalCDF[l+3]) - MarginalVerticalCDF[l+2];
        v = lerp(l+2, l+3, (xi.y - MarginalVerticalCDF[l+2]) / range)/marginalCDFDims.y;
        y = l+2;
        workingPdf = range;
    }

    // search for the u coordinate
    l=0; h=marginalCDFDims.x;
    while (true) {
        uint m = (l+h)>>1;
        float midCDF = MarginalHorizontalCDF[uint2(m, y)];
        if (xi.x < midCDF) {
            h = m;
        } else {
            l = m;
        }
        if ((l+3)>=h) break;
    }

    float u;
    if (xi.y < MarginalHorizontalCDF[uint2(l+1, y)]) {
        float range = MarginalHorizontalCDF[uint2(l+1, y)] - MarginalHorizontalCDF[uint2(l, y)];
        u = lerp(l, l+1, (xi.y - MarginalHorizontalCDF[uint2(l,y)]) / range)/marginalCDFDims.y;
        workingPdf *= range;
    } else if (xi.y < MarginalHorizontalCDF[uint2(l+2, y)]) {
        float range = MarginalHorizontalCDF[uint2(l+2, y)] - MarginalHorizontalCDF[uint2(l+1, y)];
        u = lerp(l+1, l+2, (xi.y - MarginalHorizontalCDF[uint2(l+1,y)]) / range)/marginalCDFDims.y;
        workingPdf *= range;
    } else {
        // xi.y must be smaller than MarginalHorizontalCDF[uint2(l+3,y)], though l+3 might be off the end
        float range = (((l+3) == marginalCDFDims.x) ? 1.0 : MarginalHorizontalCDF[uint2(l+3,y)]) - MarginalHorizontalCDF[uint2(l+2,y)];
        u = lerp(l+2, l+3, (xi.y - MarginalHorizontalCDF[uint2(l+2,y)]) / range)/marginalCDFDims.y;
        workingPdf *= range;
    }

    pdf = workingPdf;
    return float2(u, v);
}

static float RadicalInverseBase5(uint a)
{
    uint Base = 5;
    float reciprocalBase = 1.0 / Base;
    uint reversedDigits = 0;
    float reciprocalBaseN = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        reciprocalBaseN *= reciprocalBase;
        a = next;
    }
    return reversedDigits * reciprocalBaseN;
}

static float RadicalInverseBase7(uint a)
{
    uint Base = 7;
    float reciprocalBase = 1.0 / Base;
    uint reversedDigits = 0;
    float reciprocalBaseN = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        reciprocalBaseN *= reciprocalBase;
        a = next;
    }
    return reversedDigits * reciprocalBaseN;
}

groupshared float4 EquiRectFilterGlossySpecular_SharedWorking[64];
[numthreads(8, 8, 1)]
    void EquiRectFilterGlossySpecular(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
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
    uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);

#if 0
    uint passesPerPixel = FilterPassParams.PassCount/(textureDims.x*textureDims.y*6);
    uint3 pixelId;
    uint linearPixel = FilterPassParams.PassIndex%(textureDims.x*textureDims.y*6);
    uint passOfThisPixel = FilterPassParams.PassIndex/(textureDims.x*textureDims.y*6);
    if (textureDims.x >= 8 && textureDims.y >= 8) {
        uint blockWidth = ((textureDims.x+7)/8), blockHeight = ((textureDims.y+7)/8);
        uint2 pixelInBlock = FFX_DNSR_Reflections_RemapLane8x8(linearPixel%64);
        uint linearBlock = (FilterPassParams.PassIndex/64)%(blockWidth*blockHeight*6);

        pixelId = uint3(
            (linearBlock%blockWidth)*8+pixelInBlock.x, 
            ((linearBlock/blockWidth)%blockHeight)*8+pixelInBlock.y, 
            linearBlock/(blockWidth*blockHeight));
    } else {
        pixelId = uint3(
            linearPixel%textureDims.x, 
            (linearPixel/textureDims.x)%textureDims.y, 
            linearPixel/(textureDims.x*textureDims.y));
    }

	if (pixelId.x < textureDims.x && pixelId.y < textureDims.y && pixelId.z < 6) {
        // The features in the filtered map are clearly biased to one direction in mip maps unless we add half a pixel here
        float2 texCoord = (pixelId.xy + 0.5.xx) / float2(textureDims);
        float3 cubeMapDirection = CalculateCubeMapDirection(pixelId.z, texCoord);
        int log2dim = firstbithigh(textureDims.x);
        float roughness = MipmapToRoughness(SpecularIBLMipMapCount-log2dim);

        const uint samplesPerPassCount = 1024;
        EquiRectFilterGlossySpecular_SharedWorking[groupThreadId.x].rgb = GenerateFilteredSpecular(
            cubeMapDirection, roughness,
            samplesPerPassCount, groupThreadId.x + passOfThisPixel*64, passesPerPixel*64);

        //////////////////////////////////
        // Sync, and then combine together the results from all of the samples
        AllMemoryBarrierWithGroupSync();
        if (groupThreadId.x == 0) {
            if (passOfThisPixel == 0)
                OutputArray[pixelId.xyz] = float4(0,0,0,1);
            float3 result = 0;
            for (uint c=0; c<64; ++c)
                result.rgb += EquiRectFilterGlossySpecular_SharedWorking[c].rgb/(64.0f*passesPerPixel);
            OutputArray[pixelId.xyz].rgb += result;
        }
    }
#endif

    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 1), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, 1))) return;
    if (helper._firstDispatch) OutputArray[uint3(helper._outputPixel.xy, 0)] = 0;

    uint2 marginalCDFDims;
	MarginalHorizontalCDF.GetDimensions(marginalCDFDims.x, marginalCDFDims.y);

    uint2 inputTextureDims;
	Input.GetDimensions(inputTextureDims.x, inputTextureDims.y);

    // OutputArray[uint3(helper._outputPixel.xy, 0)] = MarginalHorizontalCDF[helper._outputPixel.xy/float2(textureDims)*marginalCDFDims.xy];
    // return;

    // OutputArray[uint3(helper._outputPixel.xy, 0)] = Input[helper._outputPixel.xy/float2(textureDims)*inputTextureDims.xy];
    // return;

    float3 value = 0;
    for (uint t=0; t<helper._thisPassSampleCount; ++t) {
        uint globalTap = t*helper._thisPassSampleStride+helper._thisPassSampleOffset;
        uint samplerIdx = SampleIndexLookup[helper._outputPixel.xy] + HaltonSamplerRepeatingStride * globalTap;
        // Using the Halton sequence in such a straightforward way as this is not going to be efficient; but we don't require a perfectly
        // optimal solution here
        float2 xi = float2(RadicalInverseBase5(samplerIdx), RadicalInverseBase7(samplerIdx));
        float pdf;
        float2 uv = SampleUV(pdf, xi, marginalCDFDims);

        value += Input[uv * inputTextureDims].rgb / helper._totalSampleCount; // / pdf;
    }

    OutputArray[uint3(helper._outputPixel.xy, 0)].rgb += value;
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

    uint2 blockDims = inputDims / outputDims;
    float blockBrightness = 0.0;
    for (uint y=0; y<blockDims.y; ++y)
        for (uint x=0; x<blockDims.x; ++x)
            blockBrightness += Brightness(Input[dispatchThreadId.xy*blockDims+uint2(x, y)].rgb);
    MarginalHorizontalCDF[dispatchThreadId.xy] = blockBrightness / float(blockDims.x * blockDims.y);
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

#if 0
[numthreads(8, 8, 6)]
    void EquiRectFilterGlossySpecularTrans(uint3 dispatchThreadId : SV_DispatchThreadID) : SV_Target0
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

///////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(64, 1, 1)]
    void ReferenceDiffuseFilter(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);

    uint3 pixelId = uint3(
        FilterPassParams.PassIndex%textureDims.x, 
        (FilterPassParams.PassIndex/textureDims.x)%textureDims.y, 
        groupId.z);
	if (pixelId.x < textureDims.x && pixelId.y < textureDims.y && pixelId.z < 6) {

        uint2 inputDims;
        Input.GetDimensions(inputDims.x, inputDims.y);

        float2 texCoord = (pixelId.xy + 0.5.xx) / float2(textureDims);
        float3 normalDirection = CalculateCubeMapDirection(pixelId.z, texCoord);
        float3 result = float3(0,0,0);

        for (uint q=0; q<(inputDims.y+63)/64; ++q) {
            uint y=q*64+groupThreadId.x;
            if (y >= inputDims.y) break;

            float texelAreaWeight = (4*pi*pi)/(2.f*inputDims.x*inputDims.y);
            float verticalDistortion = sin(pi * (float(y)+0.5f) / float(inputDims.y));
            texelAreaWeight *= verticalDistortion;

            for (uint x=0; x<inputDims.x; ++x) {
                float3 sampleDirection = EquirectangularCoordToDirection_YUp(float2(x, y) / float2(inputDims));
                float cosFilter = max(0.0, dot(sampleDirection, normalDirection)) / pi;
                [branch] if (cosFilter > 0)
                    result += texelAreaWeight * cosFilter * Input.Load(uint3(x, y, 0)).rgb;
            }
        }
        EquiRectFilterGlossySpecular_SharedWorking[groupThreadId.x].rgb = result;

        AllMemoryBarrierWithGroupSync();
        if (groupThreadId.x == 0) {
            float4 result = float4(0,0,0,1);
            for (uint c=0; c<64; ++c)
                result.rgb += EquiRectFilterGlossySpecular_SharedWorking[c].rgb;
            OutputArray[pixelId.xyz] = result;    // note -- weighted by 4*pi steradians
        }
    }

    // float2 back = EquirectangularMappingCoord(direction);
    // float2 diff = back - float2(position.xy) / float2(dims);
    // return float4(abs(diff.xy), 0, 1);
}
#endif

////////////////////////////////////////////////////////////////////////////////

// Take an input equirectangular input texture and generate the spherical
// harmonic coefficients that best represent it.
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

    EquiRectFilterGlossySpecular_SharedWorking[groupThreadId.x].rgb = result;

    AllMemoryBarrierWithGroupSync();
    if (groupThreadId.x == 0) {
        result = 0;
        for (uint c=0; c<64; ++c)
            result += EquiRectFilterGlossySpecular_SharedWorking[c].rgb;
        
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

