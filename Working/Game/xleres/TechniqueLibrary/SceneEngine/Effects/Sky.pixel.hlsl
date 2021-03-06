// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Lighting/LightingAlgorithm.hlsl"
#include "../../Framework/SystemUniforms.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../Utility/Colour.hlsl"
#include "../../Framework/Binding.hlsl"

#if SKY_PROJECTION==1
	Texture2D ReflectionBox[3]	BIND_NUMERIC_T0;
#elif SKY_PROJECTION==5
	TextureCube ReflectionCube	BIND_NUMERIC_T0;
#endif

float3 CalculateBaseSkyColor(float2 texCoord, float3 viewFrustumVector)
{
	#if SKY_PROJECTION==1

		uint2 reflectionTextureDims;
		ReflectionBox[0].GetDimensions(
			reflectionTextureDims.x,
			reflectionTextureDims.y);

		return ReadReflectionHemiBox(
			normalize(viewFrustumVector),
			ReflectionBox[0], ReflectionBox[1], ReflectionBox[2],
			ClampingSampler,
			reflectionTextureDims, 0).rgb;

	#elif SKY_PROJECTION == 2

			// projection is already in the texture coordinates. Just sample
			// from the texture coordinate position
		return DiffuseTexture.Sample(ClampingSampler, texCoord);

	#elif (SKY_PROJECTION == 3) || (SKY_PROJECTION == 4)

			//	this is "panoramic projection."
		#if (SKY_PROJECTION == 3)
			float2 t = DirectionToEquirectangularCoord_YUp(viewFrustumVector);
		#else
			float2 t = DirectionToHemiEquirectangularCoord_YUp(viewFrustumVector);
		#endif
		{

				// 	Texture coordinates wrap strangely in this mode.
				// 	At the wrapping point the "x" texture coordinate will approach 1,
				//	and then suddenly drop down to 0. This causes the
				//	mipmapping algorithm to get the wrong result. We
				//	can correct for this, though, compensating for
				//	wrapping in the derviatives values.
				//	(or, I guess, always use the top mip-map level...?)

			float2 tddx = ddx(t);
			float2 tddy = ddy(t);
			if (tddx.x < -0.5f) { tddx.x += 1.f; }
			if (tddx.x >  0.5f) { tddx.x -= 1.f; }
			if (tddy.x < -0.5f) { tddy.x += 1.f; }
			if (tddy.x >  0.5f) { tddy.x -= 1.f; }
			return DiffuseTexture.SampleGrad(WrapUSampler, t, tddx, tddy).rgb;
		}

		return 0.0.xxx;

	#elif SKY_PROJECTION==5

		return ReflectionCube.Sample(DefaultSampler, AdjSkyCubeMapCoords(viewFrustumVector));

	#else

		return 0.0.xxx;

	#endif
}

cbuffer SkySettings BIND_MAT_B0 { float SkyBrightness; }

[earlydepthstencil]
float4 CalculateColour0(float2 texCoord, float3 viewFrustumVector, float3 worldSpacePosition)
{
	float3 color = SkyBrightness * CalculateBaseSkyColor(texCoord, viewFrustumVector);

	// #if (BLEND_FOG==1)
	//	float4 fog = CalculateDistantFog(worldSpacePosition, NegativeDominantLightDirection);
	//	color = lerp(fog.rgb, color, fog.a);
	// #endif

	return float4(LightingScale * color, 1.f);
}

	//////////////////////////////////

[earlydepthstencil]
float4 main(float4 position : SV_Position, float2 oTexCoord : TEXCOORD0, float3 viewFrustumVector : VIEWFRUSTUMVECTOR) : SV_Target0
{
	return CalculateColour0(oTexCoord, viewFrustumVector, SysUniform_GetWorldSpaceView() + 10000.f * viewFrustumVector);
}

[earlydepthstencil]
float4 ps_HalfCube(VSOUT geo) : SV_Target0
{
	return CalculateColour0(VSOUT_GetTexCoord0(geo), normalize(VSOUT_GetWorldPosition(geo) - SysUniform_GetWorldSpaceView()), VSOUT_GetWorldPosition(geo));
}

	//////////////////////////////////

VSOUT vs_main(VSIN input)
{
	VSOUT output;
	float3 localPosition = VSIN_GetLocalPosition(input);
	float3 worldPosition = float3(50.f * localPosition.xy + SysUniform_GetWorldSpaceView().xy, 2.f * 50.f * localPosition.z);
	output.position		 = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));

	output.position.z	 = 1.0f * output.position.w;		// push to infinity

	#if VSOUT_HAS_TEXCOORD>=1
		output.texCoord = VSIN_GetTexCoord0(input);
	#endif

	#if VSOUT_HAS_WORLD_POSITION==1
		output.worldPosition = worldPosition.xyz;
	#endif

	return output;
}
