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

Texture2D_MaybeMS<float>	DepthTexture	 	BIND_SEQ_T9;

struct ResolvePixelProperties
{
	float3 worldPosition;
	LightScreenDest screenDest;
	float worldSpaceDepth;
	float ndcDepth;
};

ResolvePixelProperties ResolvePixelProperties_Create(float4 position, float3 viewFrustumVector, SystemInputs sys)
{
	ResolvePixelProperties result;

	int2 pixelCoords = position.xy;
	
    result.screenDest.pixelCoords = pixelCoords;
    result.screenDest.sampleIndex = GetSampleIndex(sys);

    // Note -- we could pre-multiply (miniProj.W/SysUniform_GetFarClip()) into the view frustum vector to optimise this slightly...?
	result.ndcDepth = LoadFloat1(DepthTexture, pixelCoords.xy, GetSampleIndex(sys));
    result.worldSpaceDepth = NDCDepthToWorldSpace(result.ndcDepth);

    const bool orthoProjection = true;
    if (!SysUniform_IsOrthogonalProjection()) {
        result.worldPosition = SysUniform_GetWorldSpaceView() + (result.worldSpaceDepth / SysUniform_GetFarClip()) * viewFrustumVector;
    } else {
        float4x4 cameraBasis = SysUniform_GetCameraBasis();
        float3 cameraForward = -float3(cameraBasis[0].z, cameraBasis[1].z, cameraBasis[2].z);
        result.worldPosition = viewFrustumVector + SysUniform_GetWorldSpaceView() - (SysUniform_GetFarClip() - result.worldSpaceDepth) * cameraForward;
    }

	return result;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

float GetLinear0To1Depth(int2 pixelCoords, uint sampleIndex)
{
	return NDCDepthToLinear0To1(LoadFloat1(DepthTexture, pixelCoords.xy, sampleIndex));
}

float GetWorldSpaceDepth(int2 pixelCoords, uint sampleIndex)
{
	return NDCDepthToWorldSpace(LoadFloat1(DepthTexture, pixelCoords.xy, sampleIndex));
}

float3 CalculateWorldPosition(int2 pixelCoords, uint sampleIndex, float3 viewFrustumVector)
{
	float depth = GetLinear0To1Depth(pixelCoords, sampleIndex);
	return CalculateWorldPosition(viewFrustumVector, depth, SysUniform_GetWorldSpaceView());
}

#endif
