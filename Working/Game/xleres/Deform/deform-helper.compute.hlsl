
#if !defined(DEFORM_HELPER_HLSL)
#define DEFORM_HELPER_HLSL

struct DeformVertex
{
	float3 position;
	float3 normal;
	float4 tangent;
};

DeformVertex PerformDeform(DeformVertex vertex, uint vertexIdx, uint instanceIdx);

struct DeformInvocationStruct
{
	uint VertexCount;
	uint FirstVertex;
	uint InstanceCount;
	uint OutputInstanceStride;
	uint DeformTemporariesInstanceStride;
	uint ParamsIdx;
};
DeformInvocationStruct GetDeformInvocationParams();

#endif
