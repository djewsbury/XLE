// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Math/TextureAlgorithm.hlsl"		// for SystemInputs
#include "../../Forward/ResolveLitColor.hlsl"
#include "../../Objects/IllumShader/PerPixel.h"

#if !((VSOUT_HAS_TEXCOORD>=1) && (MAT_ALPHA_TEST==1)) && (VULKAN!=1)
	[earlydepthstencil]
#endif
GBufferEncoded deferred(VSOUT geo)
{
	DoAlphaTest(geo, GetAlphaThreshold());

	#if (VIS_ANIM_PARAM!=0) && (VSOUT_HAS_COLOR==1)
		{
			GBufferValues visResult = GBufferValues_Default();
			#if VIS_ANIM_PARAM==1
				visResult.diffuseAlbedo = geo.color.rrr;
			#elif VIS_ANIM_PARAM==2
				visResult.diffuseAlbedo = geo.color.ggg;
			#elif VIS_ANIM_PARAM==3
				visResult.diffuseAlbedo = geo.color.bbb;
			#elif VIS_ANIM_PARAM==4
				visResult.diffuseAlbedo = geo.color.aaa;
			#elif VIS_ANIM_PARAM==5
				visResult.diffuseAlbedo = geo.color.rgb;
			#endif
			return Encode(visResult);
		}
	#endif

	GBufferValues result = IllumShader_PerPixel(geo);
	return Encode(result);
}

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
		ResolveLitColor(
			sample, directionToEye, VSOUT_GetWorldPosition(geo),
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

#if (VULKAN!=1)
    [earlydepthstencil]
#endif
void depthonly() {}

#if !((VSOUT_HAS_TEXCOORD>=1) && (MAT_ALPHA_TEST==1)) && (VULKAN!=1)
	[earlydepthstencil]
#endif
DepthNormalVelocityEncoded depthNormalVelocity(VSOUT geo)
{
	DoAlphaTest(geo, GetAlphaThreshold());
	GBufferValues result = IllumShader_PerPixel(geo);

	float3 prevPos;
	#if (VSOUT_HAS_PREV_POSITION==1)
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthNormalVelocity(result, int2(prevPos.xy));
}
