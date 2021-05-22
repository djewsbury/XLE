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
}

float4x4 	SysUniform_GetWorldToClip() { return WorldToClip; }
float4 		SysUniform_GetFrustumCorners(int cornerIdx) { return FrustumCorners[cornerIdx]; }
float3 		SysUniform_GetWorldSpaceView() { return WorldSpaceView; }
float 		SysUniform_GetFarClip() { return abs(FarClip); }
float4 		SysUniform_GetMinimalProjection() { return MinimalProjection; }
float4x4 	SysUniform_GetCameraBasis() { return CameraBasis; }
bool 		SysUniform_IsOrthogonalProjection() { return FarClip < 0; }

cbuffer LocalTransform BIND_DRAW_B0
{
	row_major float3x4 LocalToWorld;
	float3 LocalSpaceView;
}

float3x4 	SysUniform_GetLocalToWorld() { return LocalToWorld; }
float3 		SysUniform_GetLocalSpaceView() { return LocalSpaceView; }

cbuffer GlobalState BIND_SEQ_B2
{
	float GlobalTime;
	uint GlobalSamplingPassIndex;
	uint GlobalSamplingPassCount;
}

float 		SysUniform_GetGlobalTime() { return GlobalTime; }
uint 		SysUniform_GetGlobalSamplingPassIndex() { return GlobalSamplingPassIndex; }
uint 		SysUniform_GetGlobalSamplingPassCount() { return GlobalSamplingPassCount; }

cbuffer ReciprocalViewportDimensionsCB BIND_SEQ_B2
{
	float2 ReciprocalViewportDimensions;
}

float2 		SysUniform_ReciprocalViewportDimensions() { return ReciprocalViewportDimensions; }

#endif
