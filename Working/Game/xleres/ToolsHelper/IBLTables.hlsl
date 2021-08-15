
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"

float3 IBLPrecalc_SampleInputTexture(float3 direction) { return 0; }
#include "../TechniqueLibrary/SceneEngine/Lighting/IBL/IBLPrecalc.hlsl"

RWTexture2D<float4> Output : register(u1, space0);

[numthreads(8, 8, 1)]
    void GenerateSplitSumGlossLUT(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 textureDims;
	Output.GetDimensions(textureDims.x, textureDims.y);
    float2 texCoord = dispatchThreadId.xy / float2(textureDims);
    float NdotV = max(texCoord.x, .1/float(textureDims.x));  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;
        
    const uint sampleCount = 64 * 1024;
    Output[dispatchThreadId.xy] = float4(0,0,0,1);
    for (uint p=0; p<6; ++p) {
        Output[dispatchThreadId.xy].rg += GenerateSplitTerm(NdotV, roughness, sampleCount, p, 6);
        AllMemoryBarrierWithGroupSync();
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

