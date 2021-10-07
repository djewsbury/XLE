// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/DeformVertex.hlsl"
#include "../SceneEngine/Lighting/resolvefog.hlsl"
#include "../Utility/Colour.hlsl"

#if VERTEX_ID_VIEW_INSTANCING
	cbuffer MultiViewProperties BIND_SEQ_B3
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

		#if VSOUT_HAS_RENDER_TARGET_INDEX
			output.renderTargetIndex = viewIdx;
		#endif

		#if VSOUT_HAS_PREV_POSITION
			// We could store a prev world-to-clip in the multi probe array, but
			// generally we don't need it with probe rendering, so avoid the overhead
			#error Prev position not supported with multi-probe rendering
		#endif
	#endif

	// Note that we're kind of forced to do srgb -> linear conversion here, because we'll loose precision
	// assuming 8 bit color inputs	
	#if VSOUT_HAS_COLOR_LINEAR
		output.color.rgb = SRGBToLinear(VSIN_GetColor0(input).rgb);
		#if VSOUT_HAS_VERTEX_ALPHA
			output.color.a = VSIN_GetColor0(input).a;
		#endif
	#elif VSOUT_HAS_VERTEX_ALPHA
		output.alpha = VSIN_GetColor0(input).a;
	#endif

	#if VSOUT_HAS_COLOR_LINEAR1
		output.color1 = VSIN_GetColor1(input);
	#endif

	#if VSOUT_HAS_TEXCOORD
		output.texCoord = VSIN_GetTexCoord0(input);
	#endif

	#if VSOUT_HAS_TEXCOORD1
		output.texCoord1 = VSIN_GetTexCoord1(input);
	#endif

	#if VSOUT_HAS_TANGENT_FRAME
		output.tangent = worldSpaceTangentFrame.tangent;
		output.bitangent = worldSpaceTangentFrame.bitangent;
	#endif

	#if VSOUT_HAS_NORMAL
		output.normal = worldSpaceTangentFrame.normal;
	#endif

	#if VSOUT_HAS_WORLD_POSITION
		output.worldPosition = worldPosition;
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME
		if (deformedVertex.coordinateSpace == 0) {
			output.localTangent = deformedVertex.tangentFrame.tangent;
			output.localBitangent = deformedVertex.tangentFrame.bitangent;
		} else {
			output.localTangent = VSIN_GetLocalTangent(input);
			output.localBitangent = VSIN_GetLocalBitangent(input);
		}
	#endif

	#if VSOUT_HAS_LOCAL_NORMAL
		if (deformedVertex.coordinateSpace == 0) {
			output.localNormal = deformedVertex.tangentFrame.normal;
		} else {
			output.localNormal = VSIN_GetLocalNormal(input);
		}
	#endif

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR
		output.localViewVector = SysUniform_GetLocalSpaceView().xyz - deformedVertex.localPosition.xyz;
	#endif

	#if VSOUT_HAS_WORLD_VIEW_VECTOR
		output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
	#endif

	#if VSOUT_HAS_PER_VERTEX_AO
		output.ambientOcclusion = 1.f;
		#if GEO_HAS_PER_VERTEX_AO
			output.ambientOcclusion = input.ambientOcclusion;
		#endif
	#endif

	#if VSOUT_HAS_PER_VERTEX_MLO
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
