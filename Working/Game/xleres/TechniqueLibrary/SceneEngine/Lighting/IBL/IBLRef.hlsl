// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(IBL_REF_H)
#define IBL_REF_H

#include "IBLAlgorithm.hlsl"

#if !defined(SampleLevelZero_Default)
    #define SampleLevelZero_Default(t, x) t.SampleLevel(DefaultSampler, x, 0)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
    //  Reference specular reflections
///////////////////////////////////////////////////////////////////////////////////////////////////

float3 SampleSpecularIBL_Ref(
    float3 normal, float3 viewDirection,
    SpecularParameters specParam, TextureCube tex,
    uint sampleOffset, uint sampleCount, uint totalSampleCount)
{
    // This is a reference implementation of glossy specular reflections from a
    // cube map texture. It's too inefficient to run at real-time. But it provides
    // a visual reference.
    // In the typical final-result, we must use precalculated lookup tables to reduce
    // the number of samples down to 1.
    // We will sample a fixed number of directions across the peak area of the GGX
    // specular highlight. We return the mean of those sample directions.
    // As standard, we use a 2D Hammersley distribution to generate sample directions.
    //
    // For reference, see
    // GPU Gems article
    //      http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html
    // Unreal course notes
    //      http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf

    float3 up = (abs(normal).y < 0.5f) ? float3(0,1,0) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = cross(normal, tangentX);

    float3 viewTangentSpace = float3(dot(viewDirection, tangentX), dot(viewDirection, tangentY), dot(viewDirection, normal));

    float alphad = RoughnessToDAlpha(specParam.roughness);
    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (uint s=0u; s<sampleCount; ++s) {
            // We could build a distribution of "H" vectors here,
            // or "L" vectors. It makes sense to use H vectors
        float2 xi = HammersleyPt(s+sampleOffset, totalSampleCount);
        precise float3 halfVectorTangentSpace = SamplerHeitzGGXVNDF_Pick(viewTangentSpace, alphad, alphad, xi.x, xi.y);
        float3 H = halfVectorTangentSpace.x * tangentX + halfVectorTangentSpace.y * tangentY + halfVectorTangentSpace.z * normal;
        float3 L = 2.f * dot(viewDirection, H) * H - viewDirection;

            // Now we can light as if the point on the reflection map
            // is a directonal light

            // note -- "CalculateSpecular" has NdotL term built-in
        float3 lightColor = SampleLevelZero_Default(tex, AdjSkyCubeMapCoords(L)).rgb;
        precise float3 brdf_costheta = CalculateSpecular(normal, viewDirection, L, H, specParam); // (also contains NdotL term)
        float pdf = SamplerHeitzGGXVNDF_PDFh(halfVectorTangentSpace, viewTangentSpace, alphad);
        // change-of-variables conversion for this (see notes in GenerateSplitTerm)
        pdf = pdf / (4.0f * dot(viewDirection, H));
        result += lightColor * brdf_costheta / pdf;
    }

    return result / float(totalSampleCount);
}

#if 0
float3 SampleTransmittedSpecularIBL_Ref(
    float3 normal, float3 viewDirection,
    SpecularParameters specParam,
    float iorIncident, float iorOutgoing,
    TextureCube tex,
    uint passSampleCount, uint passIndex, uint passCount)
{
    float3 ot = viewDirection;

        // This is a reference implementation of transmitted IBL specular.
        // We're going to follow the same method and microfacet distribution as
        // SampleSpecularIBL_Ref
    float alphad = RoughnessToDAlpha(specParam.roughness);
    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (uint s=0u; s<passSampleCount; ++s) {
        // using the same distribution of half-vectors that we use for reflection
        // (except we flip the normal because of the way the equation is built)
        precise float3 H = SampleMicrofacetNormalGGX(s*passCount+passIndex, passSampleCount*passCount, normal, alphad);

        // following Walter07 here, we need to build "i", the incoming direction.
        // Actually Walter builds the outgoing direction -- but we need to reverse the
        // equation and get the incoming direction.
        float3 i;
        if (!CalculateTransmissionIncident(i, ot, H, iorIncident, iorOutgoing))
            continue;

        // float3 test = CalculateTransmissionOutgoing(i, H, iorIncident, iorOutgoing);
        // if (abs(length(test - ot)) > 0.001f) return float3(1,0,0);

        // ok, we've got our incoming vector. We can do the cube map lookup
        // Note that when we call "CalculateSpecular", it's going to recalculate
        // the transmission half vector and come out with the same result.
        float3 lightColor = SampleLevelZero_Default(tex, AdjSkyCubeMapCoords(i)).rgb;
        // precise float3 brdf = CalculateSpecular(normal, viewDirection, i, H, specParam); // (also contains NdotL term)

        float bsdf;
        //GGXTransmission(
        //    iorIncident, iorOutgoing, specParam.roughness,
        //    i, viewDirection, -normal,        // (note flipping normal)
        //    bsdf);

        bsdf = 1.f;
        bsdf *= SmithG(abs(dot( i,  normal)), RoughnessToGAlpha(specParam.roughness));
        bsdf *= SmithG(abs(dot(ot,  normal)), RoughnessToGAlpha(specParam.roughness));
        bsdf *= TrowReitzD(abs(dot( H, normal)), alphad);

        // One term from the Walter07 paper is causing problems... This is the
        // only brightness term that uses the index of refraction. So it may be intended
        // to to take into account focusing of light. Sure enough, without this term the
        // refraction tends to match the brightness of the incoming light closely.
        //
        // However, with it, it produces numbers that are quite high (> 100.f) and it's
        // making our solution just too bright... For large iorOutgoing values, it seems fine
        //  -- but it's just causing problems when iorOutgoing approaches 1.f)
        // It's not clear at the moment what the best solution is for dealing with this term.
        // I want to know what the derivation is, and why it gets so bright as iorOutgoing
        // approaches zero.
        //
        // My feeling is that this term is intended to actually be the term below (with a sign
        // reversed). This new equation equals "4" when iorIncident == iorOutgoing, and I think
        // that 4 might have been factored out with the "4" in the denominator in the microfacet
        // equation.
        //
        // However, on further inspection, this "4" is causing problems.
        // When iorOutgoing == iorIncident == 1 and dot(i, H) = -dot(ot, H) = +/-1, this term
        // must equal 1 (meaning all light is refracted in a straight line, with no focusing).
        // The unmodified equation comes to 1.f/4.f. So we have to compensate for the 4 in the numerator.
        // Actually, Walter07 comments that situations where the indicies of refraction are equal are
        // ill-defined... However, it seems reasonable that this equation should converge on 1.
        // Indeed, other renderers behave as if this term is converging on one.
        //
        // I've tried a few variations on this equation, and eventually taken a guess at an equation
        // that looks right. But this is just a guess! I need to do some further research on this.
        #if 0
            bsdf *= Sq(iorOutgoing) / Sq(iorIncident * dot(i, H) + iorOutgoing * dot(ot, H));
        #else
            bsdf *= Sq(iorOutgoing + iorIncident) / Sq(iorIncident * dot(i, H) - iorOutgoing * dot(ot, H));

            // bsdf *= Sq(Sq(dot(i, H)) + abs(dot(ot, H))) / (iorOutgoing/iorIncident*abs(dot(ot, H)));
            // float eta = iorIncident/iorOutgoing;
            // float cosO = abs(dot(i, H));
            // float mut = sqrt(cosO + eta*eta - 1.f);
            // bsdf *= (eta * mut) / Sq(cosO - mut);
        #endif

        bsdf *= abs(dot(i, H)) * abs(dot(ot, H));
        bsdf /= abs(dot(i, normal)) * abs(dot(ot, normal));

            // todo -- these needs to be fixed for flipped mode!
        //bsdf *= GGXTransmissionFresnel(
        //    i, viewDirection, specParam.F0.g,
        //    iorIncident, iorOutgoing);

        // calculating the fresnel effect to match the reflection case
        float q = SchlickFresnelCore(abs(dot(viewDirection, H)));
        // bsdf *= 1.f - lerp(specParam.F0.g, 1.f, q);
        bsdf *= (1.f - specParam.F0.g) * (1.f - q);

        bsdf *= -dot(i, normal);

        // We have to apply the distribution weight. Since our half vectors are distributed
        // in the same fashion as the reflection case, we should have the same weight.
        // (But we need to consider the flipping that occurs)
        float pdfWeight = InversePDFWeight(H, normal, viewDirection, alphad);

        result += lightColor * bsdf * pdfWeight;
    }

    return result / float(passSampleCount);
}
#endif

float3 SampleDiffuseIBL_Ref(
    float3 normal, TextureCube tex,
    uint passSampleCount, uint passIndex, uint passCount)
{
    // As per the specular solutions, let's sample the environment randomly
    // and build a reference brightness value for diffuse.
    // We will sample incident directions according to a pdf similar
    // to the diffuse equation (eg, just NdotL; even if we're using a more complex
    // diffuse equation, this is a good approximation)

    float3 up = (abs(normal).y < 0.5f) ? float3(0,1,0) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = cross(normal, tangentX);

    float3 result = float3(0.0f, 0.0f, 0.0f);
    for (uint s=0u; s<passSampleCount; ++s) {
        float2 xi = HammersleyPt(s*passCount+passIndex, passSampleCount*passCount);
        float pdf;
        float3 i_tangentSpace = SamplerCosineHemisphere_Pick(pdf, xi);
        float3 i = i_tangentSpace.x * tangentX + i_tangentSpace.y * tangentY + i_tangentSpace.z * normal;
        float3 lightColor = SampleLevelZero_Default(tex, AdjSkyCubeMapCoords(i)).rgb;
        // here, our lighting equation (lambert diffuse) is just the same as the pdf -- so it's gets factored out
        result += lightColor;
    }

    const float normalizationFactor = 1.f/pi;
    return result / float(passSampleCount) * normalizationFactor;
}

#endif
