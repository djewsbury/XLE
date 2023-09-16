// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SPECULAR_METHODS_H)
#define SPECULAR_METHODS_H

#include "LightingAlgorithm.hlsl"

#if !defined(SPECULAR_METHOD)
    #define SPECULAR_METHOD 1
#endif

    //////////////////////////////////////////////////////////////////////////
        //   G G X                                                      //
    //////////////////////////////////////////////////////////////////////////

float ShadowingMaskingLambda(float cosTheta, float alpha)
{
    // From Walter07 & Heitz14, (capital) lambda function used with Smith-G based shadowing/masking functions
    //  https://jcgt.org/published/0003/02/03/paper.pdf
    // useful when considering height correlation issues.
    // See also pbr-book, which uses this same formulation
    //
    // lambda theta = ( -1 + sqrt(1 + (1/a^2)) ) / 2
    // where a = 1/(alpha*tan(theta))
    //
    // tan(theta) = sqrt(1 - cos^2(theta)) / cos(theta), for 0 <= theta <= pi/2
    float tanTheta = sqrt(1 - cosTheta*cosTheta) / cosTheta;
    float a = 1/(alpha*tanTheta);
    float rcpa = rcp(a);
    return (-1 + sqrt(1 + rcpa*rcpa)) / 2;
}

float SmithG(float cosTheta, float alpha)
{
    // This is one part of the G term in Torrent-Sparrow
    // Note that 'Smith-G' equations like this are generated from a particular
    // 'D' equation, given an assumption about the arrangement of microfacets
    // So this implementation must be used with TrowReitzD
    //
    // This implementation doesn't consider height correlation issues and so
    // over-darkens from certain angles
    //
    // See Heitz14 (Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs)
    //      https://jcgt.org/published/0003/02/03/paper.pdf
    // Heitz breaksdown how the G term is derived, and why the traditional G * G approach
    // can over-shadow when the view and normal are close (along with other properties)
    //
    // G = 1 / (1 + lambda(cosTheta))
    //
    // We can express this another way as (just a different factoring):
    // float a = alpha * alpha;
    // float b = cosTheta * cosTheta;
    // return (2.f * cosTheta) / (cosTheta + sqrt(lerp(b, 1.0f, a)));
    return 1 / (1 + ShadowingMaskingLambda(cosTheta, alpha));
}

float TrowReitzD(float NdotH, float alpha)
{
    // Note that the Disney model generalizes this
    // a little further by making the denomination power
    // variable.
    // They call it "GTR"

    #if 1
        float alphaSqr = alpha * alpha;
        float denom = 1.f + (alphaSqr - 1.f) * NdotH * NdotH;
        // note --  When roughness is 0 and NdotH is 1, denum will be 0
        //          This is causes a singularity in D around +infinity
        //          This makes sense, because in this case the microfacet
        //          normal has a 100% chance to be parallel to the surface
        //          normal. But it really screws with our calculations. We
        //          could clamp here -- or otherwise we could clamp the
        //          minimum roughness value.
        // denom = max(denom, 1e-3f);
        return alphaSqr / (pi * denom * denom);
    #else
        // This the version of D as it appears exactly in
        // Walter07 paper (Only correct when NdotH > 0.f)
        // It's just the same, but expressed differently
        float cosTheta = NdotH;
        // tan^2 + 1 = 1.f / (cos^2)
        // tan = sqrt(1.f / cos^2 - 1);
        float tanThetaSq = 1.f / Sq(NdotH) - 1.f;
        float c4 = Sq(cosTheta); c4 *= c4;
        float a2 = Sq(alpha);
        return a2 / (pi * c4 * Sq(a2 + tanThetaSq));
    #endif
}

float TrowReitzDInverse(float D, float alpha)
{
    // This is the inverse of the GGX "D" normal distribution
    // function. We only care about the [0,1] part -- so we can
    // ignore some secondary solutions.
    //
    // float alphaSq = alpha * alpha;
    // float denom = 1.f + (alphaSq - 1.f) * NdotH * NdotH;
    // return alphaSq / (pi * denom * denom);
    //
    // For 0 <= alpha < 1, there is always a solution for D above around 0.3182
    // For smaller D values, there sometimes is not a solution.

    float alphaSq = alpha * alpha;
    float A = sqrt(alphaSq / (pi*D)) - 1.f;
    float B = A / (alphaSq - 1.f);
    if (B < 0.f) return 0.f;    // these cases have no solution
    return saturate(sqrt(B));
}

float TrowReitzDInverseApprox(float alpha)
{
    // This is an approximation of TrowReitzDInverseApprox(0.32f, alpha);
    // It's based on a Taylor series.
    // It's fairly accurate for alpha < 0.5... Above that it tends to fall
    // off. The third order approximation is better above alpha .5. But really
    // small alpha values are more important, so probably it's fine.
    // third order: y=.913173-0.378603(a-.2)+0.239374(a-0.2)^2-0.162692(a-.2)^3
    // For different "cut-off" values of D, we need to recalculate the Taylor series.

    float b = alpha - .2f;
    return .913173f - 0.378603f * b + .239374f * b * b;
}

float AlphaToRadialMetric(float alpha)
{
	const float constant = 0.0312909;
	return alpha / (alpha + constant);
}

float RadialMetricToAlpha(float radialMetric)
{
	const float constant = 0.0312909;
	return -constant * radialMetric / (radialMetric - 1);
}

float RoughnessToGAlpha(float roughness)
{
    // This is the remapping to convert from a roughness
    // value into the "alpha" term used in the G part of
    // the brdf equation.
    // Disney suggest a remapping. It helps to reduce
    // the brighness of the specular around the edges of high
    // roughness materials; however this breaks the correctness of
    // the smith-G assumption, since the 'G' equation is just based
    // on the 'D' equation
    const bool useDisneyRemapping = false;
    float alphag = roughness;
    if (useDisneyRemapping) alphag = roughness*.5+.5;
    alphag *= alphag;
    return alphag;
}

float RoughnessToDAlpha(float roughness)
{
    // This is the remapping to convert from a roughness
    // value into the "alpha" term used in the D part of
    // the brdf equation.
    return roughness * roughness;
}

float3 ReferenceSpecularGGX(
    float3 normal,
    float3 directionToEye,
    float3 negativeLightDirection,
    float3 halfVector,
    float roughness, float3 F0,
    bool mirrorSurface)
{
    // This is reference implementation of "GGX" specular
    // The math here can be significantly optimized, however it's easier to work with
    // in this format

    // Our basic microfacet specular equation is:
    //
    //   D(thetah) * F(thetad) * G(thetal, thetav)
    // ---------------------------------------------
    //            4cos(thetal)cos(thetav)

    // D is our microfacet distribution function
    // F is fresnel
    // G is the shadowing/masking factor (geometric attenuation)

    float NdotL = dot(normal, negativeLightDirection);
    float NdotV = dot(normal, directionToEye);

     // The following the the approach used by Walter, et al, in the GGX
     // paper for dealing with surfaces that are pointed away from the light.
     // This is important for surfaces that can transmit light (eg, the glass
     // Walter used for his demonstrations, or leaves).
     // With this method, we get a highlight on the side light, reguardless of
     // which direction the normal is actually facing. Infact, if we reverse the
     // normal (with "normal = -normal") it has no impact on the result.
    #if MAT_DOUBLE_SIDED_LIGHTING
        float sndl = sign(NdotL);
        halfVector *= sndl;
        NdotV *= sndl;
        NdotL *= sndl;
    #else
        float sndl = 1.f;
    #endif

    float NdotH = dot(normal, halfVector);
    if (NdotV <= 0.f) return float3(0.0f, 0.0f, 0.0f);

    #if !MAT_DOUBLE_SIDED_LIGHTING
        // Getting some problems on grazing angles, possibly when
        // the signs of NdotV, NdotH & NdotL don't agree. Need to
        // check these cases with double sided lighting, as well.
        // Note that roughness is zero, and NdotH is zero -- we'll get a divide by zero
        // in the D term
        if (NdotH <= 0.f || NdotL <= 0.f) return float3(0.0f, 0.0f, 0.0f);
    #endif

    /////////// Shadowing factor ///////////
        // Note; there's an interesting question about whether
        // we should use HdotL and HdotV here, instead of NdotL
        // and NdotV. Walter07 mentions this -- I assume the N
        // is standing in for the average of all microfacet normals.
        // After consideration, it seems like that only makes sense
        // when there are multiple microfacet normals sampled (ie, for
        // a ray tracer). When we are using the half-vector for the
        // microfacet normal, NdotL and NdotV will be identical, so it
        // doesn't make much sense)
    float alphag = RoughnessToGAlpha(roughness);

    precise float G = SmithG(NdotL, alphag) * SmithG(NdotV, alphag);

    /////////// Fresnel ///////////
    float q;
    if (!mirrorSurface) {
        q = SchlickFresnelCore(sndl * dot(negativeLightDirection, halfVector));
    } else {
        q = SchlickFresnelCore(sndl * dot(negativeLightDirection, normal));
    }
    #if TAPER_OFF_FRESNEL
        float3 upperLimit = min(1.0.xxx, 50.0f * F0);
        float3 F = F0 + (upperLimit - F0) * q;
    #else
        float3 F = lerp(F0, float3(1.f, 1.f, 1.0f), q);
    #endif

    /////////// Microfacet ///////////
    precise float D = TrowReitzD(NdotH, RoughnessToDAlpha(roughness));

    float denom = 4.f * NdotV * NdotL;

        // note that the NdotL part here is not part of the BRDF function, but still
        // required for correct results.
        // See pbr-book 5.5 for good reference. NdotL is part of the integral
        // We can think of this as accounting for the orientation of the infinitesimal 
        // tangent space of the incident ray relative to the shape it's bouncing
        // off of.
        // We can also think of it as related to the transformation from radiance to 
        // irradiance
    float A = (NdotL * G * D / denom);
    return F * A;
}

float3 CalculateSpecular_GGX(
    float3 normal, float3 directionToEye, float3 negativeLightDirection,
    float3 halfVector,
    float roughness, float3 F0, bool mirrorSurface)
{
    return ReferenceSpecularGGX(
        normal, directionToEye, negativeLightDirection, halfVector,
        roughness, F0, mirrorSurface);

    #if 0
        float aveF0 = 0.3333f * (F0.r + F0.g + F0.b);
        return LightingFuncGGX_REF(normal, directionToEye, negativeLightDirection, roughness, aveF0).xxx;

        if (!mirrorSurface) {
            return LightingFuncGGX_OPT5(normal, directionToEye, negativeLightDirection, roughness, aveF0).xxx;
        } else {
            return LightingFuncGGX_OPT5_Mirror(normal, directionToEye, negativeLightDirection, roughness, aveF0).xxx;
        }
    #endif
}

    //////////////////////////////////////////////////////////////////////////
        //   E N T R Y   P O I N T                                      //
    //////////////////////////////////////////////////////////////////////////

struct SpecularParameters
{
    float   roughness;
    float3  F0;
    float3  transmission;
    bool    mirrorSurface;
};

SpecularParameters SpecularParameters_Init(float roughness, float refractiveIndex)
{
    SpecularParameters result;
    result.roughness = roughness;
    float f0 = RefractiveIndexToF0(refractiveIndex);
    result.F0 = float3(f0, f0, f0);
    result.mirrorSurface = false;
    result.transmission = float3(0.0f, 0.0f, 0.0f);
    return result;
}

SpecularParameters SpecularParameters_RoughF0(float roughness, float3 F0, bool mirrorSurface)
{
    SpecularParameters result;
    result.roughness = roughness;
    result.F0 = F0;
    result.mirrorSurface = mirrorSurface;
    result.transmission = float3(0.0f, 0.0f, 0.0f);
    return result;
}

SpecularParameters SpecularParameters_RoughF0(float roughness, float3 F0)
{
    return SpecularParameters_RoughF0(roughness, F0, false);
}

SpecularParameters SpecularParameters_RoughF0Transmission(float roughness, float3 F0, float3 transmission)
{
    SpecularParameters result;
    result.roughness = roughness;
    result.F0 = F0;
    result.mirrorSurface = false;
    result.transmission = transmission;
    return result;
}

#if MAT_TRANSMITTED_SPECULAR==1
    #include "../SceneEngine/Lighting/Diagrams/GGXTransmission.hlsl"
#endif

float GGXTransmissionFresnel(float3 i, float3 ot, float F0, float iorIncident, float iorOutgoing)
{
    // When calculation the fresnel effect, should we consider the incident direction, or
    // outgoing direction? Or both?
    // Outgoing makes more sense, because this will more closely match the calculation we use
    // for the reflection case. Actually; couldn't we just reuse the result we got with the
    // reflection case?
    // Walter07 paper uses i and Ht. He also uses abs() here, but that produces very strange results.
    // float HdotI = max(0, dot(CalculateHt(i, ot, iorIncident, iorOutgoing), i));
    float HdotI = max(0.0f, dot(CalculateHt(i, ot, iorIncident, iorOutgoing), i));
    // return 1.f - lerp(F0, 1.f, SchlickFresnelCore(HdotI));
    // return lerp(1.f - F0, 0.f, SchlickFresnelCore(HdotI));
    return (1.f - F0) * (1.f - SchlickFresnelCore(HdotI));
}

float3 CalculateSpecular(
    float3 normal, float3 directionToEye,
    float3 negativeLightDirection, float3 halfVector,
    SpecularParameters parameters)
{
    #if SPECULAR_METHOD==0
        return CalculateSpecular_CookTorrence(
            normal, directionToEye, negativeLightDirection,
            parameters.roughness, parameters.F0).xxx;
    #elif SPECULAR_METHOD==1
        float3 reflected = CalculateSpecular_GGX(
            normal, directionToEye, negativeLightDirection, halfVector,
            parameters.roughness, parameters.F0, parameters.mirrorSurface);

        // Calculate specular light transmitted through
        // For most cases of transmission, there should actually be 2 interfaces
        //		-- 	when the light enters the material, and when it exits it.
        // The light will bend at each interface. So, to calculate the refraction
        // properly, we really need to know the thickness of the object. That will
        // determine how much the light actually bends. If we know the thickness,
        // we can calculate an approximate bending due to refaction. But for now, ignore
        // thickness.
        //
        // It may be ok to consider the microfacet distribution only on a single
        // interface.
        // Walter's implementation is based solving for a surface pointing away from
        // the camera. And, as he mentions in the paper, the higher index of refraction
        // should be inside the material (ie, on the camera side). Infact, this is
        // required to get the transmission half vector, ht, pointing in the right direction.
        // So, in effect we're solving for the microfacets on an imaginary back face where
        // the light first entered the object on it's way to the camera.
        //
        // In theory, we could do the fresnel calculation for r, g & b separately. But we're
        // just going to ignore that and only do a single channel. This might produce an
        // incorrect result for metals; but why would we get a large amount of transmission
        // through metals?
        float transmitted = 0.f;

        #if MAT_TRANSMITTED_SPECULAR==1
                // note -- constant ior. Could be tied to "specular" parameter?
            const float iorIncident = 1.f;
            const float iorOutgoing = SpecularTransmissionIndexOfRefraction;
            GGXTransmission(
                parameters.roughness, iorIncident, iorOutgoing,
                negativeLightDirection, directionToEye, -normal,        // (note flipping normal)
                transmitted);

            transmitted *= GGXTransmissionFresnel(
                negativeLightDirection, directionToEye, parameters.F0.g,
                iorIncident, iorOutgoing);

            #if MAT_DOUBLE_SIDED_LIGHTING
                transmitted *= abs(dot(negativeLightDirection, -normal));
            #else
                transmitted *= max(0, dot(negativeLightDirection, -normal));
            #endif

        #endif

        return reflected + parameters.transmission * transmitted;

    #elif SPECULAR_METHOD==2

        return pow(dot(reflect(negativeLightDirection, normal), directionToEye), 4);

    #endif
}


#endif
