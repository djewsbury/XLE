// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(VSOUT_H)
#define VSOUT_H

#include "../Math/SurfaceAlgorithm.hlsl"

struct VSOUT /////////////////////////////////////////////////////
{
	float4 position : SV_Position;
	#if VSOUT_HAS_COLOR>=2
		float3 color : COLOR0;
	#elif VSOUT_HAS_COLOR>=1
		float4 color : COLOR0;
	#endif

	#if VSOUT_HAS_COLOR1>=1
		float4 color1 : COLOR1;
	#endif

	#if VSOUT_HAS_TEXCOORD>=1
		float2 texCoord : TEXCOORD0;
	#endif

	#if VSOUT_HAS_TEXCOORD1>=1
		float2 texCoord1 : TEXCOORD1;
	#endif

	#if VSOUT_HAS_TANGENT_FRAME==1
		float3 tangent : TEXTANGENT;
		float3 bitangent : TEXBITANGENT;
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		float4 localTangent : LOCALTANGENT;
		float3 localBitangent : LOCALBITANGENT;
	#endif

	#if (VSOUT_HAS_NORMAL==1)
		float3 normal : NORMAL;
	#endif

	#if (VSOUT_HAS_LOCAL_NORMAL==1)
		float3 localNormal : LOCALNORMAL;
	#endif

	#if (VSOUT_HAS_LOCAL_VIEW_VECTOR==1)
		float3 localViewVector : LOCALVIEWVECTOR;
	#endif

	#if (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
		float3 worldViewVector : WORLDVIEWVECTOR;
	#endif

	#if (VSOUT_HAS_PRIMITIVE_ID==1)
		nointerpolation uint primitiveId : SV_PrimitiveID;
	#endif

	#if (VSOUT_HAS_RENDER_TARGET_INDEX==1)
		nointerpolation uint renderTargetIndex : SV_RenderTargetArrayIndex;
	#endif

	#if (VSOUT_HAS_WORLD_POSITION==1)
		float3 worldPosition : WORLDPOSITION;
	#endif

	#if (VSOUT_HAS_PREV_POSITION==1)
		float4 prevPosition : PREVPOSITION;
	#endif

	#if (VSOUT_HAS_BLEND_TEXCOORD==1)
		float3 blendTexCoord : TEXCOORD1;
	#endif

	#if (VSOUT_HAS_FOG_COLOR==1)
		float4 fogColor : FOGCOLOR;
	#endif

	#if (VSOUT_HAS_PER_VERTEX_AO==1)
		float ambientOcclusion : AMBIENTOCCLUSION;
	#endif

	#if (VSOUT_HAS_PER_VERTEX_MLO==1)
		float mainLightOcclusion : MAINLIGHTOCCLUSION;
	#endif

	#if (VSOUT_HAS_INSTANCE_ID==1)
		uint instanceId : SV_InstanceID;
	#endif
}; //////////////////////////////////////////////////////////////////

float2 VSOUT_GetTexCoord0(VSOUT geo)
{
	#if VSOUT_HAS_TEXCOORD>=1 /////////////////////////////////////////////
		return geo.texCoord;
	#else
		return 0.0.xx;
	#endif //////////////////////////////////////////////////////////////
}

float4 VSOUT_GetColor0(VSOUT geo)
{
	#if VSOUT_HAS_COLOR>=2 ////////////////////////////////////////////////
		return float4(geo.color.rgb, 1.f);
	#elif VSOUT_HAS_COLOR>=1
		return geo.color;
	#else
		return 1.0.xxxx;
	#endif //////////////////////////////////////////////////////////////
}

float3 VSOUT_GetWorldViewVector(VSOUT geo)
{
	#if VSOUT_HAS_WORLD_VIEW_VECTOR==1
		return geo.worldViewVector;
	#elif VSOUT_HAS_WORLD_POSITION==1
		return SysUniform_GetWorldSpaceView().xyz - geo.worldPosition;	// if we have either the world-view-world or world-position it's a bit redundant to have the other
	#else
		return 0.0.xxx;
	#endif
}

float3 VSOUT_GetLocalViewVector(VSOUT geo)
{
	#if VSOUT_HAS_LOCAL_VIEW_VECTOR==1
		return geo.localViewVector;
	#else
		return 0.0.xxx;
	#endif
}

float3 VSOUT_GetWorldPosition(VSOUT geo)
{
	#if VSOUT_HAS_WORLD_POSITION==1
		return geo.worldPosition;
	#elif VSOUT_HAS_WORLD_VIEW_VECTOR==1
		return SysUniform_GetWorldSpaceView().xyz - geo.worldViewVector;	// if we have either the world-view-world or world-position it's a bit redundant to have the other
	#else
		return 0.0.xxx;
	#endif
}

#if (VSOUT_HAS_TANGENT_FRAME==1)
	TangentFrame VSOUT_GetWorldTangentFrame(VSOUT geo)
	{
		TangentFrame result;
		result.tangent = geo.tangent.xyz;
		result.bitangent = geo.bitangent;
		result.normal = geo.normal;

		// note -- 	The denormalization caused by per vertex interpolation
		// 			is fairly subtle. We could perhaps skip this on all but
		//			the highest quality modes..?
		//			Also, there are other options:
		//				- higher order interpolation across the triangle using geometry shaders
		//				- using cotangent stuff particularly with derivative maps
		const bool doRenormalize = true;
		if (doRenormalize) {
			result.tangent = normalize(result.tangent);
			result.bitangent = normalize(result.bitangent);
			result.normal = normalize(result.normal);
		}
		result.handiness = 1.f; // (handiness value is lost in this case)
		return result;
	}
#endif

#if (VSOUT_HAS_LOCAL_TANGENT_FRAME==1)
	TangentFrame VSOUT_GetLocalTangentFrame(VSOUT geo)
	{
		TangentFrame result;
		result.tangent	 	= normalize(geo.localTangent.xyz);
		result.bitangent    = normalize(geo.localBitangent);
		result.handiness 	= GetWorldTangentFrameHandiness(geo.localTangent);
		#if (VSOUT_HAS_LOCAL_NORMAL)
			result.normal   = normalize(geo.localNormal);
		#else
				// note --  it's possible that the tangent and bitangent could
				//          fall out of alignment during edge interpolation. That
				//          could potentially result in a non-unit length normal
				//          (but it would also result in other subtle artefacts in
				//          the normal map. Let's try to cheat and avoid the normalize,
				//          (and just assume it's close to unit length)
			result.normal   = NormalFromTangents(result.tangent, result.bitangent, result.handiness);
		#endif
		return result;
	}
#endif

float3 VSOUT_GetVertexNormal(VSOUT geo)
{
	#if VSOUT_HAS_TANGENT_FRAME==1
		return normalize(geo.normal);
	#elif VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		return VSOUT_GetLocalTangentFrame(geo).normal;
	#elif (VSOUT_HAS_NORMAL==1)
		return normalize(geo.normal);
	#else
		return 0.0.xxx;
	#endif
}

float3 TransformTangentSpaceToWorld(float3 normalTextureSample, VSOUT geo)
{
	#if VSOUT_HAS_TANGENT_FRAME==1

		TangentFrame tangentFrame = VSOUT_GetWorldTangentFrame(geo);
		float3x3 normalsTextureToWorld = float3x3(tangentFrame.tangent.xyz, tangentFrame.bitangent, tangentFrame.normal);
		return mul(normalTextureSample, normalsTextureToWorld);

    #elif VSOUT_HAS_LOCAL_TANGENT_FRAME==1

		TangentFrame localTangentFrame = VSOUT_GetLocalTangentFrame(geo);
		float3x3 normalsTextureToLocal = float3x3(localTangentFrame.tangent.xyz, localTangentFrame.bitangent, localTangentFrame.normal);
		float3 localNormal = mul(normalTextureSample, normalsTextureToLocal);

            // note --  Problems when there is a scale on SysUniform_GetLocalToWorld() here.
            //          There are many objects with uniform scale values, and they require a normalize here.
            //          Ideally we'd have a SysUniform_GetLocalToWorld() matrix with the scale removed,
            //          or at least a "uniform scale" scalar to remove the scaling
        return normalize(mul(GetLocalToWorldUniformScale(), localNormal));

	#elif (VSOUT_HAS_NORMAL==1) && ((VSOUT_HAS_WORLD_VIEW_VECTOR==1) || (VSOUT_HAS_WORLD_VIEW_VECTOR==1)) && (VSOUT_HAS_TEXCOORD > 0)

	    float3x3 normalsTextureToWorld = AutoCotangentFrame(normalize(geo.normal), VSOUT_GetWorldViewVector(geo), geo.texCoord);
			// Note -- matrix multiply opposite from normal (so we can initialise normalsTextureToWorld easily)
		return mul(normalTextureSample, normalsTextureToWorld);

    #elif (VSOUT_HAS_LOCAL_NORMAL==1) && (VSOUT_HAS_LOCAL_VIEW_VECTOR==1) && (VSOUT_HAS_TEXCOORD > 0)

		float3x3 normalsTextureToWorld = AutoCotangentFrame(normalize(geo.localNormal), VSOUT_GetLocalViewVector(geo), geo.texCoord);
			// Note -- matrix multiply opposite from normal (so we can initialise normalsTextureToWorld easily)
		return mul(normalTextureSample, normalsTextureToWorld);

    #elif (VSOUT_HAS_NORMAL==1)

        return normalize(geo.normal);

	#else

		return 0.0.xxx;

	#endif
}

#endif

