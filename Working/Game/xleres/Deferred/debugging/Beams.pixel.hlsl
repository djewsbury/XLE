// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Framework/gbuffer.hlsl"

#if SHADOWS==1

	void main(float4 position : SV_Position, float3 normal : NORMAL, uint colorIndex : COLORINDEX)
	{
	}

#else

	void main(	float4 position : SV_Position, float3 normal : NORMAL,
				uint colorIndex : COLORINDEX,
				out GBufferEncoded output)
	{
		float4 colorTable[3] = 
		{
			float4(.13,.33,.63,1),
			float4(3.*.4, 3.*.2, 3.*.15, .35),
			float4(.4, .2, .15, 1)
		};
		// oDiffuse = colorTable[colorIndex];
		// oNormal = float4(normal.xyz, 1);

		GBufferValues outputValues = GBufferValues_Default();
		outputValues.diffuseAlbedo = .5f * colorTable[colorIndex];
		outputValues.worldSpaceNormal = normal.xyz;
		output = Encode(outputValues);
	}	

#endif

void predepth(float4 position : SV_Position, float3 normal : NORMAL, uint colorIndex : COLORINDEX)
{
}
