// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../TechniqueLibrary/Utility/Colour.hlsl"
#include "../../TechniqueLibrary/Framework/SystemUniforms.hlsl"
#include "../../TechniqueLibrary/Math/ProjectionMath.hlsl"

Texture2DMS<float>  DepthsTexture;
Texture2D<uint>		LitSamplesMetrics;

float4 ps_depthave(float4 position : SV_Position) : SV_Target
{
    uint2 dims; uint sampleCount;
    DepthsTexture.GetDimensions(dims.x, dims.y, sampleCount);

    float aveDepthValue = 0.f;
    for (uint c=0; c<sampleCount; ++c)
        aveDepthValue += DepthsTexture.Load(uint2(position.xy), c);
    aveDepthValue /= float(sampleCount);
    if (aveDepthValue >= 1.f) discard;
    aveDepthValue = NDCDepthToWorldSpace_Perspective(aveDepthValue, GlobalMiniProjZW());
    return float4(LightingScale * aveDepthValue.xxx / 100.f, 0.f);
}

float4 ps_pixelmetrics(float4 position : SV_Position) : SV_Target
{
    uint layers = LitSamplesMetrics.Load(uint3(position.xy, 0));
    if (layers == 0) discard;
    float3 col = lerp(float3(0,0,1), float3(1,0,0), saturate((layers-1)/64.f));
    return float4(col, .75f);
}

#include "../../TechniqueLibrary/Profiling/metricsriglsl.hlsl"
#include "../../TechniqueLibrary/Profiling/metrics.hlsl"

StructuredBuffer<MetricsStructure>	MetricsObject;

class LitFragmentCount : IGetValue { uint GetValue()
{
    return MetricsObject[0].StocasticTransLitFragmentCount;
}};

class AveLitFragment : IGetValue { uint GetValue()
{
    return uint(MetricsObject[0].StocasticTransLitFragmentCount / float(ScreenDimensions.x * ScreenDimensions.y));
}};

class PartialLitFragment : IGetValue { uint GetValue()
{
    return uint(MetricsObject[0].StocasticTransPartialLitFragmentCount);
}};
