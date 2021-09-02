// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/DeformVertex.hlsl"
#include "../SceneEngine/Lighting/resolvefog.hlsl"

#if VERTEX_ID_VIEW_INSTANCING
	cbuffer MultiViewProperties BIND_SEQ_B5
	{
		row_major float4x4 MultiViewWorldToClip[64];
	}
	uint GetMultiViewIndex(uint instanceId)
	{
		// Find the position of the instanceId'th bit set in the view mask
		// this has a little processing to the vertex shader, but we gain something
		// by avoiding having to store an array of view indices in LocalTransform 
		uint mask = LocalTransform.ViewMask;
		while (instanceId) {
			mask ^= 1u << firstbithigh(mask);
			--instanceId;
		}
		return firstbithigh(mask);
	}
#endif

VSOUT BuildVSOUT(
	DeformedVertex deformedVertex,
	VSIN input)
{
	float3 worldPosition;
	TangentFrame worldSpaceTangentFrame;

	if (deformedVertex.coordinateSpace == 0) {
		worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
	 	worldSpaceTangentFrame = TransformLocalToWorld(deformedVertex.tangentFrame, VSIN_TangentVectorToReconstruct());
	} else {
		worldPosition = deformedVertex.position;
		worldSpaceTangentFrame = deformedVertex.tangentFrame;
	}

	VSOUT output;
	#if !VERTEX_ID_VIEW_INSTANCING
		output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

		#if VSOUT_HAS_PREV_POSITION
			output.prevPosition = mul(SysUniform_GetPrevWorldToClip(), float4(worldPosition,1));
		#endif
	#else
		uint viewIdx = GetMultiViewIndex(input.instanceId);
		output.position = mul(MultiViewWorldToClip[viewIdx], float4(worldPosition,1));

		#if (VSOUT_HAS_RENDER_TARGET_INDEX==1)
			output.renderTargetIndex = viewIdx;
		#endif

		#if VSOUT_HAS_PREV_POSITION
			// We could store a prev world-to-clip in the multi probe array, but
			// generally we don't need it with probe rendering, so avoid the overhead
			#error Prev position not supported with multi-probe rendering
		#endif
	#endif

	#if VSOUT_HAS_COLOR>=1
		output.color = VSIN_GetColor0(input);
	#endif

	#if VSOUT_HAS_TEXCOORD>=1
		output.texCoord = VSIN_GetTexCoord0(input);
	#endif

	#if VSOUT_HAS_TANGENT_FRAME==1
		output.tangent = worldSpaceTangentFrame.tangent;
		output.bitangent = worldSpaceTangentFrame.bitangent;
	#endif

	#if (VSOUT_HAS_NORMAL==1)
		output.normal = worldSpaceTangentFrame.normal;
	#endif

	#if VSOUT_HAS_WORLD_POSITION==1
		output.worldPosition = worldPosition;
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		if (deformedVertex.coordinateSpace == 0) {
			output.localTangent = deformedVertex.tangentFrame.tangent;
			output.localBitangent = deformedVertex.tangentFrame.bitangent;
		} else {
			output.localTangent = VSIN_GetLocalTangent(input);
			output.localBitangent = VSIN_GetLocalBitangent(input);
		}
	#endif

	#if (VSOUT_HAS_LOCAL_NORMAL==1)
		if (deformedVertex.coordinateSpace == 0) {
			output.localNormal = deformedVertex.tangentFrame.normal;
		} else {
			output.localNormal = VSIN_GetLocalNormal(input);
		}
	#endif

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR==1
		output.localViewVector = SysUniform_GetLocalSpaceView().xyz - deformedVertex.localPosition.xyz;
	#endif

	#if VSOUT_HAS_WORLD_VIEW_VECTOR==1
		output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
	#endif

	#if (VSOUT_HAS_PER_VERTEX_AO==1)
		output.ambientOcclusion = 1.f;
		#if (GEO_HAS_PER_VERTEX_AO==1)
			output.ambientOcclusion = input.ambientOcclusion;
		#endif
	#endif

	#if (VSOUT_HAS_PER_VERTEX_MLO==1)
		output.mainLightOcclusion = 1.f;
		#if (SPAWNED_INSTANCE==1)
			output.mainLightOcclusion *= GetInstanceShadowing(input);
		#endif
	#endif

	#if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = ResolveOutputFogColor(worldPosition.xyz, SysUniform_GetWorldSpaceView().xyz);
	#endif

	return output;
}
