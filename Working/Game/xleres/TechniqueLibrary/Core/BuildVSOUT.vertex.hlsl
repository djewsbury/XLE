// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/SystemUniforms.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Framework/VSIN.hlsl"
#include "../Framework/DeformVertex.hlsl"
#include "../SceneEngine/Lighting/resolvefog.hlsl"

VSOUT BuildVSOUT(
	DeformedVertex deformedVertex,
	VSIN input)
{
	float3 worldPosition;
	TangentFrame worldSpaceTangentFrame;

	if (deformedVertex.coordinateSpace == 0) {
		worldPosition = mul(SysUniform_GetLocalToWorld(), float4(deformedVertex.position,1)).xyz;
	 	worldSpaceTangentFrame = AsTangentFrame(TransformLocalToWorld(deformedVertex.tangentFrame));
	} else {
		worldPosition = deformedVertex.position;
		worldSpaceTangentFrame = AsTangentFrame(deformedVertex.tangentFrame);
	}

	VSOUT output;
	output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

	#if VSOUT_HAS_PREV_POSITION
		output.prevPosition = mul(SysUniform_GetPrevWorldToClip(), float4(worldPosition,1));
	#endif

	#if VSOUT_HAS_COLOR>=1
		output.color = VSIN_GetColor0(input);
	#endif

	#if VSOUT_HAS_TEXCOORD>=1
		output.texCoord = VSIN_GetTexCoord0(input);
	#endif

	#if GEO_HAS_TEXTANGENT==1
		#if VSOUT_HAS_TANGENT_FRAME==1
			output.tangent = worldSpaceTangentFrame.tangent;
			output.bitangent = worldSpaceTangentFrame.bitangent;
		#endif

		#if (VSOUT_HAS_NORMAL==1)
			output.normal = worldSpaceTangentFrame.normal;
		#endif
	#else
		#if (VSOUT_HAS_NORMAL==1)
			output.normal = mul(GetLocalToWorldUniformScale(), VSIN_GetLocalNormal(input));
		#endif
	#endif

	#if VSOUT_HAS_WORLD_POSITION==1
		output.worldPosition = worldPosition;
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		if (deformedVertex.coordinateSpace == 0) {
			TangentFrame localTangentFrame = AsTangentFrame(deformedVertex.tangentFrame);
			output.localTangent = localTangentFrame.tangent;
			output.localBitangent = localTangentFrame.bitangent;
			#if (VSOUT_HAS_LOCAL_NORMAL==1)
				output.localNormal = localTangentFrame.normal;
			#endif
		} else {
			output.localTangent = VSIN_GetLocalTangent(input);
			output.localBitangent = VSIN_GetLocalBitangent(input);
			#if (VSOUT_HAS_LOCAL_NORMAL==1)
				output.localNormal = VSIN_GetLocalNormal(input);
			#endif
		}
	#else
		#if (VSOUT_HAS_LOCAL_NORMAL==1)
			if (deformedVertex.coordinateSpace == 0) {
				TangentFrame localTangentFrame = AsTangentFrame(deformedVertex.tangentFrame);
				output.localNormal = localTangentFrame.normal;
			} else {
				output.localNormal = VSIN_GetLocalNormal(input);
			}
		#endif
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
		#if (GEO_HAS_INSTANCE_ID==1)
			output.mainLightOcclusion *= GetInstanceShadowing(input);
		#endif
	#endif

	#if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = ResolveOutputFogColor(worldPosition.xyz, SysUniform_GetWorldSpaceView().xyz);
	#endif

	return output;
}
