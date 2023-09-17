#include "../Framework/VSIN.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/WorkingVertex.hlsl"
#include "../Framework/BuildVSOUT.vertex.hlsl"
#include "../../Objects/Templates.vertex.hlsl"
#include "../../Objects/default.vertex.hlsl"

VSOUT main(VSIN input)
{
	WorkingVertex deformedVertex = VertexPatch_Standard(input);
	return BuildVSOUT(deformedVertex, input);
}
