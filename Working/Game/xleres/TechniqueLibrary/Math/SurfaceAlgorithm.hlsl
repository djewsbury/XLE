// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SURFACE_ALGORITHM_H)
#define SURFACE_ALGORITHM_H

#include "../Framework/SystemUniforms.hlsl"

struct CompressedTangentFrame
{
	float3 basisVector0, basisVector1;
	float handiness;
};

struct TangentFrame
{
    float3 tangent, bitangent, normal;
    float handiness;
};

TangentFrame BuildTangentFrame(float3 tangent, float3 bitangent, float3 normal, float handiness)
{
    TangentFrame result;
    result.tangent = tangent;
    result.bitangent = bitangent;
    result.normal = normal;
    result.handiness = handiness;
    return result;
}

float GetWorldTangentFrameHandiness(float4 tangent)
{
	#if LOCAL_TO_WORLD_HAS_FLIP==1
		return sign(-tangent.w);
	#else
		return sign(tangent.w);
	#endif
}

float GetLocalTangentFrameHandiness(float4 tangent)
{
	return sign(tangent.w);
}

float3 NormalFromTangents(float3 tangent, float3 bitangent, float handiness)
{
		// Note -- the order of this cross product
		// 		depends on whether we have a right handed or
		//		left handed tangent frame. That should depend
		//		on how the tangent frame is generated, and
		//		whether there is a flip on any transforms applied.
	return cross(bitangent, tangent) * handiness;
}

float3 BitangentFromNormalTangent(float3 normal, float3 tangent, float handiness)
{
	return cross(tangent, normal) * handiness;
}

float3 TangentFromNormalBitangent(float3 normal, float3 bitangent, float handiness)
{
	return cross(normal, bitangent) * handiness;
}

float3 SampleNormalMap(Texture2D normalMap, SamplerState samplerObject, bool dxtFormatNormalMap, float2 texCoord)
{
	if (dxtFormatNormalMap) {
		float3 rawNormal = normalMap.Sample(samplerObject, texCoord).xyz * 2.f - 1.0.xxx;
            // note... we may have to do a normalize here; because the normal can
            // become significantly denormalized after filtering.
        rawNormal = normalize(rawNormal);
        return rawNormal;
    } else {
		float2 result = normalMap.Sample(samplerObject, texCoord).xy * 2.f - 1.0.xx;

            // The following seems to give the best results on the "nyra" model currently...
            // It seems that maybe that model is using a wierd coordinate scheme in the normal map?
        float2 coordTwiddle = float2(result.x, result.y);
        #if (MAT_HACK_TWIDDLE_NORMAL_MAP==1)
            coordTwiddle = float2(result.x, -result.y);
        #endif
		return float3(coordTwiddle, sqrt(saturate(1.f + dot(result.xy, -result.xy))));
        // return normalize(float3(coordTwiddle, 1.f - saturate(dot(result.xy, result.xy))));
    }
}

float3 NormalMapAlgorithm(  Texture2D normalMap, SamplerState samplerObject, bool dxtFormatNormalMap,
                            float2 texCoord, TangentFrame tangentFrame)
{
    row_major float3x3 normalsTextureToWorld = float3x3(tangentFrame.tangent.xyz, tangentFrame.bitangent, tangentFrame.normal);
    float3 normalTextureSample = SampleNormalMap(normalMap, samplerObject, dxtFormatNormalMap, texCoord);
		// Note -- matrix multiply opposite from typical
        //          (so we can initialise normalsTextureToWorld easily)
	return mul(normalTextureSample, normalsTextureToWorld);
    //return
    //      normalTextureSample.x * tangentFrame.tangent.xyz
    //    + normalTextureSample.y * tangentFrame.bitangent
    //    + normalTextureSample.z * tangentFrame.normal;
}

float3x3 GetLocalToWorldUniformScale()
{
        // note -- here, we assume that local-to-world doesn't have a nonuniform
        // scale.
	return float3x3(SysUniform_GetLocalToWorld()[0].xyz, SysUniform_GetLocalToWorld()[1].xyz, SysUniform_GetLocalToWorld()[2].xyz);
}

float3 LocalToWorldUnitVector(float3 localSpaceVector)
{
    float3 result = mul(GetLocalToWorldUniformScale(), localSpaceVector);
    #if !defined(NO_SCALE)
        result = normalize(result); // store scale value in constant...?
    #endif
    return result;
}

float3x3 AutoCotangentFrame(float3 inputNormal, float3 negativeViewVector, float2 texCoord)
{
		// get edge vectors of the pixel triangle
	float3 dp1	= ddx(negativeViewVector);
	float3 dp2	= ddy(negativeViewVector);
	float2 duv1	= ddx(texCoord);
	float2 duv2	= ddy(texCoord);

		// solve the linear system
	float3 dp2perp = cross(dp2, inputNormal);
	float3 dp1perp = cross(inputNormal, dp1);
	float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	float3 B = dp2perp * duv1.y + dp1perp * duv2.y;

		// construct a scale-invariant frame
	float invmax = rsqrt( max( dot(T,T), dot(B,B) ) );
	return float3x3(T * invmax, B * invmax, inputNormal);
}

CompressedTangentFrame TransformLocalToWorld(CompressedTangentFrame inputFrame)
{
	CompressedTangentFrame result;
	result.basisVector0 = LocalToWorldUnitVector(inputFrame.basisVector0);
	result.basisVector1 = LocalToWorldUnitVector(inputFrame.basisVector1);
	#if LOCAL_TO_WORLD_HAS_FLIP==1
		result.handiness = sign(-inputFrame.handiness);
	#else
		result.handiness = sign(inputFrame.handiness);
	#endif
	return result;
}

TangentFrame TransformLocalToWorld(TangentFrame inputFrame, uint vectorToReconstruct)
{
	// pass vectorToReconstruct == 0 to reconstruct tangent
	// pass vectorToReconstruct == 1 to reconstruct bitangent
	// pass vectorToReconstruct == 2 to reconstruct normal

		// note that if we can guarantee no scale on local-to-world, we can skip normalize of worldtangent/worldbitangent
	TangentFrame result;
	if (vectorToReconstruct == 2) {
		result.tangent = LocalToWorldUnitVector(inputFrame.tangent);
		result.bitangent = LocalToWorldUnitVector(inputFrame.bitangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.normal = NormalFromTangents(result.tangent, result.bitangent, result.handiness);
	} else if (vectorToReconstruct == 1) {
		result.normal = LocalToWorldUnitVector(inputFrame.normal);
		result.tangent = LocalToWorldUnitVector(inputFrame.tangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.bitangent = BitangentFromNormalTangent(result.normal, result.tangent, result.handiness);
	} else if (vectorToReconstruct == 0) {
		result.normal = LocalToWorldUnitVector(inputFrame.normal);
		result.bitangent = LocalToWorldUnitVector(inputFrame.bitangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.tangent = TangentFromNormalBitangent(result.normal, result.bitangent, result.handiness);
	} else if (vectorToReconstruct == 3) {
		// just normal
		result.normal = LocalToWorldUnitVector(inputFrame.normal);
	}
	return result;
}

TangentFrame Transform(float3x3 transform, TangentFrame inputFrame, uint vectorToReconstruct)
{
	// pass vectorToReconstruct == 0 to reconstruct tangent
	// pass vectorToReconstruct == 1 to reconstruct bitangent
	// pass vectorToReconstruct == 2 to reconstruct normal

	TangentFrame result;
	if (vectorToReconstruct == 2) {
		result.tangent = mul(transform, inputFrame.tangent);		// skipping re-normalize
		result.bitangent = mul(transform, inputFrame.bitangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.normal = NormalFromTangents(result.tangent, result.bitangent, result.handiness);
	} else if (vectorToReconstruct == 1) {
		result.normal = mul(transform, inputFrame.normal);
		result.tangent = mul(transform, inputFrame.tangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.bitangent = BitangentFromNormalTangent(result.normal, result.tangent, result.handiness);
	} else if (vectorToReconstruct == 0) {
		result.normal = mul(transform, inputFrame.normal);
		result.bitangent = mul(transform, inputFrame.bitangent);
		#if LOCAL_TO_WORLD_HAS_FLIP==1
			result.handiness = sign(-inputFrame.handiness);
		#else
			result.handiness = sign(inputFrame.handiness);
		#endif
		result.tangent = TangentFromNormalBitangent(result.normal, result.bitangent, result.handiness);
	}else if (vectorToReconstruct == 3) {
		// just normal
		result.normal = mul(transform, inputFrame.normal);
	}
	return result;
}

float3 GetNormal(CompressedTangentFrame inputFrame)
{
	return NormalFromTangents(inputFrame.basisVector0, inputFrame.basisVector1, inputFrame.handiness);
}

TangentFrame AsTangentFrame(CompressedTangentFrame inputFrame)
{
	float3 normal = NormalFromTangents(inputFrame.basisVector0, inputFrame.basisVector1, inputFrame.handiness);
	return BuildTangentFrame(inputFrame.basisVector0, inputFrame.basisVector1, normal, inputFrame.handiness);
}

#endif
