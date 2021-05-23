// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SSConstants.hlsl"
#include "../MaterialQuery.hlsl"
#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../TechniqueLibrary/Math/TextureAlgorithm.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"

// #define USE_UINT_DBUFFER

#if defined(USE_UINT_DBUFFER)
	Texture2D_MaybeMS<uint>			DepthTexture : register(t3);
#else
	Texture2D_MaybeMS<float>		DepthTexture : register(t3);
#endif
Texture2D_MaybeMS<float4>			GBuffer_Diffuse : register(t0);		// we need the diffuse buffer for roughness (in alpha) and metal F0
Texture2D_MaybeMS<float4>			GBuffer_Normals : register(t1);

#if (HAS_PROPERTIES_BUFFER==1)
	Texture2D_MaybeMS<float4>		GBuffer_Params : register(t2);
#endif

// #define DEPTH_IN_LINEAR_COORDS

GBufferValues Sample(uint2 position, uint sampleIndex)
{
	GBufferEncoded encoded;
	encoded.diffuseBuffer = LoadFloat4(GBuffer_Diffuse, position, sampleIndex);
	encoded.normalBuffer = LoadFloat4(GBuffer_Normals, position, sampleIndex);
	#if (HAS_PROPERTIES_BUFFER==1)
		encoded.propertiesBuffer = LoadFloat4(GBuffer_Params, position, sampleIndex);
	#endif
	return Decode(encoded);
}

float SampleDepth(uint2 position, uint sampleIndex)
{
	#if defined(DEPTH_IN_LINEAR_COORDS)
		return NDCDepthToLinear0To1(LoadFloat1(DepthTexture, position, sampleIndex));
	#else
		return LoadFloat1(DepthTexture, position, sampleIndex);
	#endif
}

float ApproxF0(GBufferValues sample)
{
	if (sample.material.roughness > RoughnessThreshold) return 0.f;
	float metalF0 = 0.33f * (sample.diffuseAlbedo.r + sample.diffuseAlbedo.g + sample.diffuseAlbedo.b);
	return lerp(Material_GetF0_0(sample), metalF0, Material_GetMetal(sample));
}

void main(
	#if defined(USE_UINT_DBUFFER)
		out uint outputDepth	: SV_Target0,
	#else
		out float outputDepth	: SV_Target0,
	#endif
	out float4 outputNormal : SV_Target1,
	float4 position			: SV_Position,
	float2 oTexCoord		: TEXCOORD0)
{

	const uint sampleIndex = 0;

		//			If there's too much discontinuity of the normal, then we
		//			shouldn't do reflections from here.
		//			Normals will be bent to wierd angles for pixels with lots
		//			of discontinuity -- in theses cases, just skip reflections

		//	note -- we could also use the texture sampler for this blend...?
#if DOWNSAMPLE_SCALE == 4

	GBufferValues sample0 = Sample(uint2(position.xy)*4 + uint2(0,0), sampleIndex);
	GBufferValues sample1 = Sample(uint2(position.xy)*4 + uint2(2,0), sampleIndex);
	GBufferValues sample2 = Sample(uint2(position.xy)*4 + uint2(0,2), sampleIndex);
	GBufferValues sample3 = Sample(uint2(position.xy)*4 + uint2(2,2), sampleIndex);
	float3 normal0 = sample0.worldSpaceNormal;
	float3 normal1 = sample1.worldSpaceNormal;
	float3 normal2 = sample2.worldSpaceNormal;
	float3 normal3 = sample3.worldSpaceNormal;

	if (	dot(normal0, normal1) <  NormalDiscontinuityThreshold
		||	dot(normal0, normal2) <  NormalDiscontinuityThreshold
		||	dot(normal0, normal3) <  NormalDiscontinuityThreshold) {
		outputNormal.xyz = 0.0.xxx;
	} else {
		outputNormal.xyz = normalize(normal0+normal1+normal2+normal3);
	}

	outputNormal.a = .25f * (ApproxF0(sample0) + ApproxF0(sample1) + ApproxF0(sample2) + ApproxF0(sample3));

	outputDepth =
		  SampleDepth(uint2(position.xy)*4 + uint2(0,0), sampleIndex)
		+ SampleDepth(uint2(position.xy)*4 + uint2(2,0), sampleIndex)
		+ SampleDepth(uint2(position.xy)*4 + uint2(0,2), sampleIndex)
		+ SampleDepth(uint2(position.xy)*4 + uint2(2,2), sampleIndex)
		;
	outputDepth *= 0.25f;

#elif DOWNSAMPLE_SCALE == 2

	GBufferValues sample0 = Sample(uint2(position.xy)*2 + uint2(0,0), sampleIndex);
	GBufferValues sample1 = Sample(uint2(position.xy)*2 + uint2(1,0), sampleIndex);
	GBufferValues sample2 = Sample(uint2(position.xy)*2 + uint2(0,1), sampleIndex);
	GBufferValues sample3 = Sample(uint2(position.xy)*2 + uint2(1,1), sampleIndex);
	float3 normal0 = sample0.worldSpaceNormal;
	float3 normal1 = sample1.worldSpaceNormal;
	float3 normal2 = sample2.worldSpaceNormal;
	float3 normal3 = sample3.worldSpaceNormal;

	if (	dot(normal0, normal1) <  NormalDiscontinuityThreshold
		||	dot(normal0, normal2) <  NormalDiscontinuityThreshold
		||	dot(normal0, normal3) <  NormalDiscontinuityThreshold) {
		outputNormal.xyz = 0.0.xxx;
	} else {
		outputNormal.xyz = normalize(normal0+normal1+normal2+normal3);
	}

	outputNormal.a = .25f * (ApproxF0(sample0) + ApproxF0(sample1) + ApproxF0(sample2) + ApproxF0(sample3));

	#if defined(DEPTH_IN_LINEAR_COORDS)
			//	convert depth values to linear values before blending
		outputDepth =
			  SampleDepth(uint2(position.xy)*2 + uint2(0,0), sampleIndex)
			+ SampleDepth(uint2(position.xy)*2 + uint2(1,0), sampleIndex)
			+ SampleDepth(uint2(position.xy)*2 + uint2(0,1), sampleIndex)
			+ SampleDepth(uint2(position.xy)*2 + uint2(1,1), sampleIndex)
			;
		outputDepth *= 0.25f;
	#else
		#if !defined(USE_UINT_DBUFFER)
			float d0 = LoadFloat1(DepthTexture, uint2(position.xy)*2 + uint2(0,0), sampleIndex);
			float d1 = LoadFloat1(DepthTexture, uint2(position.xy)*2 + uint2(1,0), sampleIndex);
			float d2 = LoadFloat1(DepthTexture, uint2(position.xy)*2 + uint2(0,1), sampleIndex);
			float d3 = LoadFloat1(DepthTexture, uint2(position.xy)*2 + uint2(1,1), sampleIndex);
			// outputDepth = (d0+d1+d2+d3)*0.25f;
			outputDepth = min(min(d0, d1), min(d2, d3));
		#else
			uint d0 = LoadUInt1(DepthTexture, uint2(position.xy)*2 + uint2(0,0), sampleIndex);
			uint d1 = LoadUInt1(DepthTexture, uint2(position.xy)*2 + uint2(1,0), sampleIndex);
			uint d2 = LoadUInt1(DepthTexture, uint2(position.xy)*2 + uint2(0,1), sampleIndex);
			uint d3 = LoadUInt1(DepthTexture, uint2(position.xy)*2 + uint2(1,1), sampleIndex);
			outputDepth = min(min(d0, d1), min(d2, d3));
		#endif
	#endif

#else

	GBufferValues sample0 = Sample(uint2(position.xy), sampleIndex);
	outputNormal.xyz = sample0.worldSpaceNormal;
	outputNormal.a = ApproxF0(sample0);
	outputDepth = SampleDepth(uint2(position.xy), sampleIndex);

#endif

}
