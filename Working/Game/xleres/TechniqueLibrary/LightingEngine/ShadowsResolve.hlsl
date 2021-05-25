// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SHADOWS_RESOLVE_H)
#define SHADOWS_RESOLVE_H

#include "ShadowSampleFiltering.hlsl"
#include "RTShadows.hlsl"
#include "../Math/ProjectionMath.hlsl"
#include "../Math/Misc.hlsl"
#include "../Math/MathConstants.hlsl"
#include "../Framework/Binding.hlsl"

#include "ShadowProjection.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   I N P U T S
///////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DArray 	ShadowTextures BIND_SHADOW_T0;
TextureCube 	ShadowCube BIND_SHADOW_T0;
Texture2D		NoiseTexture BIND_SHARED_LIGHTING_T1;

#if !defined(SHADOW_FILTER_MODEL)
    #define SHADOW_FILTER_MODEL 0
#endif

#if !defined(SHADOW_FILTER_CONTACT_HARDENING)
    #define SHADOW_FILTER_CONTACT_HARDENING 0
#endif

#define SHADOW_FILTER_MODEL_NONE 0
#define SHADOW_FILTER_MODEL_POISSONDISC 1
#define SHADOW_FILTER_MODEL_SMOOTH 2

static const bool ShadowsPerspectiveProjection = false;

static const uint FilterKernelSize = 32;
cbuffer ShadowFilteringTable BIND_SHARED_LIGHTING_B0
{
    // #define PACK_FILTER_KERNEL
    #if defined(PACK_FILTER_KERNEL)
        float4 FilterKernel[16];
    #else
        float4 FilterKernel[32];
    #endif
}

cbuffer ShadowResolveParameters BIND_SHADOW_B1
{
    float ShadowBiasWorldSpace;
    float TanBlurAngle;
    float MinBlurRadiusNorm;
    float MaxBlurRadiusNorm;
    float ShadowTextureSize;
}

struct ShadowResolveConfig
{
    uint _filteringMode;
    bool _pcDoFilterRotation;
    bool _doContactHardening;
    bool _hasHybridRT;
};

ShadowResolveConfig ShadowResolveConfig_Default()
{
    ShadowResolveConfig result;
    result._filteringMode = SHADOW_FILTER_MODEL;
    result._pcDoFilterRotation = true;
    result._doContactHardening = SHADOW_FILTER_CONTACT_HARDENING;
    result._hasHybridRT = false;
    return result;
}

ShadowResolveConfig ShadowResolveConfig_NoFilter()
{
    ShadowResolveConfig result;
    result._filteringMode = SHADOW_FILTER_MODEL_NONE;
    result._pcDoFilterRotation = false;
    result._hasHybridRT = false;
    return result;
}

float2 GetRawShadowSampleFilter(uint index)
{
    #if MSAA_SAMPLES > 1		// hack -- shader optimiser causes a problem with shadow filtering...
        return 0.0.xx;
    #else
        #if defined(PACK_FILTER_KERNEL)	// this only works efficiently if we can unpack all of the shadow loops
            if (index >= 16) {
                return FilterKernel[index-16].zw;
            } else
        #endif
        {
            return FilterKernel[index].xy;
        }
    #endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   C O N T A C T   H A R D E N I N G
///////////////////////////////////////////////////////////////////////////////////////////////////

float CalculateShadowCasterDistance(
    float2 texCoords, float comparisonDistance,
    uint arrayIndex, uint msaaSampleIndex,
    float ditherPatternValue)
{
    float accumulatedDistance = 0.0f;
    float accumulatedSampleCount = 0.0001f;

    float angle = 2.f * pi * ditherPatternValue;
    float2 filterRotation;
    sincos(angle, filterRotation.x, filterRotation.y);

    const float searchSize = MaxBlurRadiusNorm;
    filterRotation *= searchSize;

    float minDistance = 1.0f;

    // We need some tolerance here, because of precision issues
    // If the depth we sample from the depth texture is from the surface we're now drawing
    // shadows for, we have to be sure to not count it 
    const float tolerance = 0.01f;
    comparisonDistance -= tolerance;

    #if MSAA_SAMPLES <= 1
            //	Undersampling here can cause some horrible artefacts.
            //		In many cases, 4 samples is enough.
            //		But on edges, we can get extreme filtering problems
            //		with few samples.
            //
        const uint sampleCount = 16;
        const uint sampleOffset = 0;
        const uint loopCount = sampleCount / 4;
        const uint sampleStep = FilterKernelSize / sampleCount;
    #else
        const uint sampleCount = 4;		// this could cause some unusual behaviour...
        const uint sampleOffset = msaaSampleIndex; // * (FilterKernelSize-sampleCount) / (MSAA_SAMPLES-1);
        const uint loopCount = sampleCount / 4;
        const uint sampleStep = (FilterKernelSize-MSAA_SAMPLES+sampleCount) / sampleCount;
        [unroll]

    #endif
    for (uint c=0; c<loopCount; ++c) {

            //
            //		Sample the depth texture, using a normal non-comparison sampler
            //
        float2 filter0 = GetRawShadowSampleFilter((c*4+0)*sampleStep+sampleOffset);
        float2 filter1 = GetRawShadowSampleFilter((c*4+1)*sampleStep+sampleOffset);
        float2 filter2 = GetRawShadowSampleFilter((c*4+2)*sampleStep+sampleOffset);
        float2 filter3 = GetRawShadowSampleFilter((c*4+3)*sampleStep+sampleOffset);

        float2 rotatedFilter0 = float2(dot(filterRotation, filter0), dot(float2(filterRotation.y, -filterRotation.x), filter0));
        float2 rotatedFilter1 = float2(dot(filterRotation, filter1), dot(float2(filterRotation.y, -filterRotation.x), filter1));
        float2 rotatedFilter2 = float2(dot(filterRotation, filter2), dot(float2(filterRotation.y, -filterRotation.x), filter2));
        float2 rotatedFilter3 = float2(dot(filterRotation, filter3), dot(float2(filterRotation.y, -filterRotation.x), filter3));

        float4 sampleDepth;
        sampleDepth.x = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + rotatedFilter0, float(arrayIndex)), 0).r;
        sampleDepth.y = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + rotatedFilter1, float(arrayIndex)), 0).r;
        sampleDepth.z = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + rotatedFilter2, float(arrayIndex)), 0).r;
        sampleDepth.w = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + rotatedFilter3, float(arrayIndex)), 0).r;

        float4 difference 		 = comparisonDistance.xxxx - sampleDepth;
        float4 sampleCount 		 = difference > 0.0f;					// array of 1s for pixels in the shadow texture closer to the light
        accumulatedSampleCount 	+= dot(sampleCount, 1.0.xxxx);			// count number of 1s in "sampleCount"
            // Clamp maximum distance considered here?
        accumulatedDistance     += dot(difference, sampleCount);		// accumulate only the samples closer to the light

        minDistance = difference.x > 0.f ? min(minDistance, sampleDepth.x) : minDistance;
        minDistance = difference.y > 0.f ? min(minDistance, sampleDepth.y) : minDistance;
        minDistance = difference.z > 0.f ? min(minDistance, sampleDepth.z) : minDistance;
        minDistance = difference.w > 0.f ? min(minDistance, sampleDepth.w) : minDistance;
    }

    return comparisonDistance - minDistance;

        //
        //		finalDistance is the assumed distance to the shadow caster
    float finalDistance = accumulatedDistance / accumulatedSampleCount;
    return finalDistance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   P E R C E N T A G E   C L O S E R
///////////////////////////////////////////////////////////////////////////////////////////////////

float TestShadow(float2 texCoord, uint arrayIndex, float comparisonDistance)
{
    // these two methods should return the same result (and probably have similiar performance...)
    const bool useGatherCmpRed = false;
    if (!useGatherCmpRed) {
        // SampleCmpLevelZero cannot be used with Vulkan, because there is no textureLod() override
        // for a a sampler2DArrayShadow 
        #if (VULKAN!=1)
            return ShadowTextures.SampleCmpLevelZero(ShadowSampler, float3(texCoord, float(arrayIndex)), comparisonDistance);
        #else
            return ShadowTextures.SampleCmp(ShadowSampler, float3(texCoord, float(arrayIndex)), comparisonDistance);
        #endif
    } else {
        float4 t = ShadowTextures.GatherCmpRed(ShadowSampler, float3(texCoord, float(arrayIndex)), comparisonDistance);
        return dot(t, 1.0.xxxx) * 0.25f;
    }
}

float CalculateFilteredShadows_PoissonDisc(
    float2 texCoords, float comparisonDistance, uint arrayIndex,
    float filterSizeNorm, float2 filterPlane,
    int2 randomizerValue, uint msaaSampleIndex)
{
    float noiseValue = NoiseTexture.Load(int3(randomizerValue.x & 0xff, randomizerValue.y & 0xff, 0)).r;
    float2 filterRotation;
    sincos(2.f * 3.14159f * noiseValue, filterRotation.x, filterRotation.y);
    filterRotation *= filterSizeNorm;

    float weightTotal = 0.f;

    const bool doFilterRotation = true;
    float shadowingTotal = 0.f;
    #if MSAA_SAMPLES <= 1
        const uint sampleCount = 32;
        const uint sampleOffset = 0;
        const uint loopCount = sampleCount / 4;
    #else
        const uint sampleCount = 4;		// We will be blending multiple samples, anyway... So minimize sample count for MSAA
        const uint sampleOffset = msaaSampleIndex * (FilterKernelSize-sampleCount) / (MSAA_SAMPLES-1);
        const uint loopCount = sampleCount / 4;
        [unroll]
    #endif
    for (uint c=0; c<loopCount; ++c) {

            // note --	we can use the screen space derivatives of sample position to
            //			bias the offsets here, and avoid some acne artefacts
        float2 filter0 = GetRawShadowSampleFilter(c*4+0+sampleOffset);
        float2 filter1 = GetRawShadowSampleFilter(c*4+1+sampleOffset);
        float2 filter2 = GetRawShadowSampleFilter(c*4+2+sampleOffset);
        float2 filter3 = GetRawShadowSampleFilter(c*4+3+sampleOffset);

        float2 rotatedFilter0, rotatedFilter1, rotatedFilter2, rotatedFilter3;
        if (doFilterRotation) {
            rotatedFilter0 = float2(dot(filterRotation, filter0), dot(float2(filterRotation.y, -filterRotation.x), filter0));
            rotatedFilter1 = float2(dot(filterRotation, filter1), dot(float2(filterRotation.y, -filterRotation.x), filter1));
            rotatedFilter2 = float2(dot(filterRotation, filter2), dot(float2(filterRotation.y, -filterRotation.x), filter2));
            rotatedFilter3 = float2(dot(filterRotation, filter3), dot(float2(filterRotation.y, -filterRotation.x), filter3));
        } else {
            rotatedFilter0 = filterSizeNorm*filter0;
            rotatedFilter1 = filterSizeNorm*filter1;
            rotatedFilter2 = filterSizeNorm*filter2;
            rotatedFilter3 = filterSizeNorm*filter3;
        }

        float cDist0 = comparisonDistance + dot(rotatedFilter0, filterPlane);
        float cDist1 = comparisonDistance + dot(rotatedFilter1, filterPlane);
        float cDist2 = comparisonDistance + dot(rotatedFilter2, filterPlane);
        float cDist3 = comparisonDistance + dot(rotatedFilter3, filterPlane);

        float4 sampleDepth;
        sampleDepth.x = TestShadow(texCoords + rotatedFilter0, arrayIndex, cDist0);
        sampleDepth.y = TestShadow(texCoords + rotatedFilter1, arrayIndex, cDist1);
        sampleDepth.z = TestShadow(texCoords + rotatedFilter2, arrayIndex, cDist2);
        sampleDepth.w = TestShadow(texCoords + rotatedFilter3, arrayIndex, cDist3);

        shadowingTotal += dot(sampleDepth, 1.0.xxxx);
    }

    return shadowingTotal * (1.f / float(sampleCount));
}

float CalculateFilteredShadows(
    float2 texCoords, float comparisonDistance, uint arrayIndex,
    float filterSizeNorm, float2 filterPlane,
    int2 randomizerValue, uint msaaSampleIndex, 
    ShadowResolveConfig config)
{
    if (config._filteringMode == SHADOW_FILTER_MODEL_POISSONDISC) {

        return CalculateFilteredShadows_PoissonDisc(
            texCoords, comparisonDistance, arrayIndex, 
            filterSizeNorm, filterPlane,
            randomizerValue, msaaSampleIndex);

    } else if (config._filteringMode == SHADOW_FILTER_MODEL_SMOOTH) {

        float fRatio = saturate(filterSizeNorm * ShadowTextureSize / float(AMD_FILTER_SIZE));
        return FixedSizeShadowFilter(
            ShadowTextures,
            float3(texCoords, float(arrayIndex)), comparisonDistance, fRatio, filterPlane);

    } else {

        return TestShadow(texCoords, arrayIndex, comparisonDistance);

    }
}

float CalculateFilterSize(
    uint cascadeIndex, float2 shadowTexCoord,
    float4 miniProjection, float comparisonDistance,
    int2 randomizerValue, uint msaaSampleIndex)
{
    float casterDistance = CalculateShadowCasterDistance(
        shadowTexCoord, comparisonDistance, cascadeIndex,
        msaaSampleIndex, DitherPatternValue(randomizerValue));

    float projectionScale = miniProjection.x;	// (note -- projectionScale.y is ignored. We need to have uniform x/y scale to rotate the filter correctly)

    float filterSizeNorm;
    if (!ShadowsPerspectiveProjection) {

            // In orthogonal projection mode, NDC depths are actually linear. So, we can convert a difference
            // of depths in NDC space (like casterDistance) into world space depth easily. Linear depth values
            // are more convenient for calculating the shadow filter radius
        float worldSpaceCasterDistance = NDCDepthDifferenceToWorldSpace_Ortho(casterDistance, AsMiniProjZW(miniProjection));

        const float distanceToWideningScale = 10.f / ShadowTextureSize * projectionScale;
        filterSizeNorm = distanceToWideningScale * worldSpaceCasterDistance;
    } else {
            //	There are various ways to calculate the filtering distance here...
            //	For example, we can assume the light source is an area light source, and
            //	calculate the appropriate penumbra for that object. But let's just use
            //	a simple method and calculate a fixed penumbra angle
        filterSizeNorm = TanBlurAngle * casterDistance * projectionScale;
    }

    filterSizeNorm = min(max(filterSizeNorm, MinBlurRadiusNorm), MaxBlurRadiusNorm);
    return filterSizeNorm;
}

float SampleDMShadows(	uint cascadeIndex, float2 shadowTexCoord, float3 cascadeSpaceNormal,
                        float4 miniProjection,
                        float comparisonDistance,
                        int2 randomizerValue, uint msaaSampleIndex,
                        ShadowResolveConfig config)
{
    float biasedDepth;
        //	Here, we bias the shadow depth using world space units.
        //	This appears to produce more reliable results and variety
        //	of depth ranges.
        //	With perspective projection, it is more expensive than biasing in NDC depth space.
        //	But with orthogonal shadows, it should be very similar
    MiniProjZW miniP = AsMiniProjZW(miniProjection);
    if (ShadowsPerspectiveProjection) {
        float worldSpaceDepth = NDCDepthToWorldSpace_Perspective(comparisonDistance, miniP);
        biasedDepth = WorldSpaceDepthToNDC_Perspective(worldSpaceDepth - ShadowBiasWorldSpace, miniP);
    } else {
        #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
            biasedDepth = comparisonDistance - WorldSpaceDepthDifferenceToNDC_Ortho(ShadowBiasWorldSpace * cascadeSpaceNormal.z, miniP);
        #endif
    }

    float filterSize = MinBlurRadiusNorm;
    if (config._doContactHardening) {
        filterSize = CalculateFilterSize(
            cascadeIndex, shadowTexCoord,
            miniProjection, comparisonDistance, randomizerValue, msaaSampleIndex);
    }

    // float2 filterPlane = CalculateFilterPlane_ScreenSpaceDerivatives(comparisonDistance, shadowTexCoord);
    cascadeSpaceNormal = normalize(cascadeSpaceNormal);
    float2 filterPlane = float2(-cascadeSpaceNormal.x / cascadeSpaceNormal.z, -cascadeSpaceNormal.y / cascadeSpaceNormal.z);
    #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
        filterPlane *= OrthoShadowCascadeScale[cascadeIndex].zz / OrthoShadowCascadeScale[0].xy;
    #endif

#if 0
    {
        float3 bitangent = normalize(cross(float3(1, 0, 0), cascadeSpaceNormal));
        float3 tangent = normalize(cross(cascadeSpaceNormal, bitangent));

        float2 filterPlane2 = float2(tangent.z / tangent.x, bitangent.z / bitangent.y);
        #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
            filterPlane2 *= OrthoShadowCascadeScale[cascadeIndex].zz / OrthoShadowCascadeScale[0].xy;
        #endif
        filterPlane = filterPlane2;
    }
#endif

    return CalculateFilteredShadows(
        shadowTexCoord, biasedDepth, cascadeIndex, filterSize, filterPlane, randomizerValue,
        msaaSampleIndex, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   R E S O L V E
///////////////////////////////////////////////////////////////////////////////////////////////////

float ResolveShadows_Cascade(
    int cascadeIndex, float4 cascadeNormCoords, float3 cascadeSpaceNormal,
    float4 miniProjection,
    int2 randomizerValue, uint msaaSampleIndex,
    ShadowResolveConfig config)
{
    [branch] if (cascadeIndex < 0) return 1.f;

    float2 texCoords;
    float comparisonDistance;
    texCoords = cascadeNormCoords.xy / cascadeNormCoords.w;
    #if VULKAN      // hack for NDC handiness in Vulkan
        texCoords = 0.5.xx + 0.5f * texCoords.xy;
    #else
        texCoords = float2(0.5f + 0.5f * texCoords.x, 0.5f - 0.5f * texCoords.y);
    #endif

    comparisonDistance = cascadeNormCoords.z / cascadeNormCoords.w;

            // 	When hybrid shadows are enabled, the first cascade might be
            //	resolved using ray traced shadows. For convenience, we'll assume
            //	the the ray traced shadow cascade always matches the first cascade
            //	of the depth map shadows...
            //	We could alternatively have a completely independent cascade; but
            //	that would make doing the hybrid blend more difficult
    if (config._hasHybridRT && cascadeIndex==0) {
        return SampleRTShadows(cascadeNormCoords.xyz/cascadeNormCoords.w, randomizerValue);
    }

    return SampleDMShadows(cascadeIndex, texCoords, cascadeSpaceNormal, miniProjection, comparisonDistance, randomizerValue, msaaSampleIndex, config);
}

float CubeMapComparisonDistance(float3 cubeMapSampleCoord, float4 miniProjection)
{
    float worldSpaceDepth;
    float3 d = abs(cubeMapSampleCoord);
    if (d.x > d.z) {
        if (d.x > d.y) {
            worldSpaceDepth = d.x;
        } else {
            worldSpaceDepth = d.y;
        }
    } else if (d.z > d.y) {
        worldSpaceDepth = d.z;
    } else {
        worldSpaceDepth = d.y;
    }
    return WorldSpaceDepthToNDC_Perspective(worldSpaceDepth, AsMiniProjZW(miniProjection));
}

float ResolveShadows_CubeMap(
    float3 cubeMapNormCoords, float4 miniProjection,
    int2 randomizerValue, uint msaaSampleIndex,
    ShadowResolveConfig config)
{
    float comparisonDistance = CubeMapComparisonDistance(cubeMapNormCoords, miniProjection);
    return ShadowCube.SampleCmpLevelZero(ShadowSampler, cubeMapNormCoords, comparisonDistance);
}

#endif
