// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(TEXTURE_ALGORITHM_H)
#define TEXTURE_ALGORITHM_H

#if defined(MSAA_SAMPLERS) && (MSAA_SAMPLERS != 0)
	#define Texture2D_MaybeMS	Texture2DMS
#else
 	#define Texture2D_MaybeMS	Texture2D
#endif

struct SystemInputs
{
	#if MSAA_SAMPLES > 1
		uint sampleIndex : SV_SampleIndex;
	#endif
};

SystemInputs SystemInputs_Default()
{
    SystemInputs result;
    #if MSAA_SAMPLES > 1
        result.sampleIndex = 0;
    #endif
    return result;
}

SystemInputs SystemInputs_SampleIndex(uint sampleIndex)
{
	SystemInputs result;
	#if MSAA_SAMPLES > 1
		result.sampleIndex = sampleIndex;
	#endif
	return result;
}

#if MSAA_SAMPLES > 1
	uint GetSampleIndex(SystemInputs inputs) { return inputs.sampleIndex; }
#else
	uint GetSampleIndex(SystemInputs inputs) { return 0; }
#endif

float4 LoadFloat4(Texture2DMS<float4> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(pixelCoords, sampleIndex);
}

float LoadFloat1(Texture2DMS<float> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(pixelCoords, sampleIndex);
}

uint LoadUInt1(Texture2DMS<uint> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(pixelCoords, sampleIndex);
}

float4 LoadFloat4(Texture2D<float4> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(int3(pixelCoords, 0));
}

float LoadFloat1(Texture2D<float> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(int3(pixelCoords, 0));
}

uint LoadUInt1(Texture2D<uint> textureObject, int2 pixelCoords, int sampleIndex)
{
	return textureObject.Load(int3(pixelCoords, 0));
}


float4 SampleFloat4(Texture2DMS<float4> textureObject, SamplerState smp, float2 tc, int sampleIndex)
{
    uint2 dimensions; uint sampleCount;
    textureObject.GetDimensions(dimensions.x, dimensions.y, sampleCount);
	return textureObject.Load(tc*dimensions.xy, sampleIndex);
}

float SampleFloat1(Texture2DMS<float> textureObject, SamplerState smp, float2 tc, int sampleIndex)
{
    uint2 dimensions; uint sampleCount;
    textureObject.GetDimensions(dimensions.x, dimensions.y, sampleCount);
	return textureObject.Load(tc*dimensions.xy, sampleIndex);
}

float4 SampleFloat4(Texture2D<float4> textureObject, SamplerState smp, float2 tc, int sampleIndex)
{
	return textureObject.SampleLevel(smp, tc, 0);
}

float SampleFloat1(Texture2D<float> textureObject, SamplerState smp, float2 tc, int sampleIndex)
{
	return textureObject.SampleLevel(smp, tc, 0);
}

// Source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
// License: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
float3 SampleCatmullRomFloat3(Texture2D<float3> tex, SamplerState bilinearSampler, in float2 uv, in float2 texelSize)
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv / texelSize;
    float2 texPos1   = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12      = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0  = texPos1 - 1.0f;
    float2 texPos3  = texPos1 + 2.0f;
    float2 texPos12 = texPos1 + offset12;

    texPos0  *= texelSize;
    texPos3  *= texelSize;
    texPos12 *= texelSize;

    float3 result = float3(0.0f, 0.0f, 0.0f);

    result += tex.SampleLevel(bilinearSampler, float2(texPos0.x,  texPos0.y),  0.0f).xyz * w0.x  * w0.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos12.x, texPos0.y),  0.0f).xyz * w12.x * w0.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos3.x,  texPos0.y),  0.0f).xyz * w3.x  * w0.y;

    result += tex.SampleLevel(bilinearSampler, float2(texPos0.x,  texPos12.y), 0.0f).xyz * w0.x  * w12.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos12.x, texPos12.y), 0.0f).xyz * w12.x * w12.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos3.x,  texPos12.y), 0.0f).xyz * w3.x  * w12.y;

    result += tex.SampleLevel(bilinearSampler, float2(texPos0.x,  texPos3.y),  0.0f).xyz * w0.x  * w3.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos12.x, texPos3.y),  0.0f).xyz * w12.x * w3.y;
    result += tex.SampleLevel(bilinearSampler, float2(texPos3.x,  texPos3.y),  0.0f).xyz * w3.x  * w3.y;

    return max(result, 0.0f);
}

#endif
