// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define DIFFUSE_METHOD 1
#define FORCE_GGX_REF 1

#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
#include "../TechniqueLibrary/LightingEngine/DirectionalResolve.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"

float3 BRDF(float3 lightDir, float3 view, float3 normal, float3 tangent, float3 bitangent)
{
    GBufferValues sample = GBufferValues_Default();
    sample.worldSpaceNormal = normal;
    sample.diffuseAlbedo = SRGBToLinear(float3(255/255.f, 33/255.f, 33/255.f)); // SRGBToLinear(float3(0.376470588f, 0.00392f, 0.00392f));
    sample.material.roughness = 1.f;
    sample.material.specular = 1.f;
    sample.material.metal = 0.f;

    LightDesc light;
    light.NegativeDirection = lightDir;
    light.Radius = 10000.f;
    light.Color.diffuse = float3(1,1,1);
    light.Color.specular = float3(1,1,1);
    light.Power = 1.f;
    light.DiffuseWideningMin = 0.5f;
    light.DiffuseWideningMax = 2.5f;

    float3 diffuse = DirectionalLightResolve_Diffuse(sample, view, lightDir, light);
    float3 specular = DirectionalLightResolve_Specular(sample, view, lightDir, light);
    return diffuse + specular;
}

float3 RotateVector(float3 v, float3 axis, float angle)
{
    axis = normalize(axis);
    float3 n = axis * dot(axis, v);
    return n + cos(angle)*(v-n) + sin(angle)*cross(axis, v);
}

static const float incidentPhi = 45.f * pi / 180.f;
static const float phiD = 90.f * pi / 180.f;

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float3 normal = float3(0,0,1), tangent = float3(1,0,0), bitangent = float3(0,1,0);

    float thetaH = texCoord.x * pi / 2;
    float thetaD = (1.f - texCoord.y) * pi / 2;

    float phiH = incidentPhi;
    float sinThetaH = sin(thetaH), cosThetaH = cos(thetaH);
    float sinPhiH = sin(phiH), cosPhiH = cos(phiH);
    float3 H = float3(sinThetaH*cosPhiH, sinThetaH*sinPhiH, cosThetaH);

    float sinThetaD = sin(thetaD), cosThetaD = cos(thetaD);
    float sinPhiD = sin(phiD), cosPhiD = cos(phiD);
    float3 D = float3(sinThetaD*cosPhiD, sinThetaD*sinPhiD, cosThetaD);

    float3 L = RotateVector(RotateVector(D, bitangent, thetaH), normal, phiH);
    float3 V = 2*dot(H,L)*H - L;
    float3 b = BRDF( L, V, normal, tangent, bitangent );
    return float4(b, 1.f);
}
