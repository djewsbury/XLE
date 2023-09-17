#include "../Framework/VSIN.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/WorkingVertex.hlsl"
#include "../Framework/BuildVSOUT.vertex.hlsl"
#include "../../Objects/Templates.vertex.hlsl"
#include "../../Objects/default.vertex.hlsl"

VSOUT frameworkEntry(VSIN input)
{
	WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);
	return BuildVSOUT(deformedVertex, input);
}

VSOUT frameworkEntryWithVertexPatch(VSIN input)
{
	WorkingVertex deformedVertex = VertexPatch(input);
	return BuildVSOUT(deformedVertex, input);
}
