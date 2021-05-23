// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(SPECULAR_METHODS_H)
#define SPECULAR_METHODS_H

#include "optimized-ggx.hlsl"
#include "LightingAlgorithm.hlsl"
#include "../Math/Misc.hlsl"

#if !defined(SPECULAR_METHOD)
    #define SPECULAR_METHOD 1
#endif

    //////////////////////////////////////////////////////////////////////////
        //   C O O K   T O R R E N C E                                  //
    //////////////////////////////////////////////////////////////////////////

float CookTorrenceSpecular(float NdotL, float NdotH, float NdotV, float VdotH, float roughnessSquared)
{
		// using D3D book wiki for reference for Cook-Torrence specular equation:

	float geo_numerator   = 2.0f * NdotH;
    float geo_denominator = VdotH;

    float geo_b = (geo_numerator * NdotV) / geo_denominator;
    float geo_c = (geo_numerator * NdotL) / geo_denominator;
    float geo   = min(1.0f, min(geo_b, geo_c));

	float finalRoughness;
	{
			// beckmann distribution
		float roughness_a = 1.0f / ( 4.0f * roughnessSquared * pow( NdotH, 4.0f ) );
        float roughness_b = NdotH * NdotH - 1.0f;
        float roughness_c = roughnessSquared * NdotH * NdotH;
        finalRoughness = roughness_a * exp( roughness_b / roughness_c );
	}

	float Rs_numerator    = (geo * finalRoughness);
    float Rs_denominator  = NdotV * NdotL;
    float Rs              = Rs_numerator/ Rs_denominator;

	return saturate(NdotL) * Rs;
}

float CalculateSpecular_CookTorrence(float3 normal, float3 directionToEye, float3 negativeLightDirection, float roughness, float F0)
{
    float3 worldSpaceReflection = reflect(-directionToEye, normal);
	float3 halfVector = normalize(negativeLightDirection + directionToEye);
	float fresnel = SchlickFresnelF0(directionToEye, halfVector, F0);

	float NdotL = saturate(dot(negativeLightDirection, normal.xyz));
	float NdotH = saturate(dot(halfVector, normal.xyz));
	float NdotV = saturate(dot(directionToEye, normal.xyz));
	float VdotH = saturate(dot(halfVector, directionToEye));

    return fresnel * CookTorrenceSpecular(NdotL, NdotH, NdotV, VdotH, roughness*roughness);
}

    //////////////////////////////////////////////////////////////////////////
        //   G G X                                                      //
    //////////////////////////////////////////////////////////////////////////

float SmithG(float NdotV, float alpha)
{
    // Filmic worlds uses Smith-Schlick implementation.
    // It's a little bit simplier...
    // Epic course notes suggest k = alpha/2
    // float k = alpha / 2.f;
    // return NdotV/(NdotV*(1.0f-k)+k);

    float a = alpha * alpha;
    float b = NdotV * NdotV;
    return (2.f * NdotV) / (NdotV + sqrt(lerp(b, 1.0f, a)));
    // return 1.f/(NdotV + sqrt(a + (1.f-a) * b));
    // return 1.f/(NdotV + sqrt(a + b - a*b));
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

float RoughnessToGAlpha(float roughness)
{
    // This is the remapping to convert from a roughness
    // value into the "alpha" term used in the G part of
    // the brdf equation.
    // We're using the Disney remapping. It helps to reduce
    // the brighness of the specular around the edges of high
    // roughness materials.
    float alphag = roughness*.5+.5;
    alphag *= alphag;
    return alphag;
}

float RoughnessToGAlpha_IBL(float roughness)
{
    // We can't do the disney remapping when using IBL.
    // It seems to change the probability density function
    // in confusing ways. We just don't get the right result.
    return roughness * roughness;
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
    // It's very close to the Disney lighting model implementation

    // Our basic microfacet specular equation is:
    //
    //   D(thetah) * F(thetad) * G(thetal, thetav)
    // ---------------------------------------------
    //            4cos(thetal)cos(thetav)

    // D is our microfacet distribution function
    // F is fresnel
    // G is the shadowing factor (geometric attenuation)

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

    // Note that when N or H contain NaNs, this calculation is doomed to also result
    // in a NaN. This will cause black boxes in the output image. So we need to be careful
    // to avoid this case.
    // if (isnan(NdotH)) return 0.0.xxx;

    /////////// Shadowing factor ///////////
        // As per the Disney model, rescaling roughness to
        // values 0.5f -> 1.f for SmithG alpha, and squaring
        //
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
        // Mapping alpha to roughness squared (as per Disney
        // model and Filmic worlds implementation)
    precise float D = TrowReitzD(NdotH, RoughnessToDAlpha(roughness));

        // Note that the denominator here can be combined with
        // the "G" equation and factored out.
        // Including here just for clarity and reference purposes.
    float denom = 4.f * NdotV * NdotL;

    // note that the NdotL part here is for scaling down the incident light
    // (and so not part of the general BRDF equation)
    float A = (NdotL * G * D / denom);

    // In extreme cases, we must clamp the maximum specular values. This is should
    // only be used when there is extreme variation in the normals (otherwise it can
    // distort the lighting model). However, for some models, it's difficult to find
    // a better solution.
    #if MAT_CLAMP_SPEC!=0
        A = min(A, 10.f);
    #endif
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

    #endif
}


#endif
