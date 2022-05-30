// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

// settings for default (nopatch) material configuration
#if !defined(ENABLE_TANGENT_FRAME)
	#define ENABLE_TANGENT_FRAME (GEO_HAS_TEXCOORD && GEO_HAS_NORMAL && RES_HAS_NormalsTexture)
#endif

#if !defined(ENABLE_ALPHA_TEST)
	#define ENABLE_ALPHA_TEST (GEO_HAS_TEXCOORD && (MAT_ALPHA_TEST || MAT_ALPHA_TEST_PREDEPTH) && (RES_HAS_DiffuseTexture || RES_HAS_OpacityTexture))
#endif
