// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VISUALIZE_TYPE_COLOR 0
#define VISUALIZE_TYPE_DEPTH 1
#define VISUALIZE_TYPE_NORMAL 2
#define VISUALIZE_TYPE_MOTION 3
#define VISUALIZE_TYPE_ALPHA 4
#define VISUALIZE_TYPE_GREYSCALE 5
#define VISUALIZE_TYPE_GBUFFERNORMALS 6

#if VISUALIZE_TYPE == VISUALIZE_TYPE_DEPTH
	Texture2D<float> VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(VisualizeInput.Load(uint3(position.xy, 0)).rrr, 1);
	}
#elif VISUALIZE_TYPE == VISUALIZE_TYPE_NORMAL
	Texture2D<float3> VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(0.5f + 0.5f * VisualizeInput.Load(uint3(position.xy, 0)).rgb, 1);
	}
#elif VISUALIZE_TYPE == VISUALIZE_TYPE_MOTION
	Texture2D<int3> VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(VisualizeInput.Load(uint3(position.xy, 0)).rg / 255.f, 0, 1);
	}
#elif VISUALIZE_TYPE == VISUALIZE_TYPE_GREYSCALE
	Texture2D<float> VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(VisualizeInput.Load(uint3(position.xy, 0)).rrr, 1);
	}
#elif VISUALIZE_TYPE == VISUALIZE_TYPE_GBUFFERNORMALS
	#include "../TechniqueLibrary/Framework/gbuffer.hlsl"
	Texture2D VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(DecompressGBufferNormal(VisualizeInput.Load(uint3(position.xy, 0)).rgb), 1);
	}
#else
	Texture2D VisualizeInput : register(t1, space1);
	float4 Value(uint2 position)
	{
		return float4(VisualizeInput.Load(uint3(position.xy, 0)).rgb, 1);
	}
#endif

cbuffer DebuggingGlobals : register(b5, space1)
{
	const uint2 ViewportDimensions;
	const int2 MousePosition;
}

uint2 ToAttachmentCoords(uint2 xy)
{
	// Adjust mapping for attachments that don't match the viewport size exactly
	uint2 outputDims;
	VisualizeInput.GetDimensions(outputDims.x, outputDims.y);
	return xy * outputDims / ViewportDimensions;
}

float4 main(float4 position : SV_Position) : SV_Target0
{
	float r = length(position.xy - MousePosition);
	if (r <= 128) {
		uint2 magnifiedCoords = MousePosition + (position.xy - MousePosition) / 3;
		magnifiedCoords = ToAttachmentCoords(magnifiedCoords.xy);
		float4 col = Value(magnifiedCoords.xy);

		float borderValue = (128 - r);
		float b = borderValue / 4;
		return lerp(float4(0,0,0,1), col, smoothstep(0, 1, saturate(b)));
	} else {
		return Value(ToAttachmentCoords(position.xy));
	}
}

