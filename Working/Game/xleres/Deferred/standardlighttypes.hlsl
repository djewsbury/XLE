// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVER_INTERFACE_H)
#define RESOLVER_INTERFACE_H

#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightShapes.hlsl"
#include "../TechniqueLibrary/LightingEngine/CascadeResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowsResolve.hlsl"
#include "../TechniqueLibrary/LightingEngine/ShadowProbes.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightDesc.hlsl"

float3 ResolveLight(
    GBufferValues sample,
    LightSampleExtra sampleExtra,
    LightDesc light,
    float3 worldPosition,
    float3 directionToEye,
    LightScreenDest screenDest)
{
    #if LIGHT_SHAPE == 1
        return SphereLightResolve(sample, sampleExtra, light, worldPosition, directionToEye, screenDest);
    #elif LIGHT_SHAPE == 2
        return TubeLightResolve(sample, sampleExtra, light, worldPosition, directionToEye, screenDest);
    #elif LIGHT_SHAPE == 3
        return RectangleLightResolve(sample, sampleExtra, light, worldPosition, directionToEye, screenDest);
    #else
        return DirectionalLightResolve(sample, sampleExtra, light, worldPosition, directionToEye, screenDest);
    #endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   S H A D O W S
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

    // note, we can use 2 modes for calculating the shadow cascade:
    //	by world position:
    //		calculates the world space position of the current pixel,
    //		and then transforms that world space position into the
    //		shadow cascade coordinates
    //	by camera position:
    //		this transforms directly from the NDC coordinates of the
    //		current pixel into the camera frustum space.
    //
    //	In theory, by camera position might be a little more accurate,
    //	because it skips the world position stage. So the camera to
    //  shadow method has been optimised for accuracy.
    //
    // However note that the resolve by camera position method doesn't work
    // when the main camera is using an orthogonal projection -- it's optimized
    // for persective
static const bool ResolveCascadeByWorldPosition = true;

CascadeAddress ResolveShadowsCascade(float3 worldPosition, float3 worldNormal, float2 camXY, float worldSpaceDepth)
{
    #if SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ARBITRARY || SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL
        if (ResolveCascadeByWorldPosition == true) {
            return ResolveCascade_FromWorldPosition(worldPosition, worldNormal);
        } else {
            return ResolveCascade_CameraToShadowMethod(camXY, worldSpaceDepth);
        }
    #elif SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP
        float3 lightPosition = float3(0,1,0);
        return CascadeAddress_CubeMap(worldPosition-lightPosition);
    #else
        return CascadeAddress_Invalid();
    #endif
}

float ResolveShadows(LightDesc light, float3 worldPosition, float3 worldNormal, float2 camXY, float worldSpaceDepth, LightScreenDest screenDesc)
{
    #if !SHADOW_PROBE
        CascadeAddress cascadeAddress = ResolveShadowsCascade(worldPosition, worldNormal, camXY, worldSpaceDepth);

        ShadowResolveConfig config = ShadowResolveConfig_Default();
        #if SHADOW_CASCADE_MODE == 0
            return 1.0f;
        #elif SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_CUBEMAP
            return ResolveShadows_CubeMap(
                cascadeAddress.frustumCoordinates.xyz, cascadeAddress.miniProjection,
                screenDesc.pixelCoords, screenDesc.sampleIndex,
                config);
        #else
            return ResolveShadows_Cascade(
                cascadeAddress,
                screenDesc.pixelCoords, screenDesc.sampleIndex,
                config);
        #endif
    #else
        if (light.StaticDatabaseLightId != 0)
            return SampleStaticDatabase(light.StaticDatabaseLightId-1, worldPosition - light.Position, screenDesc);
        return 0;
    #endif
}

#endif
