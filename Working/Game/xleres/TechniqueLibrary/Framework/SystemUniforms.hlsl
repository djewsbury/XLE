// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(TRANSFORM_H)
#define TRANSFORM_H

#include "../Framework/Binding.hlsl"

cbuffer GlobalTransform BIND_SEQ_B0
{
	row_major float4x4 WorldToClip;
	float4 FrustumCorners[4];
	float3 WorldSpaceView;
    float FarClip;
	float4 MinimalProjection;
    row_major float4x4 CameraBasis;
	row_major float4x4 PrevWorldToClip;
}

float4x4 	SysUniform_GetWorldToClip() { return WorldToClip; }
float4 		SysUniform_GetFrustumCorners(int cornerIdx) { return FrustumCorners[cornerIdx]; }
float3 		SysUniform_GetWorldSpaceView() { return WorldSpaceView; }
float 		SysUniform_GetFarClip() { return abs(FarClip); }
float4 		SysUniform_GetMinimalProjection() { return MinimalProjection; }
float4x4 	SysUniform_GetCameraBasis() { return CameraBasis; }
bool 		SysUniform_IsOrthogonalProjection() { return FarClip < 0; }
float4x4 	SysUniform_GetPrevWorldToClip() { return PrevWorldToClip; }

#if defined(VULKAN) && !defined(LOCAL_TRANSFORM_PUSH_CONSTANTS)
	#define LOCAL_TRANSFORM_PUSH_CONSTANTS 1
#endif

#if LOCAL_TRANSFORM_PUSH_CONSTANTS
	[[vk::push_constant]] struct LocalTransformStruct
	{
		row_major float3x4 LocalToWorld;
		float3 LocalSpaceView;
		#if VERTEX_ID_VIEW_INSTANCING
			uint ViewMask;
		#endif
	} LocalTransform;

	// note -- these are only available in the vertex shader due to the pipeline layout configuration
	float3x4 	SysUniform_GetLocalToWorld() { return LocalTransform.LocalToWorld; }
	float3 		SysUniform_GetLocalSpaceView() { return LocalTransform.LocalSpaceView; }
#else
	cbuffer LocalTransform BIND_NUMERIC_B3
	{
		row_major float3x4 LocalToWorld;
		float3 LocalSpaceView;
		#if VERTEX_ID_VIEW_INSTANCING
			uint ViewMask;
		#endif
	}

	float3x4 	SysUniform_GetLocalToWorld() { return LocalToWorld; }
	float3 		SysUniform_GetLocalSpaceView() { return LocalSpaceView; }
#endif

cbuffer GlobalState BIND_SEQ_B2
{
	float GlobalTime;
	uint GlobalSamplingPassIndex;
	uint GlobalSamplingPassCount;
}

float 		SysUniform_GetGlobalTime() { return GlobalTime; }
uint 		SysUniform_GetGlobalSamplingPassIndex() { return GlobalSamplingPassIndex; }
uint 		SysUniform_GetGlobalSamplingPassCount() { return GlobalSamplingPassCount; }

cbuffer ReciprocalViewportDimensionsCB BIND_SEQ_B1
{
	float2 ReciprocalViewportDimensions;
	float2 ViewportMinXY;
	float2 ViewportWidthHeight;
}

float2 		SysUniform_ReciprocalViewportDimensions() { return ReciprocalViewportDimensions; }
float2 		SysUniform_GetViewportMinXY() { return ViewportMinXY; }
float2 		SysUniform_GetViewportWidthHeight() { return ViewportWidthHeight; }

#endif
