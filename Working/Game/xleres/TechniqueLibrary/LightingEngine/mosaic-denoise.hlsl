// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// #define REFERENCE_SPATIAL_VARIANCE 1
#define USE_COMPACT_TYPES 1
// #define PIXEL_LOCAL_VARIANCE 1

#if USE_COMPACT_TYPES
#define ValueType min16float
#define ValueType2 min16float2
#define DepthType min16float
#define DepthType2 min16float2
#define FloatType min16float
#define FloatType2 min16float2
#else
#define ValueType float
#define ValueType2 float2
#define DepthType float
#define DepthType2 float2
#define FloatType float
#define FloatType2 float2
#endif

groupshared ValueType GroupValues[16][16];
groupshared DepthType GroupDepths[16][16];

void WriteOutputTexture(uint2 coords, ValueType value);
DepthType LoadFullResDepth(uint2 coords);
DepthType LoadDownsampleDepth(uint2 coords);
ValueType LoadWorking(uint2 coords);
void WriteAccumulation(uint2 coords, ValueType);
ValueType LoadAccumulationPrev(uint2 coords);
void WriteAccumulationAndPixelVariance(uint2 coords, ValueType2);
ValueType2 LoadAccumulationAndPixelVariancePrev(uint2 coords);
int2 LoadMotion(uint2 coords);
FloatType LoadHistoryConfidence(uint2 coords, int2 motion);

FloatType GetNValue();
FloatType GetVariationTolerance();

//////////////////////////////////////////////////////////////////////////////////////////////////

uint2 ClampWorkingCoord(int2 input, uint2 dims) { return uint2(clamp(input.x, 0, dims.x-1), clamp(input.y, 0, dims.y-1)); }

void MergeVariances(
	out FloatType outMean, out FloatType outVariance,
	FloatType mean1, FloatType mean2,
	FloatType variance1, FloatType variance2,
	uint count1, uint count2)
{
	// From "UPDATING FORMULAE AND A PAIRWISE ALGORITHM FOR COMPUTING SAMPLE VARIANCES"
	// http://i.stanford.edu/pub/cstr/reports/cs/tr/79/773/CS-TR-79-773.pdf
	// we can generalize Welford's method for calculating variance to allow merging variance
	// calculation from two different ranges from the same dataset
	// 
	// See also https://en.wikipedia.org/wiki/Algorithms_for_calculating_varianc
	FloatType delta = mean2 - mean1;
	outMean = mean1 + delta * (count2 / (count1+count2));
	outVariance = variance1 + variance2 + delta * delta * (count1*count2) / (count1+count2);

	/*

		See also this alternative formation, from the original paper

		FloatType m = count1;
		FloatType n = count2;
		FloatType A = ((n/m) * sum1 - sum2);
		return variance1 + variance2 + m / (n*(m+n)) * A * A;
	*/
}

ValueType2 CalculateVariance(int2 groupThreadId)
{
#if !REFERENCE_SPATIAL_VARIANCE

	// Parallelizable variance calculation based on MergeVariances

	ValueType K = GroupValues[0][0];
	ValueType A = GroupValues[groupThreadId.y][groupThreadId.x] - K;
	ValueType B = GroupValues[groupThreadId.y][groupThreadId.x+8] - K;
	ValueType C = GroupValues[groupThreadId.y+8][groupThreadId.x] - K;
	ValueType D = GroupValues[groupThreadId.y+8][groupThreadId.x+8] - K;
	ValueType sum = A+B+C+D;
	ValueType localMean = sum / 4;
	ValueType S = (A-localMean)*(A-localMean) + (B-localMean)*(B-localMean) + (C-localMean)*(C-localMean) + (D-localMean)*(D-localMean);

	GroupDepths[groupThreadId.y][groupThreadId.x] = S;
	GroupDepths[groupThreadId.y][groupThreadId.x+8] = localMean;
	GroupMemoryBarrier();

	// MergeVariances combines 2 variances at a time. So we must build a small binary tree of operations to combine them all together
	// Let's do this using a simple parallizable method that just collapses horizontally and then vertically
	uint x = groupThreadId.x, y = groupThreadId.y;

	// collapse horizontally
	if (!(x & 0x1)) {
		MergeVariances(
			GroupDepths[y][x+8], GroupDepths[y][x],
			GroupDepths[y][x+8], GroupDepths[y][x+1+8],
			GroupDepths[y][x], GroupDepths[y][x+1],
			4, 4);
	}
	GroupMemoryBarrier();

	if (!(x & 0x3)) {
		MergeVariances(
			GroupDepths[y][x+8], GroupDepths[y][x],
			GroupDepths[y][x+8], GroupDepths[y][x+2+8],
			GroupDepths[y][x], GroupDepths[y][x+2],
			8, 8);
	}
	GroupMemoryBarrier();

	if (!x) {
		MergeVariances(
			GroupDepths[y][x+8], GroupDepths[y][x],
			GroupDepths[y][x+8], GroupDepths[y][x+4+8],
			GroupDepths[y][x], GroupDepths[y][x+4],
			16, 16);
		GroupMemoryBarrier();

		// collapse vertically
		if (!(y & 0x1)) {
			MergeVariances(
				GroupDepths[y][x+8], GroupDepths[y][x],
				GroupDepths[y][x+8], GroupDepths[y+1][x+8],
				GroupDepths[y][x], GroupDepths[y+1][x],
				32, 32);
		}
		GroupMemoryBarrier();

		if (!(y & 0x3)) {
			MergeVariances(
				GroupDepths[y][x+8], GroupDepths[y][x],
				GroupDepths[y][x+8], GroupDepths[y+2][x+8],
				GroupDepths[y][x], GroupDepths[y+2][x],
				64, 64);
		}
		GroupMemoryBarrier();

		if (!y) {
			MergeVariances(
				GroupDepths[y][x+8], GroupDepths[y][x],
				GroupDepths[y][x+8], GroupDepths[y+4][x+8],
				GroupDepths[y][x], GroupDepths[y+4][x],
				128, 128);
		}
		GroupMemoryBarrier();
	}

	ValueType valueMean = GroupDepths[0][8] + K;
	return ValueType2(GroupDepths[0][0] / (16*16-1), valueMean);

#else
	// Welford's method for calculating mean / variation
	// it's numerically more well behaved than other methods
	// See Knuth TAOCP vol 2, 3rd edition, page 232
	ValueType valueMean = GroupValues[0][0];
	ValueType runningVariation = 0;
	FloatType counter = 1;
	for (uint x=1; x<16; ++x) {
		FloatType oldMean = valueMean;
		valueMean += (GroupValues[0][x] - valueMean) / counter;
		runningVariation += (GroupValues[0][x]-oldMean)*(GroupValues[0][x]-valueMean);
		counter += 1.0;
	}
	for (uint y=1; y<16; ++y)
		for (uint x=0; x<16; ++x) {
			FloatType oldMean = valueMean;
			valueMean += (GroupValues[y][x] - valueMean) / counter;
			runningVariation += (GroupValues[y][x]-oldMean)*(GroupValues[y][x]-valueMean);
			counter += 1.0;
		}
	return ValueTyp2(runningVariation / (counter - 1.0), valueMean);
#endif
}

ValueType2 InitializeGroupSharedMem(int2 dispatchThreadId, int2 groupThreadId, uint2 downsampleDims)
{
	// Load a 16x16 region which we'll access randomly in this group. This creates some overlaps with neighbouring
	// groups. Each thread loads 4 of the 16x16 samples
	dispatchThreadId.xy -= 4;
	GroupValues[groupThreadId.y][groupThreadId.x] = LoadWorking(ClampWorkingCoord(dispatchThreadId.xy, downsampleDims));
	GroupValues[groupThreadId.y][groupThreadId.x+8] = LoadWorking(ClampWorkingCoord(dispatchThreadId.xy+int2(8,0), downsampleDims));
	GroupValues[groupThreadId.y+8][groupThreadId.x] = LoadWorking(ClampWorkingCoord(dispatchThreadId.xy+int2(0,8), downsampleDims));
	GroupValues[groupThreadId.y+8][groupThreadId.x+8] = LoadWorking(ClampWorkingCoord(dispatchThreadId.xy+int2(8,8), downsampleDims));
	GroupMemoryBarrier();
	ValueType2 spatialVariance = CalculateVariance(groupThreadId);

	GroupDepths[groupThreadId.y][groupThreadId.x] = LoadDownsampleDepth(ClampWorkingCoord(dispatchThreadId.xy, downsampleDims));
	GroupDepths[groupThreadId.y][groupThreadId.x+8] = LoadDownsampleDepth(ClampWorkingCoord(dispatchThreadId.xy+int2(8,0), downsampleDims));
	GroupDepths[groupThreadId.y+8][groupThreadId.x] = LoadDownsampleDepth(ClampWorkingCoord(dispatchThreadId.xy+int2(0,8), downsampleDims));
	GroupDepths[groupThreadId.y+8][groupThreadId.x+8] = LoadDownsampleDepth(ClampWorkingCoord(dispatchThreadId.xy+int2(8,8), downsampleDims));
	GroupMemoryBarrier();

	return spatialVariance;
}

void DoTemporalAccumulation(
	out ValueType newAccumulator,
	out ValueType newPixelVariance,
	int2 groupThreadId, int2 srcPixel, ValueType spatialStdDev, ValueType spatialMean, uint2 downsampleDims)
{
	int2 motion = LoadMotion(srcPixel*2);
	ValueType2 accumulationYesterday;
	FloatType alpha;
	int2 yesterdayReadCoord = srcPixel.xy + motion / 2;
	if (all(yesterdayReadCoord >= 0) && all(yesterdayReadCoord < downsampleDims)) {
		#if PIXEL_LOCAL_VARIANCE
			accumulationYesterday = LoadAccumulationAndPixelVariancePrev(yesterdayReadCoord);
		#else
			accumulationYesterday.x = LoadAccumulationPrev(yesterdayReadCoord);
		#endif
		const FloatType Nvalue = GetNValue();
		alpha = 2.0/(Nvalue+1.0);
		alpha = 1-alpha;
		alpha *= LoadHistoryConfidence(srcPixel*2, motion);		// scale alpha by our confidence in the "yesterday" data
	} else {
		alpha = 0;
		accumulationYesterday = 0;
	}

	ValueType2 accumulationToday;
	ValueType x = GroupValues[groupThreadId.y][groupThreadId.x];

	#if PIXEL_LOCAL_VARIANCE
		// Some pixels have significantly more variation due to the way we distribute samples in a mosaic pattern
		// Because of this, we need to allow more tolerance based on that local pixel variation
		ValueType instVariationEstimate = (x-accumulationYesterday.x)*(x-accumulationYesterday.x); // Simple estimate of the evolving variation at the pixel level -- this is just a rough estimate, it's not rigourous or accurate
		ValueType localStdDev = 2.0 * sqrt(accumulationYesterday.y);
		spatialStdDev = max(spatialStdDev, localStdDev);
	#endif
	ValueType minV = x - spatialStdDev;
	ValueType maxV = x + spatialStdDev;
	accumulationYesterday.x = clamp(accumulationYesterday.x, minV, maxV);

	newAccumulator = accumulationYesterday.x * alpha + x * (1-alpha);
	#if PIXEL_LOCAL_VARIANCE
		newPixelVariance = accumulationYesterday.y * alpha + instVariationEstimate * (1-alpha);
	#else
		newPixelVariance = 0;
	#endif
}

void LateTemporalFiltering(int2 dispatchThreadId, int2 groupThreadId, uint2 downsampleDims, ValueType2 spatialVariance)
{
	const ValueType valueStd = sqrt(spatialVariance.x);
	const ValueType clampingRange = GetVariationTolerance();
	ValueType2 a0, a1, a2, a3;
	DoTemporalAccumulation(a0.x, a0.y, groupThreadId, dispatchThreadId+int2(-4,-4), clampingRange*valueStd, spatialVariance.y, downsampleDims);
	DoTemporalAccumulation(a1.x, a1.y, groupThreadId+int2(0,8), dispatchThreadId+int2(-4,4), clampingRange*valueStd, spatialVariance.y, downsampleDims);
	DoTemporalAccumulation(a2.x, a2.y, groupThreadId+int2(8,0), dispatchThreadId+int2(4,-4), clampingRange*valueStd, spatialVariance.y, downsampleDims);
	DoTemporalAccumulation(a3.x, a3.y, groupThreadId+int2(8,8), dispatchThreadId+int2(4,4), clampingRange*valueStd, spatialVariance.y, downsampleDims);

	GroupValues[groupThreadId.y][groupThreadId.x] = a0.x;
	GroupValues[groupThreadId.y+8][groupThreadId.x] = a1.x;
	GroupValues[groupThreadId.y][groupThreadId.x+8] = a2.x;
	GroupValues[groupThreadId.y+8][groupThreadId.x+8] = a3.x;

	// We only have to write out one pixel within the 4,4 -> 12,12 box in GroupValues
	// let's ensure that we write one that we calculated in this thread
	// int2 pixelToWrite = groupThreadId + int2(groupThreadId.x<4?8:0, groupThreadId.y<4?8:0);
	// WriteAccumulationAndPixelVariation(
	// 	dispatchThreadId.xy-groupThreadId.xy+pixelToWrite.xy-int2(4,4),
	// 	ValueType2(GroupValues[pixelToWrite.y][pixelToWrite.x], GroupVariance[pixelToWrite.y][pixelToWrite.x]));
	int2 pixelToWrite = dispatchThreadId.xy - int2(4,4);
	if (groupThreadId.x < 4) {
		pixelToWrite.x += 8;
		a0 = a2;
		a1 = a3;
	}
	if (groupThreadId.y < 4) {
		pixelToWrite.y += 8;
		a0 = a1;
	}
	#if PIXEL_LOCAL_VARIANCE
		WriteAccumulationAndPixelVariance(pixelToWrite, a0);
	#else
		WriteAccumulation(pixelToWrite, a0.x);
	#endif

	GroupMemoryBarrier();
}

ValueType LoadGroupSharedAO(int2 base, int2 offset) { return GroupValues[base.y+offset.y+4][base.x+offset.x+4]; }
ValueType LoadGroupSharedDepth(int2 base, int2 offset) { return GroupDepths[base.y+offset.y+4][base.x+offset.x+4]; }

//////////////////////////////////////////////////////////////////////////////////////////////////

FloatType Weight(DepthType downsampleDepth, DepthType originalDepth, DepthType2 depthRangeScale)
{
	// This calculation is extremely important for the sharpness of the final image
	// We're comparing a nearby pixel in the *downsampled* AO buffer to a full resolution pixel
	// and we want to know if they lie on the same plane in 3d space. If they don't lie on the same
	// plane, we don't want to use this AO sample, beause that would mean bleeding the AO values
	// across a discontuity. That will end up desharpening the image, and it can be quite dramatic
	const DepthType baseAccuracyValue = 8.f/65535.f; 	// assuming 16 bit depth buffer this is enough for a 4x4 sampling kernel

	// We have to expand the "accurate" range by some factor of the depth derivatives. This is because
	// when we downsample to the reduced depth buffer, we take the min/max of several pixels. So we don't know where
	// in side the downsamples pixel that particular height really occurs -- it could be nearer or further away from 
	// from the "originalDepth" pixel
	DepthType accuracy = 2*max(abs(depthRangeScale.x), abs(depthRangeScale.y)) + baseAccuracyValue;
	return abs(downsampleDepth - originalDepth) <= accuracy;
}

void AccumulateSample(
	ValueType value, DepthType depth, inout ValueType outValue, inout FloatType outWeight, DepthType dstDepth, DepthType2 depthRangeScale, FloatType weightMultiplier)
{
	FloatType w = Weight(depth, dstDepth, depthRangeScale);
	w *= weightMultiplier;
	outValue += w * value;
	outWeight += w;
}

void MosiacDenoiseUpsample(uint2 groupThreadId, uint2 groupId, uint2 downsampleDims)
{
	uint2 downsampledCoord = groupId.xy*8+groupThreadId.xy;

	#if defined(DISABLE_DENOISE)
		WriteOutputTexture(downsampledCoord.xy*2, LoadWorking(downsampledCoord.xy + int2(0,0)));
		WriteOutputTexture(downsampledCoord.xy*2 + uint2(1,0), LoadWorking(downsampledCoord.xy + int2(0,0)));
		WriteOutputTexture(downsampledCoord.xy*2 + uint2(0,1), LoadWorking(downsampledCoord.xy + int2(0,0)));
		WriteOutputTexture(downsampledCoord.xy*2 + uint2(1,1), LoadWorking(downsampledCoord.xy + int2(0,0)));
		return;
	#endif

	ValueType2 spatialVariance = InitializeGroupSharedMem(downsampledCoord, groupThreadId.xy, downsampleDims);
	LateTemporalFiltering(downsampledCoord, groupThreadId.xy, downsampleDims, spatialVariance);

	// experimental filtering, inspired by demosaicing. We're assuming that there's
	// an underlying pattern in the input data we're upsampling: there is a pattern that
	// repeats in each block of 4x4 pixels. In this case, the AO sampling direction is
	// a fixed dither pattern that repeats in 4x4 blocks.
	int2 twiddler = int2(0,1);
	DepthType outDepth0 = LoadFullResDepth(downsampledCoord.xy*2);
	DepthType outDepth1 = LoadFullResDepth(downsampledCoord.xy*2 + twiddler.yx);
	DepthType outDepth2 = LoadFullResDepth(downsampledCoord.xy*2 + twiddler.xy);
	DepthType outDepth3 = LoadFullResDepth(downsampledCoord.xy*2 + twiddler.yy);

	int2 base = groupThreadId.xy;

	DepthType2 depth0divs = DepthType2(outDepth1 - outDepth0, outDepth2 - outDepth0);
	DepthType2 depth1divs = DepthType2(outDepth1 - outDepth0, outDepth3 - outDepth1);
	DepthType2 depth2divs = DepthType2(outDepth3 - outDepth2, outDepth2 - outDepth0);
	DepthType2 depth3divs = DepthType2(outDepth3 - outDepth2, outDepth3 - outDepth1);

	FloatType out0 = 0, out1 = 0, out2 = 0, out3 = 0;
	FloatType out0TotalWeight = 0, out1TotalWeight = 0, out2TotalWeight = 0, out3TotalWeight = 0;

#if !defined(DITHER3x3)

	const FloatType2 offsetHighRes0 = FloatType2(0,0);
	const FloatType2 offsetHighRes1 = FloatType2(1,0);
	const FloatType2 offsetHighRes2 = FloatType2(0,1);
	const FloatType2 offsetHighRes3 = FloatType2(1,1);

	#define ACC(X, N, W) AccumulateSample(X, X##Depth, out##N, out##N##TotalWeight, outDepth##N + dot(depth##N##divs, X##OffsetHighRes - offsetHighRes##N + FloatType2(1, 1)), depth##N##divs, W)

	const FloatType weightCenter = 1, weightNearEdge = .75, weightFarEdge = .25;
	const FloatType weightNearCorner = weightNearEdge*weightNearEdge, weightMidCorner = weightNearEdge*weightFarEdge, weightFarCorner = weightFarEdge*weightFarEdge;

	ValueType topLeft10 = LoadGroupSharedAO(base.xy, int2(-2,-2));
	DepthType topLeft10Depth = LoadGroupSharedDepth(base.xy, int2(-2,-2));
	FloatType2 topLeft10OffsetHighRes = FloatType2(-4, -4);
	ACC(topLeft10, 0, weightNearCorner);
	ACC(topLeft10, 1, weightMidCorner);
	ACC(topLeft10, 2, weightMidCorner);
	ACC(topLeft10, 3, weightFarCorner);

	ValueType top11 = LoadGroupSharedAO(base.xy, int2(-1,-2));
	DepthType top11Depth = LoadGroupSharedDepth(base.xy, int2(-1,-2));
	FloatType2 top11OffsetHighRes = FloatType2(-2, -4);
	ACC(top11, 0, weightNearEdge);
	ACC(top11, 1, weightNearEdge);
	ACC(top11, 2, weightFarEdge);
	ACC(top11, 3, weightFarEdge);

	ValueType top8 = LoadGroupSharedAO(base.xy, int2(0,-2));
	DepthType top8Depth = LoadGroupSharedDepth(base.xy, int2(0,-2));
	FloatType2 top8OffsetHighRes = FloatType2(0, -4);
	ACC(top8, 0, weightNearEdge);
	ACC(top8, 1, weightNearEdge);
	ACC(top8, 2, weightFarEdge);
	ACC(top8, 3, weightFarEdge);

	ValueType top9 = LoadGroupSharedAO(base.xy, int2(1,-2));
	DepthType top9Depth = LoadGroupSharedDepth(base.xy, int2(1,-2));
	FloatType2 top9OffsetHighRes = FloatType2(2, -4);
	ACC(top9, 0, weightNearEdge);
	ACC(top9, 1, weightNearEdge);
	ACC(top9, 2, weightFarEdge);
	ACC(top9, 3, weightFarEdge);

	ValueType topRight10 = LoadGroupSharedAO(base.xy, int2(2,-2));
	DepthType topRight10Depth = LoadGroupSharedDepth(base.xy, int2(2,-2));
	FloatType2 topRight10OffsetHighRes = FloatType2(4, -4);
	ACC(topRight10, 0, weightMidCorner);
	ACC(topRight10, 1, weightNearCorner);
	ACC(topRight10, 2, weightFarCorner);
	ACC(topRight10, 3, weightMidCorner);

	////////////////////////////////////////////////////////

	ValueType left14 = LoadGroupSharedAO(base.xy, int2(-2,-1));
	DepthType left14Depth = LoadGroupSharedDepth(base.xy, int2(-2,-1));
	FloatType2 left14OffsetHighRes = FloatType2(-4, -2);
	ACC(left14, 0, weightNearEdge);
	ACC(left14, 1, weightFarEdge);
	ACC(left14, 2, weightNearEdge);
	ACC(left14, 3, weightFarEdge);

	ValueType center15 = LoadGroupSharedAO(base.xy, int2(-1,-1));
	DepthType center15Depth = LoadGroupSharedDepth(base.xy, int2(-1,-1));
	FloatType2 center15OffsetHighRes = FloatType2(-2, -2);
	ACC(center15, 0, weightCenter);
	ACC(center15, 1, weightCenter);
	ACC(center15, 2, weightCenter);
	ACC(center15, 3, weightCenter);

	ValueType center12 = LoadGroupSharedAO(base.xy, int2(0,-1));
	DepthType center12Depth = LoadGroupSharedDepth(base.xy, int2(0,-1));
	FloatType2 center12OffsetHighRes = FloatType2(0, -2);
	ACC(center12, 0, weightCenter);
	ACC(center12, 1, weightCenter);
	ACC(center12, 2, weightCenter);
	ACC(center12, 3, weightCenter);

	ValueType center13 = LoadGroupSharedAO(base.xy, int2(1,-1));
	DepthType center13Depth = LoadGroupSharedDepth(base.xy, int2(1,-1));
	FloatType2 center13OffsetHighRes = FloatType2(2, -2);
	ACC(center13, 0, weightCenter);
	ACC(center13, 1, weightCenter);
	ACC(center13, 2, weightCenter);
	ACC(center13, 3, weightCenter);

	ValueType right14 = LoadGroupSharedAO(base.xy, int2(2,-1));
	DepthType right14Depth = LoadGroupSharedDepth(base.xy, int2(2,-1));
	FloatType2 right14OffsetHighRes = FloatType2(4, -2);
	ACC(right14, 0, weightFarEdge);
	ACC(right14, 1, weightNearEdge);
	ACC(right14, 2, weightFarEdge);
	ACC(right14, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	ValueType left2 = LoadGroupSharedAO(base.xy, int2(-2,0));
	DepthType left2Depth = LoadGroupSharedDepth(base.xy, int2(-2,0));
	FloatType2 left2OffsetHighRes = FloatType2(-4, 0);
	ACC(left2, 0, weightNearEdge);
	ACC(left2, 1, weightFarEdge);
	ACC(left2, 2, weightNearEdge);
	ACC(left2, 3, weightFarEdge);

	ValueType center3 = LoadGroupSharedAO(base.xy, int2(-1,0));
	DepthType center3Depth = LoadGroupSharedDepth(base.xy, int2(-1,0));
	FloatType2 center3OffsetHighRes = FloatType2(-2, 0);
	ACC(center3, 0, weightCenter);
	ACC(center3, 1, weightCenter);
	ACC(center3, 2, weightCenter);
	ACC(center3, 3, weightCenter);

	ValueType center0 = LoadGroupSharedAO(base.xy, int2(0,0));
	DepthType center0Depth = LoadGroupSharedDepth(base.xy, int2(0,0));
	FloatType2 center0OffsetHighRes = FloatType2(0, 0);
	out0 += center0; out0TotalWeight += weightCenter;
	out1 += center0; out1TotalWeight += weightCenter;
	out2 += center0; out2TotalWeight += weightCenter;
	out3 += center0; out3TotalWeight += weightCenter;

	ValueType center1 = LoadGroupSharedAO(base.xy, int2(1,0));
	DepthType center1Depth = LoadGroupSharedDepth(base.xy, int2(1,0));
	FloatType2 center1OffsetHighRes = FloatType2(2, 0);
	ACC(center1, 0, weightCenter);
	ACC(center1, 1, weightCenter);
	ACC(center1, 2, weightCenter);
	ACC(center1, 3, weightCenter);

	ValueType right2 = LoadGroupSharedAO(base.xy, int2(2,0));
	DepthType right2Depth = LoadGroupSharedDepth(base.xy, int2(2,0));
	FloatType2 right2OffsetHighRes = FloatType2(4, 0);
	ACC(right2, 0, weightFarEdge);
	ACC(right2, 1, weightNearEdge);
	ACC(right2, 2, weightFarEdge);
	ACC(right2, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	ValueType left6 = LoadGroupSharedAO(base.xy, int2(-2,1));
	DepthType left6Depth = LoadGroupSharedDepth(base.xy, int2(-2,1));
	FloatType2 left6OffsetHighRes = FloatType2(-4, 2);
	ACC(left6, 0, weightNearEdge);
	ACC(left6, 1, weightFarEdge);
	ACC(left6, 2, weightNearEdge);
	ACC(left6, 3, weightFarEdge);

	ValueType center7 = LoadGroupSharedAO(base.xy, int2(-1,1));
	DepthType center7Depth = LoadGroupSharedDepth(base.xy, int2(-1,1));
	FloatType2 center7OffsetHighRes = FloatType2(-2, 2);
	ACC(center7, 0, weightCenter);
	ACC(center7, 1, weightCenter);
	ACC(center7, 2, weightCenter);
	ACC(center7, 3, weightCenter);

	ValueType center4 = LoadGroupSharedAO(base.xy, int2(0,1));
	DepthType center4Depth = LoadGroupSharedDepth(base.xy, int2(0,1));
	FloatType2 center4OffsetHighRes = FloatType2(0, 2);
	ACC(center4, 0, weightCenter);
	ACC(center4, 1, weightCenter);
	ACC(center4, 2, weightCenter);
	ACC(center4, 3, weightCenter);

	ValueType center5 = LoadGroupSharedAO(base.xy, int2(1,1));
	DepthType center5Depth = LoadGroupSharedDepth(base.xy, int2(1,1));
	FloatType2 center5OffsetHighRes = FloatType2(2, 2);
	ACC(center5, 0, weightCenter);
	ACC(center5, 1, weightCenter);
	ACC(center5, 2, weightCenter);
	ACC(center5, 3, weightCenter);

	ValueType right6 = LoadGroupSharedAO(base.xy, int2(2,1));
	DepthType right6Depth = LoadGroupSharedDepth(base.xy, int2(2,1));
	FloatType2 right6OffsetHighRes = FloatType2(4, 2);
	ACC(right6, 0, weightFarEdge);
	ACC(right6, 1, weightNearEdge);
	ACC(right6, 2, weightFarEdge);
	ACC(right6, 3, weightNearEdge);

	////////////////////////////////////////////////////////

	ValueType bottomLeft10 = LoadGroupSharedAO(base.xy, int2(-2,2));
	DepthType bottomLeft10Depth = LoadGroupSharedDepth(base.xy, int2(-2,2));
	FloatType2 bottomLeft10OffsetHighRes = FloatType2(-4, 4);
	ACC(bottomLeft10, 0, weightMidCorner);
	ACC(bottomLeft10, 1, weightFarCorner);
	ACC(bottomLeft10, 2, weightNearCorner);
	ACC(bottomLeft10, 3, weightMidCorner);

	ValueType bottom11 = LoadGroupSharedAO(base.xy, int2(-1,2));
	DepthType bottom11Depth = LoadGroupSharedDepth(base.xy, int2(-1,2));
	FloatType2 bottom11OffsetHighRes = FloatType2(-2, 4);
	ACC(bottom11, 0, weightFarEdge);
	ACC(bottom11, 1, weightFarEdge);
	ACC(bottom11, 2, weightNearEdge);
	ACC(bottom11, 3, weightNearEdge);

	ValueType bottom8 = LoadGroupSharedAO(base.xy, int2(0,2));
	DepthType bottom8Depth = LoadGroupSharedDepth(base.xy, int2(0,2));
	FloatType2 bottom8OffsetHighRes = FloatType2(0, 4);
	ACC(bottom8, 0, weightFarEdge);
	ACC(bottom8, 1, weightFarEdge);
	ACC(bottom8, 2, weightNearEdge);
	ACC(bottom8, 3, weightNearEdge);

	ValueType bottom9 = LoadGroupSharedAO(base.xy, int2(1,2));
	DepthType bottom9Depth = LoadGroupSharedDepth(base.xy, int2(1,2));
	FloatType2 bottom9OffsetHighRes = FloatType2(2, 4);
	ACC(bottom9, 0, weightFarEdge);
	ACC(bottom9, 1, weightFarEdge);
	ACC(bottom9, 2, weightNearEdge);
	ACC(bottom9, 3, weightNearEdge);

	ValueType bottomRight10 = LoadGroupSharedAO(base.xy, int2(2,2));
	DepthType bottomRight10Depth = LoadGroupSharedDepth(base.xy, int2(2,2));
	FloatType2 bottomRight10OffsetHighRes = FloatType2(4, 4);
	ACC(bottomRight10, 0, weightFarCorner);
	ACC(bottomRight10, 1, weightMidCorner);
	ACC(bottomRight10, 2, weightMidCorner);
	ACC(bottomRight10, 3, weightNearCorner);

#else

	// const FloatType weightVeryStrong = 1.2, weightStrong = 1, weightWeak = 0.8, weightVeryWeak = 0.5;
	// const FloatType weightVeryStrong = 1, weightStrong = 1, weightWeak = 0.75, weightVeryWeak = 0.75;
	const FloatType weightVeryStrong = 1, weightStrong = 1, weightWeak = 1, weightVeryWeak = 1;

	ValueType center0 = LoadWorking(base.xy + int2(0,0));
	DepthType center0Depth = LoadDownsampleDepth(base.xy + int2(0,0));
	AccumulateSample(center0, center0Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightVeryStrong);
	AccumulateSample(center0, center0Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightVeryStrong);

	ValueType center1 = LoadWorking(base.xy + int2(1,0));
	DepthType center1Depth = LoadDownsampleDepth(base.xy + int2(1,0));
	AccumulateSample(center1, center1Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightWeak);
	AccumulateSample(center1, center1Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightStrong);
	AccumulateSample(center1, center1Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightWeak);
	AccumulateSample(center1, center1Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightStrong);

	ValueType center2 = LoadWorking(base.xy + int2(-1,0));
	DepthType center2Depth = LoadDownsampleDepth(base.xy + int2(-1,0));
	AccumulateSample(center2, center2Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightStrong);
	AccumulateSample(center2, center2Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightWeak);
	AccumulateSample(center2, center2Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightStrong);
	AccumulateSample(center2, center2Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightWeak);

	ValueType center3 = LoadWorking(base.xy + int2(0,1));
	DepthType center3Depth = LoadDownsampleDepth(base.xy + int2(0,1));
	AccumulateSample(center3, center3Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightWeak);
	AccumulateSample(center3, center3Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightWeak);
	AccumulateSample(center3, center3Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightStrong);
	AccumulateSample(center3, center3Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightStrong);

	ValueType center4 = LoadWorking(base.xy + int2(1,1));
	DepthType center4Depth = LoadDownsampleDepth(base.xy + int2(1,1));
	AccumulateSample(center4, center4Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightVeryWeak);
	AccumulateSample(center4, center4Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightWeak);
	AccumulateSample(center4, center4Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightWeak);
	AccumulateSample(center4, center4Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightStrong);

	ValueType center5 = LoadWorking(base.xy + int2(-1,1));
	DepthType center5Depth = LoadDownsampleDepth(base.xy + int2(-1,1));
	AccumulateSample(center5, center5Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightWeak);
	AccumulateSample(center5, center5Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightVeryWeak);
	AccumulateSample(center5, center5Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightStrong);
	AccumulateSample(center5, center5Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightWeak);

	ValueType center6 = LoadWorking(base.xy + int2(0,-1));
	DepthType center6Depth = LoadDownsampleDepth(base.xy + int2(0,-1));
	AccumulateSample(center6, center6Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightStrong);
	AccumulateSample(center6, center6Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightStrong);
	AccumulateSample(center6, center6Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightWeak);
	AccumulateSample(center6, center6Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightWeak);

	ValueType center7 = LoadWorking(base.xy + int2(1,-1));
	DepthType center7Depth = LoadDownsampleDepth(base.xy + int2(1,-1));
	AccumulateSample(center7, center7Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightWeak);
	AccumulateSample(center7, center7Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightStrong);
	AccumulateSample(center7, center7Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightVeryWeak);
	AccumulateSample(center7, center7Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightWeak);

	ValueType center8 = LoadWorking(base.xy + int2(-1,-1));
	DepthType center8Depth = LoadDownsampleDepth(base.xy + int2(-1,-1));
	AccumulateSample(center8, center8Depth, out0, out0TotalWeight, outDepth0, depth0divs, weightStrong);
	AccumulateSample(center8, center8Depth, out1, out1TotalWeight, outDepth1, depth1divs, weightWeak);
	AccumulateSample(center8, center8Depth, out2, out2TotalWeight, outDepth2, depth2divs, weightWeak);
	AccumulateSample(center8, center8Depth, out3, out3TotalWeight, outDepth3, depth3divs, weightVeryWeak);

#endif

	WriteOutputTexture(downsampledCoord.xy*2, out0 / out0TotalWeight);
	WriteOutputTexture(downsampledCoord.xy*2 + twiddler.yx, out1 / out1TotalWeight);
	WriteOutputTexture(downsampledCoord.xy*2 + twiddler.xy, out2 / out2TotalWeight);
	WriteOutputTexture(downsampledCoord.xy*2 + twiddler.yy, out3 / out3TotalWeight);
}

