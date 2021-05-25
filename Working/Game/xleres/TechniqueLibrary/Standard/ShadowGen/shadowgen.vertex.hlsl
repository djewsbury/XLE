// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowGenGeometryConfiguration.hlsl"
#include "../../LightingEngine/ShadowProjection.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/VSIN.hlsl"
#include "../../Framework/VSOUT.hlsl"
#include "../../Framework/VSShadowOutput.hlsl"
#include "../../Framework/DeformVertex.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../../Nodes/Templates.vertex.sh"

#if !defined(SHADOW_CASCADE_MODE)
	#error expecting SHADOW_CASCADE_MODE to be set
#endif

VSShadowOutput BuildVSShadowOutput(
	DeformedVertex deformedVertex,
	VSIN input)
{
	float3 worldPosition;
	if (deformedVertex.coordinateSpace == 0) {
		worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
	} else {
		worldPosition = deformedVertex.position;
	}

	VSShadowOutput result;

	#if VSOUT_HAS_TEXCOORD>=1
		result.texCoord = input.texCoord;
	#endif

	result.shadowFrustumFlags = 0;

	uint count = min(GetShadowSubProjectionCount(), VSOUT_HAS_SHADOW_PROJECTION_COUNT);

	#if SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ARBITRARY || SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_CUBEMAP
///////////////////////////////////////////////////////////////////////////////////////////////////

		result.position = float4(worldPosition.xyz, 1);

		#if (VSOUT_HAS_SHADOW_PROJECTION_COUNT>0)
			for (uint c=0; c<count; ++c) {
				float4 p = ShadowProjection_GetOutput(worldPosition, c);
				bool	left	= p.x < -p.w,
						right	= p.x >  p.w,
						top		= p.y < -p.w,
						bottom	= p.y >  p.w;
				result.shadowFrustumFlags |= (left | (right<<1) | (top<<2) | (bottom<<3)) << (c*4);
			}
		#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
	#elif SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ORTHOGONAL
///////////////////////////////////////////////////////////////////////////////////////////////////

		float3 basePosition = mul(OrthoShadowWorldToView, float4(worldPosition, 1));

		result.position = float4(basePosition, 1);
		for (uint c=0; c<count; ++c) {
			float3 cascade = AdjustForOrthoCascade(basePosition, c);
			bool	left	= cascade.x < -1.f,
					right	= cascade.x >  1.f,
					top		= cascade.y < -1.f,
					bottom	= cascade.y >  1.f;

			result.shadowFrustumFlags |=
				(left | (right<<1) | (top<<2) | (bottom<<3)) << (c*4);
		}

///////////////////////////////////////////////////////////////////////////////////////////////////
	#endif

	return result;
}

VSShadowOutput nopatches(VSIN input)
{
	DeformedVertex deformedVertex = DeformedVertex_Initialize(input);
	return BuildVSShadowOutput(deformedVertex, input);
}

VSShadowOutput frameworkEntryWithDeformVertex(VSIN input)
{
	DeformedVertex deformedVertex = DeformedVertex_Initialize(input);
	deformedVertex = DeformVertex(deformedVertex, input);
	return BuildVSShadowOutput(deformedVertex, input);
}
