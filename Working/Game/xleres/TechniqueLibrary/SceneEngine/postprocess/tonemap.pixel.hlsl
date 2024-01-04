// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "tonemap.hlsl"
#include "../../Framework/CommonResources.hlsl"
#include "../../Utility/Colour.hlsl"

#if DO_COLOR_GRADING==1
	#include "../../xleres_cry/postprocess/ColorGrading.h"
#endif

#if OPERATOR==2
	#include "../../xleres_cry/postprocess/crytonemap.h"
#endif

Texture2D	InputTexture BIND_MAT_T3;
Texture2D	BloomMap BIND_MAT_T4;

struct LuminanceBufferStruct
{
	float	_currentLuminance;
	float	_prevLuminance;
};

StructuredBuffer<LuminanceBufferStruct>	LuminanceBuffer BIND_MAT_T5;

float4 Uncharted2Tonemap(float4 x)
{
		//	It's a little like a logarithm graph
		//	slowly approaches y=1, hits a singularity there
		//	when x=0, y=0
	float A = 0.15f;
	float B = 0.50f;
	float C = 0.10f;
	float D = 0.20f;
	float E = 0.02f;
	float F = 0.30f;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float3 ApplyToneMap(float3 color)
{
	const float imageLuminance = max(1e-4, LuminanceBuffer[0]._currentLuminance);
	float scale = SceneKey / imageLuminance;

	const uint toneMapOperator = OPERATOR;
	if (toneMapOperator==0) {

			//	Basic Reinhard operator. Not very effective result
			//	(for demonstration purposes)
		float3 L = scale * color;
		const float3 LWhite = float3(2.,2.,2.);
		const float3 A = 1.0.xxx + L/(LWhite*LWhite);
		return (L * A) / (1.f + L);

	} else if (toneMapOperator==1) {

			//	Uncharted 2 operator. Nice basic filmic method
			//	(should make a good reference point)
			//		See Uncharted 2 programmer's blog:
			//			http://filmicgames.com/archives/75

		float3 L = scale * color;
		const float whitepoint = Whitepoint;
		float4 postToneMap = Uncharted2Tonemap(float4(L,whitepoint));

		const bool doWhiteAdjustment = true;
		if (doWhiteAdjustment) {
			postToneMap.rgb = postToneMap.rgb/postToneMap.w;
		}
		return postToneMap.rgb;

	} else if (toneMapOperator==2) {

		#if OPERATOR==2
			return CryToneMapOperator(color, scale);
		#endif

	}

	return scale * color;
}

bool ToneMapOutputsLinearSpace()
{
	const uint toneMapOperator = OPERATOR;
	return toneMapOperator != 2;		// operator 2 returns SRGB space, others return linear space
}

float4 main(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float4 input = InputTexture.Load(int3(position.xy,0));

	#if ENABLE_BLOOM==1
			//	Should the bloom color go through the
			//	tonemapping, also?... It seems to work much
			//	better when it does. (plus, SRGB is handled correctly).
		float3 bloomValue = BloomScale * BloomMap.SampleLevel(ClampingSampler, texCoord, 0).rgb;

			// note --	should the bloom value go through the tone map
			//			operator? If, so, add the bloom value before ApplyToneMap
			//			this causes some wierd problems though -- the bloom
			//			seems to get exponentially stronger. The result seems to be
			//			better, visually, applying the tonemap afterwards

		input.rgb += bloomValue;
	#endif

	input.rgb = ApplyToneMap(input.rgb);

	bool writeOutLinearSpaceValue = (HARDWARE_SRGB_ENABLED!=0);

	#if DO_COLOR_GRADING==1

			//
			//	Note -- there's a problem here. Some tone map operators convert
			//			color values into SRGB space, so in those cases, the color
			//			grading is performed in SRGB space.
			//			But other tone map operators output linear space. So they
			//			end up doing color grading in linear space. It can produce
			//			very different result.
			//

		if (ToneMapOutputsLinearSpace()) {
			input.rgb = LinearToSRGB(input.rgb);
			input.rgb = DoColorGrading(input.rgb);
		} else {
			input.rgb = DoColorGrading(input.rgb);
		}

		if (writeOutLinearSpaceValue) {
			input.rgb = SRGBToLinear(input.rgb);
		}

	#else

			//
			//	Compare the color space of the output from our tone map operator
			//	to the color space we're expected to write out. If they don't match
			//	we need to do some kind of conversion.
			//
		if (ToneMapOutputsLinearSpace()) {
			if (!writeOutLinearSpaceValue) {
				input.rgb = LinearToSRGB(input.rgb);
			}
		} else {
			if (writeOutLinearSpaceValue) {
				input.rgb = SRGBToLinear(input.rgb);
			}
		}

	#endif

	return input;		// probably an implied saturate here
}
