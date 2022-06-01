// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Framework/CommonResources.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Math/perlinnoise.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../Math/Misc.hlsl"
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
	float3				SpawnPosition;
	float3				SpawnVelocity;
	float				ElapsedTime;
	int					ParticleCountWidth;
	uint				TimeRandomizer;
}

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
	// float2 randomSeed = float2(dispatchThreadId.xy) / float2(ParticleCountWidth.xx);
	float2 randomSeed = float2(
		IntegerHash((dispatchThreadId.x << 10) ^ TimeRandomizer) / float(0xffffffff),
		IntegerHash((dispatchThreadId.y << 10) ^ TimeRandomizer) / float(0xffffffff));

		//	If this particle begins outside of the camera frustum, then cull it and
		//	find a new starting point inside the frustum
	float velocityMagSquared = dot(input.velocity, input.velocity);
	float4 projectedPoint = mul(SysUniform_GetWorldToClip(), float4(input.position, 1.f));
	if (!InsideFrustum(projectedPoint) || velocityMagSquared < 0.7f) {
		input.position = SpawnPosition;
		input.velocity = 2.5f * SpawnVelocity;
		//input.velocity.x *= (0.25f + RandomValue(randomSeed));
		//input.velocity.y *= (0.25f + RandomValue(1.0.xx - randomSeed));
		//input.velocity.z *= (0.25f + RandomValue(randomSeed.yx));
		input.velocity.x += 4.f * (2.f * RandomValue(randomSeed) - 1.f);
		input.velocity.y += 4.f * (2.f * RandomValue(randomSeed.yx) - 1.f);
		input.velocity.z += 4.f * RandomValue(1.0.xx - randomSeed);
		input.position += RandomValue(1.0.xx - randomSeed) * ElapsedTime * input.velocity;
	}

	float3 newPosition = input.position + input.velocity * ElapsedTime + .5f * ElapsedTime * ElapsedTime * Accel;
	// input.velocity *= 0.9995f;
	input.velocity += ElapsedTime * Accel;

	float3 collisionPoint;
	int2 collisionTexCoords;
	if (FindCollision(input.position, newPosition, collisionPoint, collisionTexCoords)) {

			// just do a reflect to simulation a basic collision & bounce off
		float4 rawNormal = NormalsBuffer.Load(int3(collisionTexCoords,0));
		float3 worldSpaceNormal = DecodeGBufferNormal(rawNormal);
		input.velocity = 0.33f * reflect(input.velocity, worldSpaceNormal);

		input.velocity.x += 3.f * (-0.5f + RandomValue(randomSeed));
		input.velocity.y += 3.f * (-0.5f + RandomValue(1.0.xx - randomSeed));

		input.position = collisionPoint + (2.f/60.f) * input.velocity;

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
	float length		= 2.0f/60.f;
	length += 1.f/60.f * ((particleId % 20) / 20.f);

	VStoGS output;
	output.positions[0]		= input.position;
	output.positions[1]		= input.position - length * input.velocity;
	output.radius			= 0.0025f;
	output.brightness		= 1.f;
	return output;
}



////////////////////////////////



struct GStoPS
{
	float4 position : SV_Position;
	float2 texCoord : TEXCOORD;
	float brightness : BRIGHTNESS;
};

[maxvertexcount(4)]
	void gs_main(point VStoGS input[1], inout TriangleStream<GStoPS> outputStream)
{
		//	simple projection... Causes problems when looking straight down or
		//	straight up.
	float4 projected0 = mul(SysUniform_GetWorldToClip(), float4(input[0].positions[0],1));
	float4 projected1 = mul(SysUniform_GetWorldToClip(), float4(input[0].positions[1],1));
	if (InsideFrustum(projected0) || InsideFrustum(projected1)) {
		float width = input[0].radius * projected0.w;

		GStoPS output;
		output.brightness = input[0].brightness;

		output.position = float4(projected0.x + width, projected0.yzw);
		output.texCoord = float2(1.f, 0.f);
		outputStream.Append(output);

		output.position = float4(projected0.x - width, projected0.yzw);
		output.texCoord = float2(0.f, 0.f);
		outputStream.Append(output);

		output.position = float4(projected1.x + .5f * width, projected1.yzw);
		output.texCoord = float2(1.f, 1.f);
		outputStream.Append(output);

		output.position = float4(projected1.x - .5f * width, projected1.yzw);
		output.texCoord = float2(0.f, 1.f);
		outputStream.Append(output);
	}
}

[earlydepthstencil]
float4 ps_main(GStoPS input) : SV_Target0
{
	// return 1.0.xxxx;

	//float brightness =
	//		(1.f - input.texCoord.x) * input.texCoord.x
	//	*	(1.f - input.texCoord.y) * input.texCoord.y;

	float brightness =
		min(1, exp(1.f - abs(2.f * (input.texCoord.x - 0.5f))) - 1.f);

	brightness = 35.f * (exp(-3.f * (1.f-brightness)) - 0.05f);

	float3 baseColour = float3(.7f, 0.3f, .9f);
	return float4(brightness * baseColour, 1.f);
}
