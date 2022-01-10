
#if !defined(WORKING_VERTEX_HLSL)
#define WORKING_VERTEX_HLSL

#if !defined(VSIN_H)
	#error Include TechniqueLibrary/Framework/VSIN.hlsl before including this file
#endif

#include "../Math/SurfaceAlgorithm.hlsl"
#include "../Utility/Colour.hlsl"

#define WORKINGVERTEX_COORDINATESPACE_LOCAL 0
#define WORKINGVERTEX_COORDINATESPACE_WORLD 1

#define WORKINGVERTEX_TANGENTFRAME_NONE 0
#define WORKINGVERTEX_TANGENTFRAME_JUSTNORMAL 1
#define WORKINGVERTEX_TANGENTFRAME_FULL 2

struct WorkingVertex
{
	float3			position;
	uint			coordinateSpace;			// WORKINGVERTEX_COORDINATESPACE_...

	TangentFrame	tangentFrame;
	uint			tangentFrameType;			// WORKINGVERTEX_TANGENTFRAME_...
	uint			tangentVectorToReconstruct;	// see DefaultTangentVectorToReconstruct

	float4			color0, color1;				// colors in linear space
	uint			colorCount;					// 0, 1, 2

	float2			texCoord0, texCoord1;
	uint			texCoordCount;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//		C O M M O N   P R O P E R T I E S   F R O M   V S I N

float3 DeriveLocalNormal(VSIN input)
{
	#if GEO_HAS_NORMAL
		return input.normal;
	#elif GEO_HAS_TEXTANGENT && GEO_HAS_TEXBITANGENT
			//  if the tangent and bitangent are unit-length and perpendicular, then we
			//  shouldn't have to normalize here. Since the inputs are coming from the
			//  vertex buffer, let's assume it's ok
		float4 localTangent = VSIN_GetEncodedTangent(input);
		float3 localBitangent = VSIN_GetEncodedBitangent(input);
		return NormalFromTangents(localTangent.xyz, localBitangent.xyz, GetLocalTangentFrameHandiness(localTangent));
	#else
		return float3(0,0,1);
	#endif
}

float4 DeriveLocalTangent(VSIN input)
{
	#if GEO_HAS_TEXTANGENT
		return input.tangent.xyzw;
	#elif GEO_HAS_NORMAL && GEO_HAS_TEXBITANGENT
		float4 bitangent = VSIN_GetEncodedBitangent(input);
		float3 normal = VSIN_GetEncodedNormal(input);
		return float4(TangentFromNormalBitangent(normal, bitangent.xyz, GetLocalTangentFrameHandiness(bitangent)), 0);
	#else
		return float4(1,0,0,0);
	#endif
}

float4 DeriveLocalBitangent(VSIN input)
{
	#if GEO_HAS_TEXBITANGENT
		return input.bitangent.xyzw;
	#elif GEO_HAS_NORMAL && GEO_HAS_TEXTANGENT
		float4 tangent = VSIN_GetEncodedTangent(input);
		float3 normal = VSIN_GetEncodedNormal(input);
		return float4(BitangentFromNormalTangent(normal, tangent.xyz, GetLocalTangentFrameHandiness(tangent)), 0);
	#else
		return float4(0,1,0,0);
	#endif
}

TangentFrame DeriveLocalTangentFrame(VSIN input)
{
	#if GEO_HAS_NORMAL && GEO_HAS_TEXTANGENT
		float4 tangent = VSIN_GetEncodedTangent(input);
		float3 normal = VSIN_GetEncodedNormal(input);
		float3 bitangent = BitangentFromNormalTangent(normal, tangent.xyz, GetLocalTangentFrameHandiness(tangent));
		return BuildTangentFrame(tangent.xyz, bitangent, normal, GetLocalTangentFrameHandiness(tangent));
	#elif GEO_HAS_TEXTANGENT && GEO_HAS_TEXBITANGENT
		float4 tangent = VSIN_GetEncodedTangent(input);
		float3 bitangent = VSIN_GetEncodedBitangent(input);
		float3 normal = NormalFromTangents(tangent.xyz, bitangent, GetLocalTangentFrameHandiness(tangent));
		return BuildTangentFrame(tangent.xyz, bitangent, normal, GetLocalTangentFrameHandiness(tangent));
	#elif GEO_HAS_NORMAL && GEO_HAS_TEXBITANGENT
		float4 bitangent = VSIN_GetEncodedBitangent(input);
		float3 normal = VSIN_GetEncodedNormal(input);
		float3 tangent = TangentFromNormalBitangent(normal, bitangent.xyz, GetLocalTangentFrameHandiness(bitangent));
		return BuildTangentFrame(tangent, bitangent.xyz, normal, GetLocalTangentFrameHandiness(bitangent));
	#elif GEO_HAS_NORMAL
		return BuildTangentFrame(float3(1,0,0), float3(0,1,0), VSIN_GetEncodedNormal(input), 1);
	#else
		return BuildTangentFrame(float3(1,0,0), float3(0,1,0), float3(0,0,1), 1);
	#endif
}

uint DefaultTangentVectorToReconstruct()
{
	// select the prefered vector to reconstruct in TransformLocalToWorld() -- ie reconstructing to avoid another transform 
	// based on what was provided in the input assembly
	// pass vectorToReconstruct == 0 to reconstruct tangent
	// pass vectorToReconstruct == 1 to reconstruct bitangent
	// pass vectorToReconstruct == 2 to reconstruct normal
	#if GEO_HAS_NORMAL && GEO_HAS_TEXTANGENT
		return 1;
	#elif GEO_HAS_NORMAL && GEO_HAS_TEXBITANGENT
		return 0;
	#elif GEO_HAS_TEXTANGENT && GEO_HAS_TEXBITANGENT
		return 2;		// implicitly no normal in this case
	#else
		// awkward case; probably not a full tangent frame
		return 0;
	#endif
}

TangentFrame DeriveWorldTangentFrame(VSIN input)
{
	return TransformLocalToWorld(DeriveLocalTangentFrame(input), DefaultTangentVectorToReconstruct());
}

CompressedTangentFrame DeriveCompressedTangentFrame(VSIN input)
{
	CompressedTangentFrame result;
	#if GEO_HAS_TEXTANGENT
		float4 localTangent = VSIN_GetEncodedTangent(input);
		float3 localBitangent = VSIN_GetEncodedBitangent(input).xyz;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

WorkingVertex WorkingVertex_DefaultInitialize(VSIN input)
{
	WorkingVertex result;

	result.position = VSIN_GetLocalPosition(input);
	result.tangentFrame = DeriveLocalTangentFrame(input);
	result.coordinateSpace = WORKINGVERTEX_COORDINATESPACE_LOCAL;
	result.tangentVectorToReconstruct = DefaultTangentVectorToReconstruct();
	#if GEO_HAS_NORMAL
		#if GEO_HAS_TEXTANGENT || GEO_HAS_TEXBITANGENT
			result.tangentFrameType = WORKINGVERTEX_TANGENTFRAME_FULL;
		#else
			result.tangentFrameType = WORKINGVERTEX_TANGENTFRAME_JUSTNORMAL;
		#endif
	#else
		result.tangentFrameType = WORKINGVERTEX_TANGENTFRAME_NONE;
	#endif

	#if !GEO_HAS_COLOR && GEO_HAS_COLOR1
		#error Expecting both "COLOR" and "COLOR1", or neither
	#endif
	#if !GEO_HAS_TEXCOORD && GEO_HAS_TEXCOORD1
		#error Expecting both "TEXCOORD" and "TEXCOORD1", or neither
	#endif

	#if GEO_HAS_COLOR
		// Note that we're kind of forced to do srgb -> linear conversion here, because we'll loose precision
		// assuming 8 bit color inputs	
		result.color0.rgb = SRGBToLinear(VSIN_GetColor0(input).rgb);
		result.color0.a = VSIN_GetColor0(input).a;

		#if GEO_HAS_COLOR1
			result.color1.rgb = SRGBToLinear(VSIN_GetColor1(input).rgb);
			result.color1.a = VSIN_GetColor1(input).a;
			result.colorCount = 2;
		#else
			result.colorCount = 1;
		#endif
	#else
		result.colorCount = 0;
	#endif

	#if GEO_HAS_TEXCOORD
		result.texCoord0 = VSIN_GetTexCoord0(input);
		#if GEO_HAS_TEXCOORD1
			result.texCoord1 = VSIN_GetTexCoord1(input);
			result.texCoordCount = 2;
		#else
			result.texCoordCount = 1;
		#endif
	#else
		result.texCoordCount = 0;
	#endif

	return result;
}

#endif

