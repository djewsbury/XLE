// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../../Math/perlinnoise.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Math/misc.hlsl"
#include "../../Framework/gbuffer.hlsl"

	//	Rain particles simulation that allows us to track a single particle's position from frame to frame.
	//	This way we can calculate collisions, or perform other tricks

struct RainParticle
{
	float3	position;
	float3	velocity;
};

RWStructuredBuffer<RainParticle>		Particles;

cbuffer SimulationParameters
{
	row_major float4x4	WorldToView;
	float3				ProjScale;
	float				ProjZOffset;
	float3				AverageRainVelocity;
	float				ElapsedTime;
	int					ParticleCountWidth;
	uint				TimeRandomizer;
}

// static const float3 AverageRainVelocity = .5f * float3(8.1f, 0.1f, -12.f);	// slower movement means more accurate collision detection
static const float3 Accel = float3(0.f, 0.f, -9.8f);

Texture2D<float> RandomValuesTexture;

float RandomValue(float2 coord)
{
	uint2 dims;
	RandomValuesTexture.GetDimensions(dims.x, dims.y);
	return RandomValuesTexture.SampleLevel(DefaultSampler, coord, 0);
}

float3 TransformViewToWorld(float3 viewSpacePosition)
{
	float3x3 worldToViewPartial =
		float3x3( WorldToView[0].xyz, WorldToView[1].xyz, WorldToView[2].xyz);
	return SysUniform_GetWorldSpaceView() + mul(transpose(worldToViewPartial), viewSpacePosition);
}

float3 RandomPointInFrustum(uint seed)
{
		//	we need to add a little bit of noise to seed, otherwise we
		//	get grid like patterns when we spawn particles
	//float2 s = seed;
	//seed.x = frac(seed.x + 47.74f * s.y);
	//seed.y = frac(seed.y + 61.23f * s.x);
	uint s = IntegerHash(seed);
	float2 pos = float2(
		(s >> 16) / float(0xffff),
		(s & 0xffff) / float(0xffff));

		//	find a camera point that's inside the camera frustum, from a given seed value
	float depth = 20.f * RandomValue(pos);
	float3 viewSpaceRandom = float3(
		-1.f + 2.f * pos.xy,
		-depth);

	viewSpaceRandom.xy *= (1.0f/ProjScale.xy) * depth;

	return TransformViewToWorld(viewSpaceRandom);
}

Texture2D<float>	DepthBuffer;
Texture2D			NormalsBuffer;

bool FindCollision(float3 startPosition, float3 endPosition, out float3 collisionPosition, out int2 collisionTexCoords)
{
		//	Walk along this point in the depth buffer, and see if we fall behind the depth values
		//	there. If we do, we can consider that a collision.

	float3 startViewSpace	= mul(WorldToView, float4(startPosition, 1.f)).xyz;
	float3 endViewSpace		= mul(WorldToView, float4(endPosition, 1.f)).xyz;

	uint2 depthDims;
	DepthBuffer.GetDimensions(depthDims.x, depthDims.y);

	bool foundCollision = false;
	uint collisionStep = 0;

	const uint stepCount = 8;
	[unroll] for (uint c=0; c<stepCount; ++c) {
		float alpha = (c+1)/float(stepCount);
		float3 testPosition = lerp(startViewSpace, endViewSpace, alpha);

			// "testPosition" is in view space (so we can walk through in
			//	linear coordinates). We need to transform into projection
			//	space -- but we can do that using a minimal 4-parameter
			//	projection transform.
		float4 projected = float4(
			ProjScale.x * testPosition.x,
			ProjScale.y * testPosition.y,
			ProjScale.z * testPosition.z + ProjZOffset,
			-testPosition.z);

		float ndcDepthTest = projected.z / projected.w;
		float2 t = float2(.5f + .5f * projected.x / projected.w, .5f - .5f * projected.y / projected.w);
		t = saturate(t);
		int2 texCoords = t * float2(depthDims.xy-int2(1,1));
		float ndcDepthQuery = DepthBuffer.Load(int3(texCoords, 0));
		if (ndcDepthTest > ndcDepthQuery && (ndcDepthTest - ndcDepthQuery) < 0.01f) {
			collisionStep = c;
			collisionTexCoords = texCoords;
			foundCollision = true;
		}
	}

	collisionPosition = TransformViewToWorld(lerp(startViewSpace, endViewSpace, collisionStep/float(stepCount)));
	return foundCollision;
}

[numthreads(32, 32, 1)]
	void SimulateDrops(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint particleIndex = dispatchThreadId.y * ParticleCountWidth + dispatchThreadId.x;
	RainParticle input = Particles[particleIndex];
	float2 randomSeed = float2(dispatchThreadId.xy) / float2(ParticleCountWidth.xx);

		//	If this particle begins outside of the camera frustum, then cull it and
		//	find a new starting point inside the frustum
	float velocityMagSquared = dot(input.velocity, input.velocity);
	float4 projectedPoint = mul(SysUniform_GetWorldToClip(), float4(input.position, 1.f));
	if (!InsideFrustum(projectedPoint) || velocityMagSquared < 0.7f) {
		input.position = RandomPointInFrustum((TimeRandomizer << 20) ^ (dispatchThreadId.x << 10) ^ (dispatchThreadId.y));
		input.velocity = (0.5f + RandomValue(randomSeed)) * AverageRainVelocity;
	}

	float3 newPosition = input.position + input.velocity * ElapsedTime + .5f * ElapsedTime * ElapsedTime * Accel;
	input.velocity *= 0.97f;
	input.velocity += ElapsedTime * Accel;

	float3 collisionPoint;
	int2 collisionTexCoords;
	if (FindCollision(input.position, newPosition, collisionPoint, collisionTexCoords)) {

			// just do a reflect to simulation a basic collision & bounce off
		float4 rawNormal = NormalsBuffer.Load(int3(collisionTexCoords,0));
		float3 worldSpaceNormal = DecodeGBufferNormal(rawNormal);
		input.velocity = .75f * reflect(input.velocity, worldSpaceNormal);
		input.velocity.z *= 0.5f;
		input.position = collisionPoint + (1.f/60.f) * input.velocity;

	} else {

		input.position = newPosition;

	}

		// write out our updated position
	Particles[particleIndex] = input;
}

struct VStoGS
{
	float3 positions[2] : POSITION;
	float radius		: RADIUS;
	float brightness	: BRIGHTNESS;
};

StructuredBuffer<RainParticle>		ParticlesInput;

VStoGS vs_main(uint particleId : SV_VertexID)
{
	RainParticle input		= ParticlesInput[particleId];
	const float length		= 2.0f/60.f;

	VStoGS output;
	output.positions[0]		= input.position;
	output.positions[1]		= input.position - length * input.velocity;
	output.radius			= 0.001f;
	output.brightness		= 1.f;
	return output;
}
