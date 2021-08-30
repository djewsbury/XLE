
#if !defined(DEFORM_VERTEX_HLSL)
#define DEFORM_VERTEX_HLSL

#if !defined(VSIN_H)
	#error Include TechniqueLibrary/Framework/VSIN.hlsl before including this file
#endif

#include "../Math/SurfaceAlgorithm.hlsl"

struct DeformedVertex
{
	float3			position;
	TangentFrame	tangentFrame;
	uint			coordinateSpace;		// 0 for object local space, 1 for world space
};

DeformedVertex DeformedVertex_Initialize(VSIN input)
{
	DeformedVertex deformedVertex;
	deformedVertex.position = VSIN_GetLocalPosition(input);
	deformedVertex.tangentFrame = VSIN_GetLocalTangentFrame(input);
	deformedVertex.coordinateSpace = 0;
	return deformedVertex;
}

#endif

