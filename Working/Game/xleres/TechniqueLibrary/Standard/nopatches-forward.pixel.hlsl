// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Math/TextureAlgorithm.hlsl"		// for SystemInputs
#include "../../Objects/IllumShader/PerPixel.h"

#include "../../Forward/ForwardPlusLighting.hlsl"       // this variable is in a separate shader file because this include brings in so much stuff

#if !((VSOUT_HAS_TEXCOORD>=1) && (MAT_ALPHA_TEST==1)) && (VULKAN!=1)
	[earlydepthstencil]
#endif
float4 forward(VSOUT geo, SystemInputs sys) : SV_Target0
{
	DoAlphaTest(geo, GetAlphaThreshold());

	GBufferValues sample = IllumShader_PerPixel(geo);

	float3 directionToEye = 0.0.xxx;
	#if (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
		directionToEye = normalize(geo.worldViewVector);
	#endif

	float4 result = float4(
		CalculateIllumination(
			sample, directionToEye, VSOUT_GetWorldPosition(geo),
			NDCDepthToLinear0To1(geo.position.z),
			LightScreenDest_Create(int2(geo.position.xy), GetSampleIndex(sys))), 1.f);

	#if VSOUT_HAS_FOG_COLOR == 1
		result.rgb = geo.fogColor.rgb + result.rgb * geo.fogColor.a;
	#endif

	result.a = sample.blendingAlpha;

	#if (VSOUT_HAS_COLOR>=1) && (MAT_VCOLOR_IS_ANIM_PARAM==0)
		result.rgb *= geo.color.rgb;
	#endif

	#if MAT_SKIP_LIGHTING_SCALE==0
		result.rgb *= LightingScale;		// (note -- should we scale by this here? when using this shader with a basic lighting pipeline [eg, for material preview], the scale is unwanted)
	#endif
	return result;
}

