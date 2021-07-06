// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(VSIN_H)
#define VSIN_H

#include "../Math/SurfaceAlgorithm.hlsl"

#if !defined(GEO_HAS_POSITION) && !defined(GEO_HAS_PIXELPOSITION)
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

	#if GEO_HAS_COLOR>=1
		float4 color : COLOR;
	#endif

	#if GEO_HAS_TEXCOORD>=1
		float2 texCoord : TEXCOORD;
	#endif

	#if GEO_HAS_TEXTANGENT==1
		float4 tangent : TEXTANGENT;
	#endif

	#if GEO_HAS_TEXBITANGENT==1
		float3 bitangent : TEXBITANGENT;
	#endif

	#if GEO_HAS_NORMAL==1
		float3 normal : NORMAL;
	#endif

	#if GEO_HAS_BONEWEIGHTS==1
		uint4 boneIndices : BONEINDICES;
		float4 boneWeights : BONEWEIGHTS;
	#endif

	#if GEO_HAS_PARTICLE_INPUTS
		float4 texCoordScale : TEXCOORDSCALE;
		float4 screenRot : PARTICLEROTATION;
		float4 blendTexCoord : TEXCOORD1;
	#endif

	#if GEO_HAS_VERTEX_ID==1
		uint vertexId : SV_VertexID;
	#endif
	
	#if GEO_HAS_INSTANCE_ID==1
		uint instanceId : SV_InstanceID;
	#endif

	#if GEO_HAS_PER_VERTEX_AO
		float ambientOcclusion : PER_VERTEX_AO;
	#endif
}; //////////////////////////////////////////////////////////////////

#if GEO_HAS_COLOR>=1 ///////////////////////////////////////////////
	float4 VSIN_GetColor0(VSIN input) { return input.color; }
#else
	float4 VSIN_GetColor0(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD>=1 /////////////////////////////////////////////
	float2 VSIN_GetTexCoord0(VSIN input) { return input.texCoord; }
#else
	float2 VSIN_GetTexCoord0(VSIN input) { return 0.0.xx; }
#endif //////////////////////////////////////////////////////////////

float3 VSIN_GetLocalPosition(VSIN input)
{
	#if GEO_HAS_POSITION
		return input.position.xyz;
	#else
		return 0.0.xxx;
	#endif
}

float4 VSIN_GetLocalTangent(VSIN input)
{
	#if (GEO_HAS_TEXTANGENT==1)
		return input.tangent.xyzw;
	#else
		return 0.0.xxxx;
	#endif
}

#if GEO_HAS_NORMAL==1
	float3 VSIN_GetLocalNormal(VSIN input)
	{
		return input.normal;
	}
#endif

float3 VSIN_GetLocalBitangent(VSIN input)
{
	#if (GEO_HAS_TEXBITANGENT==1)
		return input.bitangent.xyz;
	#elif (GEO_HAS_TEXTANGENT==1) && (GEO_HAS_NORMAL==1)
		float4 tangent = VSIN_GetLocalTangent(input);
		float3 normal = VSIN_GetLocalNormal(input);
		return cross(tangent.xyz, normal) * GetWorldTangentFrameHandiness(tangent);
	#else
		return 0.0.xxx;
	#endif
}

#if GEO_HAS_NORMAL!=1
	float3 VSIN_GetLocalNormal(VSIN input)
	{
		#if GEO_HAS_TEXTANGENT==1
				//  if the tangent and bitangent are unit-length and perpendicular, then we
				//  shouldn't have to normalize here. Since the inputs are coming from the
				//  vertex buffer, let's assume it's ok
			float4 localTangent = VSIN_GetLocalTangent(input);
			float3 localBitangent = VSIN_GetLocalBitangent(input);
			return NormalFromTangents(localTangent.xyz, localBitangent.xyz, GetWorldTangentFrameHandiness(localTangent));
		#else
			return float3(0,0,1);
		#endif
	}
#endif

#if (GEO_HAS_TEXTANGENT==1)
	TangentFrame VSIN_GetWorldTangentFrame(VSIN input)
	{
			//	If we can guarantee no scale on local-to-world, we can skip normalize of worldtangent/worldbitangent
		float4 localTangent     = VSIN_GetLocalTangent(input);
		float3 worldTangent 	= LocalToWorldUnitVector(localTangent.xyz);
		float3 worldBitangent 	= LocalToWorldUnitVector(VSIN_GetLocalBitangent(input));
		float handiness 		= GetWorldTangentFrameHandiness(localTangent);

			//  There's some issues here. If local-to-world has a flip on it, it might flip
			//  the direction we get from the cross product here... That's probably not
			//  what's expected.
			//  (worldNormal shouldn't need to be normalized, so long as worldTangent
			//  and worldNormal are perpendicular to each other)
		#if GEO_HAS_NORMAL==1
			float3 worldNormal	= LocalToWorldUnitVector(VSIN_GetLocalNormal(input));
		#else
			float3 worldNormal  = NormalFromTangents(worldTangent, worldBitangent, handiness);
		#endif
		return BuildTangentFrame(worldTangent, worldBitangent, worldNormal, handiness);
	}
#endif

CompressedTangentFrame VSIN_GetCompressedTangentFrame(VSIN input)
{
	CompressedTangentFrame result;
	#if GEO_HAS_TEXTANGENT==1
		float4 localTangent = VSIN_GetLocalTangent(input);
		float3 localBitangent = VSIN_GetLocalBitangent(input);
		result.basisVector0 = localTangent.xyz;
		result.basisVector1 = localBitangent;
		result.handiness = localTangent.w;
	#else
		result.basisVector0 = 0.0.xxx;
		result.basisVector1 = 0.0.xxx;
		result.handiness = 0.0;
	#endif
	return result;
}

#endif
