
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../TechniqueLibrary/Framework/SystemUniforms.hlsl"
#include "../TechniqueLibrary/Framework/VSOUT.hlsl"

float SkyReflectionFresnelFactor(VSOUT geo)
{
    float3 worldSpaceNormal = VSOUT_GetNormal(geo);
    float3 viewVector = SysUniform_GetWorldSpaceView() - VSOUT_GetWorldPosition(geo);
    float viewVectorMag = rsqrt(dot(viewVector, viewVector));
    return SchlickFresnelCore(dot(worldSpaceNormal, viewVector) * viewVectorMag);
}
