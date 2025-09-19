// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../RenderOverlays/Legacy/DebuggingPanels.hlsl"
#include "../TechniqueLibrary/Framework/CommonResources.hlsl"

Texture2DArray<float>		ShadowTextures	 	: register(t0);

Texture3D<float>			InscatterTexture				: register(t1);
Texture3D<float>			TransmissionTexture				: register(t2);
Texture3D<float>			InputInscatterShadowingValues	: register(t3);
Texture3D<float>			DensityValues					: register(t4);

float4 VolumeShadows(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	const bool showShadows = false;
	const bool vis3DShadows = false;
	if (showShadows) {
		float4 result = float4(0.0.xxx, 0.f);
		RenderTile(float2(.5f, 0.f), float2(1.f, .5f), texCoord, ShadowTextures, 0, result);
		RenderTile(float2(.8f, .05f), float2(.95f, .2f), texCoord, ShadowTextures, 1, result);
		RenderTile(float2(.8f, .3f), float2(.95f, .45f), texCoord, ShadowTextures, 2, result);
		return result;
	} else if (vis3DShadows) {
		float min = 1.f;
		const int samples = 128;
		[loop] for (int c=samples-1; c>=0; --c) {
			float d = c/float(samples);
			float shd = InputInscatterShadowingValues.SampleLevel(ClampingSampler, float3(texCoord.xy, d), 0);
			if (shd < 0.8f) min = d;
		}
		return float4(lerp(float3(1,0,0), float3(0,0,1), min), 0.25f);
	} else {
		const uint tileSize = 64;
		if (((uint(position.x) % tileSize) == tileSize-1) || ((uint(position.y) % tileSize) == tileSize-1)) return 1.0.xxxx;

		uint2 screenSize = uint2(position.xy / texCoord.xy);
		uint tilesAlong = screenSize.x / tileSize;
		uint tilesDown = screenSize.y / tileSize;
		uint tileIndex = (floor(position.y / tileSize) * tilesAlong) + (position.x / tileSize);
		float z = tileIndex / float(tilesDown * tilesAlong);
		return InscatterTexture.SampleLevel(ClampingSampler, float3(frac(position.xy/tileSize), z), 0);
	}
}
