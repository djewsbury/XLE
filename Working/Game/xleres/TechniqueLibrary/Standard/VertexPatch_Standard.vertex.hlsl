
#if !defined(VERTEX_PATCH_STANDARD_HLSL)
#define VERTEX_PATCH_STANDARD_HLSL

#if !defined(WORKING_VERTEX_HLSL)
	#error Include TechniqueLibrary/Core/WorkingVertex.hlsl before including this file
#endif

#include "../Core/Animation/SkinTransform.hlsl"
#include "../SceneEngine/Vegetation/WindAnim.hlsl"
#include "../SceneEngine/Vegetation/InstanceVS.hlsl"

WorkingVertex VertexPatch_Standard(VSIN input)
{
	WorkingVertex result = WorkingVertex_DefaultInitialize(input);

	#if GEO_HAS_BONEWEIGHTS
		result.position = TransformPositionThroughSkinning(input, result.position);
		result.tangentFrame.tangent = TransformDirectionVectorThroughSkinning(input, result.tangentFrame.tangent);
		result.tangentFrame.bitangent = TransformDirectionVectorThroughSkinning(input, result.tangentFrame.bitangent);
		result.tangentFrame.normal = TransformDirectionVectorThroughSkinning(input, result.tangentFrame.normal);
	#endif

	return result;
}

WorkingVertex VertexPatch_WindBending(VSIN input)
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

#endif
