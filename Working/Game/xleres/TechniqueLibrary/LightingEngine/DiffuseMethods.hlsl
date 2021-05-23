// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(DIFFUSE_METHODS_H)
#define DIFFUSE_METHODS_H

#include "../Math/MathConstants.hlsl"
#include "../Math/Misc.hlsl"

float RaiseTo5(float x) { float x2 = x*x; return x2*x2*x; }

float DiffuseMethod_Disney(
    float3 normal, float3 directionToEye, float3 negativeLightDirection,
    float roughness, float wideningMin, float wideningMax)
{
		// This is the Disney diffuse model:
		//		http://disney-animation.s3.amazonaws.com/library/s2012_pbs_disney_brdf_notes_v2.pdf
    //
    // The model is designed to flatten out the diffuse reponse (like Oren-Nayar and Hanrahan-Krueger),
    // while also giving what Disney calls "a retroreflective peak".
    // It's quite nice and also cheaper than Oren-Nayar!
    // It uses 2 fresnel calculations to approximate the focusing of light as it enters and
    // exits the surface.
    //
    // The same "roughness" parameter that is used for specular, is also used there. Higher
    // roughness values should result in a more flattened look (like the Oren-Nayar roughness).

	float cosThetaL = dot(normal, negativeLightDirection);
	float cosThetaV = dot(normal, directionToEye);

        // Note that we're using the half vector as a parameter to the fresnel
        // equation. See also "Physically Based Lighting in Call of Duty: Black Ops"
        // (Lazarov/Treyarch) for another example of this. The theory is this:
        //      If we imagine the surface as being microfacetted, then the facets
        //      that are reflecting light aren't actually flat on the surface... They
        //      are the facets that are raised towards the viewer. Our "halfVector" is
        //      actually an approximation of an average normal of these active facets.
        // (disney calls this the "difference angle")
	float3 halfVector = lerp(negativeLightDirection, directionToEye, .5f);
    // halfVector = normalize(halfVector); // (note that in theory we should have a normalize here.. but it doesn't seem to matter)
	float cosThetaD = dot(halfVector, negativeLightDirection);

    #if MAT_DOUBLE_SIDED_LIGHTING
        float sndl = sign(cosThetaL);
        cosThetaL *= sndl;
        cosThetaV *= sndl;
        cosThetaD *= sndl;
        if (cosThetaV <= 0.f) return 0.f;
    #else
        cosThetaL = saturate(cosThetaL);
        cosThetaV = saturate(cosThetaV);
        cosThetaD = max(cosThetaD, 0.f);
    #endif

        // The following factor controls how broad the diffusing effect is
        // at grazing angles. The problem is, with high levels of "roughness"
        // this will end up producing an extra highlight in the opposite direction
        // of the light (eg, in the shadowed area).
        // Disney artists liked it, so they used it. But it seems to cause problems
        // for us, because it also affects the edge of the shadowed area. With basic
        // lambert shading, this edge becomes blurred by the lambert shading falling
        // off. But if that edge is too bright, flaws in the shadowing algorithm can
        // become more obvious.
        // The two factors here, seem to be arbitrarily picked by Disney. So we should
        // make them flexible, and reduce the overall impact slightly.
	float FD90v = lerp(wideningMin, wideningMax, cosThetaD * cosThetaD * roughness);
    float FD90l = FD90v;

        // I wonder if there is any benefit to using the shadowing factor from our
        // BRDF as a parameter here...? Obviously it's quite an expensive addition.

        // We could consider power of 4 as a cheaper approximation to power of 5...?
	float result =
          (1.f + (FD90l - 1.f) * RaiseTo5(1.f - cosThetaL))
        * (1.f + (FD90v - 1.f) * RaiseTo5(1.f - cosThetaV))
        ;

    // note that the "saturate" here prevents strange results on high grazing angles
    const float normalizationFactor = reciprocalPi;
    return result * normalizationFactor;
}

float DiffuseMethod_Lambert()
{
        // Plain lambert lighting is not energy conserving. However,
        // the normalization factor is constant -- it's just 1.0f/pi.
        // In practice this is just the same as scaling the light brightness,
        // and since the light brightness isn't really defined in physically
        // based units, it doesn't really matter... But by adding the scale
        // factor here, it should allow us to better balance this lighting
        // model against other lighting models that are corectly normalized.
        // Note that the results are also similarly simple for broadened
        // lambert models (where we offset and scale the value from the dot product)
    const float normalizationFactor = reciprocalPi;
	return normalizationFactor;
}

//////////////////////////////////////////////////////////////////////////
    //   E N T R Y   P O I N T                                      //
//////////////////////////////////////////////////////////////////////////

struct DiffuseParameters
{
    float roughness;
    float diffuseWideningMin;
    float diffuseWideningMax;
};

DiffuseParameters DiffuseParameters_Roughness(float roughness, float diffuseWideningMin, float diffuseWideningMax)
{
    DiffuseParameters result;
    result.roughness = roughness;
    result.diffuseWideningMin = diffuseWideningMin;
    result.diffuseWideningMax = diffuseWideningMax;
    return result;
}

float CalculateDiffuse( float3 normal, float3 directionToEye,
                        float3 negativeLightDirection,
                        DiffuseParameters parameters)
{
    #if DIFFUSE_METHOD==0
        return DiffuseMethod_Lambert();
    #elif DIFFUSE_METHOD==1
        return DiffuseMethod_Disney(
            normal, directionToEye, negativeLightDirection,
            parameters.roughness, parameters.diffuseWideningMin, parameters.diffuseWideningMax);
    #else
        return 1;
    #endif
}

#endif
