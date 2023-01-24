// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "xleres/TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "xleres/TechniqueLibrary/Math/Misc.hlsl"
#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"
#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

RWTexture2D<float> Working;
Texture2D<float> FullResolutionDepths;
Texture2D<float> HierarchicalDepths;
RWTexture2D<float> DownsampleDepths;
Texture2D InputNormals;

cbuffer AOProps
{
	uint SearchSteps;					// 32
	float MaxWorldSpaceDistanceSq;		// 1
	uint FrameIdx;
	uint ClearAccumulation;
	float ThicknessHeuristicFactor;		// 0.15
}

// #define BOTH_WAYS 1
// #define DITHER3x3 1
// #define HAS_HIERARCHICAL_DEPTHS 1
// #define DO_LATE_TEMPORAL_FILTERING 1
// #define ORTHO_CAMERA 1
// #define ENABLE_HIERARCHICAL_STEPPING 1
// #define ENABLE_FILTERING 1
// #define ENABLE_THICKNESS_HEURISTIC 1
// #define HAS_HISTORY_CONFIDENCE_TEXTURE 1

static const uint FrameWrap = 6;

// static const uint DitherTable[96] = {24, 72, 0, 48, 60, 12, 84, 36, 90, 42, 66, 18, 6, 54, 30, 78, 7, 91, 61, 25, 55, 43, 13, 73, 31, 67, 85, 1, 79, 19, 37, 49, 80, 20, 38, 50, 32, 68, 86, 2, 56, 44, 14, 74, 8, 92, 62, 26, 9, 57, 33, 81, 93, 45, 69, 21, 63, 15, 87, 39, 27, 75, 3, 51, 52, 4, 76, 28, 40, 88, 16, 64, 22, 70, 46, 94, 82, 34, 58, 10, 29, 65, 95, 11, 77, 17, 47, 59, 5, 89, 71, 35, 53, 41, 23, 83};
Buffer<uint> DitherTable;

float LoadHierarchicalDepth(uint2 coord, uint mipLevel)
{
	// Expecting mipLevel to always be >= 1 here. We don't include the most detailed resolution in the hierarchical depths
	return HierarchicalDepths.Load(uint3(coord.xy, mipLevel-1));
}

float TraverseRay_NonHierachicalDepths(uint2 pixelId, float cosPhi, float sinPhi, uint2 textureDims)
{
	// in world space units, how big is the distance between samples in the sample direction?
	// For orthogonal, this only depends on the angle phi since there's no foreshortening

	float d0 = LoadHierarchicalDepth(pixelId.xy, 1);
	[branch] if (d0 == 0) return 0;		// early out for sky, to avoid excessive noise in the image

	float maxCosTheta = 0.0;
	float xStep, yStep;
	float stepDistanceSq;
	if (abs(sinPhi) > abs(cosPhi)) {
		xStep = cosPhi / abs(sinPhi);
		yStep = sign(sinPhi);
	} else {
		xStep = sign(cosPhi);
		yStep = sinPhi / abs(cosPhi);
	}

	#if ORTHO_CAMERA
		float2 worldSpacePixelSize = 2 / SysUniform_GetMinimalProjection().xy / float2(textureDims.xy/2);
		worldSpacePixelSize *= float2(xStep, yStep);
		stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);
	#else
		float wsDepth0 = NDCDepthToWorldSpace_Perspective(d0, GlobalMiniProjZW());
		// worldSpacePixelSize calculation simplifed by assuming moving on constant depth plane (which will not be entirely correct)
		float2 worldSpacePixelSize = 2 / SysUniform_GetMinimalProjection().xy * wsDepth0 / float2(textureDims.xy/2);
		worldSpacePixelSize *= float2(xStep, yStep);
		stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);
	#endif

	float2 xy = pixelId.xy + float2(xStep, yStep);

	int c=1;
	for (; c<SearchSteps; ++c) {
		float d = LoadHierarchicalDepth(xy, 1);
		xy += float2(xStep, yStep);
		#if ORTHO_CAMERA
			float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		#else
			float worldSpaceDepthDifference = wsDepth0 - NDCDepthToWorldSpace_Perspective(d, GlobalMiniProjZW());
		#endif
		float C = float(c);		// maybe -0.5... or even 0.5. It depends if we wwant to see the pixels as cubes, pyramids or slopes?
		float azmuthalXSq = C * C * stepDistanceSq;

		//
		// cosTheta = dot(normalized(vector to test point), direction to eye)
		// the vector to test point in the azimuthal plane is just (x, worldSpaceDepthDifference)
		// and direction to eye is always (0, 1) -- (assuming orthogonal, or at least ignoring distortion from persective)
		// so we can just take worldSpaceDepthDifference / length(vector to test point)
		//
		float cosTheta = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);
		maxCosTheta = max(cosTheta, maxCosTheta);

		// note -- thickness heuristic
	}

	return maxCosTheta;
}

float TraverseRay_HierachicalDepths_1(uint2 pixelId, float cosPhi, float sinPhi, uint2 mostDetailedMipDims)
{
	// There are 2 ways to ray march through the depth field
	//	a) visit every pixel that intersects the given ray even if the ray only touches the edge
	//	b) take the longest axis and step along that; even if it means missing some intersecting pixels
	// type (b) is a lot more efficient, and just seems to produce few artifacts. Since we're sampling
	// so many directions, the skipped pixels will still be accounted for; so type (B) just seems better
	float d0 = LoadHierarchicalDepth(pixelId.xy, 1);
	[branch] if (d0 == 0) return 0;		// early out for sky, to avoid excessive noise in the image

	float maxCosTheta = 0.0;
	float xStep, yStep;
	float stepDistanceSq;
	if (abs(sinPhi) > abs(cosPhi)) {
		xStep = cosPhi / abs(sinPhi);
		yStep = sign(sinPhi);
	} else {
		xStep = sign(cosPhi);
		yStep = sinPhi / abs(cosPhi);
	}

	const uint mostDetailedMipLevel = 1;		// mostDetailedMipLevel must be >= 1, because we don't store the most detailed depth layer in HierarchicalDepths
	uint currentMipLevel = mostDetailedMipLevel;
	float currentMipLevelScale = exp2(currentMipLevel-mostDetailedMipLevel);

	#if ORTHO_CAMERA
		float2 worldSpacePixelSize = 2 / SysUniform_GetMinimalProjection().xy / mostDetailedMipDims;
		worldSpacePixelSize *= float2(xStep, yStep);
		stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);
	#else
		float wsDepth0 = NDCDepthToWorldSpace_Perspective(d0, GlobalMiniProjZW());
		// worldSpacePixelSize calculation simplifed by assuming moving on constant depth plane (which will not be entirely correct)
		float2 worldSpacePixelSize = 2 / SysUniform_GetMinimalProjection().xy * wsDepth0 / mostDetailedMipDims;
		worldSpacePixelSize *= float2(xStep, yStep);
		stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);
	#endif

	const float maxWSDistanceSq = MaxWorldSpaceDistanceSq;

	const float initialStepSize = 1;
	float2 xy = pixelId.xy + initialStepSize*float2(xStep, yStep);
	int c=SearchSteps;
	float stepsAtMostDetailedRes = initialStepSize;
	float lastCosTheta = 1;
	while (c--) {
		if (any(xy < 0 || xy >= mostDetailedMipDims)) break;
		float d = LoadHierarchicalDepth(xy/currentMipLevelScale, currentMipLevel);

		#if ORTHO_CAMERA
			float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		#else
			float worldSpaceDepthDifference = wsDepth0 - NDCDepthToWorldSpace_Perspective(d, GlobalMiniProjZW());
		#endif
		float azmuthalXSq = stepsAtMostDetailedRes * stepsAtMostDetailedRes * stepDistanceSq;
		if (azmuthalXSq > maxWSDistanceSq) break;

		float cosTheta = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);

		// This is the "thickness' heutristic from the gtoa paper. It helps reduce the intensity of AO shadows around thin objects, and
		// also takes the edge of some of the less desirable effects of the method.
		// The idea is to soften out changes in the "cosTheta" value by tracking the exponential moving average of cosTheta, rather than
		// using the raw values directly. For big features, this should have little impact, because the average will catch up to real
		// values very quickly -- and while the final calculated angle will be a little wrong, it don't be significantly of.
		// The justification given in the paper is to say that features tend to be roughly as thick as they are wide, and therefore
		// using width is valid. This is a little misleading, though, because even thin features do end up creating AO shadows.
		// We could extend this concept further, though, if we could make a more accurate guess at the width of occluder and comparing
		// it to worldSpaceDepthDifference.
		// Either way, this idea doesn't need to be accurate to work well, and I really like the overall idea.
		bool improveAccuracy = cosTheta > maxCosTheta;
		#if defined(ENABLE_THICKNESS_HEURISTIC)
			if (cosTheta > lastCosTheta)
				cosTheta = lerp(lastCosTheta, cosTheta, ThicknessHeuristicFactor*currentMipLevel);
			lastCosTheta = cosTheta;
		#endif
		
		// Shift the mip level we're testing up or down to either refine the estimate or attempt to make a bigger step
		// on the next loop iteration
		// Here, we're assuming that the depth downsampling is using a min() filter (ie, each depth value is the closest to the camera
		// of the source pixels from the next more detailed mip). But, it may look visually acceptable with other mip filters also
		#if defined(ENABLE_HIERARCHICAL_STEPPING)
			if (improveAccuracy) {
				if (currentMipLevel == mostDetailedMipLevel) {
					maxCosTheta = max(maxCosTheta, cosTheta);
					xy += float2(xStep, yStep);
					stepsAtMostDetailedRes += 1.0f;
				} else {
					// refine estimate at higher mip level before we commit to it
					--currentMipLevel;
					currentMipLevelScale /= 2;
				}
			} else {
				float2 startXY = xy;
				xy += float2(xStep, yStep)*currentMipLevelScale;
				stepsAtMostDetailedRes += currentMipLevelScale;

				// if we're on a boundary with the next mip level, then decrease
				float2 test = xy / (2*currentMipLevelScale);
				// bool nextMipLevelBoundary = any(frac(test + 0.125) < 0.25);
				bool nextMipLevelBoundary = any(uint2(startXY/(2*currentMipLevelScale)) != uint2(test));
				currentMipLevel += nextMipLevelBoundary;
				currentMipLevelScale *= nextMipLevelBoundary?2:1;
			}
		#else
			maxCosTheta = max(maxCosTheta, cosTheta);
			xy += float2(xStep, yStep);
			stepsAtMostDetailedRes += 1.0f;
		#endif
	}

	return maxCosTheta;
}

float TraverseRay(uint2 pixelId, float cosPhi, float sinPhi, uint2 mostDetailedMipDims)
{
	#if !defined(HAS_HIERARCHICAL_DEPTHS)
		return TraverseRay_NonHierachicalDepths(pixelId, cosPhi, sinPhi, mostDetailedMipDims);
	#else
		return TraverseRay_HierachicalDepths_1(pixelId, cosPhi, sinPhi, mostDetailedMipDims);
	#endif
}

uint Dither3x3PatternInt(uint2 pixelCoords)
{
	uint ditherArray[9] =
	{
		2, 7, 0,
		5, 1, 3,
		8, 4, 6,
	};
	uint2 t = pixelCoords.xy%3;
	return ditherArray[t.x+t.y*3];
}

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
#if !defined(HAS_HIERARCHICAL_DEPTHS)
	uint2 textureDims;
	FullResolutionDepths.GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x+(2*8)-1)/(2*8), (textureDims.y+(2*8)-1)/(2*8));
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);

	///////////// downsample ///////////
	float zero  = FullResolutionDepths.Load(uint3(pixelId.xy*2, 0));
	float one   = FullResolutionDepths.Load(uint3(pixelId.xy*2 + uint2(1,0), 0));
	float two   = FullResolutionDepths.Load(uint3(pixelId.xy*2 + uint2(0,1), 0));
	float three = FullResolutionDepths.Load(uint3(pixelId.xy*2 + uint2(1,1), 0));
	if (((pixelId.x+pixelId.y)&1) == 0) {
		DownsampleDepths[pixelId.xy] = min(min(min(zero, one), two), three);
	} else {
		DownsampleDepths[pixelId.xy] = max(max(max(zero, one), two), three);
	}
	//////////////////////////////////
	AllMemoryBarrierWithGroupSync();
#else
	uint2 textureDims;
	Working.GetDimensions(textureDims.x, textureDims.y);
	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	if (any(pixelId >= textureDims)) return;
#endif

#if !defined(DITHER3x3)
	// The cost of looking up the dither pattern here is not trivially cheap; but you have a big impact
	// on the visual result... If we had a way of doing this without a table lookup it might be a bit faster
	uint idx = DitherTable[(pixelId.x%4)+(pixelId.y%4)*4+(FrameIdx%6)*16];
	#if BOTH_WAYS
		float phi = idx / 96.0 * 3.14159;
	#else
		float phi = idx / 96.0 * 2.0 * 3.14159;
	#endif
#else
	uint frameIdxOrder[] = { 0, 4, 2, 5, 3 };
	// uint frameIdxOrder[] = { 0, 10, 5, 2, 8, 11, 3, 6, 1, 9, 4, 7 };
	uint ditherValue = Dither3x3PatternInt(pixelId.xy);
	uint idx = frameIdxOrder[FrameIdx%FrameWrap] * 9 + ditherValue * FrameWrap;
	#if BOTH_WAYS
		float phi = frac(idx / 54.0) * 3.14159;
	#else
		float phi = frac(idx / 54.0) * 2.0 * 3.14159;
	#endif
#endif

	float cosPhi, sinPhi;
	sincos(phi, sinPhi, cosPhi);
	float maxCosTheta = TraverseRay(pixelId, cosPhi, sinPhi, textureDims);

#if BOTH_WAYS
	float cosMaxTheta2 = TraverseRay(pixelId, -cosPhi, -sinPhi, textureDims);
#endif

	//
	// GTOA analytic integral
	// 0.25 * (-cos (2*theta - gamma) + cos(gamma) + 2 * theta * sin(gamma))
	//
	// This is one half of a-hat; however the other half is identical, just with the
	// angle from the other direction. We don't have to calculate the other half as in the exact  
	// other direction here; because in effect we will be combining this result with the samples for
	// many other "phi" directions. 
	//
	// it looks like we need both cos(theta) and theta; I'm not sure if trigonometric idenities are
	// going to help us simplify this too much either.
	//
	// Here's an alternative form (A is theta, B is gamma)
	// 2 A sin(B) + cos^2(A) (-cos(B)) + sin^2(A) cos(B) - 2 sin(A) cos(A) sin(B) + cos(B)
	//

	float3 worldSpaceNormal = DecodeGBufferNormal(InputNormals.Load(uint3(pixelId.xy*2, 0)).rgb);

	float3 cameraRight = float3(SysUniform_GetCameraBasis()[0].x, SysUniform_GetCameraBasis()[1].x, SysUniform_GetCameraBasis()[2].x);
	float3 cameraUp = float3(SysUniform_GetCameraBasis()[0].y, SysUniform_GetCameraBasis()[1].y, SysUniform_GetCameraBasis()[2].y);
	float3 negCameraForward = float3(SysUniform_GetCameraBasis()[0].z, SysUniform_GetCameraBasis()[1].z, SysUniform_GetCameraBasis()[2].z);
	float projNormX = dot(cameraRight * cosPhi - cameraUp * sinPhi, worldSpaceNormal);
	float projNormZ = dot(negCameraForward, worldSpaceNormal);
	float reciprocalMagProjectedWorldSpaceNormal = rsqrt(projNormX*projNormX+projNormZ*projNormZ);
	float cosGamma = saturate(projNormZ*reciprocalMagProjectedWorldSpaceNormal);
	float gamma = acos(cosGamma);
	float sinGamma = sin(gamma);

	float maxTheta = acos(min(1,maxCosTheta));
	float gtoa = cosGamma + 2 * maxTheta * sinGamma - cos(2*maxTheta - gamma);	// 0.25f * reciprocalMagProjectedWorldSpaceNormal below

#if BOTH_WAYS
	float maxTheta2 = acos(min(1,cosMaxTheta2));
	float gtoa2 = cosGamma + 2 * maxTheta2 * sinGamma - cos(2*maxTheta2 - gamma);	// 0.25f * reciprocalMagProjectedWorldSpaceNormal below

	float final = gtoa + gtoa2;
	final *= 0.25; // / reciprocalMagProjectedWorldSpaceNormal;
#else
		// Jimenez et al divide by reciprocalMagProjectedWorldSpaceNormal here, but that creates extra darkening on surfaces sloped
		// relative to the camera. It's also not completely clear what the motivation is, given that gamma is just an angle on the
		// disc we're sampling, and the equations above should find reliably. For now, we seem to get a better result with that
		// term ommitted.
		// (also don't need to divide by pi here -- we're taking the integral 0->pi then dividing by pi. The pis just cancel out)
	float final = gtoa * 2.0 * 0.25; 
#endif

#if defined(ENABLE_FILTERING) && !defined(DO_LATE_TEMPORAL_FILTERING)
	int2 vel = GBufferMotion.Load(uint3(pixelId.xy*2, 0)).rg;
	float accumulationYesterday = AccumulationAOLast.Load(uint3(pixelId.xy + vel / 2, 0));
	float2 diff = accumulationYesterdayPos.xy - float2(pixelId.xy);
	float magSq = dot(diff, diff);

	// We have to set the "Nvalue" here to a multiple of frameWrap, or we will start to get
	// a strobing effect. Just tweaking this for what looks right
	// (also tone it down a little bit when we've moved considerably from the previous sample)
	float Nvalue = FrameWrap*2;
	float alpha = 2.0/(Nvalue+1.0);
	alpha = 1-alpha;
	alpha *= HistoryAcc.Load(uint3(pixelId.xy*2, 0));		// scale alpha by our confidence in the "yesterday" data
	float accumulationToday = accumulationYesterday * alpha + final * (1-alpha);
	Working[pixelId.xy] = accumulationToday;
#else
	Working[pixelId.xy] = final;
#endif
}

#include "mosaic-denoise.hlsl"
#include "history-confidence.hlsl"

RWTexture2D<float> OutputTexture;

Texture2D<int2> GBufferMotion;
Texture2D<float> HistoryAcc;

RWTexture2D<float> AccumulationAO;
Texture2D<float> AccumulationAOLast;

Texture2D<float> DepthPrev;
Texture2D<float4> GBufferNormalPrev;

void WriteOutputTexture(uint2 coords, ValueType value) { OutputTexture[coords] = value; }
DepthType LoadFullResDepth(uint2 coords) { return FullResolutionDepths.Load(uint3(coords, 0)); }
DepthType LoadDownsampleDepth(uint2 coords) { return HierarchicalDepths.Load(uint3(coords, 1-1)); }
ValueType LoadWorking(uint2 coords) { return Working[coords]; }
void WriteAccumulation(uint2 coords, ValueType newValue) { AccumulationAO[coords] = newValue; }
ValueType LoadAccumulationPrev(uint2 coords) { return AccumulationAOLast[coords]; }
int2 LoadMotion(uint2 coords) { return GBufferMotion.Load(uint3(coords, 0)).rg; }

float4 NormalAndRoughnessFromGBuffer(float4 sample)
{
	float3 normal = DecodeGBufferNormal(sample.xyz);
	float roughness = sample.w;
	return float4(normal, roughness);
}
float4 LoadNormalAndRoughnessPrev(uint2 loadPos) { return NormalAndRoughnessFromGBuffer(GBufferNormalPrev.Load(int3(loadPos, 0))); }
float LoadDepthPrev(uint2 loadPos) { return DepthPrev.Load(uint3(loadPos, 0)); }

FloatType LoadHistoryConfidence(uint2 coords, int2 motion)
{
	#if HAS_HISTORY_CONFIDENCE_TEXTURE
		return HistoryAcc.Load(uint3(coords, 0));
	#else
		// if we don't have a precompute history confidence texture, we have to calculate it now
		float4 todayNormalAndRoughness = NormalAndRoughnessFromGBuffer(InputNormals.Load(int3(coords, 0)));
		int2 depthPrevDims;
		DepthPrev.GetDimensions(depthPrevDims.x, depthPrevDims.y);
		return CalculatePixelHistoryConfidence(
			coords, motion,
			todayNormalAndRoughness.xyz, todayNormalAndRoughness.w, FullResolutionDepths.Load(int3(coords, 0)),
			depthPrevDims);
	#endif
}

FloatType GetNValue() { return FrameWrap*2; }
FloatType GetVariationTolerance() { return 2.5f; }

[numthreads(8, 8, 1)]
	void UpsampleOp(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 downsampleDims;
	Working.GetDimensions(downsampleDims.x, downsampleDims.y);

	// Not sure that the thread group tiling actually helps any more with better use of group shared
	// memory. It doesn't seem particularly impactful with some basic profiling 
	// See also FFX_DNSR_Reflections_RemapLane8x8() in GPUOpen repo for AMD's approach to this 
	#if 0
		uint2 threadGroupCounts = uint2((downsampleDims.x+8-1)/8, (downsampleDims.y+8-1)/8);
		uint2 downsampledCoord = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 16, groupThreadId.xy, groupId.xy);
		groupId.xy = downsampledCoord.xy/8;
		groupThreadId.xy = downsampledCoord.xy-groupId.xy*8;
	#endif

	MosiacDenoiseUpsample(groupThreadId.xy, groupId.xy, downsampleDims);
}
