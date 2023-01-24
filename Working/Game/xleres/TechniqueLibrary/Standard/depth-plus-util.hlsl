// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/gbuffer.hlsl"
#include "../Framework/Binding.hlsl"
#include "../LightingEngine/history-confidence.hlsl"

#if !defined(VSOUT_H)
	#error Include VSOUT.hlsl before including this file
#endif

#if !defined(SYSTEM_UNIFORMS_H)
	#error Include SystemUniforms.hlsl before including this file
#endif

#if DEPTH_PLUS_HISTORY_ACCUMULATION
	Texture2D<float> DepthPrev BIND_SEQ_T6;
	Texture2D<float4> GBufferNormalPrev BIND_SEQ_T7;

	float4 LoadNormalAndRoughnessPrev(uint2 loadPos)
	{
		float4 yesterdayNormalSample = GBufferNormalPrev.Load(int3(loadPos, 0));
		float3 yesterdayNormal = DecodeGBufferNormal(yesterdayNormalSample.xyz);
		float yesterdayRoughness = yesterdayNormalSample.w;
		return float4(yesterdayNormal, yesterdayRoughness);
	}

	float LoadDepthPrev(uint2 loadPos)
	{
		return DepthPrev.Load(int3(loadPos, 0));
	}
#endif

DepthPlusEncoded ResolveDepthPlus(VSOUT geo, GBufferValues sample)
{
	float3 prevPos;
	float historyAccumulationWeight = 1;
	#if VSOUT_HAS_PREV_POSITION
		prevPos = geo.prevPosition.xyz / geo.prevPosition.w;
		prevPos.xy = SysUniform_GetViewportCenter() + prevPos.xy * SysUniform_GetViewportHalfWidthHeight();
		prevPos.xyz -= geo.position.xyz;
		prevPos.xy = clamp(round(prevPos.xy), -127, 127);

		#if DEPTH_PLUS_HISTORY_ACCUMULATION
			int2 depthPrevDims;
			DepthPrev.GetDimensions(depthPrevDims.x, depthPrevDims.y);
			historyAccumulationWeight = CalculatePixelHistoryConfidence(
				geo.position.xy, prevPos.xy,
				sample.worldSpaceNormal, sample.material.roughness, geo.prevPosition.z / geo.prevPosition.w,
				depthPrevDims);
		#endif
	#else
		prevPos = 0.0.xxx;
	#endif
	return EncodeDepthPlus(sample, int2(prevPos.xy), historyAccumulationWeight);
}