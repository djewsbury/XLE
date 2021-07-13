// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define FORCE_GGX_REF

#include "Cubemap.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/SphericalHarmonics.hlsl"

Texture2D Input : register(t0, space0);
RWTexture2DArray<float4> OutputArray : register(u1, space0);
RWTexture2D<float4> Output : register(u1, space0);
SamplerState EquirectangularBilinearSampler : register(s2, space0);

// static const uint PassSampleCount = 256;
static const uint PassSampleCount = 16;
cbuffer FilterPassParams : register(b3, space0)
{
    uint MipIndex;
    uint PassIndex, PassCount;
};

float3 IBLPrecalc_SampleInputTexture(float3 direction)
{
    float2 coord = DirectionToEquirectangularCoord_YUp(direction);
    return Input.SampleLevel(EquirectangularBilinearSampler, coord, 0).rgb;
}

#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

[numthreads(8, 8, 6)]
    void GenerateSplitSumGlossLUT(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    // float2 dims = position.xy / texCoord.xy;

    uint2 textureDims;
	Output.GetDimensions(textureDims.x, textureDims.y);
    float2 texCoord = dispatchThreadId.xy / float2(textureDims);
    float NdotV = texCoord.x + (.1/float(textureDims.x));  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;
    Output[dispatchThreadId.xy] = float4(GenerateSplitTerm(NdotV, roughness, sampleCount, 0, 1), 0, 1);
}

float4 GenerateSplitSumGlossTransmissionLUT(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float NdotV = texCoord.x;
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;

    const uint ArrayIndex = 0;

    float specular = saturate(0.05f + ArrayIndex / 32.f);
    float iorIncident = F0ToRefractiveIndex(SpecularParameterToF0(specular));
    float iorOutgoing = 1.f;
    return float4(GenerateSplitTermTrans(NdotV, roughness, iorIncident, iorOutgoing, sampleCount, 0, 1), 0, 0, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

[numthreads(8, 8, 6)]
    void EquiRectFilterGlossySpecular(uint3 dispatchThreadId : SV_DispatchThreadID) : SV_Target0
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
	if (dispatchThreadId.x < textureDims.x && dispatchThreadId.y < textureDims.y) {
        float2 texCoord = dispatchThreadId.xy / float2(textureDims);
        float3 cubeMapDirection = CalculateCubeMapDirection(dispatchThreadId.z, texCoord);
        float roughness = MipmapToRoughness(MipIndex);
        float3 r = GenerateFilteredSpecular(
            cubeMapDirection, roughness,
            PassSampleCount, PassIndex, PassCount);
        if (PassIndex == 0) OutputArray[dispatchThreadId.xyz] = float4(0,0,0,1);
        OutputArray[dispatchThreadId.xyz].rgb += r / float(PassCount);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

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
        float roughness = MipmapToRoughness(MipIndex);
        float iorIncident = SpecularTransmissionIndexOfRefraction;
        float iorOutgoing = 1.f;
        float3 r = CalculateFilteredTextureTrans(
            cubeMapDirection, roughness,
            iorIncident, iorOutgoing,
            PassSampleCount, PassIndex, PassCount);
        if (PassIndex == 0) OutputArray[dispatchThreadId.xyz] = float4(0,0,0,1);
        OutputArray[dispatchThreadId.xyz].rgb += r / float(PassCount);
        OutputArray[dispatchThreadId.xyz].rgb = 1.0.xxx;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float4 ReferenceDiffuseFilter(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    uint2 dims = uint2(position.xy / texCoord);
    float3 direction = EquirectangularCoordToDirection_YUp(float2(position.xy) / float2(dims));

    float3 result = float3(0,0,0);

    uint2 inputDims;
    Input.GetDimensions(inputDims.x, inputDims.y);
    for (uint y=0; y<inputDims.y; ++y) {
		float texelAreaWeight = (4*pi*pi)/(2.f*inputDims.x*inputDims.y);
		float verticalDistortion = sin(pi * (float(y)+0.5f) / float(inputDims.y));
        texelAreaWeight *= verticalDistortion;

        for (uint x=0; x<inputDims.x; ++x) {
            float3 sampleDirection = EquirectangularCoordToDirection_YUp(float2(x, y) / float2(inputDims));
            float cosFilter = max(0.0, dot(sampleDirection, direction)) / pi;

            result += texelAreaWeight * cosFilter * Input.Load(uint3(x, y, 0)).rgb;
        }
    }

    return float4(result, 1.0f);    // note -- weighted by 4*pi steradians

    // float2 back = EquirectangularMappingCoord(direction);
    // float2 diff = back - float2(position.xy) / float2(dims);
    // return float4(abs(diff.xy), 0, 1);
}


////////////////////////////////////////////////////////////////////////////////

static const uint coefficientCount = 25;

// Take an input equirectangular input texture and generate the spherical
// harmonic coefficients that best represent it.
float4 ProjectToSphericalHarmonic(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    // We're only going to support the first 3 orders; which means we generate 9 coefficients

    uint index = uint(position.x)%coefficientCount;
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
    for (uint y=0; y<inputDims.y; ++y) {

		// Let's weight the texel area based on the solid angle of each texel.
		// The accumulated weight should total 4*pi, which is the total solid angle
		// across a sphere in steradians.
		//
		// The solid angle varies for each row of the input texture. The integral
		// of the "verticalDistortion" equation is 2.
		//

        // float texelAreaWeight = 1.0f/(inputDims.x*inputDims.y); // (2.0f * pi / inputDims.x) * (pi / inputDims.y);
		float texelAreaWeight = (4*pi*pi)/(2.f*inputDims.x*inputDims.y);
		float verticalDistortion = sin(pi * (y+0.5f) / float(inputDims.y));
        texelAreaWeight *= verticalDistortion;

        for (uint x=0; x<inputDims.x; ++x) {
            float3 sampleDirection = EquirectangularCoordToDirection_YUp(float2(x, y) / float2(inputDims));

			float value = EvalSHBasis(index, sampleDirection);
            result += (texelAreaWeight) * value * Input.Load(uint3(x, y, 0)).rgb;

			weightAccum += texelAreaWeight;
        }
    }

	// we should expect weightAccum to be exactly 4*pi here
	// return float4((4*pi)/weightAccum.xxx, 1.0);
    return float4(result, 1.0f);    // note -- weighted by 4*pi steradians
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
    const bool useSharedCodePath = false;
    if (!useSharedCodePath) {
    	const uint coefficients = 25;
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
        float3 coefficients[9];
        for (uint c=0; c<9; ++c) coefficients[c] = Input.Load(uint3(c,0,0)).rgb;
        return ResolveSH_Reference(coefficients, direction);
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

