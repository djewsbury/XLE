// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "sampling-shader-helper.hlsl"
#include "../TechniqueLibrary/LightingEngine/SpecularMethods.hlsl"
#include "../TechniqueLibrary/LightingEngine/LightingAlgorithm.hlsl"
#include "../TechniqueLibrary/LightingEngine/IBL/IBLAlgorithm.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //  Split-term LUT
///////////////////////////////////////////////////////////////////////////////////////////////////

float2 GenerateSplitTerm(
    float NdotV, float roughness,
    uint thisPassSampleBegin, uint thisPassSampleCount, uint sampleStride, uint totalSampleCount)
{
    // This generates the lookup table used by the glossy specular reflections
    // split sum approximation.
    // Based on the method presented by Unreal course notes:
    //  http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

    precise float3 normal = float3(0.0f, 0.0f, 1.0f);
    precise float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

        // Karis suggests that using our typical Disney remapping
        // for the alpha value for the G term will make IBL much too dark.
        // Indeed, it changes the output texture a lot.
        // However, it seems like a good improvement to me. It does make
        // low roughness materials slightly darker. However, it seems to
        // have the biggest effect on high roughness materials. These materials
        // get much lower reflections around the edges. This seems to be
        // good, however... Because without it, low roughness get a clear
        // halo around their edges. This doesn't happen so much with the
        // runtime specular. So it actually seems better with the this remapping.
    precise float alphag = RoughnessToGAlpha(roughness);
    precise float alphad = RoughnessToDAlpha(max(roughness, MinSamplingRoughness));
    precise float G2 = SmithG(NdotV, alphag);

    precise float A = 0.0, B = 0.0;
    LOOP_DIRECTIVE for (uint s=0u; s<thisPassSampleCount; ++s) {
        // Note that "HammersleyPt" always produces (0,0) as the first points
        //      -- this will become a direction equal to "normal"
        uint sampleIdx = (s*sampleStride+thisPassSampleBegin)%totalSampleCount;
        precise float2 xi = HammersleyPt(sampleIdx, totalSampleCount);

        // float3 tangentSpaceHalfVector = SamplerGGXHalfVector_Pick(xi, alphad);
        precise float3 tangentSpaceHalfVector = SamplerHeitzGGXVNDF_Pick(V, alphad, alphad, xi.x, xi.y);
        precise float3 H = tangentSpaceHalfVector;  //(normal is fixed, so we don't need TransformByArbitraryTangentFrame(tangentSpaceHalfVector, normal);
        precise float3 L = 2.f * dot(V, H) * H - V;

        precise float NdotL = L.z;
        precise float NdotH = H.z;
        precise float VdotH = dot(V, H);

        if (NdotL <= 0.0f) continue;

        // using "precise" here has a massive effect...
        // Without it, there are clear errors in the result.
        //
        // Remember that our expected value estimation of the integral is
        // 1/sampleCount * sum( f(x) / p(x) ), where f(x) is the function we're integrating and p(x) is the pdf

        precise float specular, pdf;
        const bool useVDNFOptimizations = true;
        if (!useVDNFOptimizations) {
            precise float G = SmithG(NdotL, alphag) * G2;
            precise float D = TrowReitzD(NdotH, alphad);
            // pdf = SamplerGGXHalfVector_PDF(tangentSpaceHalfVector, alphad);
            pdf = SamplerHeitzGGXVNDF_PDFh(tangentSpaceHalfVector, V, alphad);

            // See PBR book chapter 14.1.1. Our pdf is distributing half vectors, but the integral we're estimating
            // is w.r.t the incident (or excident) light direction. Half vectors are obviously more tightly distributed,
            // by nature of how to reflect the light. As a result they are more "densely" distributed, so obviously the
            // pdf over the hemisphere is different.
            //
            // Convert by calculating d omega-h / d omega-i (see pbr book for the working out here)
            pdf = pdf / (4.0f * VdotH);

            specular = D*G/(4.0*NdotL*NdotV);

            // We still consider the incident light "radiance" -- meaning we have to include that term
            // that takes into account the orientation of incoming light to the surface
            // without this, samples at shearing angles contribute too much overall
            specular *= NdotL;
        } else {
            // Due to how VDNF works, we can 
            // pdf = D * excid_G * VdotM / (4 * VdotN * VdotM)
            // specular = D*incid_G*excid_G/(4.0*NdotL*VdotN);
            // factored -- incid_G / NdotL
            //  (plus multiply by NdotL at the end)
            specular = SmithG(NdotL, alphag);
            pdf = 1.0;
        }

        precise float F = SchlickFresnelCore(VdotH);
        // adding small numbers to large numbers -- probably not ideal for precision
        // todo -- consider kahan sum here -- https://en.wikipedia.org/wiki/Kahan_summation_algorithm 
        // we have to do some protection against infinites for nvidia hardware
        float a = ((1.f - F) * specular) / pdf;
        float b = (F * specular) / pdf;
        if (a == a) A += a;
        if (b == b) B += b;
    }

    precise float reciprocalN = 1.0 / (totalSampleCount);
    return float2(A, B) * reciprocalN;
}

float GenerateSplitTermTrans(
    float NdotV, float roughness,
    float iorIncident, float iorOutgoing,
    uint passSampleCount, uint passIndex, uint passCount)
{
#if 0
    // This is for generating the split-term lookup for specular transmission
    // It uses the same approximations and simplifications as for reflected
    // specular.
    // Note that we're actually assuming a constant iorOutgoing for all materials
    // This might be ok, because the effect of this term also depends on the geometry
    // arrangement (ie, how far away refracted things are). So maybe it's not a
    // critical parameter for materials (even though it should in theory be related
    // to the "specular" parameter)

    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);

    float3 ot = V;

    roughness = max(roughness, MinSamplingRoughness);
    float alphad = RoughnessToDAlpha(roughness);

    uint goodSampleCount = 0u;
    float A = 0.f;
    LOOP_DIRECTIVE for (uint s=0u; s<passSampleCount; ++s) {

        precise float3 H = SampleMicrofacetNormalGGX(
            s*passCount+passIndex, passSampleCount*passCount, normal, alphad);

        float3 i;
        if (!CalculateTransmissionIncident(i, ot, H, iorIncident, iorOutgoing))
            continue;

        // As per the reflection case, our probability distribution function is
        //      D * NdotH / (4 * VdotH)
        // However, it doesn't factor out like it does in the reflection case.
        // So, we have to do the full calculation, and then apply the inverse of
        // the pdf afterwards.

        float bsdf;
        #if 0
            GGXTransmission(
                roughness, iorIncident, iorOutgoing,
                i, ot, normal,
                bsdf);
        #else
            bsdf = 1.f;
            bsdf *= SmithG(abs(dot( i,  normal)), RoughnessToGAlpha(roughness));
            bsdf *= SmithG(abs(dot(ot,  normal)), RoughnessToGAlpha(roughness));
            bsdf *= TrowReitzD(abs(dot( H, normal)), alphad);

            // bsdf *= Sq(iorOutgoing) / Sq(iorIncident * dot(i, H) - iorOutgoing * dot(ot, H));
            // bsdf *= Sq(iorIncident/iorOutgoing);
            bsdf /= max(0.005f, 4.f * RefractionIncidentAngleDerivative2(dot(ot, H), iorIncident, iorOutgoing));
            // bsdf /= 2.f * RefractionIncidentAngleDerivative(dot(ot, H), iorIncident, iorOutgoing);
            #if 0
            // This is an equation from Stam's paper
            //  "An Illumination Model for a Skin Layer Bounded by Rough Surfaces"
            // it's solving a similar problem, but the math is very different.
            {
                float eta = iorIncident/iorOutgoing;
                float cosO = abs(dot(i, H));
                float mut = abs(dot(ot, H));
                // float mut = sqrt(cosO*cosO + Sq(1.f/eta) - 1.f);
                // float mut = sqrt(1.f + eta*eta*(cosO*cosO - 1.f));
                // float mut = sqrt(1.f - Sq(eta)*(1.f - cosO*cosO));
                bsdf *= (eta * mut) / Sq(cosO - mut);
            }
            #endif

            bsdf *= abs(dot(i, H)) * abs(dot(ot, H));
            bsdf /= abs(dot(i, normal)) * abs(dot(ot, normal));
        #endif

        bsdf *= -dot(i, normal);

        float pdfWeight = InversePDFWeight(H, normal, V, alphad);

        // We have to use "saturate" here, not "abs" -- this gives us a better black edge around the outside
        // Using "abs" is too tolerant and edge never drops off to zero.
        precise float F = 1.f - SchlickFresnelCore(saturate(dot(V, H)));

        // bsdf = 1.f / RefractionIncidentAngleDerivative(dot(ot, H), iorIncident, iorOutgoing);
        // bsdf = 1.f / max(0.005f, RefractionIncidentAngleDerivative2(dot(ot, H), iorIncident, iorOutgoing));
        // bsdf = Sq(iorOutgoing + iorIncident) / Sq(iorIncident * dot(i, H) - iorOutgoing * dot(ot, H));
        // F = 1.f;

        A += F * bsdf * pdfWeight;

        goodSampleCount++;
    }

    return A / float(passSampleCount);
#else
    return 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RWTexture2D<float4> Output : register(u1, space0);

[[vk::push_constant]] struct ControlUniformsStruct
{
    SamplingShaderUniforms _samplingShaderUniforms;
    uint4 A;
} ControlUniforms;
uint ControlUniforms_GetMipIndex() { return ControlUniforms.A.x; }

[numthreads(8, 8, 1)]
    void GenerateSplitSumGlossLUT(uint3 groupThreadId : SV_GroupThreadID, uint3 groupId : SV_GroupID)
{
    uint2 textureDims;
	Output.GetDimensions(textureDims.x, textureDims.y);

    PixelBalancingShaderHelper helper = PixelBalancingShaderCalculate(groupThreadId, groupId, uint3(textureDims, 1), ControlUniforms._samplingShaderUniforms);
    if (any(helper._outputPixel >= uint3(textureDims, 1))) return;

    float2 texCoord = helper._outputPixel.xy / float2(textureDims);
    float NdotV = max(texCoord.x, .1/float(textureDims.x));  // (add some small amount just to get better values in the lower left corner)
    float roughness = texCoord.y;

    if (helper._firstDispatch)
        Output[helper._outputPixel.xy] = 0;
    Output[helper._outputPixel.xy].xy += GenerateSplitTerm(
        NdotV, roughness,
        helper._thisPassSampleOffset, helper._thisPassSampleCount, helper._thisPassSampleStride, helper._totalSampleCount);
}

float4 GenerateSplitSumGlossTransmissionLUT(float4 position : SV_Position, float2 texCoord : TEXCOORD0) : SV_Target0
{
    float NdotV = texCoord.x;
    float roughness = texCoord.y;
    const uint sampleCount = 64 * 1024;

    const uint ArrayIndex = 0;

    float specular = saturate(0.05f + ArrayIndex / 32.f);
    float iorIncident = F0ToRefractiveIndex(SpecularParameterToF0(specular));
    float iorOutgoing = 1.f;
    return float4(GenerateSplitTermTrans(NdotV, roughness, iorIncident, iorOutgoing, sampleCount, 0, 1), 0, 0, 1);
}

