// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define DEPTH_PLUS_HISTORY_ACCUMULATION 1

#include "../Framework/VSOUT.hlsl"
#include "../Math/TextureAlgorithm.hlsl"		// for SystemInputs
#include "../../Objects/IllumShader/PerPixel.h"

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
	[earlydepthstencil]
#endif
GBufferEncoded deferred(VSOUT geo)
{
	DoAlphaTest(geo, GetAlphaThreshold());

	#if (VIS_ANIM_PARAM!=0) && VSOUT_HAS_COLOR_LINEAR
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

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
    [earlydepthstencil] void depthonly() {}
#else
	void depthonly(VSOUT geo) 
	{
		DoAlphaTest(geo, GetAlphaThreshold());
	}
#endif

#if DEPTH_PLUS_HISTORY_ACCUMULATION
	Texture2D<float> DepthPrev BIND_NUMERIC_T0;
	Texture2D<float4> GBufferNormalPrev BIND_NUMERIC_T1;
#endif

// Based on approach from AMD SSR shaders
float CalculateEdgeStoppingNormalWeight(float3 normalToday, float3 normalYesterday)
{
	const float sigma = 4.0;
	return pow(max(dot(normalToday, normalYesterday), 0.0), sigma);
}

float CalculateEdgeStoppingRoughnessWeight(float roughnessToday, float roughnessYesterday)
{
	const float sigmaMin = 0.001f;
	const float sigmaMax = 0.01f;
	return 1.0 - smoothstep(sigmaMin, sigmaMax, abs(roughnessToday - roughnessYesterday));
}

float CalculateEdgeStoppingDepthWeight(float depthToday, float depthYesterday)
{
	float ratio = min(depthToday, depthYesterday) / max(depthToday, depthYesterday);
	if (ratio > 0.9) return smoothstep(0, 1, (ratio-0.9)/0.1);
	return 0;
}

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
	[earlydepthstencil]
#endif
DepthPlusEncoded depthPlus(VSOUT geo)
{
	DoAlphaTest(geo, GetAlphaThreshold());
	GBufferValues sample = IllumShader_PerPixel(geo);

	float3 prevPos;
	float historyAccumulationWeight = 1;
	#if VSOUT_HAS_PREV_POSITION
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.x = prevPos.x * 0.5 + 0.5;
		prevPos.y = prevPos.y * 0.5 + 0.5;
		prevPos.xy = SysUniform_GetViewportMinXY() + prevPos.xy * SysUniform_GetViewportWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);

		#if DEPTH_PLUS_HISTORY_ACCUMULATION
			int2 loadPos = int2(geo.position.xy+prevPos.xy);
			if (all(loadPos > 0) && loadPos.x < 1920 && loadPos.y < 1080) {
				float depthPrev = DepthPrev.Load(int3(loadPos, 0));
				float4 normalSamplePrev = GBufferNormalPrev.Load(int3(loadPos, 0));
				float3 normalPrev = DecodeGBufferNormal(normalSamplePrev.xyz);
				float roughnessPrev = normalSamplePrev.w;
				// Note that historyAccumulationWeight will be reduced a little bit just because of the normal compression method
				historyAccumulationWeight
					= CalculateEdgeStoppingNormalWeight(sample.worldSpaceNormal, normalPrev)
					* CalculateEdgeStoppingRoughnessWeight(sample.material.roughness, roughnessPrev)
					* CalculateEdgeStoppingDepthWeight(geo.prevPosition.z / geo.prevPosition.w, depthPrev)
					;

				if (depthPrev == 0) historyAccumulationWeight = 0;
			} else {
				historyAccumulationWeight = 0;
			}
		#endif
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthPlus(sample, int2(prevPos.xy), historyAccumulationWeight);
}

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
	[earlydepthstencil]
#endif
float4 flatColor(VSOUT geo) : SV_Target0
{
	DoAlphaTest(geo, GetAlphaThreshold());
	return VSOUT_GetColor0(geo);
}

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
	[earlydepthstencil]
#endif
float4 copyDiffuseAlbedo(VSOUT geo) : SV_Target0
{
	DoAlphaTest(geo, GetAlphaThreshold());
	GBufferValues sample = IllumShader_PerPixel(geo);
	return float4(sample.diffuseAlbedo, sample.blendingAlpha);
}
