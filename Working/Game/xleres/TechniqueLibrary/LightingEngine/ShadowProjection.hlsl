// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SHADOW_PROJECTION_H)
#define SHADOW_PROJECTION_H

#define SHADOW_CASCADE_MODE_NONE 0
#define SHADOW_CASCADE_MODE_ARBITRARY 1
#define SHADOW_CASCADE_MODE_ORTHOGONAL 2
#define SHADOW_CASCADE_MODE_CUBEMAP 3

#include "../Framework/Binding.hlsl"

#if !defined(SHADOW_SUB_PROJECTION_COUNT)
	#define SHADOW_SUB_PROJECTION_COUNT 1
#endif

#if defined(SHADOW_GEN_SHADER)
	#define ShadowProjectionBinding BIND_SEQ_B5
#else
	#define ShadowProjectionBinding BIND_SHADOW_B0
#endif

cbuffer ShadowProjection ShadowProjectionBinding
{
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)
		row_major float4x4 ShadowWorldToProj[SHADOW_SUB_PROJECTION_COUNT];
		float4 ShadowMinimalProjection[SHADOW_SUB_PROJECTION_COUNT];
		uint SubProjectionCount;
	#elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
		row_major float3x4 OrthoShadowWorldToView;
		float4 OrthoShadowMinimalProjection;
		float4 OrthoShadowCascadeScale[SHADOW_SUB_PROJECTION_COUNT];
		float4 OrthoShadowCascadeTrans[SHADOW_SUB_PROJECTION_COUNT];
		#if SHADOW_ENABLE_NEAR_CASCADE
			row_major float3x4 OrthoNearCascade;
			float4 OrthoShadowNearMinimalProjection;
		#endif
		uint SubProjectionCount;
	#endif
}

cbuffer ScreenToShadowProjection BIND_SHADOW_B2
{
	float2 XYScale;
	float2 XYTrans;
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)
		row_major float4x4 CameraToShadow[SHADOW_SUB_PROJECTION_COUNT];
	#else
		row_major float4x4 OrthoCameraToShadow;	// the "definition" projection for cascades in ortho mode
		row_major float4x4 OrthoNearCameraToShadow;
	#endif
}

#if defined(SHADOW_CASCADE_MODE)
	uint GetShadowCascadeMode() { return SHADOW_CASCADE_MODE; }
#else
	uint GetShadowCascadeMode() { return SHADOW_CASCADE_MODE_NONE; }
#endif

uint GetShadowSubProjectionCount()
{
	#if !defined(SHADOW_CASCADE_MODE) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_NONE)
		return 0;
	#else
		return SubProjectionCount;
	#endif
}

float3 AdjustForOrthoCascade(float3 basePosition, uint cascadeIndex)
{
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
		return float3(
			basePosition.x * OrthoShadowCascadeScale[cascadeIndex].x + OrthoShadowCascadeTrans[cascadeIndex].x,
			basePosition.y * OrthoShadowCascadeScale[cascadeIndex].y + OrthoShadowCascadeTrans[cascadeIndex].y,
			basePosition.z * OrthoShadowCascadeScale[cascadeIndex].z + OrthoShadowCascadeTrans[cascadeIndex].z);
	#else
		return basePosition;
	#endif
}

float4 ShadowProjection_GetOutput(float3 position, uint cascadeIndex)
{
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)
        return mul(ShadowWorldToProj[cascadeIndex], float4(position,1));
    #elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
        float3 a = AdjustForOrthoCascade(mul(OrthoShadowWorldToView, float4(position, 1)), cascadeIndex);
        return float4(a, 1.f);
    #else
        return 0.0.xxxx;
    #endif
}

float4 ShadowProjection_GetMiniProj_NotNear(uint cascadeIndex)
{
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)
		return ShadowMinimalProjection[cascadeIndex];
	#elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
		float4 result = OrthoShadowMinimalProjection;
		result.xy = OrthoShadowCascadeScale[cascadeIndex].xy;
		return result;
	#else
		return 1.0.xxxx;
	#endif
}

uint GetShadowCascadeSubProjectionCount()
{
	return SHADOW_SUB_PROJECTION_COUNT;		// we could optionally make this a constant to give us room for frame-to-frame variation 
}

float4 ShadowProjection_GetMiniProj(uint cascadeIndex)
{
	#if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL) && SHADOW_ENABLE_NEAR_CASCADE
		if (cascadeIndex == GetShadowCascadeSubProjectionCount())
			return OrthoShadowNearMinimalProjection;
	#endif
	return ShadowProjection_GetMiniProj_NotNear(cascadeIndex);
}

#endif
