// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SHADOWS_RESOLVE_H)
#define SHADOWS_RESOLVE_H

#if !defined(SHADOW_PROJECTION_H)
    #error Please include ShadowProjection.hlsl before this file
#endif

#if !defined(CASCADE_RESOLVE_H)
    #error Please include CascadeResolve.hlsl before this file
#endif

#include "ShadowSampleFiltering.hlsl"
#include "RTShadows.hlsl"
#include "../Math/MathConstants.hlsl"
#include "../Math/PoissonDisc.hlsl"
#include "../Framework/Binding.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   I N P U T S
///////////////////////////////////////////////////////////////////////////////////////////////////
Texture2DArray<float> 	ShadowTextures BIND_SHADOW_T3;
TextureCube<float> 	    ShadowCube BIND_SHADOW_T3;
Texture2D<float>		NoiseTexture BIND_SHARED_LIGHTING_T1;

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

cbuffer ShadowResolveParameters BIND_SHADOW_B1
{
    float ShadowWorldSpaceResolveBias;
    float TanBlurAngle;
    float MinBlurRadiusNorm;
    float MaxBlurRadiusNorm;            // note that cascades can override this (particularly during the transition between cascades)
    float ShadowTextureSize;
    float CasterDistanceExtraBias;
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
    #if SHADOW_FILTER_MODEL == SHADOW_FILTER_MODEL_POISSONDISC
        result._filteringMode = SHADOW_FILTER_MODEL_POISSONDISC;
    #elif SHADOW_FILTER_MODEL == SHADOW_FILTER_MODEL_SMOOTH
        result._filteringMode = SHADOW_FILTER_MODEL_SMOOTH;
    #else
        result._filteringMode = SHADOW_FILTER_MODEL_NONE;
    #endif
    result._pcDoFilterRotation = true;
    #if SHADOW_FILTER_CONTACT_HARDENING == 1
        result._doContactHardening = true;
    #else
        result._doContactHardening = false;
    #endif
    result._hasHybridRT = false;
    return result;
}

ShadowResolveConfig ShadowResolveConfig_NoFilter()
{
    ShadowResolveConfig result;
    #if SHADOW_FILTER_MODEL == SHADOW_FILTER_MODEL_POISSONDISC
        result._filteringMode = SHADOW_FILTER_MODEL_POISSONDISC;
    #elif SHADOW_FILTER_MODEL == SHADOW_FILTER_MODEL_SMOOTH
        result._filteringMode = SHADOW_FILTER_MODEL_SMOOTH;
    #else
        result._filteringMode = SHADOW_FILTER_MODEL_NONE;
    #endif
    result._pcDoFilterRotation = false;
    result._doContactHardening = false;
    result._hasHybridRT = false;
    return result;
}

float2 GetRawShadowSampleFilter(uint index)
{
    return gPoissonDisc32Tap[index];
}

uint GetRawShadowSampleKernelSize() { return 32; }

float GetNoisyValue(int2 randomizerValue, uint idx)
{
    //
    // Many different ways to get a noisy value from these coords
    // 0: lookup in balanced_noise texture (which just contains a lot of evenly distributed random values)
    //      if the texture is built well, we can bias the noise to minimize clumping 
    // 1: use IntegerHash to scrample the bits of the input and 
    //      no texture lookups, everything is just algorithm. Plus there's no repeats, it's unique across the whole domain 
    // 2: use DitherPatternValue
    //      sometimes the regularity of the pattern can actually make it visually more appealing
    //
    // The relative cost of each method might depend greatly on the particular hardware, particularly for that
    // noise texture lookup!
    //
    const uint noiseMethod = 0;
    if (noiseMethod == 0) {
        if (idx == 0) {
            return NoiseTexture.Load(int3(randomizerValue.x & 0xff, randomizerValue.y & 0xff, 0)).r;
        } else {
            return NoiseTexture.Load(int3((randomizerValue.x + 17) & 0xff, (randomizerValue.y + 33) & 0xff, 0)).r;
        }
    } else if (noiseMethod == 1) {
        if (idx == 0) {
            return asfloat((IntegerHash(randomizerValue.y * 2048 + randomizerValue.x) & ((1 << 23) - 1)) | 0x3f800000) - 1.0f;
        } else {
            return asfloat((IntegerHash((2048*2048)+randomizerValue.x * 2048 + randomizerValue.y) & ((1 << 23) - 1)) | 0x3f800000) - 1.0f;
        }
    } else if (noiseMethod == 2) {
        if (idx == 0) {
            return DitherPatternValue(randomizerValue);
        } else {
            return DitherPatternValue(randomizerValue.yx);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   C O N T A C T   H A R D E N I N G
///////////////////////////////////////////////////////////////////////////////////////////////////

float CalculateShadowCasterDistance(
    float2 texCoords, float comparisonDistance,
    uint arrayIndex, float searchSize, float2 filterPlane, uint msaaSampleIndex, float noisyValue)
{
    float accumulatedDistance = 0.f;
    float accumulatedSampleCount = 0.f;

    float angle = 2.0f * pi * noisyValue;
    float2 filterRotation;
    sincos(angle, filterRotation.x, filterRotation.y);
    filterRotation *= searchSize;

    const float minDifferenceInitial = 100000.0f;
    float minDifference = minDifferenceInitial;

    // We need some tolerance here, because of precision issues
    // If the depth we sample from the depth texture is from the surface we're now drawing
    // shadows for, we have to be sure to not count it
    float extraBias = -CasterDistanceExtraBias;
    extraBias += searchSize*2.f/65535.f;		// 16 bit (trying to compensate for rounding and other minor inaccuracies
    extraBias += 1.f / 1024.f * max(abs(filterPlane.x), abs(filterPlane.y));    // additional bias based on slope can help a lot without creating peter-panning
    comparisonDistance += extraBias;

    #if MSAA_SAMPLES <= 1
            //	Undersampling here can cause some horrible artefacts.
            //		In many cases, 4 samples is enough.
            //		But on edges, we can get extreme filtering problems
            //		with few samples.
            //
        const uint sampleCount = 8;
        const uint sampleOffset = 0;
        const uint loopCount = sampleCount / 4;
        const uint sampleStep = GetRawShadowSampleKernelSize() / sampleCount;
    #else
        const uint sampleCount = 4;		// this could cause some unusual behaviour...
        const uint sampleOffset = msaaSampleIndex; // * (GetRawShadowSampleKernelSize()-sampleCount) / (MSAA_SAMPLES-1);
        const uint loopCount = sampleCount / 4;
        const uint sampleStep = (GetRawShadowSampleKernelSize()-MSAA_SAMPLES+sampleCount) / sampleCount;
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

        // Note that we have to flip this comparison for the ReverseZ projection modes, since
        // larger values mean closer to the light in ReverseZ, while smaller numbers mean closer to the
        // light in non-ReverseZ modes
        float4 difference = sampleDepth - comparisonDistance.xxxx;
        difference.x -= dot(rotatedFilter0, filterPlane);       // subtract this bias, which is the same as adding to comparisonDistance earlier
        difference.y -= dot(rotatedFilter1, filterPlane);
        difference.z -= dot(rotatedFilter2, filterPlane);
        difference.w -= dot(rotatedFilter3, filterPlane);

        float4 sampleCount 		 = difference > 0.0f;					// array of 1s for pixels in the shadow texture closer to the light
        accumulatedSampleCount 	+= dot(sampleCount, 1.0.xxxx);			// count number of 1s in "sampleCount"
            // Clamp maximum distance considered here?
        accumulatedDistance     += dot(difference, sampleCount);		// accumulate only the samples closer to the light

        const bool excludeOutlier = true;
        if (!excludeOutlier) {
            minDifference = difference.x > 0.0f ? min(minDifference, difference.x) : minDifference;
            minDifference = difference.y > 0.0f ? min(minDifference, difference.y) : minDifference;
            minDifference = difference.z > 0.0f ? min(minDifference, difference.z) : minDifference;
            minDifference = difference.w > 0.0f ? min(minDifference, difference.w) : minDifference;
        } else {
            // From each group of four, exclude the smallest as an outlier and find the next smallest
            // this is an experiment to try to cut down on the noise slightly
            // It reduces the amount of noise considerably, but might be a bit subjective
            difference += (difference < 0.f) * minDifferenceInitial; // add massive amount to samples we want to ignore
            float m = min(min(min(difference.x, difference.y), difference.z), difference.w);
            difference += (difference == m) * minDifferenceInitial;
            // The following approach is technically more accurate (since if there are 2 identical samples, only
            // one will be rejecected). But in practice it doesn't seem to matter. Perhaps if the light, caster and lit
            // surface were all arranged very neatly it might matter
            // if (difference.x < difference.y) {
            //     if (difference.x < difference.z && difference.x < difference.w) difference.x += minDifferenceInitial;
            //     else if (difference.z < difference.w) difference.z += minDifferenceInitial;
            //     else difference.w += minDifferenceInitial;
            // } else {
            //     if (difference.y < difference.z && difference.y < difference.w) difference.y += minDifferenceInitial;
            //     else if (difference.z < difference.w) difference.z += minDifferenceInitial;
            //     else difference.w += minDifferenceInitial;
            // }
            m = min(min(min(difference.x, difference.y), difference.z), difference.w);
            minDifference = min(minDifference, m);
        }
    }

    // if we didn't find anything that looks like a caster, we should return 0
    // This will ensure the final shadow testing will be done with a smaller kernal
    // This reduces the possibility of acne in unshadows pixels significantly
    if (minDifference == minDifferenceInitial) return 0.f;

    return minDifference + extraBias;
    // return accumulatedDistance / accumulatedSampleCount;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   P E R C E N T A G E   C L O S E R
///////////////////////////////////////////////////////////////////////////////////////////////////

float TestShadow(float2 texCoord, uint arrayIndex, float comparisonDistance)
{
    // these two methods should return the same result (and probably have similiar performance...)
    const bool useGatherCmpRed = false;
    if (!useGatherCmpRed) {
        // SampleCmpLevelZero cannot be used when cross compiling via glsl, because there is no textureLod() override
        // for a a sampler2DArrayShadow 
        #if !defined(HLSLCC)
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
    float noiseValue = GetNoisyValue(randomizerValue, 0);
    float2 filterRotation;
    sincos(2.0f * pi * noiseValue, filterRotation.x, filterRotation.y);
    filterRotation *= filterSizeNorm;

    float weightTotal = 0.0f;

    // We want to use the "filterPlane" to detect if the shadow map point is on the same geometric plane as the surface we're sampling
    // We need to use a little bit of extra bias to make that work, though, to order to compensate for lost precision in the math 
    // along the way -- particularly due to rounding for the integer depth representations
    const float filterPlaneBiasFactor = 1.f/1024.f;
    comparisonDistance += filterPlaneBiasFactor * max(abs(filterPlane.x), abs(filterPlane.y));

    const bool doFilterRotation = true;
    float shadowingTotal = 0.0f;
    #if MSAA_SAMPLES <= 1
        const uint sampleCount = 32;
        const uint sampleOffset = 0;
        const uint loopCount = sampleCount / 4;
    #else
        const uint sampleCount = 4;		// We will be blending multiple samples, anyway... So minimize sample count for MSAA
        const uint sampleOffset = msaaSampleIndex * (GetRawShadowSampleKernelSize()-sampleCount) / (MSAA_SAMPLES-1);
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

    return shadowingTotal * (1.0f / float(sampleCount));
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
    float4 miniProjection, float comparisonDistance, float searchSize, float2 filterPlane,
    int2 randomizerValue, uint msaaSampleIndex)
{
    float casterDistance = CalculateShadowCasterDistance(
        shadowTexCoord, comparisonDistance, cascadeIndex, searchSize, filterPlane,
        msaaSampleIndex, GetNoisyValue(randomizerValue, 1));

    float projectionScale = miniProjection.x;	// (note -- projectionScale.y is ignored. We need to have uniform x/y scale to rotate the filter correctly)

    float filterSizeNorm;
    if (!ShadowsPerspectiveProjection) {

            // For ReverseZ modes, we need to negate casterDistance for the NDCDepthDifferenceToWorldSpace_Ortho call to
            // make any sense. We've already reversed depth inside of CalculateShadowCasterDistance in order to get
            // a positive casterDistance. But NDCDepthDifferenceToWorldSpace_Ortho expects two values directly from
            // the projection calculation without any compensation for ReverseZ
        casterDistance = -casterDistance;

            // In orthogonal projection mode, NDC depths are actually linear. So, we can convert a difference
            // of depths in NDC space (like casterDistance) into world space depth easily. Linear depth values
            // are more convenient for calculating the shadow filter radius
        float worldSpaceCasterDistance = NDCDepthDifferenceToWorldSpace_Ortho(casterDistance, AsMiniProjZW(miniProjection));

        filterSizeNorm = TanBlurAngle * worldSpaceCasterDistance * projectionScale;
    } else {
            //	There are various ways to calculate the filtering distance here...
            //	For example, we can assume the light source is an area light source, and
            //	calculate the appropriate penumbra for that object. But let's just use
            //	a simple method and calculate a fixed penumbra angle
        filterSizeNorm = TanBlurAngle * casterDistance * projectionScale;
    }

    return filterSizeNorm;
}

float SampleDMShadows(	uint cascadeIndex, float2 shadowTexCoord, float3 cascadeSpaceNormal,
                        float4 miniProjection, float maxBlurNorm,
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
    if (ShadowsPerspectiveProjection) {
        float worldSpaceDepth = NDCDepthToWorldSpace_Perspective(comparisonDistance, AsMiniProjZW(miniProjection));
        biasedDepth = WorldSpaceDepthToNDC_Perspective(worldSpaceDepth - ShadowWorldSpaceResolveBias, AsMiniProjZW(miniProjection));
    } else {
        #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
            biasedDepth = comparisonDistance - WorldSpaceDepthDifferenceToNDC_Ortho(ShadowWorldSpaceResolveBias * cascadeSpaceNormal.z, AsMiniProjZW(miniProjection));
        #endif
    }

    float2 filterPlane;
    const bool calculateFilterPlaneFromNormal = true;
    if (calculateFilterPlaneFromNormal) {
        filterPlane = float2(-cascadeSpaceNormal.x / cascadeSpaceNormal.z, -cascadeSpaceNormal.y / cascadeSpaceNormal.z);
        #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
            filterPlane *= OrthoShadowCascadeScale[cascadeIndex].zz / OrthoShadowCascadeScale[cascadeIndex].xy;
        #endif
        filterPlane *= 2.0f; // scale here by 2.0 to compensate for the viewport transform (ie, ndc [-1,1] -> [0, 1])
    } else {
        filterPlane = CalculateFilterPlane_ScreenSpaceDerivatives(comparisonDistance, shadowTexCoord);
    }

    float filterSize = MinBlurRadiusNorm;
    if (config._doContactHardening) {
        // We need a world space maximum search distance here; even though we later clamp the
        // maximum distance in pixel space. This is to get consistency between the cascades -- because
        // if we search further in larger cascades, it emphasizes the differences between them
        // Also don't use maxBlurNorm, because we don't want the transitionary effect on this lookup
        const float blockerFilterToMainFilterRatio = 0.5;
        #if (SHADOW_CASCADE_MODE == SHADOW_CASCADE_MODE_ORTHOGONAL)
            float searchSize = OrthoShadowCascadeScale[cascadeIndex].x / OrthoShadowCascadeScale[0].x * blockerFilterToMainFilterRatio * MaxBlurRadiusNorm;
        #else
            float searchSize = blockerFilterToMainFilterRatio * MaxBlurRadiusNorm;
        #endif
        filterSize = CalculateFilterSize(
            cascadeIndex, shadowTexCoord, miniProjection, biasedDepth, 
            searchSize, filterPlane, randomizerValue, msaaSampleIndex);

        // return (filterSize - MinBlurRadiusNorm) / (maxBlurNorm - MinBlurRadiusNorm);

        // If the filter size is very small, let's do a cheaper single tap test (offten a pretty fair amount of samples end up here)
        [branch] if (filterSize <= 1/ShadowTextureSize)
            return TestShadow(shadowTexCoord, cascadeIndex, biasedDepth);

        filterSize = min(max(filterSize, MinBlurRadiusNorm), maxBlurNorm);
    } else {
        filterSize = max(MinBlurRadiusNorm, maxBlurNorm);
    }

    return CalculateFilteredShadows(
        shadowTexCoord, biasedDepth, cascadeIndex, filterSize, filterPlane, randomizerValue,
        msaaSampleIndex, config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //   R E S O L V E
///////////////////////////////////////////////////////////////////////////////////////////////////

float ResolveShadows_Cascade(
    CascadeAddress cascade,
    int2 randomizerValue, uint msaaSampleIndex,
    ShadowResolveConfig config)
{
    [branch] if (cascade.cascadeIndex < 0) return 1.0f;

    float2 texCoords;
    float comparisonDistance;
    texCoords = cascade.frustumCoordinates.xy / cascade.frustumCoordinates.w;
    #if VULKAN      // hack for NDC handiness in Vulkan
        texCoords = 0.5.xx + 0.5f * texCoords.xy;
    #else
        texCoords = float2(0.5f + 0.5f * texCoords.x, 0.5f - 0.5f * texCoords.y);
    #endif

    comparisonDistance = cascade.frustumCoordinates.z / cascade.frustumCoordinates.w;

            // 	When hybrid shadows are enabled, the first cascade might be
            //	resolved using ray traced shadows. For convenience, we'll assume
            //	the the ray traced shadow cascade always matches the first cascade
            //	of the depth map shadows...
            //	We could alternatively have a completely independent cascade; but
            //	that would make doing the hybrid blend more difficult
    if (config._hasHybridRT && cascade.cascadeIndex==0) {
        return SampleRTShadows(cascade.frustumCoordinates.xyz/cascade.frustumCoordinates.w, randomizerValue);
    }

    return SampleDMShadows(
        cascade.cascadeIndex, texCoords, cascade.frustumSpaceNormal, cascade.miniProjection, cascade.maxBlurNorm, 
        comparisonDistance, randomizerValue, msaaSampleIndex, config);
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
