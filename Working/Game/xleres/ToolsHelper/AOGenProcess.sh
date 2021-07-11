// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

Texture2DArray<float> InputCubeMap : register(t0);
RWTexture2DArray<float> OutputTexture : register(u0);

float AreaElement(float x, float y)
{
    return atan2(x * y, sqrt(x * x + y * y + 1.f));
}

float CubeMapTexelSolidAngle(uint2 tc, uint2 dims)
{
        // Based on the method from here:
        //      http://www.rorydriscoll.com/2012/01/15/cubemap-texel-solid-angle/
        // We can calculate the solid angle of a single texel of the
        // cube map (which represents its weight in an angular based system)
        // On that page, Rory shows an algebraic derivation of this formula. See also
        // the comments section for a number of altnerative derivations (including
        // an interesting formula for the ratio of the area of a texel and the area on
        // the equivalent sphere surface).

    float2 reciprocalDims = 1.0f / float2(dims);

        // scale up to [-1, 1] range (inclusive), offset by 0.5 to point to texel center.
    float U = (2.0f * reciprocalDims.x * (float(tc.x) + 0.5f)) - 1.0f;
    float V = (2.0f * reciprocalDims.y * (float(tc.y) + 0.5f)) - 1.0f;

        // U and V are the -1..1 texture coordinate on the current face.
        // Get projected area for this texel
    float x0 = U - reciprocalDims;
    float y0 = V - reciprocalDims;
    float x1 = U + reciprocalDims;
    float y1 = V + reciprocalDims;
    return AreaElement(x0, y0) - AreaElement(x0, y1) - AreaElement(x1, y0) + AreaElement(x1, y1);
}

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
