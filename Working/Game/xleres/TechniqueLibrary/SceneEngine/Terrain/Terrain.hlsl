// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(TERRAIN_H)
#define TERRAIN_H

cbuffer TerrainTile
{
	float4 TerrainBaseMapping;
	row_major float4x4 TerrainLayerMapping;
	row_major float4x4 TerrainLayerMappingDistortion;
	float4 TerrainLayerMapping_Params;
}

#if RES_HAS_NormalsTexture
	#undef VSOUT_HAS_TANGENT_FRAME
	#define VSOUT_HAS_TANGENT_FRAME 1
	#define VSOUT_HAS_NORMAL 1
#endif

#endif
