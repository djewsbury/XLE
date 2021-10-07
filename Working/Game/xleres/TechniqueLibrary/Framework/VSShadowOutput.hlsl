// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(VS_SHADOW_OUTPUT_HLSL)
#define VS_SHADOW_OUTPUT_HLSL

struct VSShadowOutput /////////////////////////////////////////////////////
{
	float4 position : SV_Position;

	#if VSOUT_HAS_VERTEX_ALPHA
		float alpha : ALPHA0;
	#endif
	
	#if VSOUT_HAS_TEXCOORD
		float2 texCoord : TEXCOORD0;
	#endif

	#if (VSOUT_HAS_SHADOW_PROJECTION_COUNT>0)
		uint shadowFrustumFlags : SHADOWFLAGS;
	#endif
}; //////////////////////////////////////////////////////////////////

#endif
