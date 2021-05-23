// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SHADOWS_RESOLVE_H)
#define SHADOWS_RESOLVE_H

#include "SampleFiltering.hlsl"
#include "RTShadows.hlsl"
#include "../Math/ProjectionMath.hlsl"
#include "../Math/Misc.hlsl"
#include "../Math/MathConstants.hlsl"
#include "../Framework/Binding.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   I N P U T S
///////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DArray 	ShadowTextures BIND_SHADOW_T0;
TextureCube 	ShadowCube BIND_SHADOW_T0;
Texture2D		NoiseTexture BIND_SHARED_LIGHTING_T1;

#if !defined(SHADOW_RESOLVE_MODEL)
    #define SHADOW_RESOLVE_MODEL 0
#endif

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
    float MinBlurRadius;
    float MaxBlurRadius;
    float ShadowTextureSize;
}

struct ShadowResolveConfig
{
    bool _doFiltering;

        // "Percentage Closer" configuration
    bool _pcUsePoissonDiskMethod;
    bool _pcDoFilterRotation;

    bool _hasHybridRT;
};

ShadowResolveConfig ShadowResolveConfig_Default()
{
    ShadowResolveConfig result;
    result._doFiltering = true;
    result._pcUsePoissonDiskMethod = SHADOW_RESOLVE_MODEL == 0;
    result._pcDoFilterRotation = true;
    result._hasHybridRT = false;
    return result;
}

ShadowResolveConfig ShadowResolveConfig_NoFilter()
{
    ShadowResolveConfig result;
    result._doFiltering = false;
    result._pcUsePoissonDiskMethod = false;
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
    float2 sinCosAngle;
    sincos(angle, sinCosAngle.x, sinCosAngle.y);
    float2x2 rotationMatrix = float2x2(
        float2(sinCosAngle.x, sinCosAngle.y),
        float2(-sinCosAngle.y, sinCosAngle.x));

    const float filterSize = 12.f / ShadowTextureSize;
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
        float2 baseFilter0 = GetRawShadowSampleFilter((c*4+0)*sampleStep+sampleOffset);
        float2 baseFilter1 = GetRawShadowSampleFilter((c*4+1)*sampleStep+sampleOffset);
        float2 baseFilter2 = GetRawShadowSampleFilter((c*4+2)*sampleStep+sampleOffset);
        float2 baseFilter3 = GetRawShadowSampleFilter((c*4+3)*sampleStep+sampleOffset);

        baseFilter0 = mul(rotationMatrix, baseFilter0);
        baseFilter1 = mul(rotationMatrix, baseFilter1);
        baseFilter2 = mul(rotationMatrix, baseFilter2);
        baseFilter3 = mul(rotationMatrix, baseFilter3);

        float4 sampleDepth;
        sampleDepth.x = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + filterSize * baseFilter0, float(arrayIndex)), 0).r;
        sampleDepth.y = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + filterSize * baseFilter1, float(arrayIndex)), 0).r;
        sampleDepth.z = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + filterSize * baseFilter2, float(arrayIndex)), 0).r;
        sampleDepth.w = ShadowTextures.SampleLevel(ShadowDepthSampler, float3(texCoords + filterSize * baseFilter3, float(arrayIndex)), 0).r;

        float4 difference 		 = comparisonDistance.xxxx - sampleDepth;
        float4 sampleCount 		 = difference > 0.0f;					// array of 1s for pixels in the shadow texture closer to the light
        accumulatedSampleCount 	+= dot(sampleCount, 1.0.xxxx);			// count number of 1s in "sampleCount"
            // Clamp maximum distance considered here?
        accumulatedDistance += dot(difference, sampleCount);		// accumulate only the samples closer to the light
    }

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

float CalculateFilteredShadows(
    float2 texCoords, float comparisonDistance, uint arrayIndex,
    float casterDistance, int2 randomizerValue,
    float2 projectionScale, uint msaaSampleIndex,
    ShadowResolveConfig config)
{
        //	In "ShadowsPerspectiveProjection", casterDistance is the difference between 2 NDC depths
        //	Otherwise, casterDistance is world space distance between the sample and caster.
    float filterSize, filterSizePixels;
    if (ShadowsPerspectiveProjection) {
        filterSize = lerp(0.001f, 0.015f, saturate(casterDistance*30.f));
        filterSize *= .03f * projectionScale.x;		// (note -- projectionScale.y is ignored. We need to have uniform x/y scale to rotate the filter correctly)
        filterSizePixels = filterSize * ShadowTextureSize;
    } else {
            //	There are various ways to calculate the filtering distance here...
            //	For example, we can assume the light source is an area light source, and
            //	calculate the appropriate penumbra for that object. But let's just use
            //	a simple method and calculate a fixed penumbra angle
        filterSizePixels  = TanBlurAngle * casterDistance;
        filterSizePixels *= projectionScale.x * (ShadowTextureSize / 2.f);

        filterSizePixels = min(max(filterSizePixels, MinBlurRadius), MaxBlurRadius);

        filterSize = filterSizePixels / ShadowTextureSize;
    }

    if (config._pcUsePoissonDiskMethod) {

        float noiseValue = NoiseTexture.Load(int3(randomizerValue.x & 0xff, randomizerValue.y & 0xff, 0)).r;
        float2 filterRotation;
        sincos(2.f * 3.14159f * noiseValue, filterRotation.x, filterRotation.y);
        filterRotation *= filterSize;

        float2 depthddTC = CalculateShadowLargeFilterBias(comparisonDistance, texCoords);

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
                rotatedFilter0 = filterSize*filter0;
                rotatedFilter1 = filterSize*filter1;
                rotatedFilter2 = filterSize*filter2;
                rotatedFilter3 = filterSize*filter3;
            }

            float cDist0 = comparisonDistance + dot(rotatedFilter0, depthddTC);
            float cDist1 = comparisonDistance + dot(rotatedFilter1, depthddTC);
            float cDist2 = comparisonDistance + dot(rotatedFilter2, depthddTC);
            float cDist3 = comparisonDistance + dot(rotatedFilter3, depthddTC);

            float4 sampleDepth;
            sampleDepth.x = TestShadow(texCoords + rotatedFilter0, arrayIndex, cDist0);
            sampleDepth.y = TestShadow(texCoords + rotatedFilter1, arrayIndex, cDist1);
            sampleDepth.z = TestShadow(texCoords + rotatedFilter2, arrayIndex, cDist2);
            sampleDepth.w = TestShadow(texCoords + rotatedFilter3, arrayIndex, cDist3);

            shadowingTotal += dot(sampleDepth, 1.0.xxxx);
        }

        return shadowingTotal * (1.f / float(sampleCount));

    } else {

        float fRatio = saturate(filterSizePixels / float(AMD_FILTER_SIZE));
        return FixedSizeShadowFilter(
            ShadowTextures,
            float3(texCoords, float(arrayIndex)), comparisonDistance, fRatio);

    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   R E S O L V E
///////////////////////////////////////////////////////////////////////////////////////////////////

float ResolveDMShadows(	uint projection, float2 shadowTexCoord,
                        float4 miniProjection,
                        float comparisonDistance,
                        int2 randomizerValue, uint msaaSampleIndex,
                        ShadowResolveConfig config)
{
    float casterDistanceComparison = comparisonDistance;
    float biasedDepth;
    const bool shadowsPerspectiveProj = ShadowsPerspectiveProjection;

        //	Here, we bias the shadow depth using world space units.
        //	This appears to produce more reliable results and variety
        //	of depth ranges.
        //	With perspective projection, it is more expensive than biasing in NDC depth space.
        //	But with orthogonal shadows, it should be very similar
    MiniProjZW miniP = AsMiniProjZW(miniProjection);
    if (shadowsPerspectiveProj) {
        float worldSpaceDepth = NDCDepthToWorldSpace_Perspective(comparisonDistance, miniP);
        biasedDepth = WorldSpaceDepthToNDC_Perspective(worldSpaceDepth - ShadowBiasWorldSpace, miniP);
    } else {
        biasedDepth = comparisonDistance - WorldSpaceDepthDifferenceToNDC_Ortho(ShadowBiasWorldSpace, miniP);
    }

    if (config._doFiltering) {
        float casterDistance = CalculateShadowCasterDistance(
            shadowTexCoord, casterDistanceComparison, projection,
            msaaSampleIndex, DitherPatternValue(randomizerValue));

        if (!shadowsPerspectiveProj) {
                // In orthogonal projection mode, NDC depths are actually linear. So, we can convert a difference
                // of depths in NDC space (like casterDistance) into world space depth easily. Linear depth values
                // are more convenient for calculating the shadow filter radius
            casterDistance = -NDCDepthDifferenceToWorldSpace_Ortho(casterDistance, miniP);
        }

        return CalculateFilteredShadows(
            shadowTexCoord, biasedDepth, projection, casterDistance, randomizerValue,
            miniProjection.xy, msaaSampleIndex, config);
    } else {
        return TestShadow(shadowTexCoord, projection, biasedDepth);
    }
}

float ResolveShadows_Cascade(
    int cascadeIndex, float4 cascadeNormCoords, float4 miniProjection,
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

    return ResolveDMShadows(cascadeIndex, texCoords, miniProjection, comparisonDistance, randomizerValue, msaaSampleIndex, config);
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
