// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(CASCADE_RESOLVE_H)
#define CASCADE_RESOLVE_H

#include "LightDesc.hlsl"      // CascadeAddress
#include "ShadowProjection.hlsl"
#include "../Framework/SystemUniforms.hlsl"
#include "../Math/ProjectionMath.hlsl"

struct CascadeAddress
{
	float4  frustumCoordinates;
    float3  frustumSpaceNormal;
	int     cascadeIndex;
	float4  miniProjection;
};

CascadeAddress CascadeAddress_Invalid()
{
	CascadeAddress result;
	result.cascadeIndex = -1;
    result.frustumCoordinates = 0.0.xxxx;
    result.frustumSpaceNormal = 0.0.xxx;
	result.miniProjection = 0.0.xxxx;
	return result;
}

CascadeAddress CascadeAddress_Create(float4 frustumCoordinates, float3 frustumSpaceNormal, int cascadeIndex, float4 miniProjection)
{
	CascadeAddress result;
	result.cascadeIndex = cascadeIndex;
	result.frustumCoordinates = frustumCoordinates;
    result.frustumSpaceNormal = frustumSpaceNormal;
	result.miniProjection = miniProjection;
	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

CascadeAddress ResolveCascade_FromWorldPosition(float3 worldPosition, float3 worldNormal)
{
        // find the first frustum we're within
    uint projectionCount = min(GetShadowSubProjectionCount(), GetShadowCascadeSubProjectionCount());

    #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
        float3 basePosition = mul(OrthoShadowWorldToView, float4(worldPosition, 1));
        float3 viewSpaceNormal = mul(OrthoShadowWorldToView, float4(worldNormal, 0)).xyz;
        #if SHADOW_ENABLE_NEAR_CASCADE
            float4 frustumCoordinates = float4(mul(OrthoNearCascade, float4(basePosition, 1)), 1.f);
            if (PtInFrustum(frustumCoordinates))
                return CascadeAddress_Create(frustumCoordinates, float3(0,0,0), projectionCount, OrthoShadowNearMinimalProjection);
        #endif

            // In ortho mode, all frustums have the same near and far depth
            // so we can check Z independently from XY
            // (except for the near cascade, which is focused on the near geometry)
        #if SHADOW_SUB_PROJECTION_COUNT == 1
            float4 frustumCoordinates = float4(AdjustForOrthoCascade(basePosition, 0), 1.f);
            return CascadeAddress_Create(frustumCoordinates, viewSpaceNormal, 0, ShadowProjection_GetMiniProj_NotNear(0));
        #else
            //[branch] if (PtInFrustumZ(float4(basePosition, 1.f))) {
                [unroll] for (uint c=0; c<GetShadowCascadeSubProjectionCount(); c++) {
                    float4 frustumCoordinates = float4(AdjustForOrthoCascade(basePosition, c), 1.f);
                    if (PtInFrustum(frustumCoordinates))
                        return CascadeAddress_Create(frustumCoordinates, viewSpaceNormal, c, ShadowProjection_GetMiniProj_NotNear(c));
                }
            //}
        #endif
    #elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)

        #if SHADOW_SUB_PROJECTION_COUNT == 1
            float4 frustumCoordinates = mul(ShadowWorldToProj[0], float4(worldPosition, 1));
            return CascadeAddress_Create(frustumCoordinates, float3(0,0,0), 0, ShadowProjection_GetMiniProj(0));
        #else
            [unroll] for (uint c=0; c<GetShadowCascadeSubProjectionCount(); c++) {
                float4 frustumCoordinates = mul(ShadowWorldToProj[c], float4(worldPosition, 1));
                if (PtInFrustum(frustumCoordinates))
                    return CascadeAddress_Create(frustumCoordinates, float3(0,0,0), c, ShadowProjection_GetMiniProj(c));
            }
        #endif

    #endif

    return CascadeAddress_Invalid();
}

float4 CameraCoordinateToShadow(float2 camCoordinate, float worldSpaceDepth, float4x4 camToShadow)
{
    const float cameraCoordinateScale = worldSpaceDepth; // (linear0To1Depth * SysUniform_GetFarClip());

        //
        //	Accuracy of this transformation is critical...
        //		We'll be comparing to values in the shadow buffer, so we
        //		should try to use the most accurate transformation method
        //
    #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)

        float3x3 cameraToShadow3x3 = float3x3(camToShadow[0].xyz, camToShadow[1].xyz, camToShadow[2].xyz);
        float3 offset = mul(cameraToShadow3x3, float3(camCoordinate, -1.f));
        offset *= cameraCoordinateScale;	// try doing this scale here (maybe increase accuracy a bit?)

        float3 translatePart = float3(camToShadow[0].w, camToShadow[1].w, camToShadow[2].w);
        return float4(offset + translatePart, 1.f);

    #elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)

        float4x3 cameraToShadow4x3 = float4x3(
            camToShadow[0].xyz, camToShadow[1].xyz,
            camToShadow[2].xyz, camToShadow[3].xyz);

            // Note the "-1" here is due to our view of camera space, where -Z is into the screen.
            // the scale by cameraCoordinateScale will later scale this up to the correct depth.
        float4 offset = mul(cameraToShadow4x3, float3(camCoordinate, -1.f));
        offset *= cameraCoordinateScale;	// try doing this scale here (maybe increase accuracy a bit?)

        float4 translatePart = float4(camToShadow[0].w, camToShadow[1].w, camToShadow[2].w, camToShadow[3].w);
        return offset + translatePart;

    #else

        return 0.0.xxxx;

    #endif
}

CascadeAddress ResolveCascade_CameraToShadowMethod(float2 texCoord, float worldSpaceDepth)
{
    const float2 camCoordinate = XYScale * texCoord + XYTrans;

    uint projectionCount = min(GetShadowSubProjectionCount(), GetShadowCascadeSubProjectionCount());

        // 	Find the first frustum we're within
        //	This first loop is kept separate and simple
        //	even though it means we need another comparison
        //	below. This is just to try to keep the generated
        //	shader code simplier.
        //
        //	Note that in order to unroll this first loop, we
        //	must make the loop terminator a compile time constant.
        //	Normally, the number of cascades is passed in a shader
        //	constant (ie, not available at compile time).
        //	However, if the cascade loop is simple, it may be better
        //	to unroll, even if it means a few extra redundant checks
        //	at the end.
        //
        //	It looks like these 2 tweaks (separating the first loop,
        //	and unrolling it) reduces the number of temporary registers
        //	required by 4 (but obvious increases the instruction count).
        //	That seems like a good improvement.

    #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)

        #if SHADOW_ENABLE_NEAR_CASCADE
            float4 nearCascadeCoord = float4(CameraCoordinateToShadow(camCoordinate, worldSpaceDepth, OrthoNearCameraToShadow).xyz, 1.f);
            if (PtInFrustum(nearCascadeCoord))
                return CascadeAddress_Create(nearCascadeCoord, float3(0,0,0), projectionCount, OrthoShadowNearMinimalProjection);
        #endif

            // in ortho mode, this is much simplier... Here is a
            // separate implementation to take advantage of that case!
        #if SHADOW_SUB_PROJECTION_COUNT == 1
            float3 baseCoord = CameraCoordinateToShadow(camCoordinate, worldSpaceDepth, OrthoCameraToShadow).xyz;
            float4 t = float4(AdjustForOrthoCascade(baseCoord, 0), 1.f);
            return CascadeAddress_Create(t, float3(0,0,0), 0, ShadowProjection_GetMiniProj_NotNear(0));
        #else
            float3 baseCoord = CameraCoordinateToShadow(camCoordinate, worldSpaceDepth, OrthoCameraToShadow).xyz;
            [branch] if (PtInFrustumZ(float4(baseCoord, 1.f))) {
                CascadeAddress result = CascadeAddress_Invalid();
                [unroll] for (int c=GetShadowCascadeSubProjectionCount()-1; c>=0; c--) {
                    float4 t = float4(AdjustForOrthoCascade(baseCoord, c), 1.f);
                    if (PtInFrustumXY(t))
                        result = CascadeAddress_Create(t, float3(0,0,0), c, ShadowProjection_GetMiniProj_NotNear(c));
                }
                return result;
            }
        #endif

    #elif (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY) || (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP)

        #if SHADOW_SUB_PROJECTION_COUNT == 1
            float4 frustumCoordinates = CameraCoordinateToShadow(camCoordinate, worldSpaceDepth, CameraToShadow[0]);
            return CascadeAddress_Create(frustumCoordinates, float3(0,0,0), 0, ShadowProjection_GetMiniProj(0));
        #else
            for (uint c=0; c<projectionCount; c++) {
                float4 frustumCoordinates = CameraCoordinateToShadow(camCoordinate, worldSpaceDepth, CameraToShadow[c]);
                if (PtInFrustum(frustumCoordinates))
                    return CascadeAddress_Create(frustumCoordinates, float3(0,0,0), c, ShadowProjection_GetMiniProj(c));
            }
        #endif

    #endif

    return CascadeAddress_Invalid();
}

CascadeAddress CascadeAddress_CubeMap(float3 lightToSamplePoint)
{
    #if SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP
        CascadeAddress result;
        result.cascadeIndex = 0;
        result.frustumCoordinates = float4(lightToSamplePoint, 1);
        result.miniProjection = ShadowProjection_GetMiniProj(0);
        return result;
    #else
        return CascadeAddress_Invalid();
    #endif
}

#endif
