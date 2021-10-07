// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "util.hlsl"
#include "../illum.pixel.hlsl"

//#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
//	[earlydepthstencil]	// (this has a big effect, because otherwise UAV output wouldn't be occluded by depth buffer)
//#endif
float4 main_oi(VSOUT geo, SystemInputs sys) : SV_Target
{
		// Do depth rejection against the depth buffer duplicate early.
		// This prevents us having to do the lighting calculations (which might
		// be expensive) on hidden fragments.
		//
		// Note -- currently we're using a duplicate of the depth buffer here.
		//		but, maybe it would be better to unbind the depth buffer completely
		//		and just use the main depth buffer.
		//		Or, alternatively, keep the main depth buffer bound and disable
		//		writing, and use earlydepthstencil.
		//		But, if we do that we can't write depth information for opaque
		//		parts... that would only work well if we have a pre-depth
		//		pass for the fully opaque parts.

	float destinationDepth = DuplicateOfDepthBuffer[uint2(geo.position.xy)];
	float ndcComparison = geo.position.z; // / geo.position.w;
	if (ndcComparison > destinationDepth)
		discard;

	DoAlphaTest(geo, GetAlphaThreshold());

	GBufferValues sample = IllumShader_PerPixel(geo);

		// note --  At alpha threshold, we just consider
		//			it opaque. It's a useful optimisation
		//			that goes hand in hand with the pre-depth pass.
	const float minAlpha =   1.f / 255.f;
	const float maxAlpha = AlphaThreshold; // 254.f / 255.f;
	if (sample.blendingAlpha < minAlpha) {
		discard;
	}

	float4 result = LightSample(sample, geo, sys);

		//
		//	Note -- we have to do a manual depth occlusion step here
		//			otherwise we might write out sampples to OutputFragmentNode
		//			that are actually depth-occluded
		//
		//			[earlydepthstencil] will also do depth occlusion
		//			before the shader... But it also writes to the
		//			depth buffer for all pixels (including "discard"ed)
		//			pixels... so we can't use that here.
		//

	if (result.a >= maxAlpha) {
		// uint oldValue;
		// InterlockedExchange(FragmentIds[uint2(geo.position.xy)], ~0, oldValue);
		return float4(LightingScale * result.rgb, 1.f); // result.a);
	} else {
			//	Multiply in alpha (if we're not using
			//	a premultiplied texture)
		#if !MAT_PREMULTIPLIED_ALPHA
			result.rgb *= result.a;
		#endif

		OutputFragmentNode(uint2(geo.position.xy), result, ndcComparison);
		discard;
		return 0.0.xxxx;
	}
}

[earlydepthstencil]
float4 main_stochastic(VSOUT geo,
	#if (STOCHASTIC_TRANS_PRIMITIVEID==1)
		uint primitiveID : SV_PrimitiveID,
	#endif
	SystemInputs sys) : SV_Target
{
	float occlusion;
	uint type = CalculateStochasticPixelType(geo.position, occlusion);
	[branch] if (type > 0) {
		if (type == 2) return float4(0.0.xxx, 1); // discard;

		// Only need to calculate the "alpha" value for this step...
		return float4(0.0.xxx, IllumShader_PerPixel(geo).blendingAlpha);
	}

	GBufferValues sample = IllumShader_PerPixel(geo);
	float4 litValue = LightSample(sample, geo, sys);
	return float4((LightingScale * (1.f - occlusion) * litValue.a) * litValue.rgb, litValue.a);
}
