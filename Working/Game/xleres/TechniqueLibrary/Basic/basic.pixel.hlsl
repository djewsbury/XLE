// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/CommonResources.hlsl"
#include "../Framework/Binding.hlsl"
#include "../Framework/VSOUT.hlsl"
#include "../Utility/Colour.hlsl"

Texture2D		InputTexture;
[[vk::input_attachment_index(0)]] SubpassInput<float4> SubpassInputAttachment;

///////////////////////////////////////////////////////////////////////////////////////////////////

float4 frameworkEntry(VSOUT vsin) : SV_Target0
{
	float4 result = 1.0.rrrr;

	#if VSOUT_HAS_TEXCOORD
		#if defined(FONT_RENDERER)
			result.a *= InputTexture.Sample(DefaultSampler, VSOUT_GetTexCoord0(vsin)).r;
		#else
			result *= InputTexture.Sample(DefaultSampler, VSOUT_GetTexCoord0(vsin));
		#endif
	#endif

	result *= VSOUT_GetColor0(vsin);
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
	return float4(InputTexture.Load(int3(position.xy, 0)).rgb / LightingScale, 1.f);
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

float4 invalid(float4 position : SV_Position)
{
	float3 color0 = float3(1.0f, 0.f, 0.f);
	float3 color1 = float3(0.0f, 0.f, 1.f);
	uint flag = (uint(position.x/4.f) + uint(position.y/4.f))&1;
	return float4(flag?color0:color1, 1.0f);
}
