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

cbuffer AOProps : register(b5, space1)
{
	uint FrameIdx;
	bool ClearAccumulation;
}

#define BOTH_WAYS 1
// #define DITHER3x3 1

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

	// in world space units, how big is the distance between samples in the sample direction?
	// For orthogonal, this only depends on the angle phi since there's no foreshortening
	float worldSpacePixelSizeSq = 100.0 / (float)textureDims.x;		// hard coded for the particular camera we're using
	worldSpacePixelSizeSq *= worldSpacePixelSizeSq;

	const uint frameWrap = 6;	
	uint frameIdxOrder[] = { 0, 4, 2, 5, 3 };
#if !defined(DITHER3x3)
	// The cost of looking up the dither pattern here is not trivially cheap; but you have a big impact
	// on the visual result... If we had a way of doing this without a table lookup it might be a bit faster
	// uint ditherValue = (pixelId.x%4)+(pixelId.y%4)*4;
	uint ditherValue = DitherPatternInt(pixelId.xy);
	uint idx = frameIdxOrder[FrameIdx%frameWrap] * 16 + ditherValue * 6;
	#if BOTH_WAYS
		float phi = idx / 96.0 * 3.14159;
	#else
		float phi = idx / 96.0 * 2.0 * 3.14159;
	#endif
#else
	uint ditherValue = Dither3x3PatternInt(pixelId.xy);
	uint idx = frameIdxOrder[FrameIdx%frameWrap] * 9 + ditherValue * 6;
	#if BOTH_WAYS
		float phi = frac(idx / 54.0) * 3.14159;
	#else
		float phi = frac(idx / 54.0) * 2.0 * 3.14159;
	#endif
#endif

	float cosPhi, sinPhi;
	sincos(phi, sinPhi, cosPhi);
	float xStep, yStep;
	if (abs(sinPhi) > abs(cosPhi)) {
		xStep = cosPhi / abs(sinPhi);
		yStep = sign(sinPhi);
		worldSpacePixelSizeSq *= 1+xStep*xStep;
	} else {
		xStep = sign(cosPhi);
		yStep = sinPhi / abs(cosPhi);
		worldSpacePixelSizeSq *= 1+yStep*yStep;
	}
	// xStep = 1;
	// yStep = 0;
	float2 xy = pixelId.xy + float2(xStep, yStep);

	float d0 = DownsampleDepths.Load(pixelId.xy);
	float cosMaxTheta = 0.0;
	int c=1;
	for (; c<8; ++c) {
		float d = DownsampleDepths.Load(xy);
		xy += float2(xStep, yStep);
		float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		float azmuthalXSq = c * c * worldSpacePixelSizeSq;

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

#if BOTH_WAYS
	float cosMaxTheta2 = 0.0;
	xy = pixelId.xy - float2(xStep, yStep);
	for (c=1; c<8; ++c) {
		float d = DownsampleDepths.Load(xy);
		xy -= float2(xStep, yStep);
		float worldSpaceDepthDifference = NDCDepthDifferenceToWorldSpace_Ortho(d0-d, GlobalMiniProjZW());
		float azmuthalXSq = c * c * worldSpacePixelSizeSq;

		//
		// cosTheta = dot(normalized(vector to test point), direction to eye)
		// the vector to test point in the azimuthal plane is just (x, worldSpaceDepthDifference)
		// and direction to eye is always (0, 1) -- (assuming orthogonal, or at least ignoring distortion from persective)
		// so we can just take worldSpaceDepthDifference / length(vector to test point)
		//
		float cosTheta2 = worldSpaceDepthDifference * rsqrt(worldSpaceDepthDifference * worldSpaceDepthDifference + azmuthalXSq);
		cosMaxTheta2 = max(cosTheta2, cosMaxTheta2);

		// note -- thickness heuristic
	}
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

#if 0
	float3 planePNormal = (cameraRight * -sinPhi - cameraUp * cosPhi);
	float3 projectedWorldSpaceNormal = worldSpaceNormal - worldSpaceNormal * dot(worldSpaceNormal, planePNormal);
	float magProjectedWorldSpaceNormal = length(projectedWorldSpaceNormal);
	float cosGamma = saturate(dot(projectedWorldSpaceNormal/magProjectedWorldSpaceNormal, negCameraForward));

	float gamma = acos(cosGamma);
	float3 planePTangent = cameraRight * cosPhi - cameraUp * sinPhi;
	if (dot(projectedWorldSpaceNormal, planePTangent) < 0) gamma = -gamma;
	float sinGamma = sin(gamma);
#else
	float projNormX = dot(cameraRight * cosPhi - cameraUp * sinPhi, worldSpaceNormal);
	float projNormZ = dot(negCameraForward, worldSpaceNormal);
	float magProjectedWorldSpaceNormal = sqrt(projNormX*projNormX+projNormZ*projNormZ);
	float cosGamma = saturate(projNormZ/magProjectedWorldSpaceNormal);
	float gamma = acos(cosGamma);
	float sinGamma = sin(gamma);
#endif

	float maxTheta = acos(min(1,cosMaxTheta));
	float gtoa = cosGamma + 2 * maxTheta * sinGamma - cos(2*maxTheta - gamma);	// 0.25f * magProjectedWorldSpaceNormal below

#if BOTH_WAYS
	float maxTheta2 = acos(min(1,cosMaxTheta2));
	float gtoa2 = cosGamma + 2 * maxTheta2 * sinGamma - cos(2*maxTheta2 - gamma);	// 0.25f * magProjectedWorldSpaceNormal below

	float final = gtoa + gtoa2;
	final *= 0.25 * magProjectedWorldSpaceNormal;
#else
	float final = gtoa * 2.0 * 0.25 * magProjectedWorldSpaceNormal;	// don't need to divide by pi here -- we're taking the integral 0->pi then dividing by pi. The pis just cancel out
#endif

	if (ClearAccumulation) {
		AccumulationAO[pixelId.xy] = final;
	} else {
		int2 vel = GBufferMotion.Load(uint3(pixelId.xy*2, 0)).rg;
		// uint2 accumulationYesterdayPos = round(pixelId.xy + vel / 2);
		uint2 accumulationYesterdayPos = pixelId.xy + vel / 2;
		float accumulationYesterday = AccumulationAOLast.Load(uint3(accumulationYesterdayPos.xy, 0));
		float2 diff = accumulationYesterdayPos.xy - float2(pixelId.xy);
		float magSq = dot(diff, diff);
		if (max(abs(vel.x), abs(vel.y)) >= 127) {
			AccumulationAO[pixelId.xy] = 0;
		} else {
			// We have to set the "Nvalue" here to a multiple of frameWrap, or we will start to get
			// a strobing effect. Just tweaking this for what looks right
			// (also tone it down a little bit when we've moved considerably from the previous sample)
			// float Nvalue = frameWrap*3;
			float Nvalue = frameWrap*lerp(3,1,saturate(magSq/(25.0*25.0)));
			float alpha = 2.0/(Nvalue+1.0);
			float accumulationToday = accumulationYesterday * (1-alpha) + final * alpha;
			AccumulationAO[pixelId.xy] = accumulationToday;
		}
	}
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

[numthreads(8, 8, 1)]
	void UpsampleOp(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	InputTexture.GetDimensions(textureDims.x, textureDims.y);
	uint2 threadGroupCounts = uint2((textureDims.x/2)/8, (textureDims.y/2)/8);
	uint2 outputPixel = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 16, groupThreadId.xy, groupId.xy);

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

	float topLeft10 = AccumulationAO[base.xy + int2(-2,-2)];
	float topLeft10Depth = DownsampleDepths[base.xy + int2(-2,-2)];
	float2 topLeft10ExpectedDepth = float2(-4, -4);
	ACC(topLeft10, 0, weightNearCorner);
	ACC(topLeft10, 1, weightMidCorner);
	ACC(topLeft10, 2, weightMidCorner);
	ACC(topLeft10, 3, weightFarCorner);

	float top11 = AccumulationAO[base.xy + int2(-1,-2)];
	float top11Depth = DownsampleDepths[base.xy + int2(-1,-2)];
	float2 top11ExpectedDepth = float2(-2, -4);
	ACC(top11, 0, weightNearEdge);
	ACC(top11, 1, weightNearEdge);
	ACC(top11, 2, weightFarEdge);
	ACC(top11, 3, weightFarEdge);

	float top8 = AccumulationAO[base.xy + int2(0,-2)];
	float top8Depth = DownsampleDepths[base.xy + int2(0,-2)];
	float2 top8ExpectedDepth = float2(0, -4);
	ACC(top8, 0, weightNearEdge);
	ACC(top8, 1, weightNearEdge);
	ACC(top8, 2, weightFarEdge);
	ACC(top8, 3, weightFarEdge);

	float top9 = AccumulationAO[base.xy + int2(1,-2)];
	float top9Depth = DownsampleDepths[base.xy + int2(1,-2)];
	float2 top9ExpectedDepth = float2(2, -4);
	ACC(top9, 0, weightNearEdge);
	ACC(top9, 1, weightNearEdge);
	ACC(top9, 2, weightFarEdge);
	ACC(top9, 3, weightFarEdge);

	float topRight10 = AccumulationAO[base.xy + int2(2,-2)];
	float topRight10Depth = DownsampleDepths[base.xy + int2(2,-2)];
	float2 topRight10ExpectedDepth = float2(4, -4);
	ACC(topRight10, 0, weightMidCorner);
	ACC(topRight10, 1, weightNearCorner);
	ACC(topRight10, 2, weightFarCorner);
	ACC(topRight10, 3, weightMidCorner);

	////////////////////////////////////////////////////////

	float left14 = AccumulationAO[base.xy + int2(-2,-1)];
	float left14Depth = DownsampleDepths[base.xy + int2(-2,-1)];
	float2 left14ExpectedDepth = float2(-4, -2);
	ACC(left14, 0, weightNearEdge);
	ACC(left14, 1, weightFarEdge);
	ACC(left14, 2, weightNearEdge);
	ACC(left14, 3, weightFarEdge);

	float center15 = AccumulationAO[base.xy + int2(-1,-1)];
	float center15Depth = DownsampleDepths[base.xy + int2(-1,-1)];
	float2 center15ExpectedDepth = float2(-2, -2);
	ACC(center15, 0, weightCenter);
	ACC(center15, 1, weightCenter);
	ACC(center15, 2, weightCenter);
	ACC(center15, 3, weightCenter);

	float center12 = AccumulationAO[base.xy + int2(0,-1)];
	float center12Depth = DownsampleDepths[base.xy + int2(0,-1)];
	float2 center12ExpectedDepth = float2(0, -2);
	ACC(center12, 0, weightCenter);
	ACC(center12, 1, weightCenter);
	ACC(center12, 2, weightCenter);
	ACC(center12, 3, weightCenter);

	float center13 = AccumulationAO[base.xy + int2(1,-1)];
	float center13Depth = DownsampleDepths[base.xy + int2(1,-1)];
	float2 center13ExpectedDepth = float2(2, -2);
	ACC(center13, 0, weightCenter);
	ACC(center13, 1, weightCenter);
	ACC(center13, 2, weightCenter);
	ACC(center13, 3, weightCenter);

	float right14 = AccumulationAO[base.xy + int2(2,-1)];
	float right14Depth = DownsampleDepths[base.xy + int2(2,-1)];
	float2 right14ExpectedDepth = float2(4, -2);
	ACC(right14, 0, weightFarEdge);
	ACC(right14, 1, weightNearEdge);
	ACC(right14, 2, weightFarEdge);
	ACC(right14, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float left2 = AccumulationAO[base.xy + int2(-2,0)];
	float left2Depth = DownsampleDepths[base.xy + int2(-2,0)];
	float2 left2ExpectedDepth = float2(-4, 0);
	ACC(left2, 0, weightNearEdge);
	ACC(left2, 1, weightFarEdge);
	ACC(left2, 2, weightNearEdge);
	ACC(left2, 3, weightFarEdge);

	float center3 = AccumulationAO[base.xy + int2(-1,0)];
	float center3Depth = DownsampleDepths[base.xy + int2(-1,0)];
	float2 center3ExpectedDepth = float2(-2, 0);
	ACC(center3, 0, weightCenter);
	ACC(center3, 1, weightCenter);
	ACC(center3, 2, weightCenter);
	ACC(center3, 3, weightCenter);

	float center0 = AccumulationAO[base.xy + int2(0,0)];
	float center0Depth = DownsampleDepths[base.xy + int2(0,0)];
	float2 center0ExpectedDepth = float2(0, 0);
	ACC(center0, 0, weightCenter);
	ACC(center0, 1, weightCenter);
	ACC(center0, 2, weightCenter);
	ACC(center0, 3, weightCenter);

	float center1 = AccumulationAO[base.xy + int2(1,0)];
	float center1Depth = DownsampleDepths[base.xy + int2(1,0)];
	float2 center1ExpectedDepth = float2(2, 0);
	ACC(center1, 0, weightCenter);
	ACC(center1, 1, weightCenter);
	ACC(center1, 2, weightCenter);
	ACC(center1, 3, weightCenter);

	float right2 = AccumulationAO[base.xy + int2(2,0)];
	float right2Depth = DownsampleDepths[base.xy + int2(2,0)];
	float2 right2ExpectedDepth = float2(4, 0);
	ACC(right2, 0, weightFarEdge);
	ACC(right2, 1, weightNearEdge);
	ACC(right2, 2, weightFarEdge);
	ACC(right2, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float left6 = AccumulationAO[base.xy + int2(-2,1)];
	float left6Depth = DownsampleDepths[base.xy + int2(-2,1)];
	float2 left6ExpectedDepth = float2(-4, 2);
	ACC(left6, 0, weightNearEdge);
	ACC(left6, 1, weightFarEdge);
	ACC(left6, 2, weightNearEdge);
	ACC(left6, 3, weightFarEdge);

	float center7 = AccumulationAO[base.xy + int2(-1,1)];
	float center7Depth = DownsampleDepths[base.xy + int2(-1,1)];
	float2 center7ExpectedDepth = float2(-2, 2);
	ACC(center7, 0, weightCenter);
	ACC(center7, 1, weightCenter);
	ACC(center7, 2, weightCenter);
	ACC(center7, 3, weightCenter);

	float center4 = AccumulationAO[base.xy + int2(0,1)];
	float center4Depth = DownsampleDepths[base.xy + int2(0,1)];
	float2 center4ExpectedDepth = float2(0, 2);
	ACC(center4, 0, weightCenter);
	ACC(center4, 1, weightCenter);
	ACC(center4, 2, weightCenter);
	ACC(center4, 3, weightCenter);

	float center5 = AccumulationAO[base.xy + int2(1,1)];
	float center5Depth = DownsampleDepths[base.xy + int2(1,1)];
	float2 center5ExpectedDepth = float2(2, 2);
	ACC(center5, 0, weightCenter);
	ACC(center5, 1, weightCenter);
	ACC(center5, 2, weightCenter);
	ACC(center5, 3, weightCenter);

	float right6 = AccumulationAO[base.xy + int2(2,1)];
	float right6Depth = DownsampleDepths[base.xy + int2(2,1)];
	float2 right6ExpectedDepth = float2(4, 2);
	ACC(right6, 0, weightFarEdge);
	ACC(right6, 1, weightNearEdge);
	ACC(right6, 2, weightFarEdge);
	ACC(right6, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	float bottomLeft10 = AccumulationAO[base.xy + int2(-2,2)];
	float bottomLeft10Depth = DownsampleDepths[base.xy + int2(-2,2)];
	float2 bottomLeft10ExpectedDepth = float2(-4, 4);
	ACC(bottomLeft10, 0, weightMidCorner);
	ACC(bottomLeft10, 1, weightFarCorner);
	ACC(bottomLeft10, 2, weightNearCorner);
	ACC(bottomLeft10, 3, weightMidCorner);

	float bottom11 = AccumulationAO[base.xy + int2(-1,2)];
	float bottom11Depth = DownsampleDepths[base.xy + int2(-1,2)];
	float2 bottom11ExpectedDepth = float2(-2, 4);
	ACC(bottom11, 0, weightFarEdge);
	ACC(bottom11, 1, weightFarEdge);
	ACC(bottom11, 2, weightNearEdge);
	ACC(bottom11, 3, weightNearEdge);

	float bottom8 = AccumulationAO[base.xy + int2(0,2)];
	float bottom8Depth = DownsampleDepths[base.xy + int2(0,2)];
	float2 bottom8ExpectedDepth = float2(0, 4);
	ACC(bottom8, 0, weightFarEdge);
	ACC(bottom8, 1, weightFarEdge);
	ACC(bottom8, 2, weightNearEdge);
	ACC(bottom8, 3, weightNearEdge);

	float bottom9 = AccumulationAO[base.xy + int2(1,2)];
	float bottom9Depth = DownsampleDepths[base.xy + int2(1,2)];
	float2 bottom9ExpectedDepth = float2(2, 4);
	ACC(bottom9, 0, weightFarEdge);
	ACC(bottom9, 1, weightFarEdge);
	ACC(bottom9, 2, weightNearEdge);
	ACC(bottom9, 3, weightNearEdge);

	float bottomRight10 = AccumulationAO[base.xy + int2(2,2)];
	float bottomRight10Depth = DownsampleDepths[base.xy + int2(2,2)];
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

	OutputTexture[base.xy*2] = out0 / out0TotalWeight;
	OutputTexture[base.xy*2 + uint2(1,0)] = out1 / out1TotalWeight;
	OutputTexture[base.xy*2 + uint2(0,1)] = out2 / out2TotalWeight;
	OutputTexture[base.xy*2 + uint2(1,1)] = out3 / out3TotalWeight;
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
