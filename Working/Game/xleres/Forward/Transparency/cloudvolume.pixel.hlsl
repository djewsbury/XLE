// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Framework/MainGeometry.hlsl"
#include "../../TechniqueLibrary/Framework/CommonResources.hlsl"
#include "../../TechniqueLibrary/Framework/Surface.hlsl"
#include "../../TechniqueLibrary/Utility/Colour.hlsl"
#include "../../TechniqueLibrary/Math/perlinnoise.hlsl"
#include "../../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"

struct FragmentListNode
{
	uint	next;
	float	depth;
};

RWTexture2D<uint>						FragmentIds : register(u1);
RWStructuredBuffer<FragmentListNode>	NodesList : register(u2);
Texture2D<float>						DuplicateOfDepthBuffer : register(t17);

void OutputFragmentNode(uint2 position, float4 color, float depth)
{
		//
		//	todo -- we could output a self balancing binary search tree...
		//			just might reduce the workload while resolving the sort
		//			order (particularly if we have a lot of elements, and need
		//			to do a partial sort)
		//				-- but it would require read/write access to NodesList,
		//					which might not be thread safe.
		//
	uint newNodeId = NodesList.IncrementCounter();
	uint oldNodeId;
	InterlockedExchange(FragmentIds[position], newNodeId, oldNodeId);

	FragmentListNode newNode;
	newNode.next = oldNodeId;
	newNode.depth = depth;
	NodesList[newNodeId] = newNode;
}

[earlydepthstencil]
	float4 main(VSOUT geo, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
	float4 result = 1.0.xxxx;
		//	use a negative depth value to mark back faces.
		//	This is important if the cloud volume intersects with opaque geometry
		//		--	in these cases, some samples will be rejected by the depth buffer,
		//			and so a back face might be missed. As a result, we need to know
		//			what are front faces, and what are back faces -- so we can compensate
		//			for missed faces in the resolve step (also helpful if the camera is
		//			within the volume)
	float depth = isFrontFace ? geo.position.z : -geo.position.z;
	OutputFragmentNode(uint2(geo.position.xy), result, depth);
	discard;
	return result;
}
