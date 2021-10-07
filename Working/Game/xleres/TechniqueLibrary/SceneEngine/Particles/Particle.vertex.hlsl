// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "particle.hlsl"
#include "../Lighting/Atmosphere.hlsl"

ParticleVStoGS main(VSIN input)
{
	ParticleVStoGS output;
	output.position = input.position;

	#if GEO_HAS_COLOR
		output.color 		= VSIN_GetColor0(input);
	#endif

	#if GEO_HAS_TEXCOORD
		output.texCoord 	= VSIN_GetTexCoord0(input);
	#endif

	#if VSOUT_HAS_FOG_COLOR == 1
		output.fogColor = CalculateFog(input.position.z, SysUniform_GetWorldSpaceView() - input.position, NegativeDominantLightDirection);
	#endif

	output.texCoordScale = input.texCoordScale;
	output.screenRot = input.screenRot;
	return output;
}

VSOUT nogs(VSIN input)
{
	VSOUT output;

	float3 cameraRight	= float3(SysUniform_GetCameraBasis()[0].x, SysUniform_GetCameraBasis()[1].x, SysUniform_GetCameraBasis()[2].x);
	float3 cameraUp		= float3(SysUniform_GetCameraBasis()[0].y, SysUniform_GetCameraBasis()[1].y, SysUniform_GetCameraBasis()[2].y);
	float3 rotatedRight = cameraRight * input.screenRot.x + cameraUp * input.screenRot.y;
	float3 rotatedUp	= cameraRight * input.screenRot.z + cameraUp * input.screenRot.w;

	float2 expXY = input.texCoordScale.zy * 2.f - 1.f;

	float3 worldPosition
		= input.position
		+ rotatedRight * expXY.x
		+ rotatedUp * expXY.y
		;
	output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

	#if VSOUT_HAS_COLOR_LINEAR
		output.color 		= input.color;
	#endif

	#if VSOUT_HAS_TEXCOORD
		output.texCoord 	= input.texCoord;
	#endif

	#if VSOUT_HAS_TANGENT_FRAME
		// build the correct tangent frame for the vertex, assuming
		// texturing over the full surface, as normal
		output.tangent = normalize(rotatedRight);
		output.bitangent = normalize(rotatedUp);
		output.normal = normalize(cross(output.tangent, output.bitangent));
	#endif

	#if VSOUT_HAS_WORLD_VIEW_VECTOR
		output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
	#endif

	#if VSOUT_HAS_BLEND_TEXCOORD
		output.blendTexCoord = input.blendTexCoord.xyz;
	#endif

    #if VSOUT_HAS_FOG_COLOR
        output.fogColor = CalculateFog(worldPosition.z, SysUniform_GetWorldSpaceView() - worldPosition, NegativeDominantLightDirection);
    #endif

	return output;
}
