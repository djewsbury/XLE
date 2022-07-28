
#include "deform-helper.compute.hlsl"

#if JOINT_INDICES_TYPE != 2 || JOINT_INDICES_PRECISION != 8
	#error Unsupported skinning joint indicies type
#endif

#if WEIGHTS_TYPE != 4 || WEIGHTS_PRECISION != 8
	#error Unsupported skinning weights type
#endif

ByteAddressBuffer StaticVertexAttachments : register(t5);

struct SkinIAParamsStruct		// per geo parameters
{
	uint WeightsOffset;
	uint JointIndicesOffsets;
	uint StaticVertexAttachmentsStride;
	uint Dummy;
};

StructuredBuffer<SkinIAParamsStruct> SkinIAParams : register(t6);
#define row_major_float3x4 row_major float3x4
StructuredBuffer<row_major_float3x4> JointTransforms : register(t7);

uint LoadWeightPack(uint vertexIdx, uint influenceCount, SkinIAParamsStruct iaParams)
{
	// Alignment rules (also applies to LoadIndexPack)
	// 1 influence/vertex: any alignment ok
	// 2 influences/vertex: must be aligned to multiple of 2
	// 4 or more influences/vertex: must be aligned to multiple of 4
	uint offset = iaParams.WeightsOffset+vertexIdx*iaParams.StaticVertexAttachmentsStride+influenceCount;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

uint LoadIndexPack(uint vertexIdx, uint influenceCount, SkinIAParamsStruct iaParams)
{
	uint offset = iaParams.JointIndicesOffsets+vertexIdx*iaParams.StaticVertexAttachmentsStride+influenceCount;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

struct SkinInvocationStruct
{
	uint SoftInfluenceCount;
	uint FirstJointTransform;		// per section, not per geo
	uint SkinParamsIdx;
	uint JointMatricesInstanceStride;
};

[[vk::push_constant]] struct PushConstantsStruct
{
	DeformInvocationStruct DeformInvocationParams;
	SkinInvocationStruct SkinInvocationParams;
} PushConstants;
DeformInvocationStruct GetDeformInvocationParams() { return PushConstants.DeformInvocationParams; }
SkinInvocationStruct GetSkinInvocationParams() { return PushConstants.SkinInvocationParams; }

DeformVertex PerformDeform(DeformVertex input, uint vertexIdx, uint instanceIdx)
{
	SkinInvocationStruct skinInvocationParams = GetSkinInvocationParams();
	SkinIAParamsStruct skinIAParams = SkinIAParams[skinInvocationParams.SkinParamsIdx];
	uint firstJointTransform = skinInvocationParams.FirstJointTransform;
	firstJointTransform += instanceIdx * skinInvocationParams.JointMatricesInstanceStride;

	float3 outputPosition = 0.0.xxx;
	float3 outputNormal = 0.0.xxx;
	float3 outputTangent = 0.0.xxx;

	#if INFLUENCE_COUNT > 0
		uint c=0;
		for (;;) {
			uint packedWeights = LoadWeightPack(vertexIdx, c, skinIAParams);
			uint packedIndices = LoadIndexPack(vertexIdx, c, skinIAParams);

			{
				uint boneIndex = packedIndices&0xff;
				boneIndex += firstJointTransform;
				packedIndices >>= 8;
				float weight = (packedWeights&0xff) / 255.f;
				packedWeights >>= 8;

				outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
				float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
				outputNormal += weight * mul(rotationPart, input.normal);
				outputTangent += weight * mul(rotationPart, input.tangent.xyz);

				++c;
				if (c == skinInvocationParams.SoftInfluenceCount) break;
			}

			#if INFLUENCE_COUNT > 1
				{
					uint boneIndex = packedIndices&0xff;
					boneIndex += firstJointTransform;
					packedIndices >>= 8;
					float weight = (packedWeights&0xff) / 255.f;
					packedWeights >>= 8;

					outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
					float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
					outputNormal += weight * mul(rotationPart, input.normal.xyz);
					outputTangent += weight * mul(rotationPart, input.tangent.xyz);

					++c;
					if (c == skinInvocationParams.SoftInfluenceCount) break;
				}

				#if INFLUENCE_COUNT > 2
					{
						uint boneIndex = packedIndices&0xff;
						boneIndex += firstJointTransform;
						packedIndices >>= 8;
						float weight = (packedWeights&0xff) / 255.f;
						packedWeights >>= 8;

						outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
						float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
						outputNormal += weight * mul(rotationPart, input.normal.xyz);
						outputTangent += weight * mul(rotationPart, input.tangent.xyz);

						++c;
						if (c == skinInvocationParams.SoftInfluenceCount) break;
					}

					{
						uint boneIndex = packedIndices&0xff;
						boneIndex += firstJointTransform;
						packedIndices >>= 8;
						float weight = (packedWeights&0xff) / 255.f;
						packedWeights >>= 8;

						outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
						float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
						outputNormal += weight * mul(rotationPart, input.normal.xyz);
						outputTangent += weight * mul(rotationPart, input.tangent.xyz);

						++c;
						if (c == skinInvocationParams.SoftInfluenceCount) break;
					}
				#endif
			#endif
		}
	#else
		outputPosition = input.position;
		outputNormal = input.normal;
		outputTangent = input.tangent.xyz;
	#endif

	DeformVertex output;
	output.position = float4(outputPosition, input.position.w);
	#if OUT_NORMAL_FORMAT
		output.normal = float4(outputNormal, input.normal.w);
	#endif
	#if OUT_TEXTANGENT_FORMAT
		// assume handiness of the tangent frame hasn't changed as a result of skinning
		// Handiness could change if the combination transform of all of the joints has an odd number of
		// negative scales. But that's a little too expensive to calculate, and an unlikely scenario
		output.tangent = float4(outputTangent, input.tangent.w);
	#endif
	return output;
}
