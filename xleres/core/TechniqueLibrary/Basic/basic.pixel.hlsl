// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/CommonResources.hlsl"
#include "../Framework/Binding.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Utility/Colour.hlsl"

#include "../Math/EdgeDetection.hlsl"

Texture2D		InputTexture;
[[vk::input_attachment_index(0)]] SubpassInput<float4> SubpassInputAttachment;
#if VSOUT_HAS_FONTTABLE && defined(FONT_RENDERER)
	Buffer<float> 	FontResource : register(t5);
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

#if VSOUT_HAS_FONTTABLE && defined(FONT_RENDERER)
	float FontEdgeTrick(VSOUT vsout)
	{
		float filteredX = 0.f, filteredY = 0.f;
		for (int y=0; y<5; ++y)
			for (int x=0; x<5; ++x) {
				int qx = int(vsout.fontTable.y*vsout.texCoord.x)+2*(x-2);
				int qy = int(vsout.fontTable.z*vsout.texCoord.y)+2*(y-2);
				if (qx >= 0 && qx < vsout.fontTable.y && qy >= 0 && qy < vsout.fontTable.z) {
					uint idx = vsout.fontTable.x + (qy * vsout.fontTable.y + qx);
					float sample = FontResource[idx].r;
					filteredX += ScharrHoriz5x5[x][y] * sample;
					filteredY += ScharrHoriz5x5[y][x] * sample;
				}
			}
		// return max(abs(filteredX), abs(filteredY));
		return (max(abs(filteredX), abs(filteredY))) < (VSOUT_GetColor0(vsout).a);
	}

	float BilinearFontLookup(VSOUT vsout)
	{
		float2 AB = float2(vsout.fontTable.y * vsout.texCoord.x, vsout.fontTable.z * vsout.texCoord.y);
		uint2 xy = uint2(AB);
		uint2 xy2 = uint2(min(vsout.fontTable.y-1, xy.x+1), min(vsout.fontTable.z-1, xy.y+1));

		AB -= xy;
		float s0 = FontResource[vsout.fontTable.x + (xy.y * vsout.fontTable.y + xy.x)].r;
		float s1 = FontResource[vsout.fontTable.x + (xy.y * vsout.fontTable.y + xy2.x)].r;
		float s2 = FontResource[vsout.fontTable.x + (xy2.y * vsout.fontTable.y + xy.x)].r;
		float s3 = FontResource[vsout.fontTable.x + (xy2.y * vsout.fontTable.y + xy2.x)].r;

		return 
			  s0 * (1.f - AB.x) * (1.f - AB.y)
			+ s1 * (      AB.x) * (1.f - AB.y)
			+ s2 * (1.f - AB.x) * (      AB.y)
			+ s3 * (      AB.x) * (      AB.y)
			;
	}

	float PointFontLookup(VSOUT vsout)
	{
		// we could do morton order style swizzling here -- but I'm not sure how useful it would be, given the src should be fairly small...? Might be better off just try to compress the src as much as possible
		uint2 xy = uint2(vsout.fontTable.y * vsout.texCoord.x, vsout.fontTable.z * vsout.texCoord.y);
		uint idx = vsout.fontTable.x + (xy.y * vsout.fontTable.y + xy.x);
		return FontResource[idx].r;
	}

#endif

float4 frameworkEntry(VSOUT vsout) : SV_Target0
{
	float4 result = 1.0.rrrr;

	#if VSOUT_HAS_FONTTABLE && defined(FONT_RENDERER)
		// return FontEdgeTrick(vsout);
		result.a *= PointFontLookup(vsout);
		if (VSOUT_GetColor0(vsout).a < 1.f)
			result.a *= FontEdgeTrick(vsout);		// (fade in trick based on gradients; just for fun)
	#elif VSOUT_HAS_TEXCOORD
		#if defined(FONT_RENDERER)
			result.a *= InputTexture.Sample(PointClampSampler, VSOUT_GetTexCoord0(vsout)).r;
		#else
			result *= InputTexture.Sample(DefaultSampler, VSOUT_GetTexCoord0(vsout));
		#endif
	#endif

	result *= VSOUT_GetColor0(vsout);
	return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

float4 copy(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return InputTexture.Load(int3(position.xy, 0));
}

float copy_depth(float4 position : SV_Position) : SV_Depth
{
	return InputTexture.Load(int3(position.xy, 0)).r;
}

float4 fake_tonemap(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return float4(InputTexture.Load(int3(position.xy, 0)).rgb, 1.f);
}

float4 copy_bilinear(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return InputTexture.SampleLevel(ClampingSampler, texCoord, 0);
}

float4 copy_inputattachment(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return SubpassInputAttachment.SubpassLoad();
}

cbuffer ScreenSpaceOutput
{
	float2 OutputMin, OutputMax;
	float2 InputMin, InputMax;
	float2 OutputDimensions;
}

float4 copy_boxfilter(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	// return InputTexture.SampleLevel(ClampingSampler, texCoord, 0);

	uint2 dims;
	InputTexture.GetDimensions(dims.x, dims.y);

	uint2 inputMin = uint2(InputMin * dims);
	uint2 inputMax = uint2(InputMax * dims);
	uint2 zero = (uint2)floor((position.xy - OutputMin) * (inputMax - inputMin) / (OutputMax - OutputMin) + dims * inputMin);
	uint2 one = (uint2)floor((position.xy + 1.0.xx - OutputMin) * (inputMax - inputMin) / (OutputMax - OutputMin) + dims * inputMin);

	float4 result = 0.0.xxxx;
	for (uint y=zero.y; y<one.y; ++y)
		for (uint x=zero.x; x<one.x; ++x) {
			uint3 coord = uint3(x,y,0);
			coord.x = clamp(coord.x, inputMin.x, inputMax.x-1);
			coord.y = clamp(coord.y, inputMin.y, inputMax.y-1);
			result += InputTexture.Load(coord);
		}
	return result / float((one.x - zero.x) * (one.y - zero.y));
}

float4 copy_boxfilter_alphacomplementweight(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	uint2 dims;
	InputTexture.GetDimensions(dims.x, dims.y);

	uint2 inputMin = uint2(InputMin * dims);
	uint2 inputMax = uint2(InputMax * dims);
	uint2 zero = (uint2)floor((position.xy - OutputMin) * (inputMax - inputMin) / (OutputMax - OutputMin) + dims * inputMin);
	uint2 one = (uint2)floor((position.xy + 1.0.xx - OutputMin) * (inputMax - inputMin) / (OutputMax - OutputMin) + dims * inputMin);

	float4 result = 0.0.xxxx;
	float rgbWeightSum = 0.f;
	for (uint y=zero.y; y<one.y; ++y)
		for (uint x=zero.x; x<one.x; ++x) {
			uint3 coord = uint3(x,y,0);
			coord.x = clamp(coord.x, inputMin.x, inputMax.x-1);
			coord.y = clamp(coord.y, inputMin.y, inputMax.y-1);
			float4 sample = InputTexture.Load(coord);
			float weight = 1.f-sample.a;
			rgbWeightSum += weight;
			result.rgb += sample.rgb * weight;
			result.a += sample.a;
		}
	return float4(
		result.rgb / max(rgbWeightSum, 0.0001f),
		result.a / float((one.x - zero.x) * (one.y - zero.y)));
}

float4 copy_point(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return InputTexture.SampleLevel(PointClampSampler, texCoord, 0);
}

float4 fill_gradient(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	return float4(texCoord, 0, 1);
}

float4 fill_background(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	uint2 xy = position.xy / 16;
	const float dark = 3 / 255.f;
	const float mid = 5 / 255.f;
	const float bright = 7 / 255.f;
	if ((xy.x+xy.y) & 1) {
		return float4(mid, mid, bright, 1);
	} else
		return float4(dark, dark, mid, 1);
}

float4 PCT_Text( float4 input : SV_Position, float4 color : COLOR0, float2 texCoord : TEXCOORD0 ) : SV_Target0
{
	return float4(	color.rgb,
					color.a * InputTexture.Sample(DefaultSampler, texCoord).r);
}

float4 P( float4 input : SV_Position ) : SV_Target0
{
	return float4(1,1,1,1);
}

float4 PC( float4 input : SV_Position, float4 color : COLOR0 ) : SV_Target0
{
	return color;
}

float4 PCT( float4 input : SV_Position, float4 color : COLOR0, float2 texCoord : TEXCOORD0 ) : SV_Target0
{
	return InputTexture.Sample(DefaultSampler, texCoord) * color;
}

cbuffer ScrollConstants
{
	float ScrollAreaMin, ScrollAreaMax;
}

float4 copy_point_scrolllimit(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
	float alpha = saturate((position.y - ScrollAreaMin) / 12.f) * saturate((ScrollAreaMax - position.y) / 12.f);
	return float4(InputTexture.SampleLevel(PointClampSampler, texCoord, 0).rgb, alpha);
}

float4 invalid(float4 position : SV_Position) : SV_Target0
{
	float3 color0 = float3(1.0f, 0.f, 0.f);
	float3 color1 = float3(0.0f, 0.f, 1.f);
	uint flag = (uint(position.x/4.f) + uint(position.y/4.f))&1;
	return float4(flag?color0:color1, 1.0f);
}

float4 blackOpaque(float4 position : SV_Position) : SV_Target0
{
	return float4(0,0,0,1);
}
