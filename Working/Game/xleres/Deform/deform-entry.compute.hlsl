
#include "deform-helper.compute.hlsl"
#include "deform-util.hlsl"

#if !IN_POSITION_FORMAT
	#define IN_POSITION_FORMAT 6       // DXGIVALUE_R32G32B32_FLOAT
#endif

#if !OUT_POSITION_FORMAT
	#define OUT_POSITION_FORMAT 6
#endif

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

	uint BufferFlags;

	uint MappingBufferByteOffset;
	uint Dummy;
};

StructuredBuffer<IAParamsStruct> IAParams : register(t3);

#if !defined(HAS_INSTANTIATION_GetDeformInvocationParams)
	[[vk::push_constant]] DeformInvocationStruct InvocationParams;
	DeformInvocationStruct GetDeformInvocationParams()
	{
		return InvocationParams;
	}
#endif

struct LoadVertexResult
{
	DeformVertex vertex;
	bool success;
	uint vertexIdx;
	uint instanceIdx;
};

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
		(iaParams.BufferFlags & 0x1)
		? LoadAsFloat3(DeformTemporaryAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InPositionsOffset)
		: LoadAsFloat3(InputAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InPositionsOffset);

	#if IN_NORMAL_FORMAT
		result.vertex.normal = 
			(iaParams.BufferFlags & 0x2)
			? LoadAsFloat3(DeformTemporaryAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InNormalsOffset)
			: LoadAsFloat3(InputAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InNormalsOffset);
	#else
		result.vertex.normal = 0;
	#endif
	#if IN_TEXTANGENT_FORMAT
		result.vertex.tangent = 
			(iaParams.BufferFlags & 0x4)
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

	if (iaParams.BufferFlags & (0x1<<16)) {
		StoreFloat3(vertex.position, DeformTemporaryAttributes, OUT_POSITION_FORMAT, temporariesOutputLoc + iaParams.OutPositionsOffset);
	} else
		StoreFloat3(vertex.position, OutputAttributes, OUT_POSITION_FORMAT, outputLoc + iaParams.OutPositionsOffset);
	#if OUT_NORMAL_FORMAT
		if (iaParams.BufferFlags & (0x2<<16)) {
			StoreFloat3(vertex.normal, DeformTemporaryAttributes, OUT_NORMAL_FORMAT, temporariesOutputLoc + iaParams.OutNormalsOffset);
		} else
			StoreFloat3(vertex.normal, OutputAttributes, OUT_NORMAL_FORMAT, outputLoc + iaParams.OutNormalsOffset);
	#endif
	#if OUT_TEXTANGENT_FORMAT
		if (iaParams.BufferFlags & (0x4<<16)) {
			StoreFloat4(vertex.tangent, DeformTemporaryAttributes, OUT_TEXTANGENT_FORMAT, temporariesOutputLoc + iaParams.OutTangentsOffset);
		} else
			StoreFloat4(vertex.tangent, OutputAttributes, OUT_TEXTANGENT_FORMAT, outputLoc + iaParams.OutTangentsOffset);
	#endif
}

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
