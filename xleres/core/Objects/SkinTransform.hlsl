// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SKIN_TRANSFORM_H)
#define SKIN_TRANSFORM_H

#include "../TechniqueLibrary/Framework/VSIN.hlsl"

#define SKIN_TRANSFORMS_MATRICES	0
#define SKIN_TRANSFORMS_QT			1
#define SKIN_TRANSFORMS_DQ			2

static const uint BoneCountMax = 256;

cbuffer BoneTransforms : register(b6)
{
		//		We have multiple options for storing bone transformations:
		//			1. float3x4 transforms	--			shader math for vector by matrix is simplier
		//												and more efficient than quaternions. So this
		//												method creates more efficient vertex shaders than
		//												method 2.
		//												Also, scaling is supported
		//
		//			2. Quaternion + translation --		Uses 2/3rds of the shader constants of method 1.
		//												So, this method means less CPU-GPU transfers, but
		//												more expensive shaders.
		//
		//			3. Dual-Quaternion --				Cheapest math for vertices with many bone weights.
		//												But the art must be specifically made for this method.
		//
		//			4. Compressed Matrix?? --			It seems like we might be able to remove some elements
		//												from the matrix representation. If we assume there is
		//												no scale, some elements can be implied by the other
		//												elements. This would reduce the constant usage, but
		//												increase the shader instructions. It might be more efficient
		//												than the Q+T method -- but it might not matter that
		//												much in D3D11. We can do big cbuffer and tbuffer loads
		//												efficiently now.
		//
#if !defined(SKIN_TRANSFORMS) || SKIN_TRANSFORMS==SKIN_TRANSFORMS_MATRICES

	row_major float3x4 BoneTransforms[BoneCountMax];

#elif SKIN_TRANSFORMS==SKIN_TRANSFORMS_QT

	float2x4 BoneTransforms[BoneCountMax];

#elif SKIN_TRANSFORMS==SKIN_TRANSFORMS_DQ

		// not implemented

#endif
}

float3 TransformVector_Q(float4 q, float3 inputDirectionVector)
{
        //	transform direction vector by quaternion
		//	quaternion by vector:
		//		t = 2 * cross(q.xyz, v)
		//		v' = v + q.w * t + cross(q.xyz, t)
        //
        //  todo -- check this method... someone haven't problems with it here:
        //      http://stackoverflow.com/questions/22497093/faster-quaternion-vector-multiplication-doesnt-work
    float3 t = 2.f * cross(q.xyz, inputDirectionVector);
	return inputDirectionVector + q.w * t + cross(q.xyz, t);
}

float3 TransformPoint_QT(float2x4 qt, float3 inputPosition)
{
		//	transform position by quaternion + translation
	return qt[1].xyz + TransformVector_Q(qt[0], inputPosition);
}

float3 TransformPositionThroughSkinning(VSIN input, float3 position)
{
    #if GEO_HAS_BONEWEIGHTS
        float3 result = 0.0.xxx;
        #if SKIN_TRANSFORMS==SKIN_TRANSFORMS_MATRICES

			[unroll] for (uint i=0; i<4; ++i) {
				result += input.boneWeights[i] * mul(BoneTransforms[input.boneIndices[i]], float4(position, 1.f));
			}

		#elif SKIN_TRANSFORMS==SKIN_TRANSFORMS_QT

			[unroll] for (uint i=0; i<4; ++i) {
				result += input.boneWeights[i] * TransformPoint_QT(BoneTransforms[input.boneIndices[i]], position);
			}

		#endif
        return result;
    #else
        return position;
    #endif
}

float3 TransformDirectionVectorThroughSkinning(VSIN input, float3 directionVector)
{
    #if GEO_HAS_BONEWEIGHTS
        float3 result = 0.0.xxx;
        #if SKIN_TRANSFORMS==SKIN_TRANSFORMS_MATRICES

			[unroll] for (uint i=0; i<4; ++i) {
                    // (assuming no scale!)
                const row_major float3x4 jointTransform = BoneTransforms[input.boneIndices[i]];
                const row_major float3x3 rotationPart = float3x3(jointTransform[0].xyz, jointTransform[1].xyz, jointTransform[2].xyz);
				result += input.boneWeights[i] * mul(rotationPart, directionVector);
			}

		#elif SKIN_TRANSFORMS==SKIN_TRANSFORMS_QT

			[unroll] for (uint i=0; i<4; ++i) {
				result += input.boneWeights[i] * TransformVector_Q(BoneTransforms[input.boneIndices[i]][0], directionVector);
			}

		#endif
        return result;
    #else
        return directionVector;
    #endif
}

#endif
