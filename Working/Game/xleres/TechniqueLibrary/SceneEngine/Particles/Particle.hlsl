// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../TechniqueLibrary/Framework/SystemUniforms.hlsl"
#include "../TechniqueLibrary/Framework/MainGeometry.hlsl"

struct ParticleVStoGS
{
    float3 position : POSITION0;
	
	#if GEO_HAS_COLOR
		float4 color : COLOR0;
	#endif

	#if GEO_HAS_TEXCOORD
		float2 texCoord : TEXCOORD;
	#endif

    float4 texCoordScale : TEXCOORDSCALE;
	float4 screenRot : PARTICLEROTATION;

    #if VSOUT_HAS_FOG_COLOR
        float4 fogColor : FOGCOLOR;
    #endif
};

