// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

Texture2DArray<float> InputCubeMap : register(t0);
RWTexture2DArray<float> OutputTexture : register(u0);

[numthreads(4, 4, 5)]
    void CubeMapStepDown(uint3 dispatchThreadId : SV_DispatchThreadID)
{
        // Each thread will take one sixteenth of a single face of the input
        // texture, and produce a result.
        // This means that each dispatch(1) will write out 4x4x5 values to
        // OutputTexture

    uint3 dims;
    InputCubeMap.GetDimensions(dims.x, dims.y, dims.z);
    uint face = dispatchThreadId.z;

    float result = 0.f;
    uint2 mins = dispatchThreadId.xy * dims.xy / 4;
    uint2 maxs = (dispatchThreadId.xy + uint2(1,1)) * dims.xy / 4;
    for (uint y=mins.y; y<maxs.y; ++y)
        for (uint x=mins.x; x<maxs.x; ++x) {
            float depthValue = InputCubeMap[uint3(x, y, face)];
            float occlusion = (depthValue >= 1.f)?0.f:1.f;
            result += occlusion * CubeMapTexelSolidAngle(uint2(x, y), dims.xy);
        }

    OutputTexture[dispatchThreadId] = result;
}
