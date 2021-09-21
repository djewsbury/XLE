// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../standardlighttypes.hlsl"
#include "../../TechniqueLibrary/LightingEngine/LightShapes.hlsl"
#include "../operator-util.hlsl"
#include "../../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../../TechniqueLibrary/Framework/Binding.hlsl"
#include "../../TechniqueLibrary/Framework/SystemUniforms.hlsl"

/////////////////////////////////////////////////////////////////////////////////////////////////////////

cbuffer LightBuffer BIND_MAT_B1
{
    LightDesc Light;
}

cbuffer DebuggingGlobals BIND_MAT_B2
{
    const uint2 ViewportDimensions;
    const int2 MousePosition;
}

void LineLineIntersection(
    out float3 L0Closest, out float3 L1Closest,
    float3 L0A, float3 L0B, float3 L1A, float3 L1B)
{
    // this is not necessarily the most optimal solution
    // (and ignores bad cases like parallel lines)
    // just for debugging...
    float3 u = normalize(L0B - L0A);
    float3 v = normalize(L1B - L1A);

    float a = dot(u, u), b = dot(u, v), c = dot(v, v);
    float d = dot(u, L0A - L1A), e = dot(v, L0A - L1A);
    float s = (b*e - c*d) / (a*c - b*b);
    float t = (a*e - b*d) / (a*c - b*b);

    L0Closest = L0A + s * u;
    L1Closest = L1A + t * v;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

float4 main(
    float4 position : SV_Position,
    float2 texCoord : TEXCOORD0,
    float3 drawingPointViewFrustumVector : VIEWFRUSTUMVECTOR,
    SystemInputs sys) : SV_Target0
{
    // We're going to draw some debugging information for the point under the mouse cursor.
    // Let's calculate the diffuse and specular representative points for that point.

    float2 tc = MousePosition.xy / float2(ViewportDimensions);
    float3 viewFrustumVector =
        lerp(
            lerp(SysUniform_GetFrustumCorners(0).xyz, SysUniform_GetFrustumCorners(1).xyz, tc.y),
            lerp(SysUniform_GetFrustumCorners(2).xyz, SysUniform_GetFrustumCorners(3).xyz, tc.y),
            tc.x);
    float3 worldPosition = LoadWorldPosition(MousePosition, 0, viewFrustumVector);

    GBufferValues sample = LoadGBuffer(MousePosition, SystemInputs_Default());

    float3 directionToEye = normalize(-viewFrustumVector);

    float3 lightCenter = Light.Position;
    float2 lightHalfSize = float2(Light.SourceRadiusX, Light.SourceRadiusY);
    float3x3 worldToLight = float3x3(Light.OrientationX, Light.OrientationY, Light.OrientationZ);
    float3 samplePt = mul(worldToLight, worldPosition - lightCenter);
    float3 sampleNormal = mul(worldToLight, sample.worldSpaceNormal);
    float3 viewDirectionLight = mul(worldToLight, directionToEye);
    // if (samplePt.z < 0.f) return float4(0.0.xxx, 1.f);

    float2 diffRepPt = RectangleDiffuseRepPoint(samplePt, sampleNormal, lightHalfSize);
    float intersectionArea;
    float2 specRepPt = RectangleSpecularRepPoint(
        intersectionArea,
        samplePt, sampleNormal, viewDirectionLight,
        lightHalfSize, sample.material.roughness);

    float3 diffPtWorld = mul(transpose(worldToLight), float3(diffRepPt, 0)) + lightCenter;
    float3 specPtWorld = mul(transpose(worldToLight), float3(specRepPt, 0)) + lightCenter;

    float4 diffClip = mul(SysUniform_GetWorldToClip(), float4(diffPtWorld,1));
    float4 specClip = mul(SysUniform_GetWorldToClip(), float4(specPtWorld,1));

    // draw a dot at the specular and diffuse representative points
    if (diffClip.z > -diffClip.w && diffClip.z < diffClip.w) {
        float2 pt = diffClip.xy / diffClip.w;
        pt = float2(ViewportDimensions.x * (0.5f + 0.5f * pt.x), ViewportDimensions.y * (0.5f - 0.5f * pt.y));
        if (length(pt - position.xy) <= 4.f)
            return float4(1,0,0,.5);
    }

    if (specClip.z > -specClip.w && specClip.z < specClip.w) {
        float2 pt = specClip.xy / specClip.w;
        pt = float2(ViewportDimensions.x * (0.5f + 0.5f * pt.x), ViewportDimensions.y * (0.5f - 0.5f * pt.y));
        if (length(pt - position.xy) <= 4.f)
            return float4(0,1,0,.5);
    }

    float4 testClip = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));
    if (testClip.z > -testClip.w && testClip.z < testClip.w) {
        float2 pt = testClip.xy / testClip.w;
        pt = float2(ViewportDimensions.x * (0.5f + 0.5f * pt.x), ViewportDimensions.y * (0.5f - 0.5f * pt.y));
        if (length(pt - position.xy) <= 4.f)
            return float4(1,1,1,.5);
    }

    float3 reflectedDir = reflect(-directionToEye, sample.worldSpaceNormal);
    float cosConeAngle = TrowReitzDInverseApprox(RoughnessToDAlpha(sample.material.roughness));

    float sinConeAngle = sqrt(1.f - cosConeAngle*cosConeAngle);
    float tanConeAngle = sinConeAngle/cosConeAngle;

    float4 result = float4(0,0,0,1);

        // draw square for eclipse approximation
    {
        float3 reflectedDirLight = reflect(-viewDirectionLight, sampleNormal);
        float distToProjectedConeCenter = (-samplePt.z/reflectedDirLight.z);
        float2 projectedCircleCenter = samplePt.xy + reflectedDirLight.xy * distToProjectedConeCenter;

        float2 S0A; float2 S0B;
        float2 S1A; float2 S1B;
        float ellipseArea;
        float squareWeighting;
        CalculateEllipseApproximation(
            S0A, S0B, S1A, S1B, ellipseArea, squareWeighting,
            reflectedDirLight,
            distToProjectedConeCenter, sinConeAngle, cosConeAngle, projectedCircleCenter);

        // We need to find the intersection of the view ray with the light plane
        // Easiest to do this if we transform the view ray into light space, and look for
        // intersections in light space.
        float3 drawingPointWorldPosition = LoadWorldPosition(position.xy, 0, drawingPointViewFrustumVector);
        float3 drawingPointLight = mul(worldToLight, drawingPointWorldPosition - lightCenter);
        float3 drawPointViewDirectionLight = mul(worldToLight, -normalize(drawingPointViewFrustumVector));
        float2 viewRayIntersection = drawingPointLight.xy + drawPointViewDirectionLight.xy * (-drawingPointLight.z/drawPointViewDirectionLight.z);

        //if (    abs(projectedCircleCenter.x-viewRayIntersection.x) < .1f
        //    &&  abs(projectedCircleCenter.y-viewRayIntersection.y) < .1f)
        //    return float4(1,1,0,1);

        if (    viewRayIntersection.x > S0A.x && viewRayIntersection.x < S0B.x
            &&  viewRayIntersection.y > S0A.y && viewRayIntersection.y < S0B.y)
            result += float4(.5,.5,0,1);

        if (    viewRayIntersection.x > S1A.x && viewRayIntersection.x < S1B.x
            &&  viewRayIntersection.y > S1A.y && viewRayIntersection.y < S1B.y)
            result += float4(0,.5,.5,1);

        // Is this point within the cone?
        {
            float3 offset = float3(viewRayIntersection,0) - samplePt;
            float A = dot(offset, reflectedDirLight);
            if (A > 0.f) {
                float3 alongAxis = reflectedDirLight * A;
                float3 perp = offset - alongAxis;
                if (length(perp) < length(alongAxis)*tanConeAngle)
                    result += float4(0,0,.5,1);
            }
        }

        if (    abs(viewRayIntersection.x) < Light.SourceRadiusX
            &&  abs(viewRayIntersection.y) < Light.SourceRadiusY)
            result += float4(.1,.1,.1,1);
            // return 1.0.xxxx;
        // return float4(0.0.xxx, 1);
    }

    float3 L0Closest, L1Closest;
    LineLineIntersection(
        L0Closest, L1Closest,
        SysUniform_GetWorldSpaceView(), SysUniform_GetWorldSpaceView() + drawingPointViewFrustumVector,
        worldPosition, worldPosition + reflectedDir);

    float u = dot(L1Closest - worldPosition, reflectedDir);
    if (u > 0.f && length(L0Closest-L1Closest) < u*tanConeAngle)
         result += float4(.03,.03,.03 /* * exp(-0.01f * dot(L0Closest-SysUniform_GetWorldSpaceView(), drawingPointViewFrustumVector))*/,.5f);

    return result;
}
