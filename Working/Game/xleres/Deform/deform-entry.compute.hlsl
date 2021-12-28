
#include "deform-helper.compute.hlsl"
#include "deform-util.hlsl"

#if !IN_POSITION_FORMAT
	#define IN_POSITION_FORMAT 6       // DXGIVALUE_R32G32B32_FLOAT
#endif

#if !OUT_POSITION_FORMAT
	#define OUT_POSITION_FORMAT 6
#endif

ByteAddressBuffer InputAttributes;
RWByteAddressBuffer OutputAttributes;
RWByteAddressBuffer DeformTemporaryAttributes;
ByteAddressBuffer VertexMapping;

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
};

StructuredBuffer<IAParamsStruct> IAParams;

[[vk::push_constant]] struct InvocationParamsStruct
{
	uint VertexCount;
	uint FirstVertex;
	uint InstanceCount;
	uint OutputInstanceStride;
	uint DeformTemporariesInstanceStride;
	uint ParamsIdx;
} InvocationParams;

[numthreads(64, 1, 1)]
	void frameworkEntry(uint vertexIdx : SV_DispatchThreadID)
{
	uint instanceIdx = vertexIdx / InvocationParams.VertexCount;
	if (instanceIdx >= InvocationParams.InstanceCount)
		return;
	vertexIdx -= instanceIdx*InvocationParams.VertexCount;
	vertexIdx += InvocationParams.FirstVertex;

	IAParamsStruct iaParams = IAParams[InvocationParams.ParamsIdx];

	DeformVertex vertex;
	vertex.position = 
		(iaParams.BufferFlags & 0x1)
		? LoadAsFloat3(DeformTemporaryAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InPositionsOffset)
		: LoadAsFloat3(InputAttributes, IN_POSITION_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InPositionsOffset);

	#if IN_NORMAL_FORMAT
		vertex.normal = 
			(iaParams.BufferFlags & 0x2)
			? LoadAsFloat3(DeformTemporaryAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InNormalsOffset)
			: LoadAsFloat3(InputAttributes, IN_NORMAL_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InNormalsOffset);
	#else
		vertex.normal = 0;
	#endif
	#if IN_TEXTANGENT_FORMAT
		vertex.tangent = 
			(iaParams.BufferFlags & 0x4)
			? LoadAsFloat4(DeformTemporaryAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.DeformTemporariesStride + iaParams.InTangentsOffset)
			: LoadAsFloat4(InputAttributes, IN_TEXTANGENT_FORMAT, vertexIdx * iaParams.InputStride + iaParams.InTangentsOffset);
	#else
		vertex.tangent = 0;
	#endif

	#if defined(VERTEX_MAPPING)
		uint mappedVertex = VertexMapping.Load(iaParams.MappingBufferByteOffset+vertexIdx*4);
	#else
		uint mappedVertex = vertexIdx;
	#endif
	vertex = PerformDeform(vertex, mappedVertex, instanceIdx);

	uint outputLoc = vertexIdx * iaParams.OutputStride + instanceIdx * InvocationParams.OutputInstanceStride;
	uint temporariesOutputLoc = vertexIdx * iaParams.DeformTemporariesStride + instanceIdx * InvocationParams.DeformTemporariesInstanceStride;

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