// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:3584) // race condition writing to shared memory detected, note that threads will be writing the same value, but performance may be diminished due to contention.

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

StructuredBuffer<Light>				InputLightList			: register(t0);
RWStructuredBuffer<ProjectedLight>	ProjectedLightList		: register(u1);

cbuffer LightCulling : register(b2)
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

Texture2D_MaybeMS<float>	DepthTexture	 			: register(t1);
Texture2D_MaybeMS<float4>	GBuffer_Normals				: register(t2);
RWTexture2D<float4>			LightOutput					: register(u0);

#if defined(_METRICS)
	RWStructuredBuffer<MetricsStructure> MetricsObject		: register(u4);
#endif
RWTexture2D<uint>			DebuggingTextureMin			: register(u5);
RWTexture2D<uint>			DebuggingTextureMax			: register(u6);
RWTexture2D<uint>			DebuggingLightCountTexture	: register(u7);

groupshared uint		DepthMin = 0xffffffff;
groupshared uint		DepthMax = 0;
groupshared uint		ActiveLightCount = 0;

static const uint		MaxLightCount = 1024;
groupshared uint		ActiveLightIndices[MaxLightCount];

static const uint		ThreadWidth = 16;
static const uint		ThreadHeight = 16;

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

[numthreads(ThreadWidth, ThreadHeight, 1)]
	void main(	uint3 threadId : SV_GroupThreadID,
				uint3 groupId : SV_GroupID,
				uint3 dispatchThreadId : SV_DispatchThreadID)
{
	int2 groupPixelCoord = threadId.xy;
	int2 pixelCoord		 = dispatchThreadId.xy;

		//		Read the depth value (as an uint) from the depth texture
	#if MSAA_SAMPLES > 1
		uint3 outputDim;
		DepthTexture.GetDimensions(outputDim.x, outputDim.y, outputDim.z);
		const uint sampleIndex = 0;
		const uint depthAsInt = asuint(DepthTexture.Load(min(pixelCoord, outputDim.xy-int2(1,1)), sampleIndex));
	#else
		uint2 outputDim;
		DepthTexture.GetDimensions(outputDim.x, outputDim.y);
		const uint depthAsInt = asuint(DepthTexture.mips[0][min(pixelCoord, outputDim.xy-int2(1,1))]);
	#endif
	if (depthAsInt < asuint(1.f)) {
		InterlockedMin(DepthMin, depthAsInt);
		InterlockedMax(DepthMax, depthAsInt);
	}

		//		Wait until all threads in this group have completed
		//		calculating Min/Max depth values.
		//		Sync entire group.

	GroupMemoryBarrierWithGroupSync();

	uint lightCullCount = 0;
	uint lightCalculateCount = 0;
	uint validTileCount = 0;

	uint finalDepthMin = DepthMin;
	uint finalDepthMax = DepthMax;
	[branch] if (finalDepthMin < finalDepthMax) {

			//
			//		Now that we know tile min/max, test each light
			//		and find the lights that are visible in this frustum
			//
			//		We could issue another compute shader dispatch to handle
			//		lights... But we can just do this in the same dispatch like
			//		this:
			//			Each thread handles a different light index.
			//			If there are too many lights, then we have to loop through
			//			multiple times.
			//

		float minAngleX = lerp(-.5f * FOV.x, .5f * FOV.x, (groupId.x    ) / float(GroupCounts.x));
		float maxAngleX = lerp(-.5f * FOV.x, .5f * FOV.x, (groupId.x + 1) / float(GroupCounts.x));
		float minAngleY = lerp(-.5f * FOV.y, .5f * FOV.y, (groupId.y    ) / float(GroupCounts.y));
		float maxAngleY = lerp(-.5f * FOV.y, .5f * FOV.y, (groupId.y + 1) / float(GroupCounts.y));

		float worldDistanceMinDepth = NDCDepthToWorldSpace(asfloat(finalDepthMin));
		float worldDistanceMaxDepth = NDCDepthToWorldSpace(asfloat(finalDepthMax));

		float2 A = float2(worldDistanceMaxDepth*tan(minAngleX), worldDistanceMaxDepth*tan(minAngleY));
		float2 B = float2(worldDistanceMaxDepth*tan(maxAngleX), worldDistanceMaxDepth*tan(maxAngleY));
		float2 C = float2(worldDistanceMinDepth*tan(minAngleX), worldDistanceMinDepth*tan(minAngleY));
		float2 D = float2(worldDistanceMinDepth*tan(maxAngleX), worldDistanceMinDepth*tan(maxAngleY));
		float2 minBox = float2(	min(min(A.x, B.x), min(C.x, B.x)),
								min(min(A.y, B.y), min(C.y, B.y)));
		float2 maxBox = float2(	max(max(A.x, B.x), max(C.x, B.x)),
								max(max(A.y, B.y), max(C.y, B.y)));

		const uint lightCount = LightCount;
		const uint lightsInOnePass = ThreadWidth * ThreadHeight;
		const uint passes = (lightCount + lightsInOnePass - 1) / lightsInOnePass;
		for (uint p=0; p<passes; ++p) {
			uint lightIndex	 = p * lightsInOnePass + threadId.x + threadId.y * ThreadWidth;
			lightIndex		 = min(lightIndex, lightCount);

				//
				//		How to do light clipping...?
				//
				//		We could transform the position and radius into post-projection
				//		clip space. This would give the most efficient culling. But it's
				//		also inaccurate -- because a sphere should be distorted in the
				//		projection process.
				//
				//		We could use pre-projection view space position for lights, and
				//		do culling in that space. But this would require no scale on the
				//		world to view transform to do it efficiently.
				//
				//		View space coordinates would make calculating the frustum planes
				//		slightly easier... But the culling math should be roughly identical
				//		as world space coordinates. Well, it might be quicker because we
				//		could ignore the X coordinate while culling against the top and
				//		bottom planes, and ignore the Y coordinate while culling against
				//		left and right (and depth becomes trivial).
				//
				//		Or, we could just use world coordinates. This would avoid
				//		pretransforming light coordinates completely.
				//
				//		We could potentially build a list of lights culled by left, right,
				//		top and bottom planes in an earlier compute shader pass.
				//
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
			[branch] if (	((p.ViewSpacePosition.x + l.Radius) > minBox.x)
				&&	((p.ViewSpacePosition.x - l.Radius) < maxBox.x)
				&&	((p.ViewSpacePosition.y + l.Radius) > minBox.y)
				&&	((p.ViewSpacePosition.y - l.Radius) < maxBox.y))
	#endif
			{

					// todo -- is near clip plane distance correctly accounted for here?

				const float flatDepth = p.ViewSpacePosition.z;
				[branch] if ((flatDepth + l.Radius) > worldDistanceMinDepth && (flatDepth - l.Radius) < worldDistanceMaxDepth) {
						//	this light affects the pixels in this tile...
						//	push it into a group shared list

					uint pushLocation;
					InterlockedAdd(ActiveLightCount, 1, pushLocation);
					// pushLocation = min(pushLocation, MaxLightCount-1);
					[branch] if (pushLocation < MaxLightCount) {
						ActiveLightIndices[pushLocation] = lightIndex;
					}
				}
			}
		}

		lightCullCount += passes;
		validTileCount = 1;
	} else {
		finalDepthMin = finalDepthMax = 0;
	}

		//		Write out the depth min / max for debugging
	#if defined(_METRICS)
		DebuggingTextureMin[pixelCoord] = finalDepthMin;
		DebuggingTextureMax[pixelCoord] = finalDepthMax;
	#endif

	float2 frustumDim = tan(.5f * FOV);		// frustum dimensions at distance of "1"

	float viewSpaceDepth = NDCDepthToWorldSpace(asfloat(depthAsInt));
	float2 AB = -1.0.xx + 2.0.xx * float2(
		(float(pixelCoord.x) + .5f) / float(outputDim.x),
		(float(pixelCoord.y) + .5f) / float(outputDim.y));
	float3 pixelViewSpacePosition = float3(AB * frustumDim * viewSpaceDepth, viewSpaceDepth);

	#if MSAA_SAMPLES > 1
		float3 normal = DecompressGBufferNormal(GBuffer_Normals.Load(pixelCoord, sampleIndex));
	#else
		float3 normal = DecompressGBufferNormal(GBuffer_Normals[pixelCoord]);
	#endif
	float3 lightQuantity = 0.0.xxx;

	GroupMemoryBarrierWithGroupSync();

	#if defined(_METRICS)
		DebuggingLightCountTexture[pixelCoord] = ActiveLightCount;
	#endif

	const uint activeLightCount = ActiveLightCount;
	for (uint c=0; c<activeLightCount; ++c) {
		Light l = GetInputLight(ActiveLightIndices[c]);
		ProjectedLight p = GetProjectedLight(ActiveLightIndices[c]);

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
		InterlockedAdd(MetricsObject[0].LightCullCount, lightCullCount, buffer);
		InterlockedAdd(MetricsObject[0].LightCalculateCount, lightCalculateCount, buffer);

		if (threadId.x == 0 && threadId.y == 0 && threadId.z == 0) {
			InterlockedAdd(MetricsObject[0].TotalTileCount, validTileCount, buffer);
		}
	#endif
}
