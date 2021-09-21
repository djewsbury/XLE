// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_UTIL_H)
#define RESOLVE_UTIL_H

#include "../TechniqueLibrary/Math/TextureAlgorithm.hlsl"		// for LoadFloat1
#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"			// float NDC->linear conversions
#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"
#include "../TechniqueLibrary/Framework/Binding.hlsl"

#if !defined(VULKAN)
    #define GBUFFER_SHADER_RESOURCE 1
#endif

#if defined(GBUFFER_SHADER_RESOURCE)
    Texture2D_MaybeMS<float>	DepthTexture	 	BIND_SEQ_T10;       // moved to T10 for GenerateShadowingDebugTextures()
    float LoadSubpassDepth(uint2 pixelCoords, uint sampleIndex)
    {
        return LoadFloat1(DepthTexture, pixelCoords.xy, sampleIndex);
    }
#else
    [[vk::input_attachment_index(3)]] SubpassInput<float>	DepthTexture	 	BIND_SEQ_T9;
    float LoadSubpassDepth(uint2 pixelCoords, uint sampleIndex)
    {
        return DepthTexture.SubpassLoad();
    }
#endif

struct LightOperatorInputs
{
	float3 worldPosition;
	LightScreenDest screenDest;
	float worldSpaceDepth;
	float ndcDepth;
};

LightOperatorInputs LightOperatorInputs_Create(float4 position, float3 viewFrustumVector, SystemInputs sys)
{
	LightOperatorInputs result;

	int2 pixelCoords = position.xy;
	
    result.screenDest.pixelCoords = pixelCoords;
    result.screenDest.sampleIndex = GetSampleIndex(sys);

    // Note -- we could pre-multiply (miniProj.W/SysUniform_GetFarClip()) into the view frustum vector to optimise this slightly...?
    result.ndcDepth = LoadSubpassDepth(pixelCoords.xy, GetSampleIndex(sys));
    result.worldSpaceDepth = NDCDepthToWorldSpace(result.ndcDepth);
    result.worldPosition = WorldPositionFromLinear0To1Depth(viewFrustumVector, result.worldSpaceDepth / SysUniform_GetFarClip());
	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

float LoadLinear0To1Depth(int2 pixelCoords, uint sampleIndex)
{
	return NDCDepthToLinear0To1(LoadSubpassDepth(pixelCoords.xy, sampleIndex));
}

float LoadWorldSpaceDepth(int2 pixelCoords, uint sampleIndex)
{
	return NDCDepthToWorldSpace(LoadSubpassDepth(pixelCoords.xy, sampleIndex));
}

float3 LoadWorldPosition(int2 pixelCoords, uint sampleIndex, float3 viewFrustumVector)
{
	float depth = LoadLinear0To1Depth(pixelCoords, sampleIndex);
	return WorldPositionFromLinear0To1Depth(viewFrustumVector, depth);
}

#endif
