
#include "deform-helper.compute.hlsl"

#define R32G32B32A32_FLOAT 2
#define R32G32B32_FLOAT 6
#define R16G16B16A16_FLOAT 10
#define R32_UINT 42
#define R16_UINT 57
#define R8G8B8A8_UNORM 28
#define R8G8B8A8_SNORM 31
#define R8G8B8_UNORM 1001
#define R8G8B8_SNORM 1004
#define R16G16B16A16_SNORM 13

#if !IN_POSITION_FORMAT
	#define IN_POSITION_FORMAT R32G32B32_FLOAT
#endif

#if !OUT_POSITION_FORMAT
	#define OUT_POSITION_FORMAT R32G32B32_FLOAT
#endif

#if !defined(BUFFER_FLAGS)
	#define BUFFER_FLAGS 0
#endif

float3 LoadAsFloat3(ByteAddressBuffer buffer, uint format, uint byteOffset);
float4 LoadAsFloat4(ByteAddressBuffer buffer, uint format, uint byteOffset);
float3 LoadAsFloat3(RWByteAddressBuffer buffer, uint format, uint byteOffset);
float4 LoadAsFloat4(RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat3(float3 value, RWByteAddressBuffer buffer, uint format, uint byteOffset);
void StoreFloat4(float4 value, RWByteAddressBuffer buffer, uint format, uint byteOffset);

ByteAddressBuffer InputAttributes : register(t0);
RWByteAddressBuffer OutputAttributes : register(u1);
RWByteAddressBuffer DeformTemporaryAttributes : register(u2);
ByteAddressBuffer VertexMapping : register(t4);

struct IAParamsStruct
{
	uint InputStride;
	uint OutputStride;
	uint DeformTemporariesStride;

	uint InPositionsOffset;
	uint InNormalsOffset;
	uint InTangentsOffset;

	uint OutPositionsOffset;
	uint OutNormalsOffset;
	uint OutTangentsOffset;

	uint MappingBufferByteOffset;
	uint Dummy[2];
};

StructuredBuffer<IAParamsStruct> IAParams : register(t3);

#if !defined(HAS_INSTANTIATION_GetDeformInvocationParams)
	[[vk::push_constant]] DeformInvocationStruct InvocationParams;
	DeformInvocationStruct GetDeformInvocationParams()
	{
		return InvocationParams;
	}
#else
	DeformInvocationStruct GetDeformInvocationParams();
#endif

struct LoadVertexResult
{
	DeformVertex vertex;
	bool success;
	uint vertexIdx;
	uint instanceIdx;
};

LoadVertexResult LoadVertex(uint dispatchIdx, DeformInvocationStruct invocationParams);
void StoreVertex(DeformVertex vertex, uint vertexIdx, uint instanceIdx, DeformInvocationStruct invocationParams);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//   E N T R Y   P O I N T

[numthreads(64, 1, 1)]
	void frameworkEntry(uint dispatchIdx : SV_DispatchThreadID)
{
	DeformInvocationStruct invocationParams = GetDeformInvocationParams();
	LoadVertexResult loadVertex = LoadVertex(dispatchIdx, invocationParams);
	if (!loadVertex.success)
		return;

	#if defined(VERTEX_MAPPING)
		IAParamsStruct iaParams = IAParams[invocationParams.ParamsIdx];
		uint mappedVertex = VertexMapping.Load(iaParams.MappingBufferByteOffset+loadVertex.vertexIdx*4);
	#else
		uint mappedVertex = loadVertex.vertexIdx;
	#endif
	DeformVertex resultVertex = PerformDeform(loadVertex.vertex, mappedVertex, loadVertex.instanceIdx);
	StoreVertex(resultVertex, loadVertex.vertexIdx, loadVertex.instanceIdx, invocationParams);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//   U T I L I T I E S

LoadVertexResult LoadVertex(uint dispatchIdx, DeformInvocationStruct invocationParams)
{
	uint vertexIdx = dispatchIdx;
	uint instanceIdx = vertexIdx / invocationParams.VertexCount;
	if (instanceIdx >= invocationParams.InstanceCount) {
		LoadVertexResult result;
		result.success = false;
		return result;
	}
	vertexIdx -= instanceIdx*invocationParams.VertexCount;
	vertexIdx += invocationParams.FirstVertex;

	IAParamsStruct iaParams = IAParams[invocationParams.ParamsIdx];

	LoadVertexResult result;
	result.vertex.position = 
		(BUFFER_FLAGS & 0x1)
		? LoadAsFloat4(DeformTemporaryAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InPositionsOffset)
		: LoadAsFloat4(InputAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InPositionsOffset);

	#if IN_NORMAL_FORMAT
		result.vertex.normal = 
			(BUFFER_FLAGS & 0x2)
			? LoadAsFloat4(DeformTemporaryAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InNormalsOffset)
			: LoadAsFloat4(InputAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InNormalsOffset);
	#else
		result.vertex.normal = 0;
	#endif
	#if IN_TEXTANGENT_FORMAT
		result.vertex.tangent = 
			(BUFFER_FLAGS & 0x4)
			? LoadAsFloat4(DeformTemporaryAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InTangentsOffset)
			: LoadAsFloat4(InputAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InTangentsOffset);
	#else
		result.vertex.tangent = 0;
	#endif

	result.success = true;
	result.vertexIdx = vertexIdx;
	result.instanceIdx = instanceIdx;
	return result;
}

void StoreVertex(DeformVertex vertex, uint vertexIdx, uint instanceIdx, DeformInvocationStruct invocationParams)
{
	IAParamsStruct iaParams = IAParams[invocationParams.ParamsIdx];

	uint outputLoc = vertexIdx * iaParams.OutputStride + instanceIdx * invocationParams.OutputInstanceStride;
	uint temporariesOutputLoc = vertexIdx * iaParams.DeformTemporariesStride + instanceIdx * invocationParams.DeformTemporariesInstanceStride;

	#if BUFFER_FLAGS & (0x1<<16)
		StoreFloat4(vertex.position, DeformTemporaryAttributes, OUT_POSITION_FORMAT, temporariesOutputLoc + iaParams.OutPositionsOffset);
	#else
		StoreFloat4(vertex.position, OutputAttributes, OUT_POSITION_FORMAT, outputLoc + iaParams.OutPositionsOffset);
	#endif
	#if OUT_NORMAL_FORMAT
		#if BUFFER_FLAGS & (0x2<<16)
			StoreFloat4(vertex.normal, DeformTemporaryAttributes, OUT_NORMAL_FORMAT, temporariesOutputLoc + iaParams.OutNormalsOffset);
		#else
			StoreFloat4(vertex.normal, OutputAttributes, OUT_NORMAL_FORMAT, outputLoc + iaParams.OutNormalsOffset);
		#endif
	#endif
	#if OUT_TEXTANGENT_FORMAT
		#if BUFFER_FLAGS & (0x4<<16)
			StoreFloat4(vertex.tangent, DeformTemporaryAttributes, OUT_TEXTANGENT_FORMAT, temporariesOutputLoc + iaParams.OutTangentsOffset);
		#else
			StoreFloat4(vertex.tangent, OutputAttributes, OUT_TEXTANGENT_FORMAT, outputLoc + iaParams.OutTangentsOffset);
		#endif
	#endif
}

// For the SNorm formats here, we're assuming that both -1 and +1 can be represented -- in effect there are 2 representations of -1
// UNorm is mapped onto -1 -> 1; since this just tends to be a more useful mapping than 0 -> 1
int SignExtend8(uint byteValue) { return byteValue + (byteValue >> 7) * 0xffffff00; }
int SignExtend16(uint shortValue) { return shortValue + (shortValue >> 15) * 0xffff0000; }

float UNorm8ToFloat(uint x) { return x * (2.0 / float(0xff)) - 1.0; }
uint FloatToUNorm8(float x) { return (uint)clamp((x + 1.0) * float(0xff) / 2.0, 0.0, float(0xff)); }

float SNorm8ToFloat(uint x) { return clamp(SignExtend8(x) / float(0x7f), -1.0, 1.0); }
uint FloatToSNorm8(float x) { return int(clamp(x, -1.0, 1.0) * float(0x7f)) & 0xff; }

float SNorm16ToFloat(uint x) { return clamp(SignExtend16(x) / float(0x7fff), -1.0, 1.0); }
uint FloatToSNorm16(float x) { return int(clamp(x, -1.0, 1.0) * float(0x7fff)) & 0xffff; }

float3 LoadAsFloat3(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be mutliple of 4
	if (format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load3(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint3(A.x&0xffff, A.x>>16, A.y&0xffff));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float3(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff));
	} else {
		return 0;	// trouble
	}
}

float4 LoadAsFloat4(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be mutliple of 4
	if (format == R32G32B32_FLOAT) {
		return float4(asfloat(buffer.Load3(byteOffset)), 1);
	} else if (format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load4(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint4(A.x&0xffff, A.x>>16, A.y&0xffff, A.y>>16));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float4(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff), SNorm16ToFloat(A.y>>16));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff), UNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff), SNorm8ToFloat((A>>24) & 0xff));
	} else {
		return 0;	// trouble
	}
}

float3 LoadAsFloat3(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be mutliple of 4
	if (format == R32G32B32_FLOAT || format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load3(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint3(A.x&0xffff, A.x>>16, A.y&0xffff));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float3(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float3(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff));
	} else {
		return 0;	// trouble
	}
}

float4 LoadAsFloat4(RWByteAddressBuffer buffer, uint format, uint byteOffset)
{
	// requires that byteOffset be mutliple of 4
	if (format == R32G32B32_FLOAT) {
		return float4(asfloat(buffer.Load3(byteOffset)), 1);
	} else if (format == R32G32B32A32_FLOAT) {
		return asfloat(buffer.Load4(byteOffset));
	} else if (format == R16G16B16A16_FLOAT) {
		uint2 A = buffer.Load2(byteOffset);
		return f16tof32(uint4(A.x&0xffff, A.x>>16, A.y&0xffff, A.y>>16));
	} else if (format == R16G16B16A16_SNORM) {
		uint2 A = buffer.Load2(byteOffset);
		return float4(SNorm16ToFloat(A.x&0xffff), SNorm16ToFloat(A.x>>16), SNorm16ToFloat(A.y&0xffff), SNorm16ToFloat(A.y>>16));
	} else if (format == R8G8B8A8_UNORM || format == R8G8B8_UNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(UNorm8ToFloat(A & 0xff), UNorm8ToFloat((A>>8) & 0xff), UNorm8ToFloat((A>>16) & 0xff), UNorm8ToFloat((A>>24) & 0xff));
	} else if (format == R8G8B8A8_SNORM || format == R8G8B8_SNORM) {
		uint A = buffer.Load(byteOffset);
		return float4(SNorm8ToFloat(A & 0xff), SNorm8ToFloat((A>>8) & 0xff), SNorm8ToFloat((A>>16) & 0xff), SNorm8ToFloat((A>>24) & 0xff));
	} else {
		return 0;	// trouble
	}
}

uint LoadAsUInt(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_UINT) {
		return buffer.Load(byteOffset);
	} else if (format == R16_UINT) {
		uint index = buffer.Load(byteOffset & ~3);
		if (byteOffset & 3) return index >> 16;
		else return index & 0xffff;
	} else {
		return 0;
	}
}

uint3 LoadAsUInt3(ByteAddressBuffer buffer, uint format, uint byteOffset)
{
	if (format == R32_UINT) {
		return buffer.Load3(byteOffset);
	} else if (format == R16_UINT) {
		uint2 raw = buffer.Load2(byteOffset & ~3);
		if (byteOffset & 3) return uint3(raw.x >> 16, raw.y & 0xffff, raw.y >> 16);
		else return uint3(raw.x & 0xffff, raw.x >> 16, raw.y & 0xffff);
	} else {
		return 0;
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
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value.x)|(FloatToSNorm16(value.y)<<16), FloatToSNorm16(value.z)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value.x)|(FloatToUNorm8(value.y) << 8u)|(FloatToUNorm8(value.z) << 16u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value.x)|(FloatToSNorm8(value.y) << 8u)|(FloatToSNorm8(value.z) << 16u));
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
	} else if (format == R16G16B16A16_SNORM) {
		buffer.Store2(byteOffset, uint2(FloatToSNorm16(value.x)|(FloatToSNorm16(value.y)<<16), FloatToSNorm16(value.z)|(FloatToSNorm16(value.w)<<16)));
	} else if (format == R8G8B8A8_UNORM) {
		buffer.Store(byteOffset, FloatToUNorm8(value.x)|(FloatToUNorm8(value.y) << 8u)|(FloatToUNorm8(value.z) << 16u)|(FloatToUNorm8(value.w) << 24u));
	} else if (format == R8G8B8A8_SNORM) {
		buffer.Store(byteOffset, FloatToSNorm8(value.x)|(FloatToSNorm8(value.y) << 8u)|(FloatToSNorm8(value.z) << 16u)|(FloatToSNorm8(value.w) << 24u));
	} else {
		// trouble
	}
}
