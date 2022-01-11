// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShadowGenGeometryConfiguration.hlsl"
#include "../../LightingEngine/ShadowProjection.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/VSIN.hlsl"
#include "../../Framework/VSOUT.hlsl"
#include "../../Framework/VSShadowOutput.hlsl"
#include "../../Framework/WorkingVertex.hlsl"
#include "../../Math/ProjectionMath.hlsl"
#include "../../../Nodes/Templates.vertex.sh"

#if !defined(SHADOW_CASCADE_MODE)
	#error expecting SHADOW_CASCADE_MODE to be set
#endif

VSShadowOutput BuildVSShadowOutput(
	WorkingVertex deformedVertex,
	VSIN input)
{
	float3 worldPosition;
	if (deformedVertex.coordinateSpace == 0) {
		worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
	} else {
		worldPosition = deformedVertex.position;
	}

	VSShadowOutput result;

	#if VSOUT_HAS_TEXCOORD
		result.texCoord = input.texCoord;
	#endif
	#if VSOUT_HAS_VERTEX_ALPHA
		result.alpha = VSIN_GetColor0(input).a;
	#endif

	result.shadowFrustumFlags = 0;

	#if VSOUT_HAS_SHADOW_PROJECTION_COUNT == 0
		#error Zero projection count in shadowgen.vertex.hlsl
	#elif VSOUT_HAS_SHADOW_PROJECTION_COUNT == 1
		uint count = 1;
	#else
		uint count = min(GetShadowSubProjectionCount(), VSOUT_HAS_SHADOW_PROJECTION_COUNT);
	#endif

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
				result.shadowFrustumFlags |= (left | (right<<1u) | (top<<2u) | (bottom<<3u)) << (c*4u);
			}
		#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
	#elif SHADOW_CASCADE_MODE==SHADOW_CASCADE_MODE_ORTHOGONAL
///////////////////////////////////////////////////////////////////////////////////////////////////

		float3 basePosition = mul(OrthoShadowWorldToView, float4(worldPosition, 1));

		result.position = float4(basePosition, 1);
		[unroll] for (uint c=0; c<count; ++c) {
			float3 cascade = AdjustForOrthoCascade(basePosition, c);
			bool	left	= cascade.x < -1.f,
					right	= cascade.x >  1.f,
					top		= cascade.y < -1.f,
					bottom	= cascade.y >  1.f;
			result.shadowFrustumFlags |= (left | (right<<1u) | (top<<2u) | (bottom<<3u)) << (c*4u);

			if (c == (count-1)) {
				// we use this to finish looping through frustums, by extinguishing all geometry 
				// at the final frustum
				result.shadowFrustumFlags |= 1u<<(VSOUT_HAS_SHADOW_PROJECTION_COUNT*4u+c);
			} else {
				// Shrink the edges of the frustum by (1*max blur radius for this cascade)+(1*max blur radius for next cascade)
				// This will allow the next cascade to do a max blur right at the transition point, and the geometry will
				// still be there for it. In practice we reduce the max blur during transition, but let's be conservative
				// const uint maxBlurSearchInPix = 32;
				// const uint halfTextureSize = 512;
				// float borderRegionPix = maxBlurSearchInPix * (1 + OrthoShadowCascadeScale[c].x / OrthoShadowCascadeScale[c+1].x);		// assuming ratios in x & y are the same
				// const float fullyInsidePoint = float(halfTextureSize-borderRegionPix) / float(halfTextureSize);
				float borderRegionNorm = 2.f*ProjectionMaxBlurRadiusNorm * (1.f + OrthoShadowCascadeScale[c].x / OrthoShadowCascadeScale[c+1].x);		// assuming ratios in x & y are the same
				const float fullyInsidePoint = 1.f - borderRegionNorm;
				if (PtInFrustumXY(float4(cascade.xy, 0, fullyInsidePoint)))
					result.shadowFrustumFlags |= 1u<<(VSOUT_HAS_SHADOW_PROJECTION_COUNT*4u+c);
			}
		}

///////////////////////////////////////////////////////////////////////////////////////////////////
	#endif

	return result;
}

VSShadowOutput nopatches(VSIN input)
{
	WorkingVertex deformedVertex = WorkingVertex_DefaultInitialize(input);
	return BuildVSShadowOutput(deformedVertex, input);
}

VSShadowOutput frameworkEntryWithVertexPatch(VSIN input)
{
	WorkingVertex deformedVertex = VertexPatch(input);
	return BuildVSShadowOutput(deformedVertex, input);
}
