// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Framework/gbuffer.hlsl"
#include "../Math/TextureAlgorithm.hlsl" // (for SystemInputs)
#include "../../Forward/ForwardPlusLighting.hlsl"
#include "../../Objects/Templates.pixel.hlsl"

#if (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 frameworkEntry(VSOUT geo, SystemInputs sys) : SV_Target0
{
	GBufferValues sample = PerPixel(geo);

	float3 directionToEye = 0.0.xxx;
	#if VSOUT_HAS_WORLD_VIEW_VECTOR
		directionToEye = normalize(geo.worldViewVector);
	#endif

	#if VSOUT_HAS_NORMAL
		const bool hasNormal = true;
	#else
		const bool hasNormal = false;
	#endif
	float4 result = float4(
		CalculateIllumination(
			sample, directionToEye, VSOUT_GetWorldPosition(geo), VSOUT_GetWorldVertexNormal(geo),
			NDCDepthToLinear0To1(geo.position.z),
			LightScreenDest_Create(int2(geo.position.xy), GetSampleIndex(sys)), 
			hasNormal), 1.f);

	#if VSOUT_HAS_FOG_COLOR == 1
		result.rgb = geo.fogColor.rgb + result.rgb * geo.fogColor.a;
	#endif

	result.a = sample.blendingAlpha;
	return result;
}

#if (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 frameworkEntryCustomLighting(VSOUT geo, SystemInputs sys) : SV_Target0
{
	return PerPixelCustomLighting(geo);
}

float4 frameworkEntryWithEarlyRejection(VSOUT geo, SystemInputs sys) : SV_Target0
{
	if (EarlyRejectionTest(geo))
        discard;

	return frameworkEntry(geo, sys);
}
