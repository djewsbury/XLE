#include "xleres/TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "xleres/TechniqueLibrary/Math/Misc.hlsl"
#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

Texture2D<float> InputTexture : register(t1, space1);
RWTexture2D<float> OutputTexture : register(u2, space1);
RWTexture2D<float> DownsampleDepths : register(u3, space1);
RWTexture2D<float> AccumulationAO : register(u4, space1);
Texture2D InputNormals : register(t6, space1);
Texture2D<int2> GBufferMotion : register(t7, space1);
Texture2D<float> AccumulationAOLast : register(t8, space1);

Texture2D<float> HierarchicalDepths : register(t9, space1);

cbuffer AOProps : register(b5, space1)
{
	uint FrameIdx;
	bool ClearAccumulation;
}

// #define BOTH_WAYS 1
// #define DITHER3x3 1
#define HAS_HIERARCHICAL_DEPTHS 1
#define DO_LATE_TEMPORAL_FILTERING 1

float TraverseRay_NonHierachicalDepths(uint2 pixelId, float cosPhi, float sinPhi, uint2 textureDims)
{
	// in world space units, how big is the distance between samples in the sample direction?
	// For orthogonal, this only depends on the angle phi since there's no foreshortening
	float2 orthoProjSize = 100.0;		// hard coded for the particular camera we're using

	float cosMaxTheta = 0.0;
	float xStep, yStep;
	float stepDistanceSq;
	if (abs(sinPhi) > abs(cosPhi)) {
		xStep = cosPhi / abs(sinPhi);
		yStep = sign(sinPhi);
	} else {
		xStep = sign(cosPhi);
		yStep = sinPhi / abs(cosPhi);
	}

	float2 worldSpacePixelSize = orthoProjSize / float2(textureDims.xy/2);
	worldSpacePixelSize *= float2(xStep, yStep);
	stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);

	float2 xy = pixelId.xy + float2(xStep, yStep);

	float d0 = HierarchicalDepths.Load(uint3(pixelId.xy, 1));
	int c=1;
	for (; c<8; ++c) {
		float d = HierarchicalDepths.Load(uint3(xy, 1));
		xy += float2(xStep, yStep);
		float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		float C = float(c);		// maybe -0.5... or even 0.5. It depends if we wwant to see the pixels as cubes, pyramids or slopes?
		float azmuthalXSq = C * C * stepDistanceSq;

		//
		// cosTheta = dot(normalized(vector to test point), direction to eye)
		// the vector to test point in the azimuthal plane is just (x, worldSpaceDepthDifference)
		// and direction to eye is always (0, 1) -- (assuming orthogonal, or at least ignoring distortion from persective)
		// so we can just take worldSpaceDepthDifference / length(vector to test point)
		//
		float cosTheta = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);
		cosMaxTheta = max(cosTheta, cosMaxTheta);

		// note -- thickness heuristic
	}

	return cosMaxTheta;
}

float TraverseRay_HierachicalDepths_0(uint2 pixelId, float cosPhi, float sinPhi, uint2 textureDims)
{
	float2 orthoProjSize = 100.0;		// hard coded for the particular camera we're using
	float cosMaxTheta = 0.0;

	float2 direction = float2(cosPhi, sinPhi);
	float2 xyJumpFromFloor = direction.xy < 0 ? 0 : 1;
	float2 uvOffset = float2(0.005,0.005) * exp2(1) / textureDims;	// shift just off the pixel edge when iterating
	// uvOffset *= 1000;
	uvOffset = direction.xy < 0 ? -uvOffset : uvOffset;

	const uint mostDetailedMipLevel = 1;
	uint currentMipLevel = mostDetailedMipLevel;
	float2 currentMipRes = textureDims/exp2(currentMipLevel);
	float2 currentUV = pixelId.xy / float2(textureDims/2);
	float2 initialUV = currentUV;
	float d0 = HierarchicalDepths.Load(uint3(currentUV*currentMipRes, currentMipLevel));

	const bool oneFreeJump = false;
	if (oneFreeJump) {
		float2 xyPlane = floor(currentUV * currentMipRes) + xyJumpFromFloor;
		xyPlane = xyPlane/currentMipRes+uvOffset;
		float2 t = (xyPlane - initialUV)/direction;
		currentUV = initialUV + min(t.x, t.y) * direction;
	}

	int c=0;
	for (; c<64; ++c) {
		float2 xyPlane = floor(currentUV * currentMipRes) + xyJumpFromFloor;
		// uvOffset = float2(0.005,0.005) * exp2(currentMipLevel) / textureDims;
		// uvOffset = direction.xy < 0 ? -uvOffset : uvOffset;
		xyPlane = xyPlane/currentMipRes+uvOffset;
		float2 t = (xyPlane - initialUV)/direction;
		float2 uvMovement = min(t.x, t.y) * direction;
		float2 newUV = initialUV + uvMovement;
		
		if (any(newUV.xy < 0) || any(newUV.xy >= 1)) break;
		if (WaveActiveCountBits(true) <= 8) break;	// exit due to low occupancy

		float azmuthalXSq;
		const bool accurateDistanceCalc = true;
		float2 texel = floor(newUV*currentMipRes);
		if (accurateDistanceCalc) {
			float2 center = (texel+0.5.xx+0.5*direction)/currentMipRes;
			float distanceToCenter = dot(direction, center-initialUV);
			// float distanceToCenter = length(center-initialUV);
			azmuthalXSq = dot(distanceToCenter*direction*orthoProjSize, distanceToCenter*direction*orthoProjSize);
		} else {
			azmuthalXSq = dot(uvMovement*orthoProjSize, uvMovement*orthoProjSize);
		}

		float d = HierarchicalDepths.Load(uint3(texel, currentMipLevel));
		float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());

		float cosTheta = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);
		
		// Shift the mip level we're testing up or down to either refine the estimate or attempt to make a bigger step
		// on the next loop iteration
		// Here, we're assuming that the depth downsampling is using a min() filter (ie, each depth value is the closest to the camera
		// of the source pixels from the next more detailed mip). But, it may look visually acceptable with other mip filters also
#if 1
		if (cosTheta > cosMaxTheta) {
			if (currentMipLevel == mostDetailedMipLevel) {
				cosMaxTheta = cosTheta;
				currentUV = newUV;
			} else {
				// refine estimate at higher mip level before we commit to it
				--currentMipLevel;
				currentMipRes *= 2;
			}
		} else {
			currentUV = newUV;
			// if we're on a boundary with the next mip level, then decrease
			float2 nextMipRes = currentMipRes*0.5;
			float2 test = (currentUV-uvOffset) * nextMipRes;
			bool nextMipLevelBoundary = any(frac(test + 0.125) < 0.25);
			currentMipLevel += nextMipLevelBoundary;
			currentMipRes = nextMipLevelBoundary ? nextMipRes : currentMipRes;
		}
#else
		cosMaxTheta = max(cosMaxTheta, cosTheta);
		currentUV = newUV;
#endif
	}

	// cosMaxTheta = float(c)/32;
	return cosMaxTheta;
}

float TraverseRay_HierachicalDepths_1(uint2 pixelId, float cosPhi, float sinPhi, uint2 textureDims)
{
	// There are 2 ways to ray march through the depth field
	//	a) visit every pixel that intersects the given ray even if the ray only touches the edge
	//	b) take the longest axis and step along that; even if it means missing some intersecting pixels
	// type (b) is a lot more efficient, and just seems to produce few artifacts. Since we're sampling
	// so many directions, the skipped pixels will still be accounted for; so type (B) just seems better
	float2 orthoProjSize = 100.0;		// hard coded for the particular camera we're using
	float cosMaxTheta = 0.0;

	float xStep, yStep;
	float stepDistanceSq;
	if (abs(sinPhi) > abs(cosPhi)) {
		xStep = cosPhi / abs(sinPhi);
		yStep = sign(sinPhi);
	} else {
		xStep = sign(cosPhi);
		yStep = sinPhi / abs(cosPhi);
	}

	const uint mostDetailedMipLevel = 1;
	uint currentMipLevel = mostDetailedMipLevel;
	float2 mostDetailedMipRes = textureDims*pow(0.5, mostDetailedMipLevel);
	float currentMipLevelScale = exp2(currentMipLevel-mostDetailedMipLevel);

	float2 worldSpacePixelSize = orthoProjSize / mostDetailedMipRes;
	worldSpacePixelSize *= float2(xStep, yStep);
	stepDistanceSq = dot(worldSpacePixelSize, worldSpacePixelSize);

	float d0 = HierarchicalDepths.Load(uint3(pixelId.xy, 1));
	const float initialStepSize = 1;
	float2 xy = pixelId.xy + initialStepSize*float2(xStep, yStep);
	int c=1;
	float stepsAtMostDetailedRes = initialStepSize;
	for (; c<32; ++c) {
		if (any(xy < 0) || any(xy >= mostDetailedMipRes)) break;
		if (WaveActiveCountBits(true) <= 8) break;	// exit due to low occupancy

		float d = HierarchicalDepths.Load(uint3(xy/currentMipLevelScale, currentMipLevel));

		float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		float azmuthalXSq = stepsAtMostDetailedRes * stepsAtMostDetailedRes * stepDistanceSq;
		float cosTheta = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);
		
		// Shift the mip level we're testing up or down to either refine the estimate or attempt to make a bigger step
		// on the next loop iteration
		// Here, we're assuming that the depth downsampling is using a min() filter (ie, each depth value is the closest to the camera
		// of the source pixels from the next more detailed mip). But, it may look visually acceptable with other mip filters also
#if 1
		if (cosTheta > cosMaxTheta) {
			if (currentMipLevel == mostDetailedMipLevel) {
				cosMaxTheta = cosTheta;
				xy += float2(xStep, yStep);
				stepsAtMostDetailedRes += 1.0f;
			} else {
				// refine estimate at higher mip level before we commit to it
				--currentMipLevel;
				currentMipLevelScale /= 2;
			}
		} else {
			xy += float2(xStep, yStep)*currentMipLevelScale;
			stepsAtMostDetailedRes += currentMipLevelScale;

			// if we're on a boundary with the next mip level, then decrease
			float2 test = xy / (2*currentMipLevelScale);
			bool nextMipLevelBoundary = any(frac(test + 0.125) < 0.25);
			currentMipLevel += nextMipLevelBoundary;
			currentMipLevelScale *= nextMipLevelBoundary?2:1;
		}
#else
		cosMaxTheta = max(cosMaxTheta, cosTheta);
		xy += float2(xStep, yStep);
		stepsAtMostDetailedRes += 1.0f;
#endif
	}

	// cosMaxTheta = float(c)/64;
	return cosMaxTheta;
}

float TraverseRay(uint2 pixelId, float cosPhi, float sinPhi, uint2 textureDims)
{
	#if !defined(HAS_HIERARCHICAL_DEPTHS)
		return TraverseRay_NonHierachicalDepths(pixelId, cosPhi, sinPhi, textureDims);
	#else
		return TraverseRay_HierachicalDepths_1(pixelId, cosPhi, sinPhi, textureDims);
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

static const uint FrameWrap = 6;
// static const uint FrameWrap = 12;

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
#if !defined(HAS_HIERARCHICAL_DEPTHS)
	uint2 textureDims;
	InputTexture.GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x/2)/8, (textureDims.y/2)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);

	///////////// downsample ///////////
	float zero  = InputTexture.Load(uint3(pixelId.xy*2, 0));
	float one   = InputTexture.Load(uint3(pixelId.xy*2 + uint2(1,0), 0));
	float two   = InputTexture.Load(uint3(pixelId.xy*2 + uint2(0,1), 0));
	float three = InputTexture.Load(uint3(pixelId.xy*2 + uint2(1,1), 0));
	if (((pixelId.x+pixelId.y)&1) == 0) {
		DownsampleDepths[pixelId.xy] = min(min(min(zero, one), two), three);
	} else {
		DownsampleDepths[pixelId.xy] = max(max(max(zero, one), two), three);
	}
	//////////////////////////////////
	AllMemoryBarrierWithGroupSync();
#else
	uint2 textureDims;
	HierarchicalDepths.GetDimensions(textureDims.x, textureDims.y);
	uint2 threadGroupCounts = uint2((textureDims.x/2)/8, (textureDims.y/2)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
#endif

	uint frameIdxOrder[] = { 0, 4, 2, 5, 3 };
	// uint frameIdxOrder[] = { 0, 10, 5, 2, 8, 11, 3, 6, 1, 9, 4, 7 };

#if !defined(DITHER3x3)
	// The cost of looking up the dither pattern here is not trivially cheap; but you have a big impact
	// on the visual result... If we had a way of doing this without a table lookup it might be a bit faster
	// uint ditherValue = (pixelId.x%4)+(pixelId.y%4)*4;
	uint ditherValue = DitherPatternInt(pixelId.xy);
	uint idx = frameIdxOrder[FrameIdx%FrameWrap] * 16 + ditherValue * FrameWrap;
	#if BOTH_WAYS
		float phi = idx / 96.0 * 3.14159;
	#else
		float phi = idx / 96.0 * 2.0 * 3.14159;
	#endif
#else
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
	float cosMaxTheta = TraverseRay(pixelId, cosPhi, sinPhi, textureDims);

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

	float3 worldSpaceNormal = InputNormals.Load(uint3(pixelId.xy*2, 0)).rgb;		// todo -- maybe can avoid this normalize with a different encoding scheme
	if (dot(worldSpaceNormal, worldSpaceNormal) == 0) {
		AccumulationAO[pixelId.xy] = 0;
		return;
	}
	worldSpaceNormal = normalize(worldSpaceNormal);

	float3 cameraRight = float3(SysUniform_GetCameraBasis()[0].x, SysUniform_GetCameraBasis()[1].x, SysUniform_GetCameraBasis()[2].x);
	float3 cameraUp = float3(SysUniform_GetCameraBasis()[0].y, SysUniform_GetCameraBasis()[1].y, SysUniform_GetCameraBasis()[2].y);
	float3 negCameraForward = float3(SysUniform_GetCameraBasis()[0].z, SysUniform_GetCameraBasis()[1].z, SysUniform_GetCameraBasis()[2].z);
	float projNormX = dot(cameraRight * cosPhi - cameraUp * sinPhi, worldSpaceNormal);
	float projNormZ = dot(negCameraForward, worldSpaceNormal);
	float reciprocalMagProjectedWorldSpaceNormal = rsqrt(projNormX*projNormX+projNormZ*projNormZ);
	float cosGamma = saturate(projNormZ*reciprocalMagProjectedWorldSpaceNormal);
	float gamma = acos(cosGamma);
	float sinGamma = sin(gamma);

	float maxTheta = acos(min(1,cosMaxTheta));
	float gtoa = cosGamma + 2 * maxTheta * sinGamma - cos(2*maxTheta - gamma);	// 0.25f * reciprocalMagProjectedWorldSpaceNormal below

#if BOTH_WAYS
	float maxTheta2 = acos(min(1,cosMaxTheta2));
	float gtoa2 = cosGamma + 2 * maxTheta2 * sinGamma - cos(2*maxTheta2 - gamma);	// 0.25f * reciprocalMagProjectedWorldSpaceNormal below

	float final = gtoa + gtoa2;
	final *= 0.25 * magProjectedWorldSpaceNormal;
#else
		// Jimenez et al divide by reciprocalMagProjectedWorldSpaceNormal here, but that creates extra darkening on surfaces sloped
		// relative to the camera. It's also not completely clear what the motivation is, given that gamma is just an angle on the
		// disc we're sampling, and the equations above should find reliably. For now, we seem to get a better result with that
		// term ommitted.
		// (also don't need to divide by pi here -- we're taking the integral 0->pi then dividing by pi. The pis just cancel out)
	float final = gtoa * 2.0 * 0.25; 
#endif

#if !defined(DO_LATE_TEMPORAL_FILTERING)
	if (ClearAccumulation) {
		AccumulationAO[pixelId.xy] = final;
	} else {
		int2 vel = GBufferMotion.Load(uint3(pixelId.xy*2, 0)).rg;
		// uint2 accumulationYesterdayPos = round(pixelId.xy + vel / 2);
		int2 accumulationYesterdayPos = pixelId.xy + vel / 2;
		if (max(abs(vel.x), abs(vel.y)) >= 127 || any(accumulationYesterdayPos<0) || any(accumulationYesterdayPos>=(textureDims/2))) {
			AccumulationAO[pixelId.xy] = final;
		} else {
			float accumulationYesterday = AccumulationAOLast.Load(uint3(accumulationYesterdayPos.xy, 0));
			float2 diff = accumulationYesterdayPos.xy - float2(pixelId.xy);
			float magSq = dot(diff, diff);

			// We have to set the "Nvalue" here to a multiple of frameWrap, or we will start to get
			// a strobing effect. Just tweaking this for what looks right
			// (also tone it down a little bit when we've moved considerably from the previous sample)
			// float Nvalue = FrameWrap*3;
			float Nvalue = FrameWrap*lerp(3,1,saturate(magSq/(25.0*25.0)));
			float alpha = 2.0/(Nvalue+1.0);
			float accumulationToday = accumulationYesterday * (1-alpha) + final * alpha;
			AccumulationAO[pixelId.xy] = accumulationToday;
		}
	}
#else
	AccumulationAO[pixelId.xy] = final;
#endif
}

float Weight(float downsampleDepth, float originalDepth)
{
#if 1
	float diff = NDCDepthDifferenceToWorldSpace_Ortho(downsampleDepth - originalDepth, GlobalMiniProjZW());
	const float graceDepth = 0.2f;		// considered continous surface within this range ()
	// todo -- consider using the surface normal in this weighting... That would help us distinquish between a discontuity and a very sloped surface
	float d = 1+3*max(0.0,abs(diff) - graceDepth);
	d = saturate(1/d);
	return d;
#else
	return 1;
#endif
}

void AccumulateSample(
	float value, float depth, inout float outValue, inout float outWeight, float dstDepth, float weightMultiplier)
{
	float w = Weight(depth, dstDepth);
	w *= weightMultiplier;
	outValue += w * value;
	outWeight += w;
}

groupshared float GroupAO[16][16];
groupshared float GroupDepths[16][16];
groupshared uint AccumulationTemporary = 0;
groupshared uint AccumulationTemporary2 = 0;

void InitializeGroupSharedMem(int2 dispatchThreadId, int2 groupThreadId)
{
	// Load a 16x16 region which we'll access randomly in this group. This creates some overlaps with neighbouring
	// groups. Each thread loads 4 of the 16x16 samples
	dispatchThreadId.xy -= 4;
	GroupAO[groupThreadId.y][groupThreadId.x] = AccumulationAO[dispatchThreadId.xy];
	GroupAO[groupThreadId.y][groupThreadId.x+8] = AccumulationAO[dispatchThreadId.xy+int2(8,0)];
	GroupAO[groupThreadId.y+8][groupThreadId.x] = AccumulationAO[dispatchThreadId.xy+int2(0,8)];
	GroupAO[groupThreadId.y+8][groupThreadId.x+8] = AccumulationAO[dispatchThreadId.xy+int2(8,8)];

	#if !defined(HAS_HIERARCHICAL_DEPTHS)
		GroupDepths[groupThreadId.y][groupThreadId.x] = DownsampleDepths[dispatchThreadId.xy];
		GroupDepths[groupThreadId.y][groupThreadId.x+8] = DownsampleDepths[dispatchThreadId.xy+int2(8,0)];
		GroupDepths[groupThreadId.y+8][groupThreadId.x] = DownsampleDepths[dispatchThreadId.xy+int2(0,8)];
		GroupDepths[groupThreadId.y+8][groupThreadId.x+8] = DownsampleDepths[dispatchThreadId.xy+int2(8,8)];
	#else
		GroupDepths[groupThreadId.y][groupThreadId.x] = HierarchicalDepths.Load(int3(dispatchThreadId.xy, 1));
		GroupDepths[groupThreadId.y][groupThreadId.x+8] = HierarchicalDepths.Load(int3(dispatchThreadId.xy+int2(8,0), 1));
		GroupDepths[groupThreadId.y+8][groupThreadId.x] = HierarchicalDepths.Load(int3(dispatchThreadId.xy+int2(0,8), 1));
		GroupDepths[groupThreadId.y+8][groupThreadId.x+8] = HierarchicalDepths.Load(int3(dispatchThreadId.xy+int2(8,8), 1));
	#endif
	#if defined(DO_LATE_TEMPORAL_FILTERING)
		if (all(groupThreadId==0)) {
			AccumulationTemporary = 0;
			AccumulationTemporary2 = 0;
		}
	#endif
	GroupMemoryBarrierWithGroupSync();
}

void DoTemporalAccumulation(int2 groupThreadId, int2 srcPixel, float minV, float maxV)
{
	int2 textureDims;
	GBufferMotion.GetDimensions(textureDims.x, textureDims.y);
	int2 vel = GBufferMotion.Load(uint3(srcPixel*2, 0)).rg;
	int2 accumulationYesterdayPos = srcPixel.xy + vel / 2;
	if (max(abs(vel.x), abs(vel.y)) >= 127 || any(accumulationYesterdayPos<0) || any(accumulationYesterdayPos>=(textureDims/2))) {
		// no change; just keep the "today" 
	} else {
		float accumulationYesterday = AccumulationAOLast.Load(uint3(accumulationYesterdayPos.xy, 0));
		const float Nvalue = FrameWrap*2;
		const float alpha = 2.0/(Nvalue+1.0);
		float accumulationToday = accumulationYesterday * (1-alpha) + GroupAO[groupThreadId.y][groupThreadId.x] * alpha;
		accumulationToday = clamp(accumulationToday, minV, maxV);
		GroupAO[groupThreadId.y][groupThreadId.x] = accumulationToday;
	}
}

void LateTemporalFiltering(int2 dispatchThreadId, int2 groupThreadId)
{
	GroupMemoryBarrierWithGroupSync();

	uint A = (uint)(255.f*GroupAO[groupThreadId.y][groupThreadId.x]);
	uint B = (uint)(255.f*GroupAO[groupThreadId.y][groupThreadId.x+8]);
	uint C = (uint)(255.f*GroupAO[groupThreadId.y+8][groupThreadId.x]);
	uint D = (uint)(255.f*GroupAO[groupThreadId.y+8][groupThreadId.x+8]);
	uint T = A+B+C+D;
	uint T2 = A*A+B*B+C*C+D*D;
	InterlockedAdd(AccumulationTemporary, T);
	InterlockedAdd(AccumulationTemporary2, T2);
	GroupMemoryBarrierWithGroupSync();
	float valueSum = float(AccumulationTemporary);
	float valueSumSq = float(AccumulationTemporary2);

	const float sampleCount = 16*16;
	float valueStd = (valueSumSq - valueSum * valueSum / sampleCount) / (sampleCount - 1.0);
    float valueMean = valueSum / sampleCount;
	valueStd /= 255.f;
	valueMean /= 255.f;
	// The clamping range here can be pretty important to minimize ghosting and streaking. Effectively we're define a value range for the post-temporal filtered
	// values based on the values in the local spatial neighbourhood. If the temporal filtered value doesn't look like it matches the kinds of
	// values we see in the spatial neighbourhood, we'll consider it to be a filtering artifact. Since for the most part the temporal distribution of
	// values should match the spatial distribution of values, this tends to do a really good job. However it can also introduce flickering and artifacts of
	// it's own in areas of high frequency changes. 
	const float clampingRange = 3.5;		
	const float minV = valueMean - clampingRange*valueStd, maxV = valueMean + clampingRange*valueStd;

	DoTemporalAccumulation(groupThreadId, dispatchThreadId+int2(-4,-4), minV, maxV);
	DoTemporalAccumulation(groupThreadId+int2(0,8), dispatchThreadId+int2(-4,4), minV, maxV);
	DoTemporalAccumulation(groupThreadId+int2(8,0), dispatchThreadId+int2(4,-4), minV, maxV);
	DoTemporalAccumulation(groupThreadId+int2(8,8), dispatchThreadId+int2(4,4), minV, maxV);

	int2 pixelToWrite = groupThreadId + int2(groupThreadId.x<4?8:0, groupThreadId.y<4?8:0);
	AccumulationAO[dispatchThreadId.xy-groupThreadId.xy+pixelToWrite.xy-int2(4,4)] = GroupAO[pixelToWrite.y][pixelToWrite.x];
	GroupMemoryBarrierWithGroupSync();
}

float LoadGroupSharedAO(int2 base, int2 offset) { return GroupAO[base.y+offset.y+4][base.x+offset.x+4]; }
float LoadGroupSharedDepth(int2 base, int2 offset) { return GroupDepths[base.y+offset.y+4][base.x+offset.x+4]; }

[numthreads(8, 8, 1)]
	void UpsampleOp(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	InputTexture.GetDimensions(textureDims.x, textureDims.y);
	uint2 threadGroupCounts = uint2((textureDims.x/2)/8, (textureDims.y/2)/8);

	// Not sure that the thread group tiling actually helps any more with better use of group shared
	// memory. It doesn't seem particularly impactful with some basic profiling 
	// See also FFX_DNSR_Reflections_RemapLane8x8() in GPUOpen repo for AMD's approach to this 
	#if 1
		uint2 outputPixel = groupId.xy*8+groupThreadId.xy;
	#else
		uint2 outputPixel = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 16, groupThreadId.xy, groupId.xy);
		groupId.xy = outputPixel.xy/8;
		groupThreadId.xy = outputPixel.xy-groupId.xy*8;
	#endif

	InitializeGroupSharedMem(outputPixel, groupThreadId.xy);
	#if defined(DO_LATE_TEMPORAL_FILTERING)
		LateTemporalFiltering(outputPixel, groupThreadId.xy);
	#endif

#if 0
	OutputTexture[outputPixel.xy*2] = AccumulationAO[outputPixel.xy + int2(0,0)];
	OutputTexture[outputPixel.xy*2 + uint2(1,0)] = AccumulationAO[outputPixel.xy + int2(0,0)];
	OutputTexture[outputPixel.xy*2 + uint2(0,1)] = AccumulationAO[outputPixel.xy + int2(0,0)];
	OutputTexture[outputPixel.xy*2 + uint2(1,1)] = AccumulationAO[outputPixel.xy + int2(0,0)];
	return;
#endif

	// experimental filtering, inspired by demosaicing. We're assuming that there's
	// an underlying pattern in the input data we're upsampling: there is a pattern that
	// repeats in each block of 4x4 pixels. In this case, the AO sampling direction is
	// a fixed dither pattern that repeats in 4x4 blocks.
	int2 base = outputPixel.xy;

	float outDepth0 = InputTexture[base.xy*2 + int2(0,0)];
	float outDepth1 = InputTexture[base.xy*2 + int2(1,0)];
	float outDepth2 = InputTexture[base.xy*2 + int2(0,1)];
	float outDepth3 = InputTexture[base.xy*2 + int2(1,1)];

#if 1
	float2 depth0divs = float2(outDepth1 - outDepth0, outDepth2 - outDepth0);
	float2 depth1divs = float2(outDepth1 - outDepth0, outDepth3 - outDepth1);
	float2 depth2divs = float2(outDepth3 - outDepth2, outDepth2 - outDepth0);
	float2 depth3divs = float2(outDepth3 - outDepth2, outDepth3 - outDepth1);
#elif 0
	float depthDDX = lerp(outDepth1 - outDepth0, outDepth3 - outDepth2, 0.5);
	float depthDDY = lerp(outDepth2 - outDepth0, outDepth3 - outDepth1, 0.5);
	float2 depth0divs = float2(depthDDX, depthDDY), depth1divs = float2(depthDDX, depthDDY), depth2divs = float2(depthDDX, depthDDY), depth3divs = float2(depthDDX, depthDDY);
#else
	float2 depth0divs = float2(0, 0), depth1divs = float2(0, 0), depth2divs = float2(0, 0), depth3divs = float2(0, 0);
#endif

	float out0 = 0, out1 = 0, out2 = 0, out3 = 0;
	float out0TotalWeight = 0, out1TotalWeight = 0, out2TotalWeight = 0, out3TotalWeight = 0;

#if !defined(DITHER3x3)

	#define ACC(X, N, W) AccumulateSample(X, X##Depth, out##N, out##N##TotalWeight, outDepth##N + dot(depth##N##divs, X##ExpectedDepth), W)
// 	#define ACC(X, N, W) AccumulateSample(X, X##Depth, out##N, out##N##TotalWeight, outDepth##N, W)

	const float weightCenter = 1, weightNearEdge = .75, weightFarEdge = .25;
	const float weightNearCorner = weightNearEdge*weightNearEdge, weightMidCorner = weightNearEdge*weightFarEdge, weightFarCorner = weightFarEdge*weightFarEdge;

	base = groupThreadId.xy;

	float topLeft10 = LoadGroupSharedAO(base.xy, int2(-2,-2));
	float topLeft10Depth = LoadGroupSharedDepth(base.xy, int2(-2,-2));
	float2 topLeft10ExpectedDepth = float2(-4, -4);
	ACC(topLeft10, 0, weightNearCorner);
	ACC(topLeft10, 1, weightMidCorner);
	ACC(topLeft10, 2, weightMidCorner);
	ACC(topLeft10, 3, weightFarCorner);

	float top11 = LoadGroupSharedAO(base.xy, int2(-1,-2));
	float top11Depth = LoadGroupSharedDepth(base.xy, int2(-1,-2));
	float2 top11ExpectedDepth = float2(-2, -4);
	ACC(top11, 0, weightNearEdge);
	ACC(top11, 1, weightNearEdge);
	ACC(top11, 2, weightFarEdge);
	ACC(top11, 3, weightFarEdge);

	float top8 = LoadGroupSharedAO(base.xy, int2(0,-2));
	float top8Depth = LoadGroupSharedDepth(base.xy, int2(0,-2));
	float2 top8ExpectedDepth = float2(0, -4);
	ACC(top8, 0, weightNearEdge);
	ACC(top8, 1, weightNearEdge);
	ACC(top8, 2, weightFarEdge);
	ACC(top8, 3, weightFarEdge);

	float top9 = LoadGroupSharedAO(base.xy, int2(1,-2));
	float top9Depth = LoadGroupSharedDepth(base.xy, int2(1,-2));
	float2 top9ExpectedDepth = float2(2, -4);
	ACC(top9, 0, weightNearEdge);
	ACC(top9, 1, weightNearEdge);
	ACC(top9, 2, weightFarEdge);
	ACC(top9, 3, weightFarEdge);

	float topRight10 = LoadGroupSharedAO(base.xy, int2(2,-2));
	float topRight10Depth = LoadGroupSharedDepth(base.xy, int2(2,-2));
	float2 topRight10ExpectedDepth = float2(4, -4);
	ACC(topRight10, 0, weightMidCorner);
	ACC(topRight10, 1, weightNearCorner);
	ACC(topRight10, 2, weightFarCorner);
	ACC(topRight10, 3, weightMidCorner);

	////////////////////////////////////////////////////////

	float left14 = LoadGroupSharedAO(base.xy, int2(-2,-1));
	float left14Depth = LoadGroupSharedDepth(base.xy, int2(-2,-1));
	float2 left14ExpectedDepth = float2(-4, -2);
	ACC(left14, 0, weightNearEdge);
	ACC(left14, 1, weightFarEdge);
	ACC(left14, 2, weightNearEdge);
	ACC(left14, 3, weightFarEdge);

	float center15 = LoadGroupSharedAO(base.xy, int2(-1,-1));
	float center15Depth = LoadGroupSharedDepth(base.xy, int2(-1,-1));
	float2 center15ExpectedDepth = float2(-2, -2);
	ACC(center15, 0, weightCenter);
	ACC(center15, 1, weightCenter);
	ACC(center15, 2, weightCenter);
	ACC(center15, 3, weightCenter);

	float center12 = LoadGroupSharedAO(base.xy, int2(0,-1));
	float center12Depth = LoadGroupSharedDepth(base.xy, int2(0,-1));
	float2 center12ExpectedDepth = float2(0, -2);
	ACC(center12, 0, weightCenter);
	ACC(center12, 1, weightCenter);
	ACC(center12, 2, weightCenter);
	ACC(center12, 3, weightCenter);

	float center13 = LoadGroupSharedAO(base.xy, int2(1,-1));
	float center13Depth = LoadGroupSharedDepth(base.xy, int2(1,-1));
	float2 center13ExpectedDepth = float2(2, -2);
	ACC(center13, 0, weightCenter);
	ACC(center13, 1, weightCenter);
	ACC(center13, 2, weightCenter);
	ACC(center13, 3, weightCenter);

	float right14 = LoadGroupSharedAO(base.xy, int2(2,-1));
	float right14Depth = LoadGroupSharedDepth(base.xy, int2(2,-1));
	float2 right14ExpectedDepth = float2(4, -2);
	ACC(right14, 0, weightFarEdge);
	ACC(right14, 1, weightNearEdge);
	ACC(right14, 2, weightFarEdge);
	ACC(right14, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float left2 = LoadGroupSharedAO(base.xy, int2(-2,0));
	float left2Depth = LoadGroupSharedDepth(base.xy, int2(-2,0));
	float2 left2ExpectedDepth = float2(-4, 0);
	ACC(left2, 0, weightNearEdge);
	ACC(left2, 1, weightFarEdge);
	ACC(left2, 2, weightNearEdge);
	ACC(left2, 3, weightFarEdge);

	float center3 = LoadGroupSharedAO(base.xy, int2(-1,0));
	float center3Depth = LoadGroupSharedDepth(base.xy, int2(-1,0));
	float2 center3ExpectedDepth = float2(-2, 0);
	ACC(center3, 0, weightCenter);
	ACC(center3, 1, weightCenter);
	ACC(center3, 2, weightCenter);
	ACC(center3, 3, weightCenter);

	float center0 = LoadGroupSharedAO(base.xy, int2(0,0));
	float center0Depth = LoadGroupSharedDepth(base.xy, int2(0,0));
	float2 center0ExpectedDepth = float2(0, 0);
	ACC(center0, 0, weightCenter);
	ACC(center0, 1, weightCenter);
	ACC(center0, 2, weightCenter);
	ACC(center0, 3, weightCenter);

	float center1 = LoadGroupSharedAO(base.xy, int2(1,0));
	float center1Depth = LoadGroupSharedDepth(base.xy, int2(1,0));
	float2 center1ExpectedDepth = float2(2, 0);
	ACC(center1, 0, weightCenter);
	ACC(center1, 1, weightCenter);
	ACC(center1, 2, weightCenter);
	ACC(center1, 3, weightCenter);

	float right2 = LoadGroupSharedAO(base.xy, int2(2,0));
	float right2Depth = LoadGroupSharedDepth(base.xy, int2(2,0));
	float2 right2ExpectedDepth = float2(4, 0);
	ACC(right2, 0, weightFarEdge);
	ACC(right2, 1, weightNearEdge);
	ACC(right2, 2, weightFarEdge);
	ACC(right2, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float left6 = LoadGroupSharedAO(base.xy, int2(-2,1));
	float left6Depth = LoadGroupSharedDepth(base.xy, int2(-2,1));
	float2 left6ExpectedDepth = float2(-4, 2);
	ACC(left6, 0, weightNearEdge);
	ACC(left6, 1, weightFarEdge);
	ACC(left6, 2, weightNearEdge);
	ACC(left6, 3, weightFarEdge);

	float center7 = LoadGroupSharedAO(base.xy, int2(-1,1));
	float center7Depth = LoadGroupSharedDepth(base.xy, int2(-1,1));
	float2 center7ExpectedDepth = float2(-2, 2);
	ACC(center7, 0, weightCenter);
	ACC(center7, 1, weightCenter);
	ACC(center7, 2, weightCenter);
	ACC(center7, 3, weightCenter);

	float center4 = LoadGroupSharedAO(base.xy, int2(0,1));
	float center4Depth = LoadGroupSharedDepth(base.xy, int2(0,1));
	float2 center4ExpectedDepth = float2(0, 2);
	ACC(center4, 0, weightCenter);
	ACC(center4, 1, weightCenter);
	ACC(center4, 2, weightCenter);
	ACC(center4, 3, weightCenter);

	float center5 = LoadGroupSharedAO(base.xy, int2(1,1));
	float center5Depth = LoadGroupSharedDepth(base.xy, int2(1,1));
	float2 center5ExpectedDepth = float2(2, 2);
	ACC(center5, 0, weightCenter);
	ACC(center5, 1, weightCenter);
	ACC(center5, 2, weightCenter);
	ACC(center5, 3, weightCenter);

	float right6 = LoadGroupSharedAO(base.xy, int2(2,1));
	float right6Depth = LoadGroupSharedDepth(base.xy, int2(2,1));
	float2 right6ExpectedDepth = float2(4, 2);
	ACC(right6, 0, weightFarEdge);
	ACC(right6, 1, weightNearEdge);
	ACC(right6, 2, weightFarEdge);
	ACC(right6, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float bottomLeft10 = LoadGroupSharedAO(base.xy, int2(-2,2));
	float bottomLeft10Depth = LoadGroupSharedDepth(base.xy, int2(-2,2));
	float2 bottomLeft10ExpectedDepth = float2(-4, 4);
	ACC(bottomLeft10, 0, weightMidCorner);
	ACC(bottomLeft10, 1, weightFarCorner);
	ACC(bottomLeft10, 2, weightNearCorner);
	ACC(bottomLeft10, 3, weightMidCorner);

	float bottom11 = LoadGroupSharedAO(base.xy, int2(-1,2));
	float bottom11Depth = LoadGroupSharedDepth(base.xy, int2(-1,2));
	float2 bottom11ExpectedDepth = float2(-2, 4);
	ACC(bottom11, 0, weightFarEdge);
	ACC(bottom11, 1, weightFarEdge);
	ACC(bottom11, 2, weightNearEdge);
	ACC(bottom11, 3, weightNearEdge);

	float bottom8 = LoadGroupSharedAO(base.xy, int2(0,2));
	float bottom8Depth = LoadGroupSharedDepth(base.xy, int2(0,2));
	float2 bottom8ExpectedDepth = float2(0, 4);
	ACC(bottom8, 0, weightFarEdge);
	ACC(bottom8, 1, weightFarEdge);
	ACC(bottom8, 2, weightNearEdge);
	ACC(bottom8, 3, weightNearEdge);

	float bottom9 = LoadGroupSharedAO(base.xy, int2(1,2));
	float bottom9Depth = LoadGroupSharedDepth(base.xy, int2(1,2));
	float2 bottom9ExpectedDepth = float2(2, 4);
	ACC(bottom9, 0, weightFarEdge);
	ACC(bottom9, 1, weightFarEdge);
	ACC(bottom9, 2, weightNearEdge);
	ACC(bottom9, 3, weightNearEdge);

	float bottomRight10 = LoadGroupSharedAO(base.xy, int2(2,2));
	float bottomRight10Depth = LoadGroupSharedDepth(base.xy, int2(2,2));
	float2 bottomRight10ExpectedDepth = float2(4, 4);
	ACC(bottomRight10, 0, weightFarCorner);
	ACC(bottomRight10, 1, weightMidCorner);
	ACC(bottomRight10, 2, weightMidCorner);
	ACC(bottomRight10, 3, weightNearCorner);

#else

	// const float weightVeryStrong = 1.2, weightStrong = 1, weightWeak = 0.8, weightVeryWeak = 0.5;
	// const float weightVeryStrong = 1, weightStrong = 1, weightWeak = 0.75, weightVeryWeak = 0.75;
	const float weightVeryStrong = 1, weightStrong = 1, weightWeak = 1, weightVeryWeak = 1;

	float center0 = AccumulationAO[base.xy + int2(0,0)];
	float center0Depth = DownsampleDepths[base.xy + int2(0,0)];
	AccumulateSample(center0, center0Depth, out0, out0TotalWeight, outDepth0, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out1, out1TotalWeight, outDepth1, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out2, out2TotalWeight, outDepth2, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out3, out3TotalWeight, outDepth3, weightVeryStrong);

	float center1 = AccumulationAO[base.xy + int2(1,0)];
	float center1Depth = DownsampleDepths[base.xy + int2(1,0)];
	AccumulateSample(center1, center1Depth, out0, out0TotalWeight, outDepth0, weightWeak);
	AccumulateSample(center1, center1Depth, out1, out1TotalWeight, outDepth1, weightStrong);
	AccumulateSample(center1, center1Depth, out2, out2TotalWeight, outDepth2, weightWeak);
	AccumulateSample(center1, center1Depth, out3, out3TotalWeight, outDepth3, weightStrong);

	float center2 = AccumulationAO[base.xy + int2(-1,0)];
	float center2Depth = DownsampleDepths[base.xy + int2(-1,0)];
	AccumulateSample(center2, center2Depth, out0, out0TotalWeight, outDepth0, weightStrong);
	AccumulateSample(center2, center2Depth, out1, out1TotalWeight, outDepth1, weightWeak);
	AccumulateSample(center2, center2Depth, out2, out2TotalWeight, outDepth2, weightStrong);
	AccumulateSample(center2, center2Depth, out3, out3TotalWeight, outDepth3, weightWeak);

	float center3 = AccumulationAO[base.xy + int2(0,1)];
	float center3Depth = DownsampleDepths[base.xy + int2(0,1)];
	AccumulateSample(center3, center3Depth, out0, out0TotalWeight, outDepth0, weightWeak);
	AccumulateSample(center3, center3Depth, out1, out1TotalWeight, outDepth1, weightWeak);
	AccumulateSample(center3, center3Depth, out2, out2TotalWeight, outDepth2, weightStrong);
	AccumulateSample(center3, center3Depth, out3, out3TotalWeight, outDepth3, weightStrong);

	float center4 = AccumulationAO[base.xy + int2(1,1)];
	float center4Depth = DownsampleDepths[base.xy + int2(1,1)];
	AccumulateSample(center4, center4Depth, out0, out0TotalWeight, outDepth0, weightVeryWeak);
	AccumulateSample(center4, center4Depth, out1, out1TotalWeight, outDepth1, weightWeak);
	AccumulateSample(center4, center4Depth, out2, out2TotalWeight, outDepth2, weightWeak);
	AccumulateSample(center4, center4Depth, out3, out3TotalWeight, outDepth3, weightStrong);

	float center5 = AccumulationAO[base.xy + int2(-1,1)];
	float center5Depth = DownsampleDepths[base.xy + int2(-1,1)];
	AccumulateSample(center5, center5Depth, out0, out0TotalWeight, outDepth0, weightWeak);
	AccumulateSample(center5, center5Depth, out1, out1TotalWeight, outDepth1, weightVeryWeak);
	AccumulateSample(center5, center5Depth, out2, out2TotalWeight, outDepth2, weightStrong);
	AccumulateSample(center5, center5Depth, out3, out3TotalWeight, outDepth3, weightWeak);

	float center6 = AccumulationAO[base.xy + int2(0,-1)];
	float center6Depth = DownsampleDepths[base.xy + int2(0,-1)];
	AccumulateSample(center6, center6Depth, out0, out0TotalWeight, outDepth0, weightStrong);
	AccumulateSample(center6, center6Depth, out1, out1TotalWeight, outDepth1, weightStrong);
	AccumulateSample(center6, center6Depth, out2, out2TotalWeight, outDepth2, weightWeak);
	AccumulateSample(center6, center6Depth, out3, out3TotalWeight, outDepth3, weightWeak);

	float center7 = AccumulationAO[base.xy + int2(1,-1)];
	float center7Depth = DownsampleDepths[base.xy + int2(1,-1)];
	AccumulateSample(center7, center7Depth, out0, out0TotalWeight, outDepth0, weightWeak);
	AccumulateSample(center7, center7Depth, out1, out1TotalWeight, outDepth1, weightStrong);
	AccumulateSample(center7, center7Depth, out2, out2TotalWeight, outDepth2, weightVeryWeak);
	AccumulateSample(center7, center7Depth, out3, out3TotalWeight, outDepth3, weightWeak);

	float center8 = AccumulationAO[base.xy + int2(-1,-1)];
	float center8Depth = DownsampleDepths[base.xy + int2(-1,-1)];
	AccumulateSample(center8, center8Depth, out0, out0TotalWeight, outDepth0, weightStrong);
	AccumulateSample(center8, center8Depth, out1, out1TotalWeight, outDepth1, weightWeak);
	AccumulateSample(center8, center8Depth, out2, out2TotalWeight, outDepth2, weightWeak);
	AccumulateSample(center8, center8Depth, out3, out3TotalWeight, outDepth3, weightVeryWeak);

#endif

	OutputTexture[outputPixel.xy*2] = out0 / out0TotalWeight;
	OutputTexture[outputPixel.xy*2 + uint2(1,0)] = out1 / out1TotalWeight;
	OutputTexture[outputPixel.xy*2 + uint2(0,1)] = out2 / out2TotalWeight;
	OutputTexture[outputPixel.xy*2 + uint2(1,1)] = out3 / out3TotalWeight;
}

///////////////////////////////////////////////////////////////////////////////////////////

cbuffer DebuggingGlobals : register(b5, space1)
{
    const uint2 ViewportDimensions;
    const int2 MousePosition;
}

float4 Value(uint2 position)
{
	return float4(OutputTexture.Load(position.xy).xxx, 1);

#if 0
    float input = InputTexture.Load(uint3(position.xy, 0));
    float minValue = 0.4, maxValue = 0.5;
#elif 0
    float input = OutputTexture.Load(position.xy);
    float minValue = 0.0, maxValue = 1.0;
#elif 0
    float input = normalize(InputNormals.Load(uint3(position.xy, 0)).xyz).r;
    float minValue = -1.0, maxValue = 1.0;
#elif 1
    float2 vel = GBufferMotion.Load(uint3(position.xy, 0)).xy;
	vel /= 127;
	vel = 0.5 + 0.5 * vel;
	return float4(vel, 0.5, 1);
	float input = 0, minValue = -1.0, maxValue = 1.0;
#endif

    input = (input - minValue) / (maxValue - minValue);
    return saturate(float4(1-2*input, 1-abs(2*input-1), 2*input-1, 1));
}

float4 visualize(float4 position : SV_Position) : SV_Target0
{
    float r = length(position.xy - MousePosition);
    if (r <= 128) {
        uint2 magnifiedCoords = MousePosition + (position.xy - MousePosition) / 3;
        float4 col = Value(magnifiedCoords.xy);

        float borderValue = (128 - r);
        // float b = borderValue / (4 * max(abs(ddx(borderValue)), abs(ddy(borderValue))));
        float b = borderValue / 4;
        return lerp(float4(0,0,0,1), col, smoothstep(0, 1, saturate(b)));
    } else {
        return Value(position.xy);
    }
}
