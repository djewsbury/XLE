// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if DEPTH_MOTION_NORMAL && GEO_HAS_TEXCOORD && GEO_HAS_NORMAL && (RES_HAS_NormalsTexture || RES_HAS_Texture1)
	// we need the tangent frame for normal map lookup
	#if !defined(VSOUT_HAS_TANGENT_FRAME)
		#define VSOUT_HAS_TANGENT_FRAME 1
	#endif
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
	#if !defined(VSOUT_HAS_NORMAL)
		#define VSOUT_HAS_NORMAL 1
	#endif
	#undef GEO_HAS_TEXBITANGENT
#else
	#undef GEO_HAS_TEXTANGENT
	#undef GEO_HAS_TEXBITANGENT
	#undef GEO_HAS_NORMAL
	#undef VSOUT_HAS_TANGENT_FRAME

	#if GEO_HAS_TEXCOORD && (MAT_ALPHA_TEST==1)
		#if !defined(VSOUT_HAS_TEXCOORD)
			#define VSOUT_HAS_TEXCOORD 1
		#endif
	#else
		#undef GEO_HAS_TEXCOORD
		#undef VSOUT_HAS_TEXCOORD
	#endif
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
#undef GEO_HAS_PER_VERTEX_AO
