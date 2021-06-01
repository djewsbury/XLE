// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/SystemUniforms.hlsl"

struct InputVertex_PNT
{
	float4 position		: SV_Position;
	float3 viewVector	: VIEWVECTOR;
	float3 normal		: NORMAL0;
	float2 texCoord		: TEXCOORD0;
};

struct OutputVertex_PNT
{
	float4 position		: SV_Position;
	float3 viewVector	: VIEWVECTOR;
	float2 texCoord		: TEXCOORD0;

	float2 barycentricCoords			: BARYCENTRIC;	// Barycentric interpolants
	nointerpolation float3 normals[6]	: SIXNORMS; // 6 normal positions
};

[maxvertexcount(3)]
	void PNT(triangle InputVertex_PNT input[3], inout TriangleStream<OutputVertex_PNT> outputStream)
{
	OutputVertex_PNT outVert;

	outVert.normals[0] = normalize(input[0].normal);
	outVert.normals[1] = normalize(input[0].normal + input[1].normal);
	outVert.normals[2] = normalize(input[1].normal);
	outVert.normals[3] = normalize(input[2].normal + input[0].normal);
	outVert.normals[4] = normalize(input[2].normal);
	outVert.normals[5] = normalize(input[1].normal + input[2].normal);

	outVert.position	= input[0].position;
	outVert.viewVector	= input[0].viewVector;
	outVert.texCoord	= input[0].texCoord;
	outVert.barycentricCoords = float2(0.f, 0.f);
	outputStream.Append(outVert);

	outVert.position	= input[1].position;
	outVert.viewVector	= input[1].viewVector;
	outVert.texCoord	= input[1].texCoord;
	outVert.barycentricCoords = float2(1.f, 0.f);
	outputStream.Append(outVert);

	outVert.position	= input[2].position;
	outVert.viewVector	= input[2].viewVector;
	outVert.texCoord	= input[2].texCoord;
	outVert.barycentricCoords = float2(0.f, 1.f);
	outputStream.Append(outVert);

	outputStream.RestartStrip();
}




struct InputVertex_PN
{
	float4 position		: SV_Position;
	float3 viewVector	: VIEWVECTOR;
	float3 normal		: NORMAL0;
};

struct OutputVertex_PN
{
	float4 position		: SV_Position;
	float3 viewVector	: VIEWVECTOR;

	float2 barycentricCoords			: BARYCENTRIC;	// Barycentric interpolants
	nointerpolation float3 normals[6]	: SIXNORMS; // 6 normal positions
};

[maxvertexcount(3)]
	void PN(triangle InputVertex_PN input[3], inout TriangleStream<OutputVertex_PN> outputStream)
{
	OutputVertex_PN outVert;

	outVert.normals[0] = normalize(input[0].normal);
	outVert.normals[1] = normalize(input[0].normal + input[1].normal);
	outVert.normals[2] = normalize(input[1].normal);
	outVert.normals[3] = normalize(input[2].normal + input[0].normal);
	outVert.normals[4] = normalize(input[2].normal);
	outVert.normals[5] = normalize(input[1].normal + input[2].normal);

	outVert.position	= input[0].position;
	outVert.viewVector	= input[0].viewVector;
	outVert.barycentricCoords = float2(0.f, 0.f);
	outputStream.Append(outVert);

	outVert.position	= input[1].position;
	outVert.viewVector	= input[1].viewVector;
	outVert.barycentricCoords = float2(1.f, 0.f);
	outputStream.Append(outVert);

	outVert.position	= input[2].position;
	outVert.viewVector	= input[2].viewVector;
	outVert.barycentricCoords = float2(0.f, 1.f);
	outputStream.Append(outVert);

	outputStream.RestartStrip();
}


struct InputVertex_PCR
{
	float4 position		: SV_Position;
	float4 color		: COLOR0;
	float radius		: RADIUS;
};

struct OutputVertex_PC
{
	float4 position		: SV_Position;
	float4 color		: COLOR0;
};

[maxvertexcount(3)]
	void PCR(point InputVertex_PCR input[1], inout PointStream<OutputVertex_PC> outputStream)
{
	OutputVertex_PC outVert;
	outVert.position = input[0].position;
	outVert.color = input[0].color;
	outputStream.Append(outVert);
}


struct PT_viewfrustumVector
{
	float4 position : SV_Position;
	float2 texCoord : TEXCOORD0;
	noperspective float3 vfv : VIEWFRUSTUMVECTOR;
};

float3 CalculateViewFrustumVectorFromClipSpacePosition(float4 clipSpacePosition)
{
	float2 positionProjected = clipSpacePosition.xy / clipSpacePosition.w;
	float2 texCoord;

	#if 1 // NDC == NDC_POSITIVE_RIGHT_HANDED
		texCoord.y = (positionProjected.y + 1.0f) / 2.0f;
	#else
		texCoord.y = (1.0f - positionProjected.y) / 2.0f;
	#endif
	texCoord.x = (positionProjected.x + 1.0f) / 2.0f;

	// We can use bilinear interpolation here to find the correct view frustum vector
	// Note that this require we use "noperspective" interpolation between vertices for this attribute, though
	float w0 = (1.0f - texCoord.x) * (1.0f - texCoord.y);
	float w1 = (1.0f - texCoord.x) * texCoord.y;
	float w2 = texCoord.x * (1.0f - texCoord.y);
	float w3 = texCoord.x * texCoord.y;

	return 
		  w0 * SysUniform_GetFrustumCorners(0).xyz
		+ w1 * SysUniform_GetFrustumCorners(1).xyz
		+ w2 * SysUniform_GetFrustumCorners(2).xyz
		+ w3 * SysUniform_GetFrustumCorners(3).xyz
		;
}

PT_viewfrustumVector Interpolate(PT_viewfrustumVector start, PT_viewfrustumVector end, float alpha)
{
	PT_viewfrustumVector result;
	result.position = start.position * (1.0f - alpha) + end.position * alpha;
	result.texCoord = start.texCoord * (1.0f - alpha) + end.texCoord * alpha;
	// result.vfv = start.vfv * (1.0f - alpha) + end.vfv * alpha;
	result.vfv = CalculateViewFrustumVectorFromClipSpacePosition(result.position);
	return result;
}

PT_viewfrustumVector ProjectOntoNearPlane(PT_viewfrustumVector input)
{
	PT_viewfrustumVector result = input;
	result.position.z = 1e-6f;
	if (!SysUniform_IsOrthogonalProjection()) {
		// The following is the distance to the near clip plane, but only valid for perspective ClipSpaceType::Positive or ClipSpaceType::PositiveRightHanded transforms
		result.position.w = SysUniform_GetMinimalProjection().w / SysUniform_GetMinimalProjection().z;
	} else {
		// For an orthoraphic projection, we can actually just clamp to z=0, we don't actually have to do the full clipping algorithm
		result.position.w = 1;
	}
	result.vfv = CalculateViewFrustumVectorFromClipSpacePosition(result.position);
	return result; 
}

[maxvertexcount(2*3)]
	void PT_viewfrustumVector_clipToNear(triangle PT_viewfrustumVector input[3], inout TriangleStream<PT_viewfrustumVector> outputStream)
{
	PT_viewfrustumVector outVert;

	// This will clip the geometry to the near clip space, and then flatten the part belond the near clip plane so that
	// it's lying exactly on the near clip plane 
	bool clip0 = input[0].position.z < 0.f;
	bool clip1 = input[1].position.z < 0.f;
	bool clip2 = input[2].position.z < 0.f;

	// A: 0->1
	// B: 1->2
	// C: 2->0

	if (clip0) {
		if (clip1) {
			if (clip2) {
				// 0, 1, 2 -> outside
				outputStream.Append(ProjectOntoNearPlane(input[0]));
				outputStream.Append(ProjectOntoNearPlane(input[1]));
				outputStream.Append(ProjectOntoNearPlane(input[2]));
			} else {
				// 0, 1 -> outside, 2 -> inside
				float alphaB = -input[1].position.z / (input[2].position.z - input[1].position.z);
				float alphaC = -input[2].position.z / (input[0].position.z - input[2].position.z);
				PT_viewfrustumVector B = Interpolate(input[1], input[2], alphaB);
				PT_viewfrustumVector C = Interpolate(input[2], input[0], alphaC);

				outputStream.Append(input[2]);
				outputStream.Append(C);
				outputStream.Append(B);
				outputStream.Append(ProjectOntoNearPlane(input[0]));
				outputStream.Append(ProjectOntoNearPlane(input[1]));
			}

		} else if (clip2) {
			// 0 -> outside, 1 -> inside, 2 -> outside
			float alphaA = -input[0].position.z / (input[1].position.z - input[0].position.z);
			float alphaB = -input[1].position.z / (input[2].position.z - input[1].position.z);
			PT_viewfrustumVector A = Interpolate(input[0], input[1], alphaA);
			PT_viewfrustumVector B = Interpolate(input[1], input[2], alphaB);

			outputStream.Append(input[1]);
			outputStream.Append(B);
			outputStream.Append(A);
			outputStream.Append(ProjectOntoNearPlane(input[2]));
			outputStream.Append(ProjectOntoNearPlane(input[0]));

		} else {
			// 0 -> outside, 1, 2 -> inside
			float alphaA = -input[0].position.z / (input[1].position.z - input[0].position.z);
			float alphaC = -input[2].position.z / (input[0].position.z - input[2].position.z);
			PT_viewfrustumVector A = Interpolate(input[0], input[1], alphaA);
			PT_viewfrustumVector C = Interpolate(input[2], input[0], alphaC);

			outputStream.Append(input[1]);
			outputStream.Append(input[2]);
			outputStream.Append(A);
			outputStream.Append(C);
			outputStream.Append(ProjectOntoNearPlane(input[0]));
		}
		
	} else if (clip1) {
		if (clip2) {
			// 0 -> inside, 1, 2 -> outside
			float alphaA = -input[0].position.z / (input[1].position.z - input[0].position.z);
			float alphaC = -input[2].position.z / (input[0].position.z - input[2].position.z);
			PT_viewfrustumVector A = Interpolate(input[0], input[1], alphaA);
			PT_viewfrustumVector C = Interpolate(input[2], input[0], alphaC);

			outputStream.Append(input[0]);
			outputStream.Append(A);
			outputStream.Append(C);
			outputStream.Append(ProjectOntoNearPlane(input[1]));
			outputStream.Append(ProjectOntoNearPlane(input[2]));

		} else {
			// 0 -> inside, 1 -> outside, 2 -> inside
			float alphaA = -input[0].position.z / (input[1].position.z - input[0].position.z);
			float alphaB = -input[1].position.z / (input[2].position.z - input[1].position.z);
			PT_viewfrustumVector A = Interpolate(input[0], input[1], alphaA);
			PT_viewfrustumVector B = Interpolate(input[1], input[2], alphaB);

			outputStream.Append(input[2]);
			outputStream.Append(input[0]);
			outputStream.Append(B);
			outputStream.Append(A);
			outputStream.Append(ProjectOntoNearPlane(input[1]));
		}
		
	} else if (clip2) {
		// 0, 1 -> inside, 2 -> outside
		float alphaB = -input[1].position.z / (input[2].position.z - input[1].position.z);
		float alphaC = -input[2].position.z / (input[0].position.z - input[2].position.z);
		PT_viewfrustumVector B = Interpolate(input[1], input[2], alphaB);
		PT_viewfrustumVector C = Interpolate(input[2], input[0], alphaC);

		outputStream.Append(input[0]);
		outputStream.Append(input[1]);
		outputStream.Append(C);
		outputStream.Append(B);
		outputStream.Append(ProjectOntoNearPlane(input[2]));
	} else {
		// all inside
		outputStream.Append(input[0]);
		outputStream.Append(input[1]);
		outputStream.Append(input[2]);
	}

	outputStream.RestartStrip();
}
