// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Foreign/ThreadGroupIDSwizzling/ThreadGroupTilingX.hlsl"

struct PixelBalancingShaderHelper
{
    uint3 _outputPixel;
    uint _thisPassSampleOffset;
    uint _thisPassSampleCount;
    uint _thisPassSampleStride;
    uint _totalSampleCount;
    bool _firstDispatch;
};

struct SamplingShaderUniforms { uint4 B; };
uint SamplingShaderUniforms_GetThisPassSampleOffset(SamplingShaderUniforms uniforms) { return uniforms.B.x; }
uint SamplingShaderUniforms_GetThisPassSampleCount(SamplingShaderUniforms uniforms) { return uniforms.B.y; }
uint SamplingShaderUniforms_GetThisPassSampleStride(SamplingShaderUniforms uniforms) { return uniforms.B.z; }
uint SamplingShaderUniforms_GetTotalSampleCount(SamplingShaderUniforms uniforms) { return uniforms.B.w; }

PixelBalancingShaderHelper PixelBalancingShaderCalculate(uint3 groupThreadId, uint3 groupId, uint3 outputDims, SamplingShaderUniforms uniforms)
{
    // Some rules:
    // 1. We can't write to the same pixel one than one for the same dispatch (regrettably)
    // 2. For shaders that don't read from other textures randomly, we can try to reorder the pixel coords for
    //      cache effectiveness; though this might not always help much
    // 3. Ideally we might want to jump though the sample indices in some even manner. the "stride" can be used
    //      for that -- but it's not always easy if we don't know the number of samples per dispatch we're 
    //      going to be doing
    // 4. For very high sample count shaders, we often end up adding very small per-sample results to a large
    //      aggregate number. This isn't ideal for numeric stability -- but there's not any very easy solution
    //      to that without designing the entire solution around that problem
    PixelBalancingShaderHelper result;
    uint2 threadGroupCounts = uint2((outputDims.x+8-1)/8, (outputDims.y+8-1)/8);
    result._outputPixel.xy = ThreadGroupTilingX(threadGroupCounts, uint2(8, 8), 8, groupThreadId.xy, groupId.xy);
    result._outputPixel.z = groupId.z;
    result._thisPassSampleOffset = SamplingShaderUniforms_GetThisPassSampleOffset(uniforms);
    result._thisPassSampleCount = SamplingShaderUniforms_GetThisPassSampleCount(uniforms);
    result._thisPassSampleStride = SamplingShaderUniforms_GetThisPassSampleStride(uniforms);
    result._totalSampleCount = SamplingShaderUniforms_GetTotalSampleCount(uniforms);
    result._firstDispatch = SamplingShaderUniforms_GetThisPassSampleOffset(uniforms) == 0;
    return result;
}
