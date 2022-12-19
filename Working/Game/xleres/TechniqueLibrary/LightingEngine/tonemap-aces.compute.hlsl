// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Utility/Colour.hlsl"

cbuffer Params
{
	row_major float3x4 PreToneScale;
	row_major float3x4 PostToneScale;
}

float3 c5c9CurveEsimate_LogY3(float3 x)
{
	// this is what we're estimating:
	// 	std::log(ACES::segmented_spline_c9_fwd(ACES::segmented_spline_c5_fwd(x)));

	const float A = 1.25588544e+03;
	const float B = -1.59161681e+02;
	const float C = 3.10987237e+01;
	const float D = 3.21980642e+02;
	const float E = 2.24585460e+00;
	const float F = 8.39043523e+00;
	const float G = 4.34339192e-01;

	float3 top    = ((A*x + B)*x + C)*x - 1.69897;
	float3 bottom = ((D*x + E)*x + F)*x + G;
	return top / bottom;
}

float3 ToneMapAces(float3 x)
{
	x = mul(PreToneScale, x);
	x = exp(c5c9CurveEsimate_LogY3(x));
	return mul(PostToneScale, x);
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// 			e n t r y   p o i n t
//////////////////////////////////////////////////////////////////////////////////////////////////

#include "xleres/Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

Texture2D<float3> HDRInput;
RWTexture2D<float3> LDROutput;		// output could be >8 bit depth, of course, but we're expecting smaller range than the input

[numthreads(8, 8, 1)]
	void main(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	LDROutput.GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);

	if (pixelId.x < textureDims.x && pixelId.y < textureDims.y)
		LDROutput[pixelId] = saturate(LinearToSRGB_Formal(ToneMapAces(HDRInput[pixelId])));
		// LDROutput[pixelId] = saturate(LinearToSRGB_Formal(HDRInput[pixelId]));
}
