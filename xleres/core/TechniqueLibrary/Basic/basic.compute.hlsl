// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Framework/Binding.hlsl"
#include "../Utility/Colour.hlsl"
#include "../../Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

RWTexture2D<float4> OutputTexture;
Texture2D<float4> InputTexture;

[numthreads(8, 8, 1)]
	void ResampleBilinear(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 outputDims;
	uint2 inputDims;
	OutputTexture.GetDimensions(outputDims.x, outputDims.y);
	InputTexture.GetDimensions(inputDims.x, inputDims.y);

	float2 inputCoord = float2(dispatchThreadId.xy) * float2(inputDims) / float2(outputDims);
	float2 base = float2(floor(inputCoord.x), floor(inputCoord.y));
	float2 alpha = inputCoord - base;

		// bilinear filter resampling

	uint2 b = uint2(base);
	float4 s0 = InputTexture[b];
	float4 s1 = InputTexture[min(uint2(base)+uint2(1,0), inputDims - uint2(1,1))];
	float4 s2 = InputTexture[min(uint2(base)+uint2(0,1), inputDims - uint2(1,1))];
	float4 s3 = InputTexture[min(uint2(base)+uint2(1,1), inputDims - uint2(1,1))];

	float w0 = (1.f-alpha.x) * (1.f-alpha.y);
	float w1 = alpha.x * (1.f-alpha.y);
	float w2 = (1.f-alpha.x) * alpha.y;
	float w3 = alpha.x * alpha.y;

	OutputTexture[dispatchThreadId.xy] = w0 * s0 + w1 * s1 + w2 * s2 + w3 * s3;
}

[numthreads(8, 8, 1)]
	void ResamplePoint(uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 outputDims; uint2 inputDims;
	OutputTexture.GetDimensions(outputDims.x, outputDims.y);
	InputTexture.GetDimensions(inputDims.x, inputDims.y);

	float2 inputCoord = float2(dispatchThreadId.xy) * float2(inputDims) / float2(outputDims);
	float2 base = float2(floor(inputCoord.x), floor(inputCoord.y));
	float4 s0 = InputTexture[min(uint2(base)+uint2(0,0), inputDims)];
	OutputTexture[dispatchThreadId.xy] = s0;
}

[numthreads(8, 8, 1)]
	void copy(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	OutputTexture.GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	if (all(pixelId < textureDims))
		OutputTexture[pixelId] = InputTexture[pixelId];
}

[numthreads(8, 8, 1)]
	void copy_nonlinearOut(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
	uint2 textureDims;
	OutputTexture.GetDimensions(textureDims.x, textureDims.y);

	uint2 threadGroupCounts = uint2((textureDims.x+8-1)/8, (textureDims.y+8-1)/8);
	uint2 pixelId = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
	if (all(pixelId < textureDims)) {
		float4 col = InputTexture[pixelId];
		col.rgb = LinearToSRGB_Formal(col.rgb);
		OutputTexture[pixelId] = col;
	}
}
