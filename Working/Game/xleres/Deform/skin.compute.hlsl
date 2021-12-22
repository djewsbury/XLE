
#include "deform-util.hlsl"

#if !IN_POSITION_FORMAT
	#define IN_POSITION_FORMAT 6       // DXGIVALUE_R32G32B32_FLOAT
#endif

#if !OUT_POSITION_FORMAT
	#define OUT_POSITION_FORMAT 6
#endif

#if JOINT_INDICES_TYPE != 2 || JOINT_INDICES_PRECISION != 8
	#error Unsupported skinning joint indicies type
#endif

#if WEIGHTS_TYPE != 4 || WEIGHTS_PRECISION != 8
	#error Unsupported skinning weights type
#endif

ByteAddressBuffer StaticVertexAttachments : register(t0);
ByteAddressBuffer InputAttributes : register(t1);
RWByteAddressBuffer OutputAttributes : register(u2);
RWByteAddressBuffer DeformTemporaryAttributes : register(u3);

struct IAParamsStruct
{
	uint InputStride;
	uint OutputStride;
	uint DeformTemporariesStride;

	uint InPositionsOffset;
	uint InNormalsOffset;
	uint InTangentsOffset;

	uint BufferFlags;

	uint OutPositionsOffset;
	uint OutNormalsOffset;
	uint OutTangentsOffset;

	uint WeightsOffset;
	uint JointIndicesOffsets;
	uint StaticVertexAttachmentsStride;

	uint JointMatricesInstanceStride;
};

StructuredBuffer<IAParamsStruct> IAParams : register(t4);
StructuredBuffer<row_major float3x4> JointTransforms : register(t5);

[[vk::push_constant]] struct InvocationParamsStruct
{
	uint VertexCount;
	uint FirstVertex;
	uint SoftInfluenceCount;
	uint FirstJointTransform;
	uint InstanceCount;
	uint OutputInstanceStride;
	uint IAParamsIdx;
} InvocationParams;

IAParamsStruct GetIAParams()
{
	return IAParams[InvocationParams.IAParamsIdx];
}

uint LoadWeightPack(uint vertexIdx, uint influenceCount)
{
	// Alignment rules (also applies to LoadIndexPack)
	// 1 influence/vertex: any alignment ok
	// 2 influences/vertex: must be aligned to multiple of 2
	// 4 or more influences/vertex: must be aligned to multiple of 4
	uint offset = GetIAParams().WeightsOffset+vertexIdx*GetIAParams().StaticVertexAttachmentsStride+influenceCount;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

uint LoadIndexPack(uint vertexIdx, uint influenceCount)
{
	uint offset = GetIAParams().JointIndicesOffsets+vertexIdx*GetIAParams().StaticVertexAttachmentsStride+influenceCount;
	return StaticVertexAttachments.Load(offset&(~3)) >> ((offset&3)*8);
}

[numthreads(64, 1, 1)]
	void main(uint vertexIdx : SV_DispatchThreadID)
{
	uint instanceIdx = vertexIdx / InvocationParams.VertexCount;
	if (instanceIdx >= InvocationParams.InstanceCount)
		return;
	vertexIdx -= instanceIdx*InvocationParams.VertexCount;
	vertexIdx += InvocationParams.FirstVertex;

	uint firstJointTransform = InvocationParams.FirstJointTransform;
	IAParamsStruct iaParams = GetIAParams();
	firstJointTransform += instanceIdx * iaParams.JointMatricesInstanceStride;

	float3 inputPosition = 
		(iaParams.BufferFlags & 0x1)
		? LoadAsFloat3(DeformTemporaryAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InPositionsOffset)
		: LoadAsFloat3(InputAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InPositionsOffset);

	#if IN_NORMAL_FORMAT
		float3 inputNormal = 
			(iaParams.BufferFlags & 0x2)
			? LoadAsFloat3(DeformTemporaryAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InNormalsOffset)
			: LoadAsFloat3(InputAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InNormalsOffset);
	#else
		float3 inputNormal = 0;
	#endif
	#if IN_TEXTANGENT_FORMAT
		float4 inputTangent = 
			(iaParams.BufferFlags & 0x4)
			? LoadAsFloat4(DeformTemporaryAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InTangentsOffset)
			: LoadAsFloat4(InputAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InTangentsOffset);
	#else
		float4 inputTangent = 0;
	#endif

	float3 outputPosition = 0.0.xxx;
	float3 outputNormal = 0.0.xxx;
	float3 outputTangent = 0.0.xxx;

	#if INFLUENCE_COUNT > 0
		uint c=0;
		for (;;) {
			uint packedWeights = LoadWeightPack(vertexIdx, c);
			uint packedIndices = LoadIndexPack(vertexIdx, c);

			{
				uint boneIndex = packedIndices&0xff;
				boneIndex += firstJointTransform;
				packedIndices >>= 8;
				float weight = (packedWeights&0xff) / 255.f;
				packedWeights >>= 8;

				outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
				float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
				outputNormal += weight * mul(rotationPart, inputNormal);
				outputTangent += weight * mul(rotationPart, inputTangent.xyz);

				++c;
				if (c == InvocationParams.SoftInfluenceCount) break;
			}

			#if INFLUENCE_COUNT > 1
				{
					uint boneIndex = packedIndices&0xff;
					boneIndex += firstJointTransform;
					packedIndices >>= 8;
					float weight = (packedWeights&0xff) / 255.f;
					packedWeights >>= 8;

					outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
					float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
					outputNormal += weight * mul(rotationPart, inputNormal);
					outputTangent += weight * mul(rotationPart, inputTangent.xyz);

					++c;
					if (c == InvocationParams.SoftInfluenceCount) break;
				}

				#if INFLUENCE_COUNT > 2
					{
						uint boneIndex = packedIndices&0xff;
						boneIndex += firstJointTransform;
						packedIndices >>= 8;
						float weight = (packedWeights&0xff) / 255.f;
						packedWeights >>= 8;

						outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
						float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
						outputNormal += weight * mul(rotationPart, inputNormal);
						outputTangent += weight * mul(rotationPart, inputTangent.xyz);

						++c;
						if (c == InvocationParams.SoftInfluenceCount) break;
					}

					{
						uint boneIndex = packedIndices&0xff;
						boneIndex += firstJointTransform;
						packedIndices >>= 8;
						float weight = (packedWeights&0xff) / 255.f;
						packedWeights >>= 8;

						outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
						float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
						outputNormal += weight * mul(rotationPart, inputNormal);
						outputTangent += weight * mul(rotationPart, inputTangent.xyz);

						++c;
						if (c == InvocationParams.SoftInfluenceCount) break;
					}
				#endif
			#endif
		}
	#else
		outputPosition = inputPosition;
		outputNormal = inputNormal;
		outputTangent = inputTangent.xyz;
	#endif

	uint outputLoc = vertexIdx * iaParams.OutputStride + instanceIdx * InvocationParams.OutputInstanceStride;
	StoreFloat3(outputPosition, OutputAttributes, OUT_POSITION_FORMAT, outputLoc + iaParams.OutPositionsOffset);
	#if OUT_NORMAL_FORMAT
		StoreFloat3(outputNormal, OutputAttributes, OUT_NORMAL_FORMAT, outputLoc + iaParams.OutNormalsOffset);
	#endif
	#if OUT_TEXTANGENT_FORMAT
		StoreFloat4(float4(outputTangent, inputTangent.w), OutputAttributes, OUT_TEXTANGENT_FORMAT, outputLoc + iaParams.OutTangentsOffset);
	#endif
}
