// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(IBL_PRECALC_H)
#define IBL_PRECALC_H

#include "IBLAlgorithm.hlsl"
#include "../../../LightingEngine/LightingAlgorithm.hlsl"
#include "../../../LightingEngine/SpecularMethods.hlsl"

///////////////////////////////////////////////////////////////////////////////////////////////////
    //  Split-term LUT
///////////////////////////////////////////////////////////////////////////////////////////////////

float2 GenerateSplitTerm(
    float NdotV, float roughness,
    uint passSampleCount, uint passIndex, uint passCount)
{
    // This generates the lookup table used by the glossy specular reflections
    // split sum approximation.
    // Based on the method presented by Unreal course notes:
    //  http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
    // While referencing the implementation in "IBL Baker":
    //  https://github.com/derkreature/IBLBaker/
    // We should maintain our own version, because we need to take into account the
    // same remapping on "roughness" that we do for run-time specular.
    //
    // For the most part, we will just be integrating the specular equation over the
    // hemisphere with white incoming light. We use importance sampling with a fixed
    // number of samples to try to make the work load manageable...

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
    alphag = alphad;
    precise float G2 = SmithG(NdotV, alphag);

    precise float A = 0.f, B = 0.f;
    LOOP_DIRECTIVE for (uint s=0u; s<passSampleCount; ++s) {
        // Note that "HammersleyPt" always produces (0,0) as the first points
        //      -- this will become a direction equal to "normal"
        float2 xi = HammersleyPt(s*passCount+passIndex, passSampleCount*passCount);

        float3 tangentSpaceHalfVector = GGXHalfVector_Sample(xi, alphad);
        // float3 tangentSpaceHalfVector = HeitzGGXVNDF_Sample(V, alphad, alphad, xi.x, xi.y);
        precise float3 H = tangentSpaceHalfVector;  //(normal is fixed, so we don't need TransformByArbitraryTangentFrame(tangentSpaceHalfVector, normal);
        precise float3 L = 2.f * dot(V, H) * H - V;

        precise float NdotL = L.z;
        precise float NdotH = H.z;
        precise float VdotH = dot(V, H);

        if (NdotL > 0.0f) {
                // using "precise" here has a massive effect...
                // Without it, there are clear errors in the result.
            precise float G = SmithG(NdotL, alphag) * G2;

            // F0 gets factored out of the equation here
            // the result we will generate is actually a scale and offset to
            // the runtime F0 value.
            precise float F = pow(1.f - VdotH, 5.f);

            // Remember that our expected value estimation of the integral is
            // 1/sampleCount * sum( f(x) / p(x) ), where f(x) is the function we're integrating and p(x) is the pdf

            #if !defined(OLD_M_DISTRIBUTION_FN)
                precise float D = TrowReitzD(NdotH, alphad);
                precise float pdf = GGXHalfVector_PDF(tangentSpaceHalfVector, alphad);
                // precise float pdf = HeitzGGXVNDF_PDF(tangentSpaceHalfVector, V, alphad);

                // See PBR book chapter 14.1.1. Our pdf is distributing half vectors, but the integral we're estimating
                // is w.r.t the incident (or exident) light direction. Half vectors are obviously more tightly distributed,
                // by nature of how to reflect the light. As a result they are more "densly" distributed, so obviously the
                // pdf over the hemisphere is different.
                //
                // Convert by calculating d omega-h / d omega-i (see pbr book for the working out here)
                pdf = pdf / (4.0f * VdotH);

                precise float specular = D*G/(4.0*NdotL*NdotV);

                // We stil consider the incident light "radiance" -- meaning we have to include that term
                // that takes into account the orientation of incoming light to the surface
                // without this, samples at shearing angles contribute too much overall
                specular *= NdotL;
            #else
                // This factors out the D term, and introduces some other terms.
                //      pdf inverse = 4.f * VdotH / (D * NdotH)
                //      specular eq = D*G*F / (4*NdotL*NdotV)
                precise float specular = G * VdotH / (NdotH * NdotV);  // (excluding F term)
                precise float pdf = 1.0f;
            #endif

            A += ((1.f - F) * specular) / (pdf * float(passSampleCount));
            B += (F * specular) / (pdf * float(passSampleCount));
        }
    }

    // Note that we're assuming that each "pass" is equally weight here. This is only true because we're
    // interleaving the samples (ie, it's not like we do all of the high cosTheta samples in a single pass)

    return float2(A, B) / float(passCount);
}

float GenerateSplitTermTrans(
    float NdotV, float roughness,
    float iorIncident, float iorOutgoing,
    uint passSampleCount, uint passIndex, uint passCount)
{
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
}

///////////////////////////////////////////////////////////////////////////////////////////////////
    //  Filtered textures
///////////////////////////////////////////////////////////////////////////////////////////////////

float3 GenerateFilteredSpecular(
    float3 cubeMapDirection, float roughness,
    uint passSampleCount, uint passIndex, uint passCount)
{
    // Here is the key simplification -- we assume that the normal and view direction are
    // the same. This means that some distortion on extreme angles is lost. This might be
    // incorrect, but it will probably only be noticeable when compared to a ray traced result.
    precise float3 normal = cubeMapDirection;
    precise float3 viewDirection = cubeMapDirection;

    // Very small roughness values break this equation, because D converges to infinity
    // when NdotH is 1.f. We must clamp roughness, or this convergence produces bad
    // floating point numbers.
    SpecularParameters specParam = SpecularParameters_RoughF0(roughness, float3(1.0f, 1.0f, 1.0f));

    precise float alphag = RoughnessToGAlpha(specParam.roughness);
    precise float alphad = RoughnessToDAlpha(max(roughness, MinSamplingRoughness));

    precise float3 result = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.f;

    #if 0
        precise float3 L0 = 2.f * dot(viewDirection, normal) * normal - viewDirection;
        precise float brdf0 = CalculateSpecular(normal, viewDirection, L0, normal, specParam).g;
    #else
        float brdf0 = 1.f;  // gets factored out completely. So don't bother calculating.
    #endif

        // We need a huge number of samples for a smooth result
        // Perhaps we should use double precision math?
        // Anyway, we need to split it up into multiple passes, otherwise
        // the GPU gets locked up in the single draw call for too long.
    LOOP_DIRECTIVE for (uint s=0u; s<passSampleCount; ++s) {

            // We could build a distribution of "H" vectors here,
            // or "L" vectors. It makes sense to use H vectors
        precise float3 H = SampleMicrofacetNormalGGX(s*passCount+passIndex, passSampleCount*passCount, normal, alphad);
        precise float3 L = 2.f * dot(viewDirection, H) * H - viewDirection;

            // Sampling the input texture here is effectively sampling the incident light from a given direction
            // The math for "split-sum" assumes that the incidient light is uniform. But, in a way, we're integrating
            // the reflected light assuming that uniform incident light, and then modulating the integration with a
            // single tap from the filtered cube map.
            //
            // We could meet the strict split-sum function by not filtering the texture at all and instead just copy
            // the values from a single tap of our environmental map... But that's not very satisfying, because we're 
            // relying on this filtering to give the right kind of look to the final result.
            //
            // So we have some flexibility about how to filter the texture; and we have a goal in that we want the
            // filtering to reflect the shape of the BRDF.
            // One way to do that is to use the BRDF itself as a filtering function. However when we do this, we 
            // need to be sure that we're only changing the blurring and not impacting the overall brightness of 
            // the end result tap -- recalling that it's actually the other part of split-sum equation that is 
            // more important for overall brightness.
            //
            // There's also a question about whether we should weight the results using the InversePDFWeight() function
            // That reflects the distribution of samples we're making with SampleMicrofacetNormalGGX. If we use that
            // weighting, it means we can take more samples around the area where we expect greatest variation, but then
            // readjust so that it's as if we'd sampled each direction evenly...
            // The problem here is that SampleMicrofacetNormalGGX() accounts for the area of variation in the filtering 
            // function, but not in the texture we're sampling in... Which means we can still end up with few samples around
            // bright areas in the image (ie, typically the sun)
        precise float3 incidentLight = IBLPrecalc_SampleInputTexture(L);

        const uint Method_Unreal = 0u;
        const uint Method_PDF = 2u;
        const uint Method_Constant = 3u;
        const uint Method_Balanced = 4u;
        const uint Method_NdotH = 5u;
        const uint Method_Complex = 6u;
        const uint method = Method_Complex;
        float weight;
        if (method == Method_Unreal) {
            // This is the method from the unreal course notes
            // It seems to distort the shape of the GGX equation
            // slightly.
            weight = saturate(dot(normal, L));
        } else if (method == Method_PDF) {
            // This method weights the each point by the GGX sampling pdf associated
            // with this direction. This may be a little odd, because it's in effect
            // just squaring the effect of "constant" weighting. However it does
            // look like, and sort of highlights the GGX feel. It also de-emphasises
            // the lower probability directions.
            weight = 1.f/InversePDFWeight(H, normal, viewDirection, alphad);
        } else if (method == Method_Balanced) {
            // In this method, we want to de-emphasise the lowest probability samples.
            // the "pdf" represents the probability of getting a certain direction
            // in our sampling distribution.
            // Any probabilities higher than 33%, we're going to leave constant
            // -- because that should give us a shape that is most true to GGX
            // For lower probability samples, we'll quiet them down a bit...
            // Note "lower probability samples" really just means H directions
            // that are far from "normal" (ie, "theta" in SampleMicrofacetNormalGGX is high)
            float pdf = 1.f/InversePDFWeight(H, normal, viewDirection, alphad);
            weight = saturate(pow(pdf * 3.f, 3.f));
        } else if (method == Method_Constant) {
            // Constant should just give us an even distribution, true to the
            // probably distribution in SampleMicrofacetNormalGGX
            weight = 1.f;
        } else if (method == Method_NdotH) {
            // seems simple and logical, and produces a good result
            weight = abs(dot(normal, H));
        } else if (method == Method_Complex) {
            // This method is more expensive... But it involves weighting each sample
            // by the full specular equation. So we get the most accurate blurring.
            // Note that "brdf1 * InversePDFWeight" factors out the D term.
            // Also, brdf0 just gets factored out completely (doing this in greyscale because
            // F0 should be 1.0.xxx here.
            precise float brdf1 = CalculateSpecular(normal, viewDirection, L, H, specParam).g;
            weight = brdf1 / brdf0 * InversePDFWeight(H, normal, viewDirection, alphad);
        }
        result += incidentLight * weight;
        totalWeight += weight;
    }

    // Might be more accurate to divide by "PassSampleCount" here, and then later on divide
    // by PassCount...?
    return result / totalWeight;
    // return result / (totalWeight + 1e-6f);
    // return result / float(passSampleCount);
}

float3 SampleNormal(float3 core, uint s, uint sampleCount)
{
    float theta = 2.f * pi * float(s)/float(sampleCount);
    const float variation = 0.2f;
    float3 H = float3(variation * cos(theta), variation * sin(theta), 0.f);
    H.z = sqrt(1.f - H.x*H.x - H.y*H.y);
    float3 up = abs(core.z) < 0.999f ? float3(0,0,1) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, core));
    float3 tangentY = cross(core, tangentX);
    return tangentX * H.x + tangentY * H.y + core * H.z;
}

float3 CalculateFilteredTextureTrans(
    float3 cubeMapDirection, float roughness,
    float iorIncident, float iorOutgoing,
    uint passSampleCount, uint passIndex, uint passCount)
{
    float3 corei = cubeMapDirection;
    float3 coreNormal = (iorIncident < iorOutgoing) ? corei : -corei;

    // Very small roughness values break this equation, because D converges to infinity
    // when NdotH is 1.f. We must clamp roughness, or this convergence produces bad
    // floating point numbers.
    roughness = max(roughness, MinSamplingRoughness);
    float alphad = RoughnessToDAlpha(roughness);
    float alphag = RoughnessToGAlpha(roughness);

    float totalWeight = 0.f;
    float3 result = float3(0.0f, 0.0f, 0.0f);
    LOOP_DIRECTIVE for (uint s=0u; s<passSampleCount; ++s) {

        // Ok, here's where it gets complex. We have a "corei" direction,
        //  -- "i" direction when H == normal. However, there are many
        // different values for "normal" that we can use; each will give
        // a different "ot" and a different sampling of H values.
        //
        // Let's take a sampling approach for "normal" as well as H. Each
        // time through the loop we'll pick a random normal within a small
        // cone around corei. This is similar to SampleMicrofacetNormalGGX
        // an it will have an indirect effect on the distribution of
        // microfacet normals.
        //
        // It will also effect the value for "ot" that we get... But we want
        // the minimize the effect of "ot" in this calculation.

        float3 normal = SampleNormal(coreNormal, s, passSampleCount);

        // There seems to be a problem with this line...? This refraction step
        // is causing too much blurring, and "normal" doesn't seem to have much
        // effect
        // float3 ot = refract(-corei, -normal, iorIncident/iorOutgoing);

        float3 ot = CalculateTransmissionOutgoing(corei, normal, iorIncident, iorOutgoing);
        if (dot(ot, ot) == 0.0f) continue;      // no refraction solution

        //float3 test = refract(-ot, normal, iorOutgoing/iorIncident);
        //if (length(test - corei) > 0.001f)
        //    return float3(1, 0, 0);

#if 1
        precise float3 H = SampleMicrofacetNormalGGX(
            s*passCount+passIndex, passSampleCount*passCount,
            normal, alphad);

        // float3 ot = CalculateTransmissionOutgoing(i, H, iorIncident, iorOutgoing);
        float3 i;
        if (!CalculateTransmissionIncident(i, ot, H, iorIncident, iorOutgoing))
            continue;
#else
        precise float3 i = SampleMicrofacetNormalGGX(
            s*passCount+passIndex, passSampleCount*passCount,
            corei, alphad);
#endif

        float3 incidentLight = IBLPrecalc_SampleInputTexture(i);

#if 1
        float bsdf;
        bsdf = 1.f;
        bsdf *= SmithG(abs(dot( i,  normal)), RoughnessToGAlpha(roughness));
        bsdf *= SmithG(abs(dot(ot,  normal)), RoughnessToGAlpha(roughness));
        bsdf *= TrowReitzD(abs(dot( H, normal)), alphad);
        // bsdf *= Sq(iorOutgoing) / Sq(iorIncident * dot(i, H) - iorOutgoing * dot(ot, H));
        bsdf /= max(0.005f, 4.f * RefractionIncidentAngleDerivative2(dot(ot, H), iorIncident, iorOutgoing));
        bsdf *= abs(dot(i, H)) * abs(dot(ot, H));
        bsdf /= abs(dot(i, normal)) * abs(dot(ot, normal));
        //bsdf *= GGXTransmissionFresnel(
        //    i, viewDirection, specParam.F0.g,
        //    iorIncident, iorOutgoing);

        bsdf *= 1.0f - SchlickFresnelCore(saturate(dot(ot, H)));
        bsdf *= -dot(i, normal);

        float weight = bsdf * InversePDFWeight(H, coreNormal, float3(0.0f, 0.0f, 0.0f), alphad);
#endif

        result += incidentLight * weight;
        totalWeight += weight;
    }

    return result / (totalWeight + 1e-6f);
}


#endif
