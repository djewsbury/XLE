// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PreconfigurationShared.hlsl"

#if GEO_HAS_COLOR
	#if !defined(VSOUT_HAS_COLOR_LINEAR)
		#define VSOUT_HAS_COLOR_LINEAR 1
	#endif
#endif

#if GEO_HAS_TEXCOORD
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#endif

#if GEO_HAS_NORMAL
	#if !defined(VSOUT_HAS_NORMAL)
		#define VSOUT_HAS_NORMAL 1
	#endif
#endif

#if GEO_HAS_TEXTANGENT || GEO_HAS_TEXBITANGENT
	#if ENABLE_TANGENT_FRAME
		#if defined(TANGENT_PROCESS_IN_PS) && TANGENT_PROCESS_IN_PS==1
			#if !defined(VSOUT_HAS_LOCAL_TANGENT_FRAME)
				#define VSOUT_HAS_LOCAL_TANGENT_FRAME 1
			#endif
		#else
			#if !defined(VSOUT_HAS_TANGENT_FRAME)
				#define VSOUT_HAS_TANGENT_FRAME 1
			#endif
		#endif
	#else
		#undef GEO_HAS_TEXTANGENT
		#undef GEO_HAS_TEXBITANGENT
	#endif
#endif

#if (GEO_HAS_NORMAL || GEO_HAS_TEXTANGENT) && (AUTO_COTANGENT==1)
	#undef VSOUT_HAS_TANGENT_FRAME
	#undef VSOUT_HAS_LOCAL_TANGENT_FRAME

		// Can do this in either local or world space -- set VSOUT_HAS_LOCAL_NORMAL & VSOUT_HAS_LOCAL_VIEW_VECTOR for normal space
	#define VSOUT_HAS_NORMAL 1
	#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
#endif

// disable ENABLE_... defines used only within the Preconfiguration headers
#undef ENABLE_TANGENT_FRAME
#undef ENABLE_ALPHA_TEST
