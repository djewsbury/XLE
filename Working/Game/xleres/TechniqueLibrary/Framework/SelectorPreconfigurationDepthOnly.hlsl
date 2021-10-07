// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if GEO_HAS_TEXCOORD && (MAT_ALPHA_TEST==1)
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#else
	#undef GEO_HAS_TEXCOORD
#endif

#if GEO_HAS_COLOR && Vertex_Alpha
	#if !defined(VSOUT_HAS_VERTEX_ALPHA)
		#define VSOUT_HAS_VERTEX_ALPHA 1
	#endif
#else
	#undef VSOUT_HAS_VERTEX_ALPHA
#endif

#if (SPAWNED_INSTANCE==1)
	#define GEO_HAS_INSTANCE_ID 1
#endif

#undef GEO_HAS_TEXCOORD1
#undef GEO_HAS_COLOR
#undef GEO_HAS_COLOR1
#undef GEO_HAS_TEXTANGENT
#undef GEO_HAS_TEXBITANGENT
#undef GEO_HAS_NORMAL
#undef GEO_HAS_PER_VERTEX_AO
