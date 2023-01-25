
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

float3 IBLPrecalc_SampleInputTexture(float3 direction) { return 0; }
#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

RWTexture2D<float4> Output : register(u1, space0);

[[vk::push_constant]] struct ControlUniformsStruct
{
    uint4 A;
} ControlUniforms;
uint ControlUniforms_GetMipIndex() { return ControlUniforms.A.x; }
uint ControlUniforms_GetGroupIdOffset() { return ControlUniforms.A.y; }

struct PixelBalancingShaderHelper
{
    uint3 _outputPixel;
    uint _outputMip;
};

PixelBalancingShaderHelper PixelBalancingShaderCalculate(uint3 groupId, uint3 outputDims)
{
    PixelBalancingShaderHelper result;
    uint linearPixel = groupId.x + ControlUniforms_GetGroupIdOffset();
    result._outputPixel = uint3(
        linearPixel%outputDims.x, 
        (linearPixel/outputDims.x)%outputDims.y, 
        linearPixel/(outputDims.x*outputDims.y));
    result._outputMip = ControlUniforms_GetMipIndex();
    return result;
}

groupshared float2 GenerateSplitSumGlossLUT_SharedWorking[64];
[numthreads(64, 1, 1)]
    void GenerateSplitSumGlossLUT(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    uint2 textureDims;
	Output.GetDimensions(textureDims.x, textureDims.y);

    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupId, uint3(textureDims, 1));
    if (any(helper._outputPixel >= uint3(textureDims, 1))) return;

    float2 texCoord = helper._outputPixel.xy / float2(textureDims);
    float NdotV = max(texCoord.x, .1/float(textureDims.x));  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;

    // our work for this pixel is spread over the entire thread group
    // single pass with 24k samples (for 512x512 texture) should succeed on embedded ryzen 7 processor, but above that will timeout
    const uint sampleCount = 6 * 64 * 1024;
    // const uint sampleCount = 24 * 1024;
    const uint groupSize = 64;
    const uint samplePerThread = sampleCount / groupSize;
    GenerateSplitSumGlossLUT_SharedWorking[groupThreadId.x] = GenerateSplitTerm(NdotV, roughness, samplePerThread, groupThreadId.x, groupSize);
    AllMemoryBarrierWithGroupSync();
    if (groupThreadId.x == 0) {
        float2 result = 0;
        for (uint c=0; c<groupSize; ++c)
            result += GenerateSplitSumGlossLUT_SharedWorking[c];
        Output[helper._outputPixel.xy] = float4(result, 0, 1);
    }
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

