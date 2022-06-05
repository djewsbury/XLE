// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_UNSHADOWED_PSH)
#define RESOLVE_UNSHADOWED_PSH

#include "standardlighttypes.hlsl"
#include "operator-util.hlsl"
#include "../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl" // for LightingScale
#include "../TechniqueLibrary/Framework/Binding.hlsl"

#if HAS_SCREENSPACE_AO==1
    Texture2D<float>	AmbientOcclusion : register(t5);
#endif

cbuffer LightBuffer BIND_SEQ_B1
{
	LightDesc Light;
}

[earlydepthstencil]
float4 main(
    float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	noperspective float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{
	GBufferValues sample = LoadGBuffer(position.xy, sys);
    LightOperatorInputs inputs = LightOperatorInputs_Create(position, viewFrustumVector, sys);

    if (inputs.ndcDepth == 0) discard;      // max distance in ReverseZ modes. Discard early to skip sky pixels

    LightSampleExtra sampleExtra;
    sampleExtra.screenSpaceOcclusion = 1.f;
	#if HAS_SCREENSPACE_AO==1
        sampleExtra.screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, position.xy, GetSampleIndex(sys));
    #endif

    float3 result = ResolveLight(
        sample, sampleExtra, Light, inputs.worldPosition,
        normalize(-viewFrustumVector), inputs.screenDest);

    // Also calculate the shadowing -- (though we could skip it if the lighting is too dim here)
    float shadow = ResolveShadows(Light, inputs.worldPosition, sample.worldSpaceNormal, texCoord, inputs.worldSpaceDepth, inputs.screenDest);
    
    // return float4(cascade.frustumCoordinates.xyz, 1.0f);
    // return float4(shadow.xxx, 1.f);
	return float4((LightingScale*shadow)*result, 1.f);
}

#endif
