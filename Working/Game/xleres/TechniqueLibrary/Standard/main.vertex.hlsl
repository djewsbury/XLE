#include "../Framework/VSIN.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/WorkingVertex.hlsl"
#include "VertexPatch_Standard.vertex.hlsl"
#include "../Core/BuildVSOUT.vertex.hlsl"
#include "../../Nodes/Templates.vertex.sh"

VSOUT frameworkEntry(VSIN input)
{
	WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);
	return BuildVSOUT(deformedVertex, input);
}

VSOUT frameworkEntryWithDeformVertex(VSIN input)
{
	WorkingVertex deformedVertex = VertexPatch(input);
	return BuildVSOUT(deformedVertex, input);
}
