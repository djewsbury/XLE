// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(RESOLVE_AMBIENT_PSH)
#define RESOLVE_AMBIENT_PSH

#if (RESOLVE_RANGE_FOG==1)
	#include "ResolveUtil.hlsl"
#endif
#include "../TechniqueLibrary/SceneEngine/Lighting/AmbientResolve.hlsl"
#include "../TechniqueLibrary/SceneEngine/Lighting/RangeFogResolve.hlsl"
#include "../TechniqueLibrary/Utility/LoadGBuffer.hlsl"
#include "../TechniqueLibrary/Utility/Colour.hlsl"	// for LightingScale
#include "../TechniqueLibrary/Framework/Binding.hlsl"

cbuffer AmbientLightBuffer BIND_MAT_B1
{
	AmbientDesc Ambient;
	RangeFogDesc RangeFog;
	float2 ReciprocalViewportDims;
}

[earlydepthstencil]
float4 ResolveAmbient(
	float4 position : SV_Position,
	float2 texCoord : TEXCOORD0,
	float3 viewFrustumVector : VIEWFRUSTUMVECTOR,
	SystemInputs sys) : SV_Target0
{
	int2 pixelCoords = position.xy;
	GBufferValues sample = LoadGBuffer(position.xy, sys);
	float3 viewDirection = -normalize(viewFrustumVector);

	AmbientResolveHelpers helpers = AmbientResolveHelpers_Default();
	helpers.ReciprocalViewportDims = ReciprocalViewportDims;

	LightScreenDest screenDest = LightScreenDest_Create(pixelCoords, GetSampleIndex(sys));
	float3 result = LightResolve_Ambient(
		sample, viewDirection, Ambient,
		screenDest, helpers);

	float outscatterScale = 1.f;
	#if (RESOLVE_RANGE_FOG==1)
		float screenSpaceDepth = LoadWorldSpaceDepth(position, GetSampleIndex(sys));

		float3 inscatter;
		LightResolve_RangeFog(RangeFog, screenSpaceDepth, outscatterScale, inscatter);
		result = result * outscatterScale + inscatter;
	#endif

	return float4(LightingScale * result, outscatterScale);
}

#endif
