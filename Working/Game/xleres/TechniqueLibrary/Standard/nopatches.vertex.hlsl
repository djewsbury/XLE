#include "../Framework/VSIN.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/WorkingVertex.hlsl"
#include "VertexPatch_Standard.vertex.hlsl"
#include "../Core/BuildVSOUT.vertex.hlsl"
#include "../../Nodes/Templates.vertex.sh"

VSOUT main(VSIN input)
{
	WorkingVertex deformedVertex = VertexPatch_Standard(input);
	return BuildVSOUT(deformedVertex, input);
}
