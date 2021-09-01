
#if !defined(ILLUM_STANDARD_VERTEX_HLSL)
#define ILLUM_STANDARD_VERTEX_HLSL

#if !defined(DEFORM_VERTEX_HLSL)
	#error Include TechniqueLibrary/Core/DeformVertex.hlsl before including this file
#endif

#include "../Core/Animation/SkinTransform.hlsl"
#include "../SceneEngine/Vegetation/WindAnim.hlsl"
#include "../SceneEngine/Vegetation/InstanceVS.hlsl"

DeformedVertex DeformVertex_Standard(DeformedVertex preDeform, VSIN input)
{
	DeformedVertex result;
	result.coordinateSpace = preDeform.coordinateSpace;
	result.position = preDeform.position;
	result.tangentFrame = preDeform.tangentFrame;

	#if SPAWNED_INSTANCE==1
		float3 objectCentreWorld;
		float3 worldNormalTemp;
		result.position = InstanceWorldPosition(input, worldNormalTemp, objectCentreWorld);
	#endif

	#if GEO_HAS_BONEWEIGHTS
		result.position = TransformPositionThroughSkinning(input, result.position);
		result.tangentFrame.basisVector0 = TransformDirectionVectorThroughSkinning(input, result.tangentFrame.basisVector0);
		result.tangentFrame.basisVector1 = TransformDirectionVectorThroughSkinning(input, result.tangentFrame.basisVector1);
	#endif

	return result;
}

DeformedVertex DeformVertex_WindBending(DeformedVertex preDeform, VSIN input)
{
	DeformedVertex result;
	result.coordinateSpace = 1;

	float3 objectCentreWorld;
	#if SPAWNED_INSTANCE==1
		float3 worldNormalTemp;
		InstanceWorldPosition(input, worldNormalTemp, objectCentreWorld);
	#else
		objectCentreWorld = float3(SysUniform_GetLocalToWorld()[0][3], SysUniform_GetLocalToWorld()[1][3], SysUniform_GetLocalToWorld()[2][3]);
	#endif

	if (preDeform.coordinateSpace == 0) {
		result.tangentFrame = TransformLocalToWorld(preDeform.tangentFrame, VSIN_TangentVectorToReconstruct());
		result.position = mul(SysUniform_GetLocalToWorld(), float4(preDeform.position,1)).xyz;
	} else {
		result.tangentFrame = preDeform.tangentFrame;
		result.position = preDeform.position;
	}

	result.position = PerformWindBending(result.position, result.tangentFrame.normal, objectCentreWorld, float3(1,0,0), VSIN_GetColor0(input).rgb);	
	return result;
}

#endif
