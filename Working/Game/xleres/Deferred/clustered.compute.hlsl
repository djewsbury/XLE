// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

struct Light
{
	float3		WorldSpacePosition;
	float		Radius;
	float3		Colour;
	float		Power;
};

struct ProjectedLight
{
	float3		ViewSpacePosition;
	float		ViewSpaceHalfSubtendingAngle;
	float2		BaseAngles;
};

StructuredBuffer<Light>				InputLightList			: register(t0, space1);
RWStructuredBuffer<ProjectedLight>	ProjectedLightList		: register(u1, space1);

cbuffer LightCulling : register(b2, space1)
{
	int					LightCount;
	int2				GroupCounts;
	row_major float4x4	WorldToView;
	float2				FOV;
}

#include "../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../TechniqueLibrary/Profiling/Metrics.hlsl"

#if MSAA_SAMPLES > 1
	#define Texture2D_MaybeMS	Texture2DMS
#else
	#define Texture2D_MaybeMS	Texture2D
#endif

Texture2D_MaybeMS<float>	DepthTexture	 			: register(t3, space1);
Texture2D_MaybeMS<float4>	GBuffer_Normals				: register(t4, space1);

RWTexture2D<float4>			LightOutput					: register(u5, space1);

#if defined(_METRICS)
	RWStructuredBuffer<MetricsStructure> MetricsObject		: register(u6, space1);
#endif
RWTexture2D<uint>			DebuggingTextureMin			: register(u7, space1);
RWTexture2D<uint>			DebuggingTextureMax			: register(u8, space1);
RWTexture2D<uint>			DebuggingLightCountTexture	: register(u9, space1);

static const uint			ThreadWidth = 16;
static const uint			ThreadHeight = 16;

groupshared uint			ClusterKeys[ThreadWidth*ThreadHeight];
groupshared uint			ClusterKeysBack[ThreadWidth*ThreadHeight];
groupshared uint			UniqueClusterKeysCount = 0;
static const uint			MaxUniqueClusterKeysCount = 16;

groupshared float			ClusterMinDepth[MaxUniqueClusterKeysCount];
groupshared float			ClusterMaxDepth[MaxUniqueClusterKeysCount];
groupshared float4			ClusterXYBounding[MaxUniqueClusterKeysCount];

groupshared uint			DepthMin = 0xffffffff;
groupshared uint			DepthMax = 0;

groupshared uint			ActiveLightCount[MaxUniqueClusterKeysCount];

static const uint			MaxLightCount = 1024;
groupshared uint			ActiveLightIndices[MaxLightCount];

void BittonicSort(int threadIndex)
{
		//
		//		Parallel bittonic sort. One thread per
		//		array element... perfect for this sort.
		//		see source:
		//			http://www.bealto.com/gpu-sorting_parallel-bitonic-local.html
		//

	uint iKey = ClusterKeys[threadIndex];

		//
		//		Have to unroll because of the group sync operations
		//		Should result in 28 comparisons
		//		after unrolling, it becomes many instructions
		//			-- about 320 instructions in the optimised version
		//
#if 0
	const int wg = ThreadWidth*ThreadHeight;
	[unroll] for (int length=1;length<wg;length<<=1) {
		bool direction = ((threadIndex & (length<<1)) != 0); // direction of sort: 0=asc, 1=desc

			// Loop on comparison distance (between keys)
		[unroll] for (int inc=length;inc>0;inc>>=1) {

			int j = threadIndex ^ inc; // sibling to compare

			uint jKey = ClusterKeys[j];

			bool smaller = (jKey < iKey); //  || (jKey == iKey && j < threadIndex);
			bool swap = smaller ^ (j < threadIndex) ^ direction;

				//
				//	What's the best way to do syncs...?
				//	Do we really need to sync each time...?
				//
				//		Only this thread will write to "ClusterKeys[threadIndex]"
				//		... but other threads read from it.
				//		We also need to make sure other thread's reads
				//		are complete before we do the write.
				//
				//	note -- we can cut this down to a single GroupMemoryBarrierWithGroupSync()
				//			by double-buffering the "ClusterKeys" array
				//

			GroupMemoryBarrierWithGroupSync();
			iKey = (swap)?jKey:iKey;
			ClusterKeys[threadIndex] = iKey;
			GroupMemoryBarrierWithGroupSync();
		}
	}
#else

	bool writingBuffer = false;
	const int wg = ThreadWidth*ThreadHeight;
	[unroll] for (int length=1;length<wg;length<<=1) {
		bool direction = ((threadIndex & (length<<1)) != 0); // direction of sort: 0=asc, 1=desc

			// Loop on comparison distance (between keys)
		[unroll] for (int inc=length;inc>0;inc>>=1) {

			int j = threadIndex ^ inc; // sibling to compare

			uint jKey;
			if (!writingBuffer) {
				jKey = ClusterKeys[j];
			} else {
				jKey = ClusterKeysBack[j];
			}

			bool smaller = (jKey < iKey); //  || (jKey == iKey && j < threadIndex);
			bool swap = smaller ^ (j < threadIndex) ^ direction;

				//
				//	What's the best way to do syncs...?
				//	Do we really need to sync each time...?
				//
				//		Only this thread will write to "ClusterKeys[threadIndex]"
				//		... but other threads read from it.
				//		We also need to make sure other thread's reads
				//		are complete before we do the write.
				//
				//	note -- we can cut this down to a single GroupMemoryBarrierWithGroupSync()
				//			by double-buffering the "ClusterKeys" array. It requires more group
				//			shared memory.. But fewer syncs may make things run better
				//

			iKey = (swap)?jKey:iKey;
			if (!writingBuffer) {
				ClusterKeysBack[threadIndex] = iKey;
			} else {
				ClusterKeys[threadIndex] = iKey;
			}
			writingBuffer = !writingBuffer;
			GroupMemoryBarrierWithGroupSync();
		}
	}
	if (writingBuffer) {
		ClusterKeys[threadIndex] = iKey;
	}
#endif
}

float DistanceAttenuation(float distanceSq, float power)
{
	float attenuation = power / (distanceSq+1);
	return attenuation;
}

float RadiusAttenuation(float distanceSq, float radius)
{
	float D = distanceSq; D *= D; D *= D;
	float R = radius; R *= R; R *= R; R *= R;
	return 1.f - saturate(3.f * D / R);
}

float PowerForHalfRadius(float halfRadius, float powerFraction)
{
	return ((halfRadius*halfRadius)+1.f) * (1.f/(1.f-powerFraction));
}

Light GetInputLight(uint lightIndex)
{
	return InputLightList[lightIndex];
}

ProjectedLight GetProjectedLight(uint lightIndex)
{
	return ProjectedLightList[lightIndex];
}

ProjectedLight BuildProjectedLight(Light l)
{
	float3 viewSpacePosition = mul(WorldToView, float4(l.WorldSpacePosition, 1)).xyz;
	float d = viewSpacePosition.z; // length(viewSpacePosition);
	float r = l.Radius;
	float viewSpaceHalfSubtendingAngle = asin(r/d);
	float baseAngleX = asin(viewSpacePosition.x/d);
	float baseAngleY = asin(viewSpacePosition.y/d);

	ProjectedLight p;
	p.ViewSpacePosition = viewSpacePosition;
	p.BaseAngles = float2(baseAngleX, baseAngleY);
	p.ViewSpaceHalfSubtendingAngle = 1.2f * viewSpaceHalfSubtendingAngle;
	return p;
}

[numthreads(256, 1, 1)]
	void PrepareLights(uint3 dispatchThreadId : SV_DispatchThreadID)
{
		//
		//		Project and calculate global parameters
		//		for input lights.
		//
		//		Do this one per frame for each light. However, we could combine this
		//		with the other dispatch call by building a list of post projected
		//		lights once per group. This would duplicate the work for each tile...
		//		But there would be less overhead involved in writing and reading from
		//		the extra structured buffer.
		//
	uint lightIndex		= min(dispatchThreadId.x, LightCount);
	Light l				= GetInputLight(lightIndex);
	ProjectedLightList[lightIndex] = BuildProjectedLight(l);
}

uint LinearDepthToClusterKey(float linearDepth)
{
	const float frustumDepth = 1024.f;
	return uint(floor(linearDepth*frustumDepth));
}

void ClusterKeyToLinearDepths(uint clusterKey, out float minDepth, out float maxDepth)
{
	const float frustumDepth = 1024.f;
	minDepth = clusterKey / frustumDepth;
	maxDepth = (clusterKey+1) / frustumDepth;
}

[numthreads(ThreadWidth, ThreadHeight, 1)]
	void main(uint3 threadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
	int2 groupPixelCoord = threadId.xy;
	int2 pixelCoord		 = dispatchThreadId.xy;

	#if MSAA_SAMPLES > 1
		uint3 outputDim;
		DepthTexture.GetDimensions(outputDim.x, outputDim.y, outputDim.z);
		const uint sampleIndex = 0;
		const float depthNDC = DepthTexture.Load(min(pixelCoord, outputDim.xy-int2(1,1)), sampleIndex);
	#else
		uint2 outputDim;
		DepthTexture.GetDimensions(outputDim.x, outputDim.y);
		const float depthNDC = DepthTexture.mips[0][min(pixelCoord, outputDim.xy-int2(1,1))];
	#endif
	const uint depthAsInt = asuint(depthNDC);
	if (depthAsInt < asuint(1.f)) {
		InterlockedMin(DepthMin, depthAsInt);
		InterlockedMax(DepthMax, depthAsInt);
	}

		//	Write cluster key
	uint clusterKey = LinearDepthToClusterKey(NDCDepthToLinear0To1(depthNDC));
	uint clusterKeyIndex = threadId.y*ThreadWidth+threadId.x;
	ClusterKeys[clusterKeyIndex] = clusterKey;

		//
		//		Tiles in the sky will intersect no lights...
		//
		//		Ideally we want some way to cancel the entire thread group if this is
		//		a tile in the sky... But there is no "discard" for compute shaders.
		//
		//		We could do it with a pre-dispatch step to
		//		cull down the good tiles, and produce a list for a DispatchIndirect
		//		call.
		//

		//	Parallel sort
	BittonicSort(clusterKeyIndex);

	uint ourClusterKey	= ClusterKeys[clusterKeyIndex];
	uint prevClusterKey = ClusterKeys[max(clusterKeyIndex, 1)-1];
	GroupMemoryBarrierWithGroupSync();	// writing back to ClusterKeys now, complete reads

		//	Compact (remove duplicates)
	if (clusterKeyIndex == 0 || ourClusterKey != prevClusterKey) {
		//	it's unique... write
		uint pushLocation;
		InterlockedAdd(UniqueClusterKeysCount, 1, pushLocation);
		ClusterKeys[pushLocation] = ourClusterKey;
	}

	GroupMemoryBarrierWithGroupSync();

	uint uniqueClusterKeysCount = min(UniqueClusterKeysCount, MaxUniqueClusterKeysCount);
	uint finalDepthMin = DepthMin;
	uint finalDepthMax = DepthMax;

	float minAngleX = lerp(-.5f * FOV.x, .5f * FOV.x, (groupId.x    ) / float(GroupCounts.x));
	float maxAngleX = lerp(-.5f * FOV.x, .5f * FOV.x, (groupId.x + 1) / float(GroupCounts.x));
	float minAngleY = lerp(-.5f * FOV.y, .5f * FOV.y, (groupId.y    ) / float(GroupCounts.y));
	float maxAngleY = lerp(-.5f * FOV.y, .5f * FOV.y, (groupId.y + 1) / float(GroupCounts.y));
	float2 tanMinAngle = tan(float2(minAngleX, minAngleY));
	float2 tanMaxAngle = tan(float2(maxAngleX, maxAngleY));

			//	There are not enough clusters... so this calculation will be duplicated
			//	across multiple threads
	uint clusterForThisThread = clusterKeyIndex%MaxUniqueClusterKeysCount;
	float clusterForThisThreadMinDepth, clusterForThisThreadMaxDepth;
	ClusterKeyToLinearDepths(
		ClusterKeys[clusterForThisThread],
		clusterForThisThreadMinDepth, clusterForThisThreadMaxDepth);
	clusterForThisThreadMinDepth = SysUniform_GetFarClip() * clusterForThisThreadMinDepth;
	clusterForThisThreadMaxDepth = SysUniform_GetFarClip() * clusterForThisThreadMaxDepth;
	ActiveLightCount[clusterForThisThread] = 0;
	float2 cA = float2(clusterForThisThreadMaxDepth*tanMinAngle.x, clusterForThisThreadMaxDepth*tanMinAngle.y);
	float2 cB = float2(clusterForThisThreadMaxDepth*tanMaxAngle.x, clusterForThisThreadMaxDepth*tanMaxAngle.y);
	float2 cC = float2(clusterForThisThreadMinDepth*tanMinAngle.x, clusterForThisThreadMinDepth*tanMinAngle.y);
	float2 cD = float2(clusterForThisThreadMinDepth*tanMaxAngle.x, clusterForThisThreadMinDepth*tanMaxAngle.y);
	float2 cMinBox = float2(min(min(cA.x, cB.x), min(cC.x, cB.x)), min(min(cA.y, cB.y), min(cC.y, cB.y)));
	float2 cMaxBox = float2(max(max(cA.x, cB.x), max(cC.x, cB.x)), max(max(cA.y, cB.y), max(cC.y, cB.y)));
	ClusterXYBounding[clusterForThisThread] = float4(cMinBox.xy, cMaxBox.xy);
	ClusterMinDepth[clusterForThisThread] = clusterForThisThreadMinDepth;
	ClusterMaxDepth[clusterForThisThread] = clusterForThisThreadMaxDepth;
	GroupMemoryBarrier();

	uint clusterErrorCount = 0;
	uint lightCullCount = 0;
	uint lightCalculateCount = 0;
	uint validTileCount = 0;

	[branch] if (finalDepthMin < finalDepthMax) {
		LightOutput[pixelCoord] = float4(0.0.xxx,1);

			//
			//		Now that we know list of clusters, test each light
			//		and find the lights that are visible in this frustum
			//
			//		Note that the ideal iteration depends on the number of
			//		lights... if there less than 256 lights, we should
			//		compare each light and cluster on different threads. But
			//		if there are more than 256 lights, then we could loop
			//		through all of the clusters on a single thread (and do
			//		a single tile min/max depth comparison as the first step)
			//

		float worldDistanceMinDepth = NDCDepthToWorldSpace(asfloat(finalDepthMin));
		float worldDistanceMaxDepth = NDCDepthToWorldSpace(asfloat(finalDepthMax));

		float2 A = float2(worldDistanceMaxDepth*tanMinAngle.x, worldDistanceMaxDepth*tanMinAngle.y);
		float2 B = float2(worldDistanceMaxDepth*tanMaxAngle.x, worldDistanceMaxDepth*tanMaxAngle.y);
		float2 C = float2(worldDistanceMinDepth*tanMinAngle.x, worldDistanceMinDepth*tanMinAngle.y);
		float2 D = float2(worldDistanceMinDepth*tanMaxAngle.x, worldDistanceMinDepth*tanMaxAngle.y);
		float2 minBox = float2(	min(min(A.x, B.x), min(C.x, B.x)),
								min(min(A.y, B.y), min(C.y, B.y)));
		float2 maxBox = float2(	max(max(A.x, B.x), max(C.x, B.x)),
								max(max(A.y, B.y), max(C.y, B.y)));

		const uint lightCount = LightCount;
		const uint operationsPerPass = ThreadWidth * ThreadHeight;
		const uint totalOperations = lightCount * uniqueClusterKeysCount;
		const uint passes = (totalOperations + operationsPerPass - 1) / operationsPerPass;
		[loop] for (uint p=0; p<passes; ++p) {

			uint operationIndex = p * operationsPerPass + threadId.x + threadId.y * ThreadWidth;
			uint lightIndex		= operationIndex / uniqueClusterKeysCount;
			uint clusterIndex	= operationIndex % uniqueClusterKeysCount;
			lightIndex			= min(lightIndex, lightCount);

			Light l = GetInputLight(lightIndex);
			ProjectedLight p = GetProjectedLight(lightIndex);

				//
				//		X/Y clip plane check...
				//			(note --	we're actually testing an aligned cube
				//						around there sphere light. Maybe this will
				//						include some redundant tiles)
				//
		#if 0
			if (	((p.BaseAngles.x + p.ViewSpaceHalfSubtendingAngle) > minAngleX)
				&&	((p.BaseAngles.x - p.ViewSpaceHalfSubtendingAngle) < maxAngleX)
				&&	((p.BaseAngles.y + p.ViewSpaceHalfSubtendingAngle) > minAngleY)
				&&	((p.BaseAngles.y - p.ViewSpaceHalfSubtendingAngle) < maxAngleY))
		#else
			if (	((p.ViewSpacePosition.x + l.Radius) > minBox.x)
				&&	((p.ViewSpacePosition.x - l.Radius) < maxBox.x)
				&&	((p.ViewSpacePosition.y + l.Radius) > minBox.y)
				&&	((p.ViewSpacePosition.y - l.Radius) < maxBox.y))
		#endif
			{

				const float flatDepth = p.ViewSpacePosition.z;
				const float clusterMinDepth = ClusterMinDepth[clusterIndex];
				const float clusterMaxDepth = ClusterMaxDepth[clusterIndex];
				if ((flatDepth + l.Radius) > clusterMinDepth && (flatDepth - l.Radius) < clusterMaxDepth) {

					bool withinTighterBox = true;
					const float4 xyBounding = ClusterXYBounding[clusterIndex];
					withinTighterBox = (	((p.ViewSpacePosition.x + l.Radius) > xyBounding.x)
										&&	((p.ViewSpacePosition.x - l.Radius) < xyBounding.z)
										&&	((p.ViewSpacePosition.y + l.Radius) > xyBounding.y)
										&&	((p.ViewSpacePosition.y - l.Radius) < xyBounding.w));

					if (withinTighterBox && (flatDepth + l.Radius) > worldDistanceMinDepth && (flatDepth - l.Radius) < worldDistanceMaxDepth) {

							//
							//		This light affects the pixels in this cluster...
							//		How do we pack this effectively? We don't know how
							//		many lights will succeed per cluster... Somehow we need
							//		to avoid creating massive arrays with lots of blank
							//		space.
							//

						uint pushLocation;
						InterlockedAdd(ActiveLightCount[clusterIndex], 1, pushLocation);
						pushLocation = pushLocation*uniqueClusterKeysCount+clusterIndex;
						pushLocation = min(pushLocation, MaxLightCount-1);
						ActiveLightIndices[pushLocation] = lightIndex;

						if (pushLocation >= MaxLightCount) {
							clusterErrorCount++;
						}
					}
				}
			}
		}
		lightCullCount = passes;
		validTileCount = 1;
	} else {
		finalDepthMin = finalDepthMax = 0;
	}

	#if defined(_METRICS)
		DebuggingLightCountTexture[pixelCoord] = UniqueClusterKeysCount;
	#endif

	float2 frustumDim = tan(.5f * FOV);		// frustum dimensions at distance of "1"

	float viewSpaceDepth = NDCDepthToWorldSpace(asfloat(depthAsInt));
	float2 AB = -1.0.xx + 2.0.xx * float2(
		(float(pixelCoord.x) + .5f) / float(outputDim.x),
		(float(pixelCoord.y) + .5f) / float(outputDim.y));
	float3 pixelViewSpacePosition = float3(AB * frustumDim * viewSpaceDepth, viewSpaceDepth);

	#if MSAA_SAMPLES > 1
		float3 normal = DecodeGBufferNormal(GBuffer_Normals.Load(pixelCoord, sampleIndex));
	#else
		float3 normal = DecodeGBufferNormal(GBuffer_Normals[pixelCoord]);
	#endif
	float3 lightQuantity = 0.0.xxx;

		//
		//		We need out find where our cluster key ended up...
		//
	uint indexInActiveLightCount = 0;
	for (uint q=0; q<uniqueClusterKeysCount; ++q) {
		if (ClusterKeys[q] == clusterKey) {
			indexInActiveLightCount = q;
			break;
		}
	}

	GroupMemoryBarrierWithGroupSync();	// Sync because we need to make sure "ActiveLightIndices" is complete

	const uint activeLightCount = ActiveLightCount[indexInActiveLightCount];
	#if defined(_METRICS)
		DebuggingLightCountTexture[pixelCoord] = activeLightCount;
		// DebuggingLightCountTexture[pixelCoord] = uniqueClusterKeysCount;
		DebuggingTextureMin[pixelCoord] = finalDepthMin;
		DebuggingTextureMax[pixelCoord] = finalDepthMax;
	#endif

	for (uint c=0; c<activeLightCount; ++c) {
		uint activeLightIndex = min(c*uniqueClusterKeysCount+indexInActiveLightCount, MaxLightCount);
		Light l = GetInputLight(ActiveLightIndices[activeLightIndex]);
		ProjectedLight p = GetProjectedLight(ActiveLightIndices[activeLightIndex]);

		float3 lightVector	 = p.ViewSpacePosition - pixelViewSpacePosition;
		float distanceSq	 = dot(lightVector, lightVector);
		float attenuation	 = DistanceAttenuation(distanceSq, l.Power);
		float directionalAttenuation = lerp(0.25f, 1.f, saturate(dot(lightVector, normal) / sqrt(distanceSq)));

			//
			//	Need a "radiusDropOff" value to make sure we hit 0 on the edges
			//
		float radiusDropOff = RadiusAttenuation(distanceSq, l.Radius);
		lightQuantity += (attenuation * directionalAttenuation * radiusDropOff) * l.Colour;
	}

	lightCalculateCount = activeLightCount;
	LightOutput[pixelCoord] = float4(lightQuantity,1);

	#if defined(_METRICS)
		uint buffer;
		InterlockedAdd(MetricsObject[0].ClusterErrorCount, clusterErrorCount, buffer);
		InterlockedAdd(MetricsObject[0].LightCullCount, lightCullCount, buffer);
		InterlockedAdd(MetricsObject[0].LightCalculateCount, lightCalculateCount, buffer);

		if (threadId.x == 0 && threadId.y == 0 && threadId.z == 0) {
			InterlockedAdd(MetricsObject[0].TotalClusterCount, uniqueClusterKeysCount, buffer);
			InterlockedAdd(MetricsObject[0].TotalTileCount, validTileCount, buffer);
		}
	#endif
}
