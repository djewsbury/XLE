// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"
#include "../../TechniqueLibrary/Framework/MainGeometry.hlsl"
#include "../../TechniqueLibrary/LightingEngine/ShadowProjection.hlsl"
#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"

struct GSInput
{
	float3 minCoords	: MINCOORDS;
	float3 maxCoords	: MAXCOORDS;
};

struct GSOutput
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	uint colorIndex : COLORINDEX;

	#if SHADOWS==1
		uint renderTargetIndex : SV_RenderTargetArrayIndex;
	#endif
};

cbuffer RecordedTransform
{
	row_major float4x4 Recorded_WorldToClip;
	float4 Recorded_FrustumCorners[4];
	float3 Recorded_WorldSpaceView;
	float Recorded_FarClip;
	float4 Recorded_MinimalProjection;
	row_major float4x4 Recorded_CameraBasis;
}

float3 ToWorldSpacePosition(float3 recordedViewFrustumCoord)
{
	float2 tc = recordedViewFrustumCoord.xy;
	float weight0 = (1.f - tc.x) * (1.0f - tc.y);
	float weight1 = (1.f - tc.x) * tc.y;
 	float weight2 = tc.x * (1.f - tc.y);
 	float weight3 = tc.x * tc.y;
 	float3 viewFrustumVector =
 			weight0 * Recorded_FrustumCorners[0].xyz + weight1 * Recorded_FrustumCorners[1].xyz
 		+   weight2 * Recorded_FrustumCorners[2].xyz + weight3 * Recorded_FrustumCorners[3].xyz
 		;

	float linear0To1Depth = NDCDepthToLinear0To1_Perspective(
		recordedViewFrustumCoord.z, AsMiniProjZW(Recorded_MinimalProjection), Recorded_FarClip);
	return WorldPositionFromLinear0To1Depth_Perspective(
		viewFrustumVector, linear0To1Depth,
		Recorded_WorldSpaceView);
}

void WriteQuad(float4 A, float4 B, float4 C, float4 D, uint colorIndex, float3 normal, uint renderTargetIndex, inout TriangleStream<GSOutput> outputStream)
{
	GSOutput output;
	output.normal = normal.xyz;
	output.colorIndex = colorIndex;

	#if SHADOWS==1
		output.renderTargetIndex = renderTargetIndex;
	#endif

		//	Quick backface culling test...
	float sign = BackfaceSign(A, B, C);
	if (sign < 0.f) {
		output.position = A; outputStream.Append(output);
		output.position = C; outputStream.Append(output);
		output.position = B; outputStream.Append(output);
		outputStream.RestartStrip();

		output.position = B; outputStream.Append(output);
		output.position = C; outputStream.Append(output);
		output.position = D; outputStream.Append(output);
		outputStream.RestartStrip();
	}
}

float3 CalculateNormal(float3 A, float3 B, float3 C)
{
	return .1f * normalize(cross(C-A, B-A));
}

float4 TransformPosition(float3 position, uint projectionIndex)
{
	#if SHADOWS==1
		return ShadowProjection_GetOutput(position, projectionIndex);
	#else
		return mul(SysUniform_GetWorldToClip(), float4(position,1));
	#endif
}

#if SHADOWS==1
	[maxvertexcount(90)]
#else
	[maxvertexcount(30)]
#endif
	void main(point GSInput input[1], inout TriangleStream<GSOutput> outputStream)
{
	float3 A0 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].minCoords.y, input[0].minCoords.z));
	float3 B0 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].minCoords.y, input[0].minCoords.z));
	float3 C0 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].maxCoords.y, input[0].minCoords.z));
	float3 D0 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].maxCoords.y, input[0].minCoords.z));

	float3 A1 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].minCoords.y, input[0].maxCoords.z));
	float3 B1 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].minCoords.y, input[0].maxCoords.z));
	float3 C1 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].maxCoords.y, input[0].maxCoords.z));
	float3 D1 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].maxCoords.y, input[0].maxCoords.z));

	#if SHADOWS==1
		const uint projectionCount = min(GetShadowSubProjectionCount(), 3);
	#else
		const uint projectionCount = 1;
		[unroll]
	#endif

	for (uint c=0; c<projectionCount; ++c) {
		float4 a0 = TransformPosition(A0, c);
		float4 b0 = TransformPosition(B0, c);
		float4 c0 = TransformPosition(C0, c);
		float4 d0 = TransformPosition(D0, c);
		float4 a1 = TransformPosition(A1, c);
		float4 b1 = TransformPosition(B1, c);
		float4 c1 = TransformPosition(C1, c);
		float4 d1 = TransformPosition(D1, c);

			// front
		WriteQuad(a0, b0, c0, d0, 0, CalculateNormal(A0, B0, C0), c, outputStream);

			// left, right, top, bottom (todo -- back face culling here...?)
		WriteQuad(a0, a1, b0, b1, 1, CalculateNormal(A0, A1, B0), c, outputStream);
		WriteQuad(b0, b1, d0, d1, 1, CalculateNormal(B0, B1, D1), c, outputStream);
		WriteQuad(d0, d1, c0, c1, 1, CalculateNormal(D0, D1, C0), c, outputStream);
		WriteQuad(c0, c1, a0, a1, 1, CalculateNormal(C0, C1, A0), c, outputStream);
	}
}

void WriteLine(float4 A, float4 B, uint colorIndex, inout LineStream<GSOutput> outputStream)
{
	GSOutput output;
	output.normal = 0.0.xxx;
	output.colorIndex = colorIndex;
	output.position = A; outputStream.Append(output);
	output.position = B; outputStream.Append(output);
	outputStream.RestartStrip();
}

[maxvertexcount(16)]
	void Outlines(point GSInput input[1], inout LineStream<GSOutput> outputStream)
{
	float3 A0 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].minCoords.y, input[0].minCoords.z));
	float3 B0 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].minCoords.y, input[0].minCoords.z));
	float3 C0 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].maxCoords.y, input[0].minCoords.z));
	float3 D0 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].maxCoords.y, input[0].minCoords.z));

	float3 A1 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].minCoords.y, input[0].maxCoords.z));
	float3 B1 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].minCoords.y, input[0].maxCoords.z));
	float3 C1 = ToWorldSpacePosition(float3(input[0].minCoords.x, input[0].maxCoords.y, input[0].maxCoords.z));
	float3 D1 = ToWorldSpacePosition(float3(input[0].maxCoords.x, input[0].maxCoords.y, input[0].maxCoords.z));

	float4 a0 = mul(SysUniform_GetWorldToClip(), float4(A0,1));
	float4 b0 = mul(SysUniform_GetWorldToClip(), float4(B0,1));
	float4 c0 = mul(SysUniform_GetWorldToClip(), float4(C0,1));
	float4 d0 = mul(SysUniform_GetWorldToClip(), float4(D0,1));
	float4 a1 = mul(SysUniform_GetWorldToClip(), float4(A1,1));
	float4 b1 = mul(SysUniform_GetWorldToClip(), float4(B1,1));
	float4 c1 = mul(SysUniform_GetWorldToClip(), float4(C1,1));
	float4 d1 = mul(SysUniform_GetWorldToClip(), float4(D1,1));

	WriteLine(a0, b0, 2, outputStream);
	WriteLine(b0, d0, 2, outputStream);
	WriteLine(d0, c0, 2, outputStream);
	WriteLine(c0, a0, 2, outputStream);

	WriteLine(a0, a1, 2, outputStream);
	WriteLine(b0, b1, 2, outputStream);
	WriteLine(c0, c1, 2, outputStream);
	WriteLine(d0, d1, 2, outputStream);
}
