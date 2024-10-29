// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// Based on approach from AMD SSR shaders
float CalculateEdgeStoppingNormalWeight(float3 normalToday, float3 normalYesterday)
{
	const float sigma = 4.0;
	return pow(max(dot(normalToday, normalYesterday), 0.0), sigma);
}

float CalculateEdgeStoppingRoughnessWeight(float roughnessToday, float roughnessYesterday)
{
	// often we're writing out roughness as a 256 bit value -- so we can only be so accurate
	const float sigmaMin = 2.f / 256.f;
	const float sigmaMax = 12.f / 256.f;
	return 1.0 - smoothstep(sigmaMin, sigmaMax, abs(roughnessToday - roughnessYesterday));
}

float CalculateEdgeStoppingDepthWeight(float depthToday, float depthYesterday)
{
	float ratio = min(depthToday, depthYesterday) / max(depthToday, depthYesterday);
	return smoothstep(1-128.f/65535.f, 1-32.f/65535.f, ratio);
}

float4 LoadNormalAndRoughnessPrev(uint2 loadPos);
float LoadDepthPrev(uint2 loadPos);

float CalculatePixelHistoryConfidence(
	uint2 todayCoord, int2 motion,
	float3 todayWorldSpaceNormal, float todayRoughness, float todayDepth,
	uint2 depthBufferDims)
{
	int2 loadPos = int2(todayCoord.xy+motion.xy);
	// Note that we consider the texture size here, not the viewport
	if (any(loadPos < 0) || any(loadPos >= depthBufferDims))
		return 0;

	float yesterdayDepth = LoadDepthPrev(loadPos);
	if (yesterdayDepth == 0) 
		return 0;

	float4 yesterdayNormalAndRoughness = LoadNormalAndRoughnessPrev(loadPos);

	// Note that resulting confidence will always be reduced a little bit just because of the normal compression method
	return
		  CalculateEdgeStoppingNormalWeight(todayWorldSpaceNormal, yesterdayNormalAndRoughness.xyz)
		* CalculateEdgeStoppingRoughnessWeight(todayRoughness, yesterdayNormalAndRoughness.w)
		* CalculateEdgeStoppingDepthWeight(todayDepth, yesterdayDepth)
		;
}

float CalculatePixelHistoryConfidence_NoDepth(
	uint2 todayCoord, int2 motion,
	float3 todayWorldSpaceNormal, float todayRoughness,
	uint2 depthBufferDims)
{
	int2 loadPos = int2(todayCoord.xy+motion.xy);
	// Note that we consider the texture size here, not the viewport
	if (any(loadPos < 0) || any(loadPos >= depthBufferDims))
		return 0;

	float4 yesterdayNormalAndRoughness = LoadNormalAndRoughnessPrev(loadPos);

	// Note that resulting confidence will always be reduced a little bit just because of the normal compression method
	return
		  CalculateEdgeStoppingNormalWeight(todayWorldSpaceNormal, yesterdayNormalAndRoughness.xyz)
		* CalculateEdgeStoppingRoughnessWeight(todayRoughness, yesterdayNormalAndRoughness.w)
		;
}
