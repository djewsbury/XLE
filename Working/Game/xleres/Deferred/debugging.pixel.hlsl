// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../TechniqueLibrary/Math/TextureAlgorithm.hlsl"
#include "../TechniqueLibrary/RenderOverlays/dd/DebuggingShapes.hlsl"
#include "../TechniqueLibrary/RenderOverlays/dd/DebuggingPanels.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"

Texture2D<float4>		LightOutput : register(t0);
Texture2D<float>		DebuggingTextureMin : register(t1);
Texture2D<float>		DebuggingTextureMax : register(t2);
Texture2D<uint>			DebuggingLightCountTexture : register(t3);
Texture2D				DigitsTexture : register(t4);

float4 DepthsDebuggingTexture(float4 pos : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	const bool drawTileGrid = true;
	if (drawTileGrid) {
		if ((uint(pos.x) % 16) == 0 || (uint(pos.y) % 16) == 0) {
			return float4(1,.85,.5,.33f);
		}
	}

	const bool drawDebuggingTextures = true;
	if (drawDebuggingTextures) {
		uint count = DebuggingLightCountTexture.Load(int3(pos.xy, 0));
		if (count > 0) {
			uint textureOffset = 10;
			float4 color = float4(0.0.xxx,1);
			if ((count/10) < 3) {
				float3 colourTable[3] = { float3(0,0,.3), float3(0,.3,0), float3(3.,0,0) };
				color = float4(colourTable[count/10], 1);
				textureOffset = count%10;
			}
			float4 textureQuery = DigitsTexture.Load(int3((pos.x%16)+(textureOffset*16), pos.y%16, 0));
			float2 A = float2(pos.xy%16)-8.0.xx;
			float radiusSq = dot(A, A);
			if (radiusSq < 7.f*7.f) {
				return float4(color.rgb + textureQuery.rgb, .75f*(color.a + textureQuery.r));
			}
		}

		// float2 depths = float2(
		// 	DebuggingTextureMin.Load(int3(pos.xy, 0)).x,
		// 	DebuggingTextureMax.Load(int3(pos.xy, 0)).x);
		// depths.x = NDCDepthToLinearDepth(depths.x);
		// depths.y = NDCDepthToLinearDepth(depths.y);
		// return float4(depths.xy, count/16.f, 1);
	}

	return float4(0.0.xxx, 0);
}

Texture2D_MaybeMS<float4>		GBuffer_Diffuse		: register(t5);
Texture2D_MaybeMS<float4>		GBuffer_Normals		: register(t6);
Texture2D_MaybeMS<float4>		GBuffer_Parameters	: register(t7);
Texture2D_MaybeMS<float>		DepthTexture		: register(t8);

Texture2DArray<float>			ShadowDepthTextures : register(t14);

float4 GBufferDebugging(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float2 outputDimensions = position.xy / texCoord.xy;
	float aspectRatio = outputDimensions.y / outputDimensions.x;

	float4 result = float4(0.0.xxx, 0.f);
	RenderTile(float2( 0,  0), float2(.5, .5), texCoord, GBuffer_Diffuse, result);
	RenderTile(float2(.5,  0), float2( 1, .5), texCoord, GBuffer_Normals, result);
	RenderTile(float2( 0, .5), float2(.5,  1), texCoord, DepthTexture, result);
	RenderTile(float2(.5, .5), float2( 1,  1), texCoord, GBuffer_Parameters, result);
	// RenderTag(float2(.1, .1f), float2(.4f, .15f), texCoord, aspectRatio, outputDimensions, result);
	// RenderHorizTweakerBarShader(float2(.1, .2f), float2(.4f, .225f), 0.155f, texCoord, aspectRatio, outputDimensions, result);

	return result;
}

ITextureLoader Loaders[4];

GBufferEncoded GetGBufferEncoded(float2 coords)
{
	GBufferEncoded result;
	result.diffuseBuffer = GBuffer_Diffuse.SampleLevel(DefaultSampler, coords, 0);
	result.normalBuffer = GBuffer_Normals.SampleLevel(DefaultSampler, coords, 0);
	#if HAS_PROPERTIES_BUFFER == 1
		result.propertiesBuffer = GBuffer_Parameters.SampleLevel(DefaultSampler, coords, 0);
	#endif
	return result;
}

class Roughness : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(Decode(GetGBufferEncoded(tc)).material.roughness.xxx, 1); }
};

class Specular : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(Decode(GetGBufferEncoded(tc)).material.specular.xxx, 1); }
};

class Metal : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(Decode(GetGBufferEncoded(tc)).material.metal.xxx, 1); }
};

class CookedAO : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(Decode(GetGBufferEncoded(tc)).cookedAmbientOcclusion.xxx, 1); }
};

class ShadowCascade0 : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(ShadowDepthTextures.SampleLevel(DefaultSampler, float3(tc, 0.f), 0).xxx, 1); }
};

class ShadowCascade1 : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(ShadowDepthTextures.SampleLevel(DefaultSampler, float3(tc, 1.f), 0).xxx, 1); }
};

class ShadowCascade2 : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(ShadowDepthTextures.SampleLevel(DefaultSampler, float3(tc, 2.f), 0).xxx, 1); }
};

class ShadowCascade3 : ITextureLoader
{
	float4 Load(float2 tc, uint a, uint s) { return float4(ShadowDepthTextures.SampleLevel(DefaultSampler, float3(tc, 3.f), 0).xxx, 1); }
};

float4 GenericDebugging(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float2 outputDimensions = position.xy / texCoord.xy;
	float aspectRatio = outputDimensions.y / outputDimensions.x;

	float4 result = float4(0.0.xxx, 0.f);
	RenderTile(float2( 0,  0), float2(.5, .5), texCoord, Loaders[0], 0, result);
	RenderTile(float2(.5,  0), float2( 1, .5), texCoord, Loaders[1], 0, result);
	RenderTile(float2( 0, .5), float2(.5,  1), texCoord, Loaders[2], 0, result);
	RenderTile(float2(.5, .5), float2( 1,  1), texCoord, Loaders[3], 0, result);

	return result;
}
