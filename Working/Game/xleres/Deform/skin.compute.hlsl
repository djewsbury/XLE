
#if !INFLUENCE_COUNT
	#define INFLUENCE_COUNT 4
#endif

#if !POSITION_FORMAT
	#define POSITION_FORMAT 6       // DXGIVALUE_R32G32B32_FLOAT
#endif

ByteAddressBuffer StaticVertexAttachments;
ByteAddressBuffer InputAttributes;
RWByteAddressBuffer OutputAttributes;

cbuffer IAParams
{
	uint InputStride;
	uint OutputStride;

	uint PositionsOffset;
	uint NormalsOffset;
	uint TangentsOffset;
}

cbuffer JointTransforms
{
	row_major float3x4 JointTransforms[200];
}

[[vk::push_constant]] struct InvocationParamsStruct
{
	uint VertexCount;
	uint FirstVertex;
	uint SoftInfluenceCount;
	uint FirstJointTransform;
} InvocationParams;

#define R32G32B32A32_FLOAT 2
#define R32G32B32_FLOAT 6
#define R16G16B16A16_FLOAT 10

float3 LoadAsFloat3(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		uint B = buffer.Load(byteOffset+4);
		uint C = buffer.Load(byteOffset+16);
		return asfloat(buffer.Load3(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint3(A.x&0xffff, A.x>>16, A.y&0xffff));
	} else {
		return 0;	// trouble
	}
}

float4 LoadAsFloat4(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32G32B32_FLOAT) {
		return float4(asfloat(buffer.Load3(byteOffset)), 1);
	} else if (format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load4(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint4(A.x&0xffff, A.x>>16, A.y&0xffff, A.y>>16));
	} else {
		return 0;	// trouble
	}
}

void StoreFloat3(float3 value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(value));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(float4(value, 1)));
	} else if (format == R16G16B16A16_FLOAT) {
		uint3 A = f32tof16(value);
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), A.z&0xffff));
	} else {
		// trouble
	}
}

void StoreFloat4(float4 value, RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32G32B32_FLOAT) {
		buffer.Store3(byteOffset, asuint(value.xyz));
	} else if (format == R32G32B32A32_FLOAT) {
		buffer.Store4(byteOffset, asuint(value));
	} else if (format == R16G16B16A16_FLOAT) {
		uint4 A = f32tof16(value);
		buffer.Store2(byteOffset, uint2((A.x&0xffff)|(A.y<<16), (A.z&0xffff)|(A.w<<16)));
	} else {
		// trouble
	}
}

[numthreads(64, 1, 1)]
	void main(uint vertexIdx : SV_DispatchThreadID)
{
	if (vertexIdx >= InvocationParams.VertexCount)
		return;
	vertexIdx += InvocationParams.FirstVertex;

	float3 inputPosition = LoadAsFloat3(InputAttributes, POSITION_FORMAT, vertexIdx * InputStride + PositionsOffset);

	#if NORMAL_FORMAT && TEXTANGENT_FORMAT
		float3 inputNormal = LoadAsFloat3(InputAttributes, NORMAL_FORMAT, vertexIdx * InputStride + NormalsOffset);
		float4 inputTangent = LoadAsFloat4(InputAttributes, TEXTANGENT_FORMAT, vertexIdx * InputStride + TangentsOffset);

		float3 outputPosition = 0.0.xxx;
		float3 outputNormal = 0.0.xxx;
		float3 outputTangent = 0.0.xxx;

		// 
		for (uint c=0; c!=InvocationParams.SoftInfluenceCount; ++c) {
			uint packedAttachments = StaticVertexAttachments.Load((vertexIdx*INFLUENCE_COUNT+c)*2);
			
			uint boneIndex = packedAttachments&0xff;
			boneIndex += InvocationParams.FirstJointTransform;
			packedAttachments >>= 8;
			float weight = (packedAttachments&0xff) / 255.f;
			packedAttachments >>= 8;

			outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
			float3x3 rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
			outputNormal += weight * mul(rotationPart, inputNormal);
			outputTangent += weight * mul(rotationPart, inputTangent.xyz);

			++c;
			if (c == InvocationParams.SoftInfluenceCount) break;

			boneIndex = packedAttachments&0xff;
			boneIndex += InvocationParams.FirstJointTransform;
			packedAttachments >>= 8;
			weight = (packedAttachments&0xff) / 255.f;
			packedAttachments >>= 8;

			outputPosition += weight * mul(JointTransforms[boneIndex], float4(inputPosition, 1)).xyz;
			rotationPart = float3x3(JointTransforms[boneIndex][0].xyz, JointTransforms[boneIndex][1].xyz, JointTransforms[boneIndex][2].xyz);
			outputNormal += weight * mul(rotationPart, inputNormal);
			outputTangent += weight * mul(rotationPart, inputTangent.xyz);
		}

		StoreFloat3(outputPosition, OutputAttributes, POSITION_FORMAT, vertexIdx * OutputStride + PositionsOffset);
		StoreFloat3(outputNormal, OutputAttributes, NORMAL_FORMAT, vertexIdx * OutputStride + NormalsOffset);
		StoreFloat4(float4(outputTangent, inputTangent.w), OutputAttributes, TEXTANGENT_FORMAT, vertexIdx * OutputStride + TangentsOffset);
	#else
		#error non-tangent-frame modes not implemented
	#endif
}
