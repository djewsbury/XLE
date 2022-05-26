// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/WorkingVertex.hlsl"
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
	WorkingVertex workingVertex,
	VSIN input)
{
	float3 worldPosition;
	TangentFrame worldSpaceTangentFrame;

	if (workingVertex.coordinateSpace == 0) {
		worldPosition = mul(SysUniform_GetLocalToWorld(), float4(workingVertex.position,1)).xyz;
	 	worldSpaceTangentFrame = TransformLocalToWorld(workingVertex.tangentFrame, workingVertex.tangentVectorToReconstruct);
	} else {
		worldPosition = workingVertex.position;
		worldSpaceTangentFrame = workingVertex.tangentFrame;
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

	
	#if VSOUT_HAS_COLOR_LINEAR
		if (workingVertex.colorCount >= 1) {
			output.color.rgb = workingVertex.color0.rgb;
			#if VSOUT_HAS_VERTEX_ALPHA
				output.color.a = workingVertex.color0.a;
			#endif
		} else {
			output.color = float4(1,1,1,1);
		}
	#elif VSOUT_HAS_VERTEX_ALPHA
		if (workingVertex.colorCount >= 1) {
			output.alpha = workingVertex.color0.a;
		} else {
			output.alpha = 1;
		}
	#endif

	#if VSOUT_HAS_COLOR_LINEAR1
		if (workingVertex.colorCount >= 2) {
			output.color1 = workingVertex.color1;
		} else {
			output.color1 = float4(1,1,1,1);
		}
	#endif

	#if VSOUT_HAS_TEXCOORD
		if (workingVertex.texCoordCount >= 1) {
			output.texCoord = workingVertex.texCoord0;
		} else {
			output.texCoord = float2(0,0);
		}
	#endif

	#if VSOUT_HAS_TEXCOORD1
		if (workingVertex.texCoordCount >= 2) {
			output.texCoord1 = workingVertex.texCoord1;
		} else {
			output.texCoord1 = float2(0,0);
		}
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
		if (workingVertex.coordinateSpace == 0) {
			output.localTangent = workingVertex.tangentFrame.tangent;
			output.localBitangent = workingVertex.tangentFrame.bitangent;
		} else {
			// not ideal, but we have to guess by going back to VSIN
			output.localTangent = VSIN_GetEncodedTangent(input);
			output.localBitangent = VSIN_GetEncodedBitangent(input);
		}
	#endif

	#if VSOUT_HAS_LOCAL_NORMAL
		if (workingVertex.coordinateSpace == 0) {
			output.localNormal = workingVertex.tangentFrame.normal;
		} else {
			// not ideal, but we have to guess by going back to VSIN
			output.localNormal = VSIN_GetEncodedNormal(input);
		}
	#endif

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR
		output.localViewVector = SysUniform_GetLocalSpaceView().xyz - workingVertex.localPosition.xyz;
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
	#endif

	#if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = ResolveOutputFogColor(worldPosition.xyz, SysUniform_GetWorldSpaceView().xyz);
	#endif

	#if VSOUT_HAS_VERTEX_ID
		output.vertexId = input.vertexId;
	#endif

	return output;
}
