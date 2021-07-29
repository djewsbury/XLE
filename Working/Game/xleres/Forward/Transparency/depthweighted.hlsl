// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../illum.pixel.hlsl"

struct DepthWeightedOutput
{
    float4 accumulator  : SV_Target0;
    float4 modulator    : SV_Target1;
    float3 refraction   : SV_Target2;
};

DepthWeightedOutput BuildDepthWeightedOutput(float4 color, VSOUT geo, SystemInputs sys)
{
    DepthWeightedOutput result;

    float coverage = 1.f;
    float3 transmissionCoefficient = (1.0f - color.a).xxx;
    float3 premultipliedReflectionAndEmission = color.rgb * color.a;

    result.modulator.rgb = coverage * (1.0.xxx - transmissionCoefficient);
    coverage *= 1.0f - (transmissionCoefficient.r + transmissionCoefficient.g + transmissionCoefficient.b) * (1.0f / 3.0f);

    float tmp = 1.0f - geo.position.z * 0.99f;
    float w = clamp(coverage * tmp * tmp * tmp * 1e3f, 1e-2f, 3e2f * 0.1f);
    result.accumulator = float4(premultipliedReflectionAndEmission, coverage) * w;

    // float backgroundZ = csPosition.z - 4;
    // Vector2 refractionOffset = (etaRatio == 1.0) ? Vector2(0) : computeRefractionOffset(backgroundZ, csNormal, csPosition, etaRatio);
    // float trueBackgroundCSZ = _reconstructCSZ(texelFetch(_depthTexture.sampler, ivec2(gl_FragCoord.xy), 0).r, _clipInfo);

    // const float k_0 = 8.0;
    // const float k_1 = 0.1;

    // _modulate.a = k_0 * coverage * (1.0 - collimation) * (1.0 - k_1 / (k_1 + csPosition.z - trueBackgroundCSZ)) / abs(csPosition.z);
    // _modulate.a *= _modulate.a;
    // if (_modulate.a > 0) {
    //     _modulate.a = max(_modulate.a, 1 / 256.0);
    // }

    // _refraction = refractionOffset * coverage * 8.0;

    result.refraction = 0.0.xxx;
    return result;
}

[earlydepthstencil]
DepthWeightedOutput main_depth_weighted_oi(VSOUT geo, SystemInputs sys)
{
    // This is based on "A Phenomenological Scattering Model for Order-Independent Transparency"
    // by McGuire and Mara.

    GBufferValues sample = IllumShader_PerPixel(geo);

    float3 directionToEye = 0.0.xxx;
    #if (VSOUT_HAS_WORLD_VIEW_VECTOR==1)
        directionToEye = normalize(geo.worldViewVector);
    #endif

    float4 result = float4(
        CalculateIllumination(
            sample, directionToEye, VSOUT_GetWorldPosition(geo),
            LightScreenDest_Create(int2(geo.position.xy), GetSampleIndex(sys))), 1.f);

    #if VSOUT_HAS_FOG_COLOR == 1
		result.rgb = geo.fogColor.rgb + result.rgb * geo.fogColor.a;
	#endif

	result.a = sample.blendingAlpha;

	#if (VSOUT_HAS_COLOR>=1) && (MAT_VCOLOR_IS_ANIM_PARAM==0)
		result.rgb *= geo.color.rgb;
	#endif

	#if MAT_SKIP_LIGHTING_SCALE==0
		// (note --       should we scale by this here? when using this shader with a
		//                basic lighting pipeline [eg, for material preview], the scale is unwanted)
		result.rgb *= LightingScale;
	#endif

	return BuildDepthWeightedOutput(result, geo, sys);
}

Texture2D Accumulator;
Texture2D Modulator;
Texture2D Refraction;

float maxComponent(float3 a) { return max(max(a.x, a.y), a.z); }

float4 resolve(float4 pos : SV_Position) : SV_Target0
{
    int2 pixelCoord = int2(pos.xy);
    float4 mod = Modulator.Load(uint3(pixelCoord, 0));
    float3 backgroundModulation = mod.rgb;
    if (maxComponent(backgroundModulation) == 1.f) discard;

    float4 acc = Accumulator.Load(uint3(pixelCoord, 0));

    if (isinf(acc.a))                   acc.a = maxComponent(acc.rgb);
    if (isinf(maxComponent(acc.rgb)))   acc = (isinf(acc.a) ? 1 : acc.a).xxxx;

    acc.rgb *= 0.5.xxx + backgroundModulation / max(0.01f, 2.0f * maxComponent(backgroundModulation));

    float3 background = 0.0.xxx;
    float3 result
        = background * backgroundModulation
        + (1.0.xxx - backgroundModulation) * acc.rgb / max(acc.a, 1e-5f);
    return float4(result.rgb, maxComponent(backgroundModulation));
}
