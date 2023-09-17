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

#endif
