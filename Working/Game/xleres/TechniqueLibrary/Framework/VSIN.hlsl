// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(VSIN_H)
#define VSIN_H

#include "../Math/SurfaceAlgorithm.hlsl"

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

	#if GEO_HAS_COLOR>=1
		float4 color : COLOR;
	#endif

	#if GEO_HAS_COLOR1>=1
		float4 color1 : COLOR1;
	#endif

	#if GEO_HAS_TEXCOORD>=1
		float2 texCoord : TEXCOORD;
	#endif

	#if GEO_HAS_TEXCOORD1>=1
		float2 texCoord1 : TEXCOORD1;
	#endif

	#if GEO_HAS_TEXTANGENT==1
		float4 tangent : TEXTANGENT;
	#endif

	#if GEO_HAS_TEXBITANGENT==1
		float4 bitangent : TEXBITANGENT;
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

#if GEO_HAS_COLOR>=1 ////////////////////////////////////////////////
	float4 VSIN_GetColor0(VSIN input) { return input.color; }
#else
	float4 VSIN_GetColor0(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_COLOR1>=1 ///////////////////////////////////////////////
	float4 VSIN_GetColor1(VSIN input) { return input.color1; }
#else
	float4 VSIN_GetColor1(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD>=1 /////////////////////////////////////////////
	float2 VSIN_GetTexCoord0(VSIN input) { return input.texCoord; }
#else
	float2 VSIN_GetTexCoord0(VSIN input) { return 0.0.xx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD1>=1 ////////////////////////////////////////////
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

float4 VSIN_GetLocalTangent(VSIN input);
float4 VSIN_GetLocalBitangent(VSIN input);

float3 VSIN_GetLocalNormal(VSIN input)
{
	#if GEO_HAS_NORMAL==1
		return input.normal;
	#elif (GEO_HAS_TEXTANGENT==1) && (GEO_HAS_TEXBITANGENT==1)
			//  if the tangent and bitangent are unit-length and perpendicular, then we
			//  shouldn't have to normalize here. Since the inputs are coming from the
			//  vertex buffer, let's assume it's ok
		float4 localTangent = VSIN_GetLocalTangent(input);
		float3 localBitangent = VSIN_GetLocalBitangent(input);
		return NormalFromTangents(localTangent.xyz, localBitangent.xyz, GetLocalTangentFrameHandiness(localTangent));
	#else
		return float3(0,0,1);
	#endif
}

float4 VSIN_GetLocalTangent(VSIN input)
{
	#if (GEO_HAS_TEXTANGENT==1)
		return input.tangent.xyzw;
	#elif (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXBITANGENT==1)
		float4 bitangent = VSIN_GetLocalBitangent(input);
		float3 normal = VSIN_GetLocalNormal(input);
		return float4(TangentFromNormalBitangent(normal, bitangent.xyz, GetLocalTangentFrameHandiness(bitangent)), 0);
	#else
		return float4(1,0,0,0);
	#endif
}

float4 VSIN_GetLocalBitangent(VSIN input)
{
	#if (GEO_HAS_TEXBITANGENT==1)
		return input.bitangent.xyzw;
	#elif (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXTANGENT==1)
		float4 tangent = VSIN_GetLocalTangent(input);
		float3 normal = VSIN_GetLocalNormal(input);
		return float4(BitangentFromNormalTangent(normal, tangent.xyz, GetLocalTangentFrameHandiness(tangent)), 0);
	#else
		return float4(0,1,0,0);
	#endif
}

TangentFrame VSIN_GetLocalTangentFrame(VSIN input)
{
	#if (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXTANGENT==1)
		float4 tangent = VSIN_GetLocalTangent(input);
		float3 normal = VSIN_GetLocalNormal(input);
		float3 bitangent = BitangentFromNormalTangent(normal, tangent.xyz, GetLocalTangentFrameHandiness(tangent));
		return BuildTangentFrame(tangent.xyz, bitangent, normal, GetLocalTangentFrameHandiness(tangent));
	#elif (GEO_HAS_TEXTANGENT==1) && (GEO_HAS_TEXBITANGENT==1)
		float4 tangent = VSIN_GetLocalTangent(input);
		float3 bitangent = VSIN_GetLocalBitangent(input);
		float3 normal = NormalFromTangents(tangent.xyz, bitangent, GetLocalTangentFrameHandiness(tangent));
		return BuildTangentFrame(tangent.xyz, bitangent, normal, GetLocalTangentFrameHandiness(tangent));
	#elif (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXBITANGENT==1)
		float4 bitangent = VSIN_GetLocalBitangent(input);
		float3 normal = VSIN_GetLocalNormal(input);
		float3 tangent = TangentFromNormalBitangent(normal, bitangent.xyz, GetLocalTangentFrameHandiness(bitangent));
		return BuildTangentFrame(tangent, bitangent.xyz, normal, GetLocalTangentFrameHandiness(bitangent));
	#elif (GEO_HAS_NORMAL==1)
		return BuildTangentFrame(float3(1,0,0), float3(0,1,0), VSIN_GetLocalNormal(input), 1);
	#else
		return BuildTangentFrame(float3(1,0,0), float3(0,1,0), float3(0,0,1), 1);
	#endif
}

uint VSIN_TangentVectorToReconstruct()
{
	// select the prefered vector to reconstruct in TransformLocalToWorld() -- ie reconstructing to avoid another transform 
	// based on what was provided in the input assembly
	// pass vectorToReconstruct == 0 to reconstruct tangent
	// pass vectorToReconstruct == 1 to reconstruct bitangent
	// pass vectorToReconstruct == 2 to reconstruct normal
	#if (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXTANGENT==1)
		return 1;
	#elif (GEO_HAS_NORMAL==1) && (GEO_HAS_TEXBITANGENT==1)
		return 0;
	#elif (GEO_HAS_TEXTANGENT==1) && (GEO_HAS_TEXBITANGENT==1)
		return 2;		// implicitly no normal in this case
	#else
		// awkward case; probably not a full tangent frame
		return 0;
	#endif
}

TangentFrame VSIN_GetWorldTangentFrame(VSIN input)
{
	return TransformLocalToWorld(VSIN_GetLocalTangentFrame(input), VSIN_TangentVectorToReconstruct());
}

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
