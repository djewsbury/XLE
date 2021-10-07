// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VSOUT_HAS_TEXCOORD 1
#define MAT_ALPHA_TEST 1
#define VSOUT_HAS_COLOR_LINEAR 2

#include "../../Framework/CommonResources.hlsl"
#include "../../Framework/MainGeometry.hlsl"
#include "../../Framework/Surface.hlsl"
#include "../../../BasicMaterial.hlsl"

void main(	VSOUT geo,
			out float4 oDiffuse : SV_Target0,
			out float4 oWorldSpaceNormal : SV_Target1)
{
	// oDiffuse = 1.0.xxxx;
	// oWorldSpaceNormal = 0.0.xxxx;
	DoAlphaTest(geo, AlphaThreshold);
	oDiffuse = DiffuseTexture.SampleLevel(ClampingSampler, geo.texCoord, 0);
	// oDiffuse = 1.0.xxxx;
	oDiffuse.rgb *= geo.color.rgb;
	oDiffuse.a = 1.f;

	const bool uNormNormalsFormat = true;
	if (uNormNormalsFormat) {
		oWorldSpaceNormal = float4(.5f,.5f,1.f, oDiffuse.a);
	} else {
		oWorldSpaceNormal = float4(1.f,1.f,1.f, oDiffuse.a);
	}
}
