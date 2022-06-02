// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Config/PreconfigurationShared.hlsl"

#undef VSOUT_HAS_COLOR_LINEAR
#undef VSOUT_HAS_LOCAL_TANGENT_FRAME
#undef VSOUT_HAS_TANGENT_FRAME
#undef VSOUT_HAS_NORMAL
#undef VSOUT_HAS_RENDER_TARGET_INDEX

#define VSOUT_HAS_COLOR_LINEAR 0
#define VSOUT_HAS_LOCAL_TANGENT_FRAME 0
#define VSOUT_HAS_TANGENT_FRAME 0
#define VSOUT_HAS_NORMAL 0
#if ENABLE_ALPHA_TEST && MAT_ALPHA_DITHER_SHADOWS
	#undef GEO_HAS_TEXCOORD
	#define GEO_HAS_TEXCOORD 0
	#undef VSOUT_HAS_TEXCOORD
	#define VSOUT_HAS_TEXCOORD 0
#endif
#define VSOUT_HAS_RENDER_TARGET_INDEX 1

//#if GEO_HAS_TEXCOORD && (MAT_ALPHA_DITHER_SHADOWS==1)
//	#define VSOUT_HAS_PRIMITIVE_ID 1
//#endif

#if !defined(VSOUT_HAS_SHADOW_PROJECTION_COUNT)
	#define VSOUT_HAS_SHADOW_PROJECTION_COUNT 6
#endif

// Normal is only required when wind animation is enabled
// So disable when we can
#if !((MAT_VCOLOR_IS_ANIM_PARAM!=0) && (GEO_HAS_COLOR!=0))
	#undef GEO_HAS_NORMAL
	#define GEO_HAS_NORMAL 0
#endif

#define SHADOW_GEN_SHADER 1
