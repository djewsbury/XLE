// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_UNSHADOWED_PSH)
#define RESOLVE_UNSHADOWED_PSH

#include "standardlighttypes.hlsl"
#include "resolveutil.hlsl"
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
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{	
	GBufferValues sample = LoadGBuffer(position.xy, sys);
    ResolvePixelProperties resolvePixel = ResolvePixelProperties_Create(position, viewFrustumVector, sys);

    LightSampleExtra sampleExtra;
    sampleExtra.screenSpaceOcclusion = 1.f;
	#if HAS_SCREENSPACE_AO==1
        sampleExtra.screenSpaceOcclusion = LoadFloat1(AmbientOcclusion, position.xy, GetSampleIndex(sys));
    #endif

    float3 result = ResolveLight(
        sample, sampleExtra, Light, resolvePixel.worldPosition,
        normalize(-viewFrustumVector), resolvePixel.screenDest);

    // Also calculate the shadowing -- (though we could skip it if the lighting is too dim here)
    CascadeAddress cascade = ResolveShadowsCascade(resolvePixel.worldPosition, sample.worldSpaceNormal, texCoord, resolvePixel.worldSpaceDepth);
    float shadow = ResolveShadows(cascade, resolvePixel.screenDest);

    // return float4(cascade.frustumCoordinates.xyz, 1.0f);
    return float4(shadow.xxx, 1.f);
	return float4((LightingScale*shadow)*result, 1.f);
}

#endif
