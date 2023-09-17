// Generate a "tube" geometry in a way that matches the tube light types.
// We extract the cardinal axes from the local to world, and use this to construct the tube shape
// The ends are capped with hemispheres (using longitude/latitude arrangement of vertices)
// It results in a singularity at the top of the hemisphere... But it has a straight equator.
//
// This is also an example of a custom built technique that inherits from, but overrides certain
// parts of the default technique.

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/WorkingVertex.hlsl"
#include "../Math/MathConstants.hlsl"

struct GeneratedVertex
{
    float3 worldPosition;
    float3 worldNormal;
};

float3 SphericalToCartesian(float3 spherical)
{
    float s0, c0, s1, c1;
    sincos(spherical.x, s0, c0);
    sincos(spherical.y, s1, c1);
    return float3(
        spherical.z * s0 * c1,
        spherical.z * s0 * s1,
        spherical.z * c0);
}

GeneratedVertex GenVertex(uint vIndex, uint shape)
{
    uint quadIndex = vIndex / 6;
    uint indexInQuad = vIndex % 6;

    if (shape == 3) {
        // rectangle shape. Just done with 6 quads.
        // This isn't going to produce efficient shader code; but it's ok for simple stuff

        float3 normals[6] = {
            float3(0.f, 0.f, -1.f), float3(0.f, 0.f, 1.f),
            float3(1.f, 0.f, 0.f), float3(-1.f, 0.f, 0.f),
            float3(0.f, 1.f, 0.f), float3(0.f, -1.f, 0.f)
        };
        float3 Us[] = {
            float3(1.f, 0.f, 0.f), float3(-1.f, 0.f, 0.f),
            float3(0.f, 1.f, 0.f), float3(0.f, -1.f, 0.f),
            float3(-1.f, 0.f, 0.f), float3(1.f, 0.f,  0.f)
        };
        float3 Vs[] = {
            float3(0.f, 1.f, 0.f), float3(0.f, 1.f, 0.f),
            float3(0.f, 0.f, -1.f), float3(0.f, 0.f, -1.f),
            float3(0.f, 0.f, -1.f), float3(0.f, 0.f, -1.f)
        };

        float3 normal = normals[quadIndex], u = Us[quadIndex], v = Vs[quadIndex];

        float2 s;
             if (indexInQuad == 0) s = float2(-1, -1);
        else if (indexInQuad == 1) s = float2(-1,  1);
        else if (indexInQuad == 2) s = float2( 1, -1);
        else if (indexInQuad == 3) s = float2( 1, -1);
        else if (indexInQuad == 4) s = float2(-1,  1);
        else                       s = float2( 1,  1);

        GeneratedVertex result;
        result.worldNormal = LocalToWorldUnitVector(normal);
        result.worldPosition = normal + s.x * u + s.y * v;

        float3x4 localToWorld = SysUniform_GetLocalToWorld();
        float3 normAxis = float3(localToWorld[0].z, localToWorld[1].z, localToWorld[2].z);
        normAxis = 0.01f * normalize(normAxis);
        localToWorld[0].z = normAxis.x; localToWorld[1].z = normAxis.y; localToWorld[2].z = normAxis.z;
        result.worldPosition = mul(localToWorld, float4(result.worldPosition,1)).xyz;
        return result;
    } else if (shape == 4) {
        // Cylinder shape.
        const uint faceCount = 32;
        float t0 = 2 * pi * float(quadIndex) / float(faceCount);
        float t1 = 2 * pi * float(quadIndex+1) / float(faceCount);

        float2 s;
             if (indexInQuad == 0) s = float2(t0, 0);
        else if (indexInQuad == 1) s = float2(t0, 1);
        else if (indexInQuad == 2) s = float2(t1, 0);
        else if (indexInQuad == 3) s = float2(t1, 0);
        else if (indexInQuad == 4) s = float2(t0, 1);
        else                       s = float2(t1, 1);

        float3 localPosition;
        localPosition.z = s.y;
        sincos(s.x, localPosition.y, localPosition.x);

        GeneratedVertex result;
        result.worldPosition = mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;
        result.worldNormal = float3(localPosition.xy, 0.f);
        return result;
    } else {
        // tube or long/lat sphere shape
        const uint quadDims = 12;

        float sx = floor(quadIndex / float(quadDims)) / quadDims;
        float sy = frac(quadIndex / float(quadDims));

        float2 sMins = float2(sx, sy);
        float2 sMaxs = float2(sx + 1.0f/float(quadDims), sy + 1.0f/float(quadDims));

        sMins.x *= pi; sMaxs.x *= pi;
        sMins.y *= 2.0f*pi; sMaxs.y *= 2.0f*pi;

        float2 s;
             if (indexInQuad == 0) s = float2(sMins.x, sMins.y);
        else if (indexInQuad == 1) s = float2(sMins.x, sMaxs.y);
        else if (indexInQuad == 2) s = float2(sMaxs.x, sMins.y);
        else if (indexInQuad == 3) s = float2(sMaxs.x, sMins.y);
        else if (indexInQuad == 4) s = float2(sMins.x, sMaxs.y);
        else                       s = float2(sMaxs.x, sMaxs.y);

        float3 baseDirection = SphericalToCartesian(float3(s, 1.f));
        bool downCap = baseDirection.z < 0.f;

        float3 xAxisLong = float3(SysUniform_GetLocalToWorld()[0].x, SysUniform_GetLocalToWorld()[1].x, SysUniform_GetLocalToWorld()[2].x);
        float3 xAxis = normalize(xAxisLong);
        float radius = length(xAxisLong);
        float3 zAxisLong = float3(SysUniform_GetLocalToWorld()[0].y, SysUniform_GetLocalToWorld()[1].y, SysUniform_GetLocalToWorld()[2].y);
        float3 zAxis = normalize(zAxisLong);
        float3 yAxisLong = float3(SysUniform_GetLocalToWorld()[0].z, SysUniform_GetLocalToWorld()[1].z, SysUniform_GetLocalToWorld()[2].z);
        float3 yAxis = normalize(yAxisLong);

        baseDirection =
            baseDirection.x * xAxis
            + baseDirection.y * yAxis
            + baseDirection.z * zAxis;

        GeneratedVertex result;
        result.worldNormal = baseDirection;
        result.worldPosition = result.worldNormal;

        result.worldPosition =
            radius * result.worldPosition + float3(SysUniform_GetLocalToWorld()[0].w, SysUniform_GetLocalToWorld()[1].w, SysUniform_GetLocalToWorld()[2].w);

        if (shape == 2) {
                // We stretch out the tube here... It's not perfect. Ideally we should add
                // another row of quads to go around the long part of the tube. But it looks
                // find for non critical purposess
            if (downCap)    result.worldPosition -= zAxisLong;
            else            result.worldPosition += zAxisLong;
        }  /* (otherwise we just get a sphere) */

        return result;
    }
}

#if !defined(GEO_HAS_VERTEX_ID)
	#error Expecting GEO_HAS_VERTEX_ID to be set
#endif

WorkingVertex VertexPatch(VSIN vInput)
{
    WorkingVertex dv = WorkingVertex_DefaultInitialize(vInput);

	GeneratedVertex genVertex = GenVertex(vInput.vertexId, SHAPE);
	dv.position = genVertex.worldPosition;
	dv.tangentFrame.tangent = 0.0.xxx;
	dv.tangentFrame.bitangent = 0.0.xxx;
    dv.tangentFrame.normal = 0.0.xxx;
	dv.tangentFrame.handiness = 0;
    dv.tangentFrameType = WORKINGVERTEX_TANGENTFRAME_NONE;
	dv.coordinateSpace = WORKINGVERTEX_COORDINATESPACE_WORLD;
	return dv;
}

// note -- 	this ensures that that the preprocessor recognizes SHAPE as a required preprocessor macro
#if defined(SHAPE)
#endif
