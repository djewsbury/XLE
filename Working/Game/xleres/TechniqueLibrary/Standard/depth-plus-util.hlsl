// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/gbuffer.hlsl"
#include "../Framework/Binding.hlsl"

#if !defined(VSOUT_H)
	#error Include VSOUT.hlsl before including this file
#endif

#if !defined(SYSTEM_UNIFORMS_H)
	#error Include SystemUniforms.hlsl before including this file
#endif

#if DEPTH_PLUS_HISTORY_ACCUMULATION
	Texture2D<float> DepthPrev BIND_SEQ_T6;
	Texture2D<float4> GBufferNormalPrev BIND_SEQ_T7;
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

DepthPlusEncoded ResolveDepthPlus(VSOUT geo, GBufferValues sample)
{
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
			// Note that we consider the texture size here, not the viewport
			int2 depthPrevDims;
			DepthPrev.GetDimensions(depthPrevDims.x, depthPrevDims.y);
			if (all(loadPos > 0 && loadPos < depthPrevDims)) {
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