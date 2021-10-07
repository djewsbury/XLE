// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Particle.hlsl"

[maxvertexcount(4)]
	void main(point ParticleVStoGS input[1], inout TriangleStream<VSOUT> outputStream)
{
		//		Expand an input vertex into a quad on screen
		//		Our vertex shader mostly just passes coordinates in
		//			(though it could do local-world transform)
		//		So, we can easily build the coordinates here...

	float3 cameraRight	= float3(SysUniform_GetCameraBasis()[0].x, SysUniform_GetCameraBasis()[1].x, SysUniform_GetCameraBasis()[2].x);
	float3 cameraUp		= float3(SysUniform_GetCameraBasis()[0].y, SysUniform_GetCameraBasis()[1].y, SysUniform_GetCameraBasis()[2].y);
	float3 rotatedRight = cameraRight * input[0].screenRot.x + cameraUp * input[0].screenRot.y;
	float3 rotatedUp	= cameraRight * input[0].screenRot.z + cameraUp * input[0].screenRot.w;

	float2 texCoords[4] =
	{
		float2(1.0f, 0.f),
		float2(0.0f, 0.f),
		float2(1.0f, 1.f),
		float2(0.0f, 1.f)
	};

	float2 axes[4] =
	{
		float2( 1.0f,  1.f),
		float2(-1.0f,  1.f),
		float2( 1.0f, -1.f),
		float2(-1.0f, -1.f)
	};

	for (uint c=0; c<4; ++c) {
		VSOUT output;
		float3 worldPosition = input[0].position
			+ rotatedRight * axes[c].x
			+ rotatedUp * axes[c].y
			;
		output.position = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

		#if VSOUT_HAS_COLOR_LINEAR
			output.color 		= input[0].color;
		#endif

		#if VSOUT_HAS_TEXCOORD
					// (not tested in X3; but it theory it should look like this)
				// output.texCoord 	= input[0].texCoord + (input[0].texCoordScale.yz*2.f-1.0.xx) * texCoords[c];
			output.texCoord = input[0].texCoord + input[0].texCoordScale.xy * texCoords[c];
		#endif

			// todo -- can also output tangent space (if we need it)

		#if VSOUT_HAS_WORLD_VIEW_VECTOR
			output.worldViewVector = SysUniform_GetWorldSpaceView().xyz - worldPosition.xyz;
		#endif

		#if VSOUT_HAS_FOG_COLOR
			output.fogColor 		= input[0].fogColor;
		#endif

		outputStream.Append(output);
	}
}


[maxvertexcount(4)]
	void autoquads(lineadj VSOUT input[4], inout TriangleStream<VSOUT> outputStream)
{
	outputStream.Append(input[0]);
	outputStream.Append(input[1]);
	outputStream.Append(input[2]);
	outputStream.Append(input[3]);
}
