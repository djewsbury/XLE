// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if GEO_HAS_COLOR
		// vertex is used only in the vertex shader when
		// "MAT_VCOLOR_IS_ANIM_PARAM" is set. So, in this case,
		// don't output to further pipeline stages.
	#if MAT_VCOLOR_IS_ANIM_PARAM!=1 || VIS_ANIM_PARAM!=0
		#if !defined(VSOUT_HAS_COLOR_LINEAR)
			#define VSOUT_HAS_COLOR_LINEAR 1
		#endif
	#endif
	#if GEO_HAS_TEXCOORD1 && GEO_HAS_PIXELPOSITION
		#define VSOUT_HAS_TEXCOORD1 1
	#endif
#endif

#if GEO_HAS_COLOR1 && GEO_HAS_PIXELPOSITION
	#define VSOUT_HAS_COLOR_LINEAR1 1
#endif

#if GEO_HAS_TEXCOORD
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#endif

#if GEO_HAS_TEXCOORD1 && GEO_HAS_PIXELPOSITION
	#define VSOUT_HAS_TEXCOORD1 1
#endif

#if GEO_HAS_TEXTANGENT || GEO_HAS_TEXBITANGENT
	#if RES_HAS_NormalsTexture
		#if defined(TANGENT_PROCESS_IN_PS) && TANGENT_PROCESS_IN_PS==1
			#if !defined(VSOUT_HAS_LOCAL_TANGENT_FRAME)
				#define VSOUT_HAS_LOCAL_TANGENT_FRAME 1
			#endif
		#else
			#if !defined(VSOUT_HAS_TANGENT_FRAME)
				#define VSOUT_HAS_TANGENT_FRAME 1
			#endif
		#endif
	#endif
#endif

#if GEO_HAS_NORMAL
	#if !defined(VSOUT_HAS_NORMAL)
		#define VSOUT_HAS_NORMAL 1
	#endif
#endif

#if GEO_HAS_PARTICLE_INPUTS
	#define VSOUT_HAS_BLEND_TEXCOORD 1
#endif

#if GEO_HAS_PER_VERTEX_AO
	#if !defined(VSOUT_HAS_PER_VERTEX_AO)
		#define VSOUT_HAS_PER_VERTEX_AO 1
	#endif
#endif

#if (MAT_DO_PARTICLE_LIGHTING==1) && GEO_HAS_TEXCOORD && RES_HAS_NormalsTexture
	#undef VSOUT_HAS_TANGENT_FRAME
	#define VSOUT_HAS_TANGENT_FRAME 1

	#if RES_HAS_CUSTOM_MAP
		#undef VSOUT_HAS_WORLD_VIEW_VECTOR
		#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
	#endif
#endif

#if (GEO_HAS_NORMAL || GEO_HAS_TEXTANGENT) && (AUTO_COTANGENT==1)
	#undef VSOUT_HAS_TANGENT_FRAME
	#undef VSOUT_HAS_LOCAL_TANGENT_FRAME

		// Can do this in either local or world space -- set VSOUT_HAS_LOCAL_NORMAL & VSOUT_HAS_LOCAL_VIEW_VECTOR for normal space
	#define VSOUT_HAS_NORMAL 1
	#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
#endif

#if MAT_REFLECTIVENESS
	#define VSOUT_HAS_WORLD_VIEW_VECTOR 1       // (need world view vector for the fresnel calculation)
#endif

#if MAT_BLEND_FOG
	#define VSOUT_HAS_FOG_COLOR 1
#endif

#if GEO_HAS_PARTICLE_INPUTS
	#define VSOUT_HAS_BLEND_TEXCOORD 1
#endif

#if (SPAWNED_INSTANCE==1)
	#define GEO_HAS_INSTANCE_ID 1
	#if !defined(VSOUT_HAS_SHADOW_PROJECTION_COUNT)
		#define PER_INSTANCE_MLO 1
	#endif
	#if (PER_INSTANCE_MLO==1)
		#define VSOUT_HAS_PER_VERTEX_MLO 1
	#endif
#endif
