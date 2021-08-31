// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define RAY_TEST_GEO 1
#include "../Framework/VSOUT.hlsl"
#include "../Framework/CommonResources.hlsl"
#include "../Framework/SystemUniforms.hlsl"
#include "../../Nodes/Templates.pixel.sh"

#if !defined(PATCH_EARLY_REJECTION)
	#include "../../BasicMaterial.hlsl"
#endif

struct GSOutput
{
	float4	triangleA : POINT0;
	float4	triangleB : POINT1;
	float4	triangleC : POINT2;
	uint4	properties : PROPERTIES;
};

cbuffer RayDefinition BIND_SEQ_B4
{
	float3	RayStart;
	float	RayLength;
	float3	RayDirection;
 	row_major float4x4 IntersectionFrustum;
}

cbuffer DrawCallProperties BIND_NUMERIC_B4
{
	uint2 MaterialGuid;
	uint CurrentDrawCallIndex;
}

Texture2D		DiffuseTexture          BIND_MAT_T3;

float3 RayTriangleIntersection(float3 p, float3 d, float3 v0, float3 v1, float3 v2)
{
		//		basic alegrabic intersection method from--
		//	http://www.lighthouse3d.com/tutorials/maths/ray-triangle-intersection/
	float3 e1 = v1 - v0;
	float3 e2 = v2 - v0;

	float3 h = cross(d,e2);
	float a = dot(e1,h);

	if (a == 0.f)		// this test has some problems with very small triangles. We can't test for close to zero, we have to test for exact equality
		return 0.0.xxx;

	float f = 1.f/a;
	float3 s = p-v0;
	float u = f * (dot(s,h));

	if (u < 0.f || u > 1.f)
		return 0.0.xxx;

	float3 q = cross(s,e1);
	float v = f * dot(d,q);

	if (v < 0.f || u + v > 1.f)
		return 0.0.xxx;

	// at this stage we can compute t to find out where
	// the intersection point is on the line
	float t = f * dot(e2,q);

	if (t > 0.00001f) // ray intersection
		return float3( t, u, v );
	else
		return 0.0.xxx;
}

bool PtInFrustum(float4 pt)
{
	float3 q = float3(abs(pt.xy), max(pt.z, pt.w-pt.z));
	float m = max(max(q.x, q.y), q.z);
	return m <= pt.w;
}

bool TriangleInFrustum(float4 p0, float4 p1, float4 p2)
{
		// 	All points must be rejected by the same frustum plane.
		//	Even if all of the points are outside of the frustum, the triangle must still be within
		//	This is a brute force implementation... we could do better with
	if (p0.x < -p0.w && p1.x < -p1.w && p2.x < -p2.w) return false;
	if (p0.x >  p0.w && p1.x >  p1.w && p2.x >  p2.w) return false;
	if (p0.y < -p0.w && p1.y < -p1.w && p2.y < -p2.w) return false;
	if (p0.y >  p0.w && p1.y >  p1.w && p2.y >  p2.w) return false;
	if (p0.z < 0.0f && p1.z < 0.0f && p2.z < -0.0f) return false;
	if (p0.z >  p0.w && p1.z >  p1.w && p2.z >  p2.w) return false;
	return true;
}

[maxvertexcount(1)]
	void triangles(triangle VSOUT input[3], inout PointStream<GSOutput> outputStream)
{
	// Test the triangle to see if there is an intersection with the given ray.
	// If we get an intersection, write the result to "outputStream"
	//
	// We're assuming that the ray is in world space. We'll use the worldSpace member
	// of the input vertices to compare against the ray.
	//
	// We're going to ignore the winding order. So callers will get both front-face
	// and back-face intersections.

#if !defined(INTERSECTION_TEST) || (INTERSECTION_TEST == 0)

	float3 intersectionResult =
		RayTriangleIntersection(
			RayStart, RayDirection,
			input[0].worldPosition, input[1].worldPosition, input[2].worldPosition);
	if (intersectionResult.x > 0.f && intersectionResult.x < RayLength) {
		float3 barycentric = float3(1.f - intersectionResult.y - intersectionResult.z, intersectionResult.yz);
		bool isOpaquePart = true;

		// If this is alpha test geometry, we need to check the texture
		// to see if this  is a transparent pixel. This method is sometimes
		// used for "picking" tests in tools. Without this alpha test check,
		// the alpha tested triangles will behave like opaque triangles, which
		// will give a confusing result for the user.
		#if defined(PATCH_EARLY_REJECTION) && (VSOUT_HAS_TEXCOORD>=1)
			float2 texCoord =
				  barycentric.x * VSOUT_GetTexCoord0(input[0])
				+ barycentric.y * VSOUT_GetTexCoord0(input[1])
				+ barycentric.z * VSOUT_GetTexCoord0(input[2])
				;
			input[0].texCoord = texCoord;
			isOpaquePart = !EarlyRejectionTest(input[0]);
		#elif (VSOUT_HAS_TEXCOORD>=1) && (MAT_ALPHA_TEST==1)
			float2 texCoord =
				  barycentric.x * VSOUT_GetTexCoord0(input[0])
				+ barycentric.y * VSOUT_GetTexCoord0(input[1])
				+ barycentric.z * VSOUT_GetTexCoord0(input[2])
				;
			isOpaquePart = DiffuseTexture.SampleLevel(DefaultSampler, texCoord, 0).a >= AlphaThreshold;
		#endif

		if (isOpaquePart) {
			GSOutput result;
			result.triangleA = float4(input[0].worldPosition, barycentric.x);
			result.triangleB = float4(input[1].worldPosition, barycentric.y);
			result.triangleC = float4(input[2].worldPosition, barycentric.z);
			result.properties.x = asuint(intersectionResult.x);
			result.properties.y = CurrentDrawCallIndex;
			result.properties.zw = MaterialGuid;
			outputStream.Append(result);
		}
	}

#elif INTERSECTION_TEST == 1

	float4 p0 = mul(IntersectionFrustum, float4(input[0].worldPosition, 1.f));
	float4 p1 = mul(IntersectionFrustum, float4(input[1].worldPosition, 1.f));
	float4 p2 = mul(IntersectionFrustum, float4(input[2].worldPosition, 1.f));
	if (TriangleInFrustum(p0, p1, p2)) {
		GSOutput result;
		result.triangleA = float4(input[0].worldPosition, 0.f);
		result.triangleB = float4(input[1].worldPosition, 0.f);
		result.triangleC = float4(input[2].worldPosition, 0.f);
		result.properties.x = asuint(1.f);
		result.properties.y = CurrentDrawCallIndex;
		result.properties.zw = MaterialGuid;
		outputStream.Append(result);
	}

#endif
}
