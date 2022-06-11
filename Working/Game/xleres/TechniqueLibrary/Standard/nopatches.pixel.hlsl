// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/VSOUT.hlsl"
#include "../Framework/SystemUniforms.hlsl"
#include "depth-plus-util.hlsl"
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

#if !(VSOUT_HAS_TEXCOORD && (MAT_ALPHA_TEST==1))
	[earlydepthstencil]
#endif
DepthPlusEncoded depthPlus(VSOUT geo)
{
	DoAlphaTest(geo, GetAlphaThreshold());
	GBufferValues sample = IllumShader_PerPixel(geo);
	return ResolveDepthPlus(geo, sample);
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
