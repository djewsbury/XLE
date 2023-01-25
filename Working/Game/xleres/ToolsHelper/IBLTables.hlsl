// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "sampling-shader-helper.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

float3 IBLPrecalc_SampleInputTexture(float3 direction) { return 0; }
#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

RWTexture2D<float4> Output : register(u1, space0);

[[vk::push_constant]] struct ControlUniformsStruct
{
    SamplingShaderUniforms _samplingShaderUniforms;
    uint4 A;
} ControlUniforms;
uint ControlUniforms_GetMipIndex() { return ControlUniforms.A.x; }

[numthreads(8, 8, 1)]
    void GenerateSplitSumGlossLUT(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    uint2 textureDims;
	Output.GetDimensions(textureDims.x, textureDims.y);

    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 1), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, 1))) return;

    float2 texCoord = helper._outputPixel.xy / float2(textureDims);
    float NdotV = max(texCoord.x, .1/float(textureDims.x));  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;

    if (helper._firstDispatch)
        Output[helper._outputPixel.xy] = 0;
    Output[helper._outputPixel.xy].xy += GenerateSplitTerm(
        NdotV, roughness,
        helper._thisPassSampleOffset, helper._thisPassSampleCount, helper._thisPassSampleStride, helper._totalSampleCount);
}

float4 GenerateSplitSumGlossTransmissionLUT(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float NdotV = texCoord.x;
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;

    const uint ArrayIndex = 0;

    float specular = saturate(0.05f + ArrayIndex / 32.f);
    float iorIncident = F0ToRefractiveIndex(SpecularParameterToF0(specular));
    float iorOutgoing = 1.f;
    return float4(GenerateSplitTermTrans(NdotV, roughness, iorIncident, iorOutgoing, sampleCount, 0, 1), 0, 0, 1);
}

