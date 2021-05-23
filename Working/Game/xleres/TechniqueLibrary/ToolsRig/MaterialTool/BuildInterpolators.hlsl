// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BUILD_INTERPOLATORS)
#define BUILD_INTERPOLATORS

#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../Math/TextureAlgorithm.hlsl"		// for SystemInputs
#include "../../LightingEngine/LightDesc.hlsl"		// for LightScreenDest
#include "../../SceneEngine/Lighting/resolvefog.hlsl"

//////////////////////////////////////////////////////////////////

float3 BuildInterpolator_WORLDPOSITION(VSIN input)
{
	#if defined(GEO_PRETRANSFORMED)
		return VSIN_GetLocalPosition(input).xyz;
	#else
		float3 localPosition = VSIN_GetLocalPosition(input);
		return mul(SysUniform_GetLocalToWorld(), float4(localPosition,1)).xyz;
	#endif
}

float4 BuildInterpolator_COLOR(VSIN input)
{
	#if (MAT_VCOLOR_IS_ANIM_PARAM!=1)
		return VSIN_GetColor0(input);
	#else
		return 1.0.xxxx;
	#endif
}

float2 BuildInterpolator_TEXCOORD(VSIN input) { return VSIN_GetTexCoord0(input); }

float4 BuildInterpolator_COLOR0(VSIN input) { return BuildInterpolator_COLOR(input); }
float2 BuildInterpolator_TEXCOORD0(VSIN input) { return BuildInterpolator_TEXCOORD(input); }

float3 BuildInterpolator_WORLDVIEWVECTOR(VSIN input)
{
	return SysUniform_GetWorldSpaceView().xyz - BuildInterpolator_WORLDPOSITION(input);
}

float4 BuildInterpolator_LOCALTANGENT(VSIN input)
{
	return VSIN_GetLocalTangent(input);
}

float3 BuildInterpolator_LOCALBITANGENT(VSIN input)
{
	return VSIN_GetLocalBitangent(input);
}

VSOUT BuildInterpolator_VSOutput(VSIN input) : NE_WritesVSOutput
{
	VSOUT output;
	float3 localPosition = VSIN_GetLocalPosition(input);
	float3 worldPosition = BuildInterpolator_WORLDPOSITION(input);
	float3 worldNormal = LocalToWorldUnitVector(VSIN_GetLocalNormal(input));

	#if VSOUT_HAS_COLOR>=1
		output.color = BuildInterpolator_COLOR0(input);
	#endif

	#if VSOUT_HAS_TEXCOORD>=1
		output.texCoord = VSIN_GetTexCoord0(input);
	#endif

	#if GEO_HAS_TEXTANGENT==1
		TangentFrame worldSpaceTangentFrame = VSIN_GetWorldTangentFrame(input);

		#if VSOUT_HAS_TANGENT_FRAME==1
			output.tangent = worldSpaceTangentFrame.tangent;
			output.bitangent = worldSpaceTangentFrame.bitangent;
		#endif

		#if GEO_HAS_NORMAL==0
			worldNormal = worldSpaceTangentFrame.normal;
		#endif
	#endif

	float3 worldViewVector = BuildInterpolator_WORLDVIEWVECTOR(input);
	float3 localNormal = VSIN_GetLocalNormal(input);

	#if (MAT_DOUBLE_SIDED_LIGHTING==1)
		if (dot(worldNormal, worldViewVector) < 0.f) {
			worldNormal *= -1.f;
			localNormal *= -1.f;
		}
	#endif

	#if (VSOUT_HAS_NORMAL==1)
		output.normal = worldNormal;
	#endif

	#if defined(GEO_PRETRANSFORMED)
		output.position = float4(VSIN_GetLocalPosition(input).xyz, 1);
	#else
		output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME==1
		output.localTangent = BuildInterpolator_LOCALTANGENT(input);
		output.localBitangent = BuildInterpolator_LOCALBITANGENT(input);
	#endif

	#if (VSOUT_HAS_LOCAL_NORMAL==1)
		output.localNormal = localNormal;
	#endif

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR==1
		output.localViewVector = SysUniform_GetLocalSpaceView().xyz - localPosition.xyz;
	#endif

	#if VSOUT_HAS_WORLD_VIEW_VECTOR==1
		output.worldViewVector = worldViewVector;
	#endif

	#if VSOUT_HAS_WORLD_POSITION==1
		output.worldPosition = worldPosition.xyz;
	#endif

	#if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = ResolveOutputFogColor(worldPosition.xyz, SysUniform_GetWorldSpaceView().xyz);
	#endif

	#if (VSOUT_HAS_PER_VERTEX_AO==1) && (GEO_HAS_INSTANCE_ID==1)
		output.ambientOcclusion = 1.f; // GetInstanceShadowing(input);
	#endif

	#if (VSOUT_HAS_INSTANCE_ID==1) && (GEO_HAS_INSTANCE_ID==1)
		output.instanceId = input.instanceId;
	#endif

	return output;
}

//////////////////////////////////////////////////////////////////

LightScreenDest BuildSystem_LightScreenDest(VSOUT input, SystemInputs sys)
{
	return LightScreenDest_Create(int2(input.position.xy), GetSampleIndex(sys));
}

SystemInputs BuildSystem_SystemInputs(VSOUT input, SystemInputs sys)
{
	return sys;
}

//////////////////////////////////////////////////////////////////

#if SHADER_NODE_EDITOR_CHART==1
	float2 NormInterp(float2 a) { return 0.5.xx + 0.5f * a.xx; }
#else
	float2 NormInterp(float2 a) { return 0.5.xx + 0.5f * a; }
#endif

float InterpolateVariable_AutoAxis(float lhs, float rhs, float2 a)
{
	return lerp(lhs.x, rhs.x, NormInterp(a).x);
}

float2 InterpolateVariable_AutoAxis(float2 lhs, float2 rhs, float2 a)
{
	return float2(lerp(lhs.x, rhs.x, NormInterp(a).x), lerp(lhs.y, rhs.y, NormInterp(a).y));
}

float3 InterpolateVariable_AutoAxis(float3 lhs, float3 rhs, float2 a)
{
	return float3(lerp(lhs.x, rhs.x, NormInterp(a).x), lerp(lhs.y, rhs.y, NormInterp(a).y), lerp(lhs.z, rhs.z, NormInterp(a).x));
}

float4 InterpolateVariable_AutoAxis(float4 lhs, float4 rhs, float2 a)
{
	return float4(lerp(lhs.x, rhs.x, NormInterp(a).x), lerp(lhs.y, rhs.y, NormInterp(a).y), lerp(lhs.z, rhs.z, NormInterp(a).x), lerp(lhs.w, rhs.w, NormInterp(a).y));
}

float  InterpolateVariable_XAxis(float  lhs, float  rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).x); }
float2 InterpolateVariable_XAxis(float2 lhs, float2 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).x); }
float3 InterpolateVariable_XAxis(float3 lhs, float3 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).x); }
float4 InterpolateVariable_XAxis(float4 lhs, float4 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).x); }
float  InterpolateVariable_YAxis(float  lhs, float  rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).y); }
float2 InterpolateVariable_YAxis(float2 lhs, float2 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).y); }
float3 InterpolateVariable_YAxis(float3 lhs, float3 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).y); }
float4 InterpolateVariable_YAxis(float4 lhs, float4 rhs, float2 a) { return lerp(lhs, rhs, NormInterp(a).y); }

float3 BuildRefractionNormal(VSOUT geo, SystemInputs sys) { return -VSOUT_GetNormal(geo); }
float3 BuildRefractionIncident(VSOUT geo, SystemInputs sys) { return normalize(float3(1.0f, 0.15f, 0.0f)); }
float3 BuildRefractionOutgoing(VSOUT geo, SystemInputs sys)
{
	float3 worldSpacePosition = VSOUT_GetNormal(geo);
	return normalize(SysUniform_GetWorldSpaceView().xyz - worldSpacePosition);
}

#endif
