
#if !defined(WORKING_VERTEX_HLSL)
	#error Include TechniqueLibrary/Core/WorkingVertex.hlsl before including this file
#endif

#include "WindAnim.hlsl"
#include "InstanceVS.hlsl"

WorkingVertex VertexPatch_WindAnim(VSIN input)
{
	WorkingVertex result = WorkingVertex_DefaultInitialize(input);

	float3 objectCentreWorld = float3(SysUniform_GetLocalToWorld()[0][3], SysUniform_GetLocalToWorld()[1][3], SysUniform_GetLocalToWorld()[2][3]);

	if (result.coordinateSpace == WORKINGVERTEX_COORDINATESPACE_LOCAL) {
		result.tangentFrame = TransformLocalToWorld(result.tangentFrame, DefaultTangentVectorToReconstruct());
		result.position = mul(SysUniform_GetLocalToWorld(), float4(result.position,1)).xyz;
		result.coordinateSpace = WORKINGVERTEX_COORDINATESPACE_WORLD;
	}

	result.position = PerformWindBending(result.position, result.tangentFrame.normal, objectCentreWorld, float3(1,0,0), VSIN_GetColor0(input).rgb);	
	return result;
}
