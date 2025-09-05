
#include "deform-helper.compute.hlsl"

#if INFLUENCE_COUNT > 0
	#if JOINT_INDICES_TYPE != 2 || (JOINT_INDICES_PRECISION != 8 && JOINT_INDICES_PRECISION != 16)
		#error Unsupported skinning joint indicies type
	#endif

	#if WEIGHTS_TYPE != 4 || WEIGHTS_PRECISION != 8
		#error Unsupported skinning weights type
	#endif
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

uint LoadWeightPack(uint vertexIdx, uint influenceByteOffset, SkinIAParamsStruct iaParams)
{
	// Alignment rules (also applies to LoadIndexPack)
	// 1 influence/vertex: any alignment ok
	// 2 influences/vertex: must be aligned to multiple of 2
	// 4 or more influences/vertex: must be aligned to multiple of 4
	uint offset = iaParams.WeightsOffset+vertexIdx*iaParams.StaticVertexAttachmentsStride+influenceByteOffset;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

uint LoadIndexPack(uint vertexIdx, uint influenceByteOffset, SkinIAParamsStruct iaParams)
{
	uint offset = iaParams.JointIndicesOffsets+vertexIdx*iaParams.StaticVertexAttachmentsStride+influenceByteOffset;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

struct SkinInvocationStruct
{
	uint SoftInfluenceCount;		// cannot be zero unless INFLUENCE_COUNT is also zero!
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
	const float reciprocal255 = 1.0f / 255.f;

	#if INFLUENCE_COUNT > 0 && JOINT_INDICES_PRECISION == 8

		// 8 bit joint indices (weights also 8 bit packed)
		// we process 4 at a time so we can load in 32 bit integers from the vertex buffer

		uint c=0;
		for (;;) {
			uint packedWeights = LoadWeightPack(vertexIdx, c, skinIAParams);
			uint packedIndices = LoadIndexPackUINT8(vertexIdx, c, skinIAParams);

			{
				uint boneIndex = packedIndices&0xff;
				boneIndex += firstJointTransform;
				packedIndices >>= 8;
				float weight = (packedWeights&0xff) * reciprocal255;
				packedWeights >>= 8;

				outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
				float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
				outputNormal += weight * mul(rotationPart, input.normal.xyz);
				outputTangent += weight * mul(rotationPart, input.tangent.xyz);

				++c;
				if (c == skinInvocationParams.SoftInfluenceCount) break;
			}

			#if INFLUENCE_COUNT > 1
				{
					uint boneIndex = packedIndices&0xff;
					boneIndex += firstJointTransform;
					packedIndices >>= 8;
					float weight = (packedWeights&0xff) * reciprocal255;
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
						float weight = (packedWeights&0xff) * reciprocal255;
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
						float weight = (packedWeights&0xff) * reciprocal255;

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

	#elif INFLUENCE_COUNT > 0 && JOINT_INDICES_PRECISION == 16

		// 16 bit joint indices (weights also 8 bit packed)
		// we still process 4 at a time, because of the weights are still 8 bit packed

		uint c=0;
		for (;;) {
			uint packedWeights = LoadWeightPack(vertexIdx, c, skinIAParams);
			uint packedIndices0 = LoadIndexPack(vertexIdx, c*2, skinIAParams);
			uint packedIndices1 = LoadIndexPack(vertexIdx, c*2+4, skinIAParams);

			{
				uint boneIndex = packedIndices0&0xffff;
				boneIndex += firstJointTransform;
				packedIndices0 >>= 16;
				float weight = (packedWeights&0xff) * reciprocal255;
				packedWeights >>= 8;

				outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
				float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
				outputNormal += weight * mul(rotationPart, input.normal.xyz);
				outputTangent += weight * mul(rotationPart, input.tangent.xyz);

				++c;
				if (c == skinInvocationParams.SoftInfluenceCount) break;
			}

			#if INFLUENCE_COUNT > 1
				{
					uint boneIndex = packedIndices0&0xffff;
					boneIndex += firstJointTransform;
					float weight = (packedWeights&0xff) * reciprocal255;
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
						uint boneIndex = packedIndices1&0xffff;
						boneIndex += firstJointTransform;
						packedIndices1 >>= 16;
						float weight = (packedWeights&0xff) * reciprocal255;
						packedWeights >>= 8;

						outputPosition += weight * mul(JointTransforms[boneIndex], float4(input.position.xyz, 1)).xyz;
						float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
						outputNormal += weight * mul(rotationPart, input.normal.xyz);
						outputTangent += weight * mul(rotationPart, input.tangent.xyz);

						++c;
						if (c == skinInvocationParams.SoftInfluenceCount) break;
					}

					{
						uint boneIndex = packedIndices1&0xffff;
						boneIndex += firstJointTransform;
						float weight = (packedWeights&0xff) * reciprocal255;

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
		outputNormal = input.normal.xyz;
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
