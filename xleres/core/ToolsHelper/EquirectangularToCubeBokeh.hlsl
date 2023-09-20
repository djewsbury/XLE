// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Cubemap.hlsl"
#include "sampling-shader-helper.hlsl"
#include "../TechniqueLibrary/Math/MathConstants.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/IBL/IBLAlgorithm.hlsl"		// for HammersleyPt

Texture2D Input;
RWTexture2DArray<float4> OutputArray;

float2 DirectionToEquirectangularCoord(float3 direction, bool hemi)
{
	if (hemi) return DirectionToHemiEquirectangularCoord_YUp(direction);
	return DirectionToEquirectangularCoord_YUp(direction);
}

float4 LoadInput(Texture2D inputTex, float2 xy, int2 dims) { return inputTex.Load(uint3((int(xy.x)+dims.x)%dims.x, (int(xy.y)+dims.y)%dims.y, 0)); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float2 UniformSampleTriangle(float2 u) 
{
	// see pbr-book 13.6.5 Sampling a Triangle
	// pdf 1 / triangle area
    float su0 = sqrt(u.x);
    return float2(1.0 - su0, u.y * su0);		// return barycentric coords u,v
}

float3 PanelBokeh(
	float2 tc, float3 panel[3],
	uint thisPassSampleOffset, uint thisPassSampleCount, uint totalSampleCount,
	Texture2D inputTex, bool hemi)
{
    float3 plusX = panel[0];
    float3 plusY = panel[1];
    float3 center = panel[2];

	float3 result = 0.0.xxx;

	float3 centerRay = center + plusX * ((tc.x*2.0)-1.0) + plusY * ((tc.y*2.0)-1.0);
	centerRay = normalize(centerRay);

	// create a tangent frame around this center ray (because we don't want the cube distortions of plusX, plusY)
	// however, this wil introduce it's own sampling issues because the tangent frame is changing with the direction of centerRay
	float3 up = (abs(centerRay).y < 0.5f) ? float3(0,1,0) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, centerRay));
    float3 tangentY = cross(centerRay, tangentX);

	int2 inputDims;
	inputTex.GetDimensions(inputDims.x, inputDims.y);

	const uint polyEdgeCount = 3;
	const uint samplerRepeatingStride = 1453;
	for (uint edge=0; edge<polyEdgeCount; ++edge) {
		float2 U, V;
		sincos(edge / float(polyEdgeCount) * 2.0 * pi, U.x, U.y);
		sincos((edge+1) / float(polyEdgeCount) * 2.0 * pi, V.x, V.y);

		for (uint c=0; c<thisPassSampleCount; ++c) {
			uint q = (edge*totalSampleCount)+c+thisPassSampleOffset;
			float2 xi = float2((c+thisPassSampleOffset)/float(totalSampleCount), VanderCorputRadicalInverse(q%samplerRepeatingStride));		// not mathematically justified
			float2 samplePoly = U * xi.x + V * xi.y;

			// The sampling point on the polygon represents a direction around the center ray
			samplePoly *= 0.02;
			float3 ray = centerRay + samplePoly.x * tangentX + samplePoly.y * tangentY;

			float2 finalCoord = DirectionToEquirectangularCoord(ray, hemi);
			result.rgb += LoadInput(Input, finalCoord.xy * float2(inputDims), inputDims).rgb / (totalSampleCount*polyEdgeCount);
		}
	}

	return result;
}

[[vk::push_constant]] struct ControlUniformsStruct
{
    SamplingShaderUniforms _samplingShaderUniforms;
} FilterPassParams;

[numthreads(8, 8, 1)]
	void EquirectToCubeBokeh(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID)
{
	uint2 textureDims; uint arrayLayerCount;
	OutputArray.GetDimensions(textureDims.x, textureDims.y, arrayLayerCount);

	PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 1), FilterPassParams._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, arrayLayerCount))) return;

	float3 col = PanelBokeh(
		(helper._outputPixel.xy + 0.5.xx) / float2(textureDims.xy), CubeMapFaces[helper._outputPixel.z],
		helper._thisPassSampleOffset, helper._thisPassSampleCount, helper._totalSampleCount,
		Input, false);

	if (helper._firstDispatch)
		OutputArray[helper._outputPixel.xyz] = float4(0,0,0,1);
	OutputArray[helper._outputPixel.xyz].rgb += col;
}

