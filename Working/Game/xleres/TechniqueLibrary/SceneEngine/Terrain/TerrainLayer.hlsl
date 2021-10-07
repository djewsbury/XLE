// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(TERRAIN_LAYER_H)
#define TERRAIN_LAYER_H

struct TerrainLayerVSOutput /////////////////////////////////////////
{
	float4 position 	: SV_Position;
	float4 color 		: COLOR0;
	float2 texCoord 	: TEXCOORD0;
	float2 baseTexCoord : TEXCOORD1;
	float3 normal		: NORMAL;

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR
        float3 localViewVector 	: LOCALVIEWVECTOR;
    #endif

    #if VSOUT_HAS_WORLD_VIEW_VECTOR
        float3 worldViewVector 	: WORLDVIEWVECTOR;
    #endif
	
	#if VSOUT_HAS_TANGENT_FRAME
		float3 tangent : TEXTANGENT;
		float3 bitangent : TEXBITANGENT;
	#endif

    #if VSOUT_HAS_FOG_COLOR
        float4 fogColor : FOGCOLOR;
    #endif
}; //////////////////////////////////////////////////////////////////

#endif

