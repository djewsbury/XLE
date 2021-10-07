// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VSOUT_HAS_TEXCOORD 1

#include "../../TechniqueLibrary/Framework/MainGeometry.hlsl"

struct IntersectionResult
{
    float4	_intersectionDistanceAndCoords : INTERSECTION;
};

cbuffer IntersectionTestParameters : register(b2)
{
    float3 RayStart;
    float3 RayEnd;
}

float RayVsTriangle(float3 rayStart, float3 rayEnd, float3 inputTriangle[3], out float3 barycentricResult)
{
        //
        //		Find the plane of the triangle, and find the intersection point
        //		between the ray and that plane. Then test to see if the intersection
        //		point is within the triangle (or on an edge).
        //

    float3 triangleNormal = normalize(cross(inputTriangle[1] - inputTriangle[0], inputTriangle[2] - inputTriangle[0]));
    float planeW = dot(inputTriangle[0], triangleNormal);

    float A = dot(rayStart, triangleNormal) - planeW;
    float B = dot(rayEnd, triangleNormal) - planeW;
    float alpha = A / (A-B);
    if (alpha < 0.f || alpha > 1.f)
        return -1.f;

    float3 intersectionPt = lerp(rayStart, rayEnd, alpha);

        //	look to see if this point is contained within the triangle
        //	we'll use barycentric coordinates (because that's useful for
        //	the caller later)
    float3 v0 = inputTriangle[1] - inputTriangle[0], v1 = inputTriangle[2] - inputTriangle[0], v2 = intersectionPt - inputTriangle[0];
    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0f - v - w;

    if (u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f && w >= 0.f && w <= 1.f) {
        barycentricResult = float3(u, v, w);
        return alpha;
    }

    return -1.f;
}

[maxvertexcount(1)]
    void gs_intersectiontest(triangle VSOUT input[3], inout PointStream<IntersectionResult> intersectionHits)
{
        //
        //		Given the input triangle, look for an intersection with the ray we're currently
        //		testing. If we find an intersection, let's append it to the IntersectionHits buffer.
        //
        //		We going to do the test in world space. But there's an important advantage to doing
        //		this in the geometry shader (instead of compute shader, or on the CPU) -- it uses
        //		the same LOD calculations as normal rendering. So we end up testing against the same
        //		triangles that get rendered (which means that the result seems consistant to the user)
        //

    #if VSOUT_HAS_WORLD_POSITION
        float3 testingTriangle[3];
        testingTriangle[0] = input[0].worldPosition;
        testingTriangle[1] = input[1].worldPosition;
        testingTriangle[2] = input[2].worldPosition;

        float3 barycentric;
        float intersection = RayVsTriangle(RayStart, RayEnd, testingTriangle, barycentric);
        if (intersection >= 0.f && intersection < 1.f) {

            #if VSOUT_HAS_TEXCOORD
                float2 surfaceCoordinates =
                      barycentric.x * input[0].texCoord
                    + barycentric.y * input[1].texCoord
                    + barycentric.z * input[2].texCoord
                    ;
            #else
                float2 surfaceCoordinates = 0.0.xx;
            #endif

            IntersectionResult intersectionResult;
            intersectionResult._intersectionDistanceAndCoords = float4(
                intersection, surfaceCoordinates.x, surfaceCoordinates.y, 1.f);

                //	can we write to an append buffer from a geometry shader?
                //	it would be a lot more convenient than having to use stream output
                //		(which can be a bit messy on the CPU side)
            intersectionHits.Append(intersectionResult);
        }

        // IntersectionResult temp;
        // temp._intersectionDistanceAndCoords = 1.0.xxxx;
        // intersectionHits.Append(temp);
    #endif
}
