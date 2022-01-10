// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(VSIN_H)
#define VSIN_H

#if !defined(GEO_HAS_POSITION) && !defined(GEO_HAS_PIXELPOSITION) && !GEO_HAS_VERTEX_ID		// "vertex generator" shaders will set GEO_HAS_VERTEX_ID, but have no positions 
	#define GEO_HAS_POSITION 1
#endif

struct VSIN //////////////////////////////////////////////////////
{
	#if GEO_HAS_POSITION
		float3 position : POSITION;
	#endif

	#if GEO_HAS_PIXELPOSITION
		float2 pixelposition : PIXELPOSITION;
	#endif

	#if GEO_HAS_COLOR
		float4 color : COLOR;
	#endif

	#if GEO_HAS_COLOR1
		float4 color1 : COLOR1;
	#endif

	#if GEO_HAS_TEXCOORD
		float2 texCoord : TEXCOORD;
	#endif

	#if GEO_HAS_TEXCOORD1
		float2 texCoord1 : TEXCOORD1;
	#endif

	#if GEO_HAS_TEXTANGENT
		float4 tangent : TEXTANGENT;
	#endif

	#if GEO_HAS_TEXBITANGENT
		float4 bitangent : TEXBITANGENT;
	#endif

	#if GEO_HAS_NORMAL
		float3 normal : NORMAL;
	#endif

	#if GEO_HAS_BONEWEIGHTS
		uint4 boneIndices : BONEINDICES;
		float4 boneWeights : BONEWEIGHTS;
	#endif

	#if GEO_HAS_PARTICLE_INPUTS
		float4 texCoordScale : TEXCOORDSCALE;
		float4 screenRot : PARTICLEROTATION;
		float4 blendTexCoord : TEXCOORD1;
	#endif

	#if GEO_HAS_VERTEX_ID
		uint vertexId : SV_VertexID;
	#endif
	
	#if GEO_HAS_INSTANCE_ID
		uint instanceId : SV_InstanceID;
	#endif

	#if GEO_HAS_PER_VERTEX_AO
		float ambientOcclusion : PER_VERTEX_AO;
	#endif
}; //////////////////////////////////////////////////////////////////

#if GEO_HAS_COLOR ////////////////////////////////////////////////
	float4 VSIN_GetColor0(VSIN input) { return input.color; }
#else
	float4 VSIN_GetColor0(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_COLOR1 ///////////////////////////////////////////////
	float4 VSIN_GetColor1(VSIN input) { return input.color1; }
#else
	float4 VSIN_GetColor1(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD /////////////////////////////////////////////
	float2 VSIN_GetTexCoord0(VSIN input) { return input.texCoord; }
#else
	float2 VSIN_GetTexCoord0(VSIN input) { return 0.0.xx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD1 ////////////////////////////////////////////
	float2 VSIN_GetTexCoord1(VSIN input) { return input.texCoord1; }
#else
	float2 VSIN_GetTexCoord1(VSIN input) { return 0.0.xx; }
#endif //////////////////////////////////////////////////////////////

float3 VSIN_GetLocalPosition(VSIN input)
{
	#if GEO_HAS_POSITION
		return input.position.xyz;
	#else
		return 0.0.xxx;
	#endif
}

float3 VSIN_GetEncodedNormal(VSIN input)
{
	#if GEO_HAS_NORMAL
		return input.normal;
	#else
		return float3(0,0,1);
	#endif
}

float4 VSIN_GetEncodedTangent(VSIN input)
{
	#if GEO_HAS_TEXTANGENT
		return input.tangent.xyzw;
	#else
		return float4(1,0,0,0);
	#endif
}

float4 VSIN_GetEncodedBitangent(VSIN input)
{
	#if GEO_HAS_TEXBITANGENT
		return input.bitangent.xyzw;
	#else
		return float4(0,1,0,0);
	#endif
}

#endif
