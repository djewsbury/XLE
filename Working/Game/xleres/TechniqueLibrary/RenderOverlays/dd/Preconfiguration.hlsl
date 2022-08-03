
#include "../../Config/Preconfiguration.hlsl"

#if GEO_HAS_COLOR
	#if !defined(VSOUT_HAS_COLOR_LINEAR)
		#define VSOUT_HAS_COLOR_LINEAR 1
	#endif
	#if !defined(VSOUT_HAS_VERTEX_ALPHA)
		#define VSOUT_HAS_VERTEX_ALPHA 1
	#endif
#endif

#if GEO_HAS_COLOR1
	#if !defined(VSOUT_HAS_COLOR_LINEAR1)
		#define VSOUT_HAS_COLOR_LINEAR1 1
	#endif
#endif

#if GEO_HAS_TEXCOORD1
	#if !defined(VSOUT_HAS_TEXCOORD1)
		#define VSOUT_HAS_TEXCOORD1 1
	#endif
#endif

#if GEO_HAS_FONTTABLE
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
	#if !defined(VSOUT_HAS_FONTTABLE)
		#define VSOUT_HAS_FONTTABLE 1
	#endif
#endif