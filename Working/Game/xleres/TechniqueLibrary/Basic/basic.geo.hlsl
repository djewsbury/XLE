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


struct ClipToNearVertex
{
	float4 position : SV_Position;
#if defined(GS_FVF)
	float2 texCoord : TEXCOORD0;
	noperspective float3 vfv : VIEWFRUSTUMVECTOR;
#endif
#if defined(GS_OBJECT_INDEX)
	nointerpolation uint objectIdx : OBJECT_INDEX;
#endif
};

static const bool ReverseZ = true;

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
	// alternatively we could just use barycentric interpolation; though we might get
	// subtly different floating point creep in each
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

ClipToNearVertex Interpolate(ClipToNearVertex start, ClipToNearVertex end, float alpha)
{
	ClipToNearVertex result;
	result.position = start.position * (1.0f - alpha) + end.position * alpha;
	if (ReverseZ) result.position.z -= 1e-4;		// (creep protection)
#if defined(GS_FVF)
	result.texCoord = start.texCoord * (1.0f - alpha) + end.texCoord * alpha;
	// result.vfv = start.vfv * (1.0f - alpha) + end.vfv * alpha;
	result.vfv = CalculateViewFrustumVectorFromClipSpacePosition(result.position);
#endif
#if defined(GS_OBJECT_INDEX)
	result.objectIdx = start.objectIdx;
#endif
	return result;
}

float NearClipAlpha_NonReverse(float Az, float Bz)
{
	return -Az / (Bz - Az);
}

ClipToNearVertex InterpolateToNear(ClipToNearVertex start, ClipToNearVertex end)
{
	if (ReverseZ) {
		// interpolate to z=+w plane
		float alpha = (start.position.w - start.position.z) / (end.position.z - end.position.w - start.position.z + start.position.w);
		// float alpha = (start.position.w + start.position.z) / (start.position.z + start.position.w - end.position.z - end.position.w);
		return Interpolate(start, end, alpha);
	} else {
		return Interpolate(start, end, NearClipAlpha_NonReverse(start.position.z, end.position.z));
	}
}

ClipToNearVertex ProjectOntoNearPlane(ClipToNearVertex input)
{
	ClipToNearVertex result = input;
	if (!SysUniform_IsOrthogonalProjection()) {
		// The following is the distance to the near clip plane, but only valid for perspective ClipSpaceType::Positive or ClipSpaceType::PositiveRightHanded transforms
		// (or ClipSpaceType::Positive_ReverseZ or ClipSpaceType::PositiveRightHanded_ReverseZ)
		float4 miniProj = SysUniform_GetMinimalProjection();
		if (ReverseZ) {
			float A = miniProj.z;
			float B = miniProj.w;
			// result.position.z = 1.0/(A/B + 1.0/B);	// more expensive, but more accurate than B/(A+1) when A is near -1
			result.position.z = B/(A+1);
			result.position.w = result.position.z;
			result.position.z -= 1e-4;		// creep protection (need more than in the non-reverse-Z case)
		} else {
			result.position.z = 1e-6;		// creep protection
			result.position.w = miniProj.w / miniProj.z;
		}
	} else {
		// For an orthographic projection, we can actually just clamp to z=0, we don't actually have to do the full clipping algorithm
		if (ReverseZ) {
			result.position.z = 1;
			result.position.w = 1;
		} else {
			result.position.z = 1e-6;		// creep protection
			result.position.w = 1;
		}
	}
#if defined(GS_FVF)
	result.vfv = CalculateViewFrustumVectorFromClipSpacePosition(result.position);
#endif
#if defined(GS_OBJECT_INDEX)
	result.objectIdx = input.objectIdx;
#endif
	return result; 
}

[maxvertexcount(2*3)]
	void ClipToNear(triangle ClipToNearVertex input[3], inout TriangleStream<ClipToNearVertex> outputStream)
{
	ClipToNearVertex outVert;

	// This will clip the geometry to the near clip space, and then flatten the part belond the near clip plane so that
	// it's lying exactly on the near clip plane
	bool clip0, clip1, clip2;
	if (ReverseZ) {
		clip0 = (input[0].position.z / input[0].position.w) < 0 || (input[0].position.z / input[0].position.w) > 1;
		clip1 = (input[1].position.z / input[1].position.w) < 0 || (input[1].position.z / input[1].position.w) > 1;
		clip2 = (input[2].position.z / input[2].position.w) < 0 || (input[2].position.z / input[2].position.w) > 1;
	} else {
		clip0 = input[0].position.z < 0.f;
		clip1 = input[1].position.z < 0.f;
		clip2 = input[2].position.z < 0.f;
	}

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
				ClipToNearVertex B = InterpolateToNear(input[1], input[2]);
				ClipToNearVertex C = InterpolateToNear(input[2], input[0]);

				outputStream.Append(input[2]);
				outputStream.Append(C);
				outputStream.Append(B);
				outputStream.Append(ProjectOntoNearPlane(input[0]));
				outputStream.Append(ProjectOntoNearPlane(input[1]));
			}

		} else if (clip2) {
			// 0 -> outside, 1 -> inside, 2 -> outside
			ClipToNearVertex A = InterpolateToNear(input[0], input[1]);
			ClipToNearVertex B = InterpolateToNear(input[1], input[2]);

			outputStream.Append(input[1]);
			outputStream.Append(B);
			outputStream.Append(A);
			outputStream.Append(ProjectOntoNearPlane(input[2]));
			outputStream.Append(ProjectOntoNearPlane(input[0]));

		} else {
			// 0 -> outside, 1, 2 -> inside
			ClipToNearVertex A = InterpolateToNear(input[0], input[1]);
			ClipToNearVertex C = InterpolateToNear(input[2], input[0]);

			outputStream.Append(input[1]);
			outputStream.Append(input[2]);
			outputStream.Append(A);
			outputStream.Append(C);
			outputStream.Append(ProjectOntoNearPlane(input[0]));
		}
		
	} else if (clip1) {
		if (clip2) {
			// 0 -> inside, 1, 2 -> outside
			ClipToNearVertex A = InterpolateToNear(input[0], input[1]);
			ClipToNearVertex C = InterpolateToNear(input[2], input[0]);

			outputStream.Append(input[0]);
			outputStream.Append(A);
			outputStream.Append(C);
			outputStream.Append(ProjectOntoNearPlane(input[1]));
			outputStream.Append(ProjectOntoNearPlane(input[2]));

		} else {
			// 0 -> inside, 1 -> outside, 2 -> inside
			ClipToNearVertex A = InterpolateToNear(input[0], input[1]);
			ClipToNearVertex B = InterpolateToNear(input[1], input[2]);

			outputStream.Append(input[2]);
			outputStream.Append(input[0]);
			outputStream.Append(B);
			outputStream.Append(A);
			outputStream.Append(ProjectOntoNearPlane(input[1]));
		}
		
	} else if (clip2) {
		// 0, 1 -> inside, 2 -> outside
		ClipToNearVertex B = InterpolateToNear(input[1], input[2]);
		ClipToNearVertex C = InterpolateToNear(input[2], input[0]);

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
