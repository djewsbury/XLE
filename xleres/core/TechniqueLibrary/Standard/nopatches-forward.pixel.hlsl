// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Math/TextureAlgorithm.hlsl"		// for SystemInputs
#include "../../Objects/default.pixel.hlsl"

#include "../../Forward/ForwardPlusLighting.hlsl"       // this variable is in a separate shader file because this include brings in so much stuff

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1)) && (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 forward(VSOUT geo, SystemInputs sys) : SV_Target0
{
	DoAlphaTest(geo, GetAlphaThreshold());

	GBufferValues sample = IllumShader_PerPixel(geo);

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

