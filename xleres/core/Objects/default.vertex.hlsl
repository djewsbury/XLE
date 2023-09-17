
#if !defined(VERTEX_PATCH_STANDARD_HLSL)
#define VERTEX_PATCH_STANDARD_HLSL

#if !defined(WORKING_VERTEX_HLSL)
	#error Include TechniqueLibrary/Core/WorkingVertex.hlsl before including this file
#endif

#include "SkinTransform.hlsl"

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

#endif
