// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LEGACY_SURFACE_H)
#define LEGACY_SURFACE_H

#include "VSOUT.hlsl"
#include "../Math/SurfaceAlgorithm.hlsl"
#include "../Framework/Binding.hlsl"

Texture2D			DiffuseTexture          BIND_MAT_T3;
Texture2D			NormalsTexture          BIND_MAT_T4;
Texture2D<float>	OpacityTexture          BIND_MAT_T7;

float3 SampleDefaultNormalMap(VSOUT geo)
{
	#if defined(RES_HAS_NormalsTexture_DXT)
		bool dxtNormalMap = RES_HAS_NormalsTexture_DXT;
	#else
		bool dxtNormalMap = false;
	#endif

	return SampleNormalMap(NormalsTexture, DefaultSampler, dxtNormalMap, VSOUT_GetTexCoord0(geo));
}

void DoAlphaTest(VSOUT geo, float alphaThreshold)
{
	#if VSOUT_HAS_TEXCOORD && ((MAT_ALPHA_TEST==1)||(MAT_ALPHA_TEST_PREDEPTH==1))
		#if RES_HAS_OpacityTexture
			#if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
				if (OpacityTexture.Sample(ClampingSampler, geo.texCoord).r < alphaThreshold)
					discard;
			#else
				if (OpacityTexture.Sample(MaybeAnisotropicSampler, geo.texCoord).r < alphaThreshold)
					discard;
			#endif
		#else
			#if (USE_CLAMPING_SAMPLER_FOR_DIFFUSE==1)
				if (DiffuseTexture.Sample(ClampingSampler, geo.texCoord).a < alphaThreshold)
					discard;
			#else
				if (DiffuseTexture.Sample(MaybeAnisotropicSampler, geo.texCoord).a < alphaThreshold)
					discard;
			#endif
		#endif
	#endif
}

float3 VSOUT_GetNormal(VSOUT geo)
{
	#if RES_HAS_NormalsTexture && VSOUT_HAS_TEXCOORD
		return TransformTangentSpaceToWorld(SampleDefaultNormalMap(geo), geo);
	#elif VSOUT_HAS_NORMAL
		return normalize(geo.normal);
	#elif VSOUT_HAS_LOCAL_TANGENT_FRAME
		return normalize(mul(GetLocalToWorldUniformScale(), VSOUT_GetLocalTangentFrame(geo).normal));
	#else
		return 0.0.xxx;
	#endif
}

#endif
