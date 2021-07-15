// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LIGHTING_ALGORITHM_H)
#define LIGHTING_ALGORITHM_H

#include "../Math/MathConstants.hlsl"
#include "../Math/Misc.hlsl"

float RefractiveIndexToF0(float refractiveIndex)
{
        // (note -- the 1.f here assumes one side of the interface is air)
	return Sq((refractiveIndex - 1.f) / (refractiveIndex + 1.f));
}

float F0ToRefractiveIndex(float F0)
{
	float sqrx = sqrt(F0);
	return (-F0-1.f) / (F0-1.f) - 2.f * sqrx / (-1.f + sqrx) / (1.f + sqrx);
}

float SchlickFresnelCore(float VdotH)
{
	float A = 1.0f - saturate(VdotH);
	float sq = A*A;
	float cb = sq*sq;
	return cb*A;	// we could also consider just using the cubed value here
}

float SchlickFresnelF0(float3 viewDirection, float3 halfVector, float F0)
{
		// Note that we're using the half vector as a parameter to the fresnel
		// equation. See also "Physically Based Lighting in Call of Duty: Black Ops"
		// (Lazarov/Treyarch) for another example of this. The theory is this:
		//      If we imagine the surface as being microfacetted, then the facets
		//      that are reflecting light aren't actually flat on the surface... They
		//      are the facets that are raised towards the viewer. Our "halfVector" is
		//      actually an approximation of an average normal of these active facets.
		// For a perfect mirror surface, maybe we could consider using the normal
		// instead of the half vector here? Might be nice for intense reflections
		// from puddles of water, etc. Maybe we could also just use the roughness
		// value to interpolate between half vector and normal...?
		//
		// We can also consider a "spherical gaussian approximation" for fresnel. See:
		//	https://seblagarde.wordpress.com/2012/06/03/spherical-gaussien-approximation-for-blinn-phong-phong-and-fresnel/
		//	pow(1 - dotEH, 5) = exp2((-5.55473 * EdotH - 6.98316) * EdotH)
		// It seems like the performance difference might be hardware dependent
		// Also, maybe we should consider matching the gaussian approximation to the full
		// fresnel equation; rather than just Schlick's approximation.
	float q = SchlickFresnelCore(dot(viewDirection, halfVector));
	return F0 + (1.f - F0) * q;	// (note, use lerp for this..?)
}

float SchlickFresnelF0_Modified(float3 viewDirection, float3 halfVector, float F0)
{
		// In this modified version, we attempt to reduce the extreme edges of
		// the reflection by imposing an upper limit.
		// The extreme edges of the reflection can often highlight rendering
		// inaccuracies (such as lack of occlusion and local reflections).
		// So, oftening it off helps to reduce problems.
	float q = SchlickFresnelCore(dot(viewDirection, halfVector));
	float upperLimit = min(1.f, 50.f * (F0+0.001f));
	return F0 + (upperLimit - F0) * q;
}

float3 SchlickFresnelF0(float3 viewDirection, float3 halfVector, float3 F0)
{
	float q = SchlickFresnelCore(dot(viewDirection, halfVector));
	return lerp(F0, float3(1.f,1.f,1.f), float3(q,q,q));
}

float3 SchlickFresnelF0_Modified(float3 viewDirection, float3 halfVector, float3 F0)
{
	float q = SchlickFresnelCore(dot(viewDirection, halfVector));
	float3 upperLimit = min(float3(1.f,1.f,1.f), 50.f * (F0+0.001f));
	return F0 + (upperLimit - F0) * q;
}

float SchlickFresnel(float3 viewDirection, float3 halfVector, float refractiveIndex)
{
    float F0 = RefractiveIndexToF0(refractiveIndex);
    return SchlickFresnelF0(viewDirection, halfVector, F0);
}

float CalculateMipmapLevel(float2 texCoord, uint2 textureSize)
{
		// Based on OpenGL 4.2 spec chapter 3.9.11 equation 3.21
		// This won't automatically match the results given by all
		// hardware -- but it should be a good alternative to the
		// built in hardware mipmap calculation when an explicit
		// formula is required.
	float2 et = abs(texCoord * float2(textureSize));
	float2 dx = ddx(et), dy = ddy(et);
	float d = max(dot(dx, dx), dot(dy, dy));
	return 0.5f*log2(d); // == log2(sqrt(d))
}

void OrenNayar_CalculateInputs(float roughness, out float rho, out float shininess)
{
	    // rho = roughness * roughness;
	    // shininess = 1.f - roughness;
    rho = 1.f;
    shininess = 2.0f / (roughness*roughness);
}

float4 ReadReflectionHemiBox( 	float3 direction,
                                Texture2D face12, Texture2D face34, Texture2D face5,
								SamplerState clampingSampler,
                                uint2 textureSize, uint defaultMipMap)
{
	float3 absDirection = abs(direction);
	direction.z = absDirection.z;

	float2 tc;
    uint textureIndex = 0;

		// Simple non-interpolated lookup to start
	[branch] if (absDirection.z > absDirection.x && absDirection.z > absDirection.y) {
		tc = 0.5f + 0.5f * direction.xy / direction.z;
        textureIndex = 2;
	} else if (absDirection.x > absDirection.y && absDirection.x > absDirection.z) {
		tc = 0.5f + 0.5f * direction.yz / absDirection.x;
		if (direction.x > 0.f) {
			tc.x = 1.f-tc.x;
			textureIndex = 0;
		} else {
			textureIndex = 1;
		}
	} else {
		tc = 0.5f + 0.5f * direction.xz / absDirection.y;
		tc.y = 1.f-tc.y;
		if (direction.y > 0.f) {
			textureIndex = 0;
		} else {
			tc.x = 1.f-tc.x;
			textureIndex = 1;
		}
	}

        // note --  mipmap calculation is incorrect on edges, where
        //          we flip from one texture to another.
        //          It's a problem... let's just use point sampling for now
    const bool usePointSampling = true;
    if (usePointSampling) {
        uint mipMap = defaultMipMap;
        int2 t = saturate(tc)*textureSize;
        t >>= mipMap;
        [branch] if (textureIndex == 2) {
            return face5.Load(int3(t, mipMap));
	    } else if (textureIndex == 0) {
		    return face12.Load(int3(t, mipMap));
	    } else {
		    return face34.Load(int3(t, mipMap));
	    }
    } else {
        uint mipMap = CalculateMipmapLevel(tc, textureSize);
        [branch] if (textureIndex == 2) {
            return face5.SampleLevel(clampingSampler, tc, mipMap);
	    } else if (textureIndex == 0) {
		    return face12.SampleLevel(clampingSampler, tc, mipMap);
	    } else {
		    return face34.SampleLevel(clampingSampler, tc, mipMap);
	    }
    }
}

float3 SphericalToCartesian_YUp(float inc, float theta)
{
    float s0, c0, s1, c1;
    sincos(inc, s0, c0);
    sincos(theta, s1, c1);
    return float3(c0 * -s1, s0, c0 * c1);
}

float3 CartesianToSpherical_YUp(float3 direction)
{
    // This method is arranged to match the projection in Substance Painter 2
    // Here, theta starts at +Z, and then wraps around towards -X
    // X,Z = ( 0,  1); theta=0
    // X,Z = (-1,  0); theta=PI/2
    // X,Z = ( 0, -1); theta=PI
    // X,Z = ( 1,  0); theta=-PI/2
    //
    // inc is the angle of inclination, and starts at 0 on the XZ plane,
    // and is a positive number looking up
    float3 result;
    // float rDist = rsqrt(dot(direction, direction));
    // result[0] = asin(direction.y * rDist);			// inc
	result[0] = atan2(direction.y, sqrt(direction.x*direction.x+direction.z*direction.z));	// inc
    result[1] = -atan2(direction.x, direction.z);	// theta
    result[2] = length(direction);
    return result;
}

float3 EquirectangularCoordToDirection_YUp(float2 inCoord)
{
    // Given the x, y pixel coord within an equirectangular texture, what
    // is the corresponding direction vector?
    float theta = 2.f * pi * inCoord.x;
    float inc = pi * (.5f - inCoord.y);
    return SphericalToCartesian_YUp(inc, theta);
}

float2 DirectionToEquirectangularCoord_YUp(float3 direction)
{
		// note -- 	the trigonometry here is a little inaccurate. It causes shaking
		//			when the camera moves. We might need to replace it with more
		//			accurate math.
	float3 spherical = CartesianToSpherical_YUp(direction);
	float x = spherical.y * 0.5f * reciprocalPi;
	float y = .5f-(spherical.x * reciprocalPi);
	return float2(x, y);
}

float2 DirectionToHemiEquirectangularCoord_YUp(float3 direction)
{
	float3 spherical = CartesianToSpherical_YUp(direction);
	float x = spherical.y * (2.0f * pi);
	float y = 1.f-(spherical.x * 2.0f * reciprocalPi);
	return float2(x, y);
}

float SimplifiedOrenNayer(float3 normal, float3 viewDirection, float3 lightDirection, float rho, float shininess)
{
		//
		//		Using a simplified Oren-Nayar implementation, without any lookup tables
		//		See http://blog.selfshadow.com/publications/s2012-shading-course/gotanda/s2012_pbs_beyond_blinn_notes_v3.pdf
		//		for details.
		//
		//		This is the non-spherical harmonic version (equation 25)
		//
		//		See also original Oren-Nayar paper:
		//			http://www1.cs.columbia.edu/CAVE/publications/pdfs/Oren_SIGGRAPH94.pdf
		//

	float NL	 = dot(normal, lightDirection);
	float NE	 = dot(normal, viewDirection);
	float EL	 = dot(viewDirection, lightDirection);
	float A      = EL - NE * NL;
	float E0	 = 1.f;		// (from the Oren-Nayar paper, E0 is the irradiance when the facet is illuminated head-on)
	float B;
	/*if (A >= 0.f) {		// (better to branch or unwrap?)
		B = min(1.f, NL / NE);		(this branch is causing a wierd discontinuity. There must be something wrong with the equation)
	} else*/ {
		B = NL;
	}

	float C = (1.0f / (2.22222f + .1f * shininess)) * A * B;
	float D = NL * (1.0f - 1.0f / (2.f + .65 * shininess)) + C;
	return saturate(E0 * rho / pi * D);
}

float TriAceSpecularOcclusion(float NdotV, float ao)
{
    // This is the "Specular Occlusion" parameter suggested by Tri-Ace.
    // This equation is not physically based, but there are some solid
    // principles. Actually, our ambient occlusion term isn't
    // fully physically based, either.
    //
    // Let's assume that the "AO" factor represents the quantity of a
    // hemidome around the normal that is occluded. We can also assume
    // that the occluded parts are evenly distributed around the lowest
    // elevation parts of the dome.
    //
    // So, given an angle between the normal and the view, we want to know
    // how much of the specular peak will be occluded.
    // (See the Tri-Ace slides from cedec2011 for more details)
    // The result should vary based on roughness. But Tri-Ace found that it
    // was more efficient just to ignore that.
    //
    // Actually, I guess we could use the HdotV there, instead of NdotV, also.
    // That might encourage less occlusion.
    float q = (NdotV + ao);
    return saturate(q * q - 1.f + ao);
    // d*d + 2*d*a + a*a - 1 + a
    // d*d - 1 +     a*(2*d + 1)
    // a*a - 1 + a + d*(2*a + d)
}

float3 AdjSkyCubeMapCoords(float3 inCoord) { return inCoord; }
float3 InvAdjSkyCubeMapCoords(float3 inCoord) { return inCoord; }

/////////////////////////////////////////////////////////////////////////////////////////////////////////

float PowerForHalfRadius(float halfRadius, float powerFraction)
{
		// attenuation = power / (distanceSq+1);
		// attenuation * (distanceSq+1) = power
		// (power*0.5f) * (distanceSq+1) = power
		// .5f*power = distanceSq+1
		// power = (distanceSq+1) / .5f
	return ((halfRadius*halfRadius)+1.f) * (1.f/(1.f-powerFraction));
}

float DistanceAttenuation(float distanceSq, float power)
{
	return power / (distanceSq+1.f);
}

float CalculateRadiusLimitAttenuation(float distanceSq, float lightRadius)
{
	// Calculate the drop-off towards the edge of the light radius...
	float D = distanceSq; D *= D; D *= D;
	float R = lightRadius; R *= R; R *= R; R *= R;
	return 1.f - saturate(3.f * D / R);
}

float3 CalculateHt(float3 i, float3 o, float iorIncident, float iorOutgoing)
{
		// Calculate the half vector used in transmitted specular equation
	return -normalize(iorIncident * i + iorOutgoing * o);
}

bool CalculateTransmissionIncident(out float3 i, float3 ot, float3 m, float iorIncident, float iorOutgoing)
{
    // here, m is the half vector (microfacet normal), and ot
    // is the direction to the viewer

    // m = -(1/l)(iorIncident * i + iorOutgoing * o);
    //  where l is length of (iorIncident * i + iorOutgoing * o)
    // -m * l = iorIncident * i + iorOutgoing * o
    // -m * l - iorOutgoing * o = iorIncident * i
    // i = -m * l / iorIncident - iorOutgoing / iorIncident * o

#if 0
	#if 1
		float flip = (iorIncident > iorOutgoing)?-1:1;
	    float c = dot(ot, (-1.f * flip) * m);
	    float b = iorOutgoing * c;
	    // if (c < 0.f || c >= 1.f) return false; // return float3(0,1,0);

	    // float a = sqrt(iorOutgoing*iorOutgoing - b*b);
	    // float a = sqrt(iorOutgoing*iorOutgoing - iorOutgoing*iorOutgoing*c*c);
	    // float a = iorOutgoing * sqrt(1.f - c*c);
	    // float asq = iorOutgoing*iorOutgoing*(1.f - c*c);
	    // if (asq >= iorIncident*iorIncident) return false;
	    // float e = sqrt(iorIncident*iorIncident - asq);
	    // float e = sqrt(iorIncident*iorIncident - iorOutgoing*iorOutgoing*(1.f - c*c));
	    float etaSq = Sq(iorOutgoing/iorIncident);
	    // float e = sqrt(iorIncident*iorIncident*(1.f - etaSq*(1.f - c*c)));
	    // float e = iorIncident*sqrt(1.f - etaSq + etaSq*c*c);
		float k = 1.f + etaSq*(c*c-1.f);
		if (k < 0.f) return false;
		float e = iorIncident*sqrt(k);
	    float l = flip * (b - e);
	#else
		float b = iorOutgoing * dot(ot, m);
		float a = sqrt(iorOutgoing*iorOutgoing - b*b);
		float e = sqrt(iorIncident*iorIncident - a*a);
		float l = e - b;
	#endif

	i = -m * l / iorIncident - iorOutgoing / iorIncident * ot;
#else
	float flip = (iorIncident > iorOutgoing)?1.f:-1.f;
	float c = dot(ot, m);

	float eta = iorOutgoing/iorIncident;
	float k = 1.f + Sq(eta)*(c*c-1.f);
	if (k < 0.f) return false;

    float l = eta * c - flip * sqrt(k);
	i = m * l - eta * ot;

	// Note that it's identical to CalculateTransmissionOutgoing (as should really
	// be expected), except with the parmeters swapped. We could generalize this
	// in a single function

#endif

    return true;
}

float RefractionIncidentAngleDerivative2(float odotm, float iorIncident, float iorOutgoing)
{
	// Similar to RefractionIncidentAngleDerivative, except now we're looking at
	// the relationship between odotm and odoti. Here O, the outgoing direction is
	// fixed, but M and I can change.
	//
	// idoto = l * odotm - eta
	//		 = (eta * c - sqrt(k)) * odotm - eta
	// where c=odotm && k=1+Sq(eta)*(c*c-1)
	// idoto = (eta * c - sqrt(1 + Sq(eta)*(c*c-1))) * c - eta
	//		 = eta*c*c - c*sqrt(1+Sq(eta)*(c*c-1)) - eta
	//		 = eta*c*c - c*sqrt(Sq(eta)*Sq(c)-Sq(eta)+1) - eta
	// 		 = eta*c*c - sqrt(Sq(eta)*c^4-Sq(eta)*Sq(c)+Sq(c)) - eta
	//		 = a*(x^2-1) - sqrt(a^2*x^4-a^2*x^2+x^2)
	// acos(a*(cos(x)^2-1) - sqrt(a^2*cos(x)^4-a^2*cos(x)^2+cos(x)^2))
	//
	// dev:
	// (a sin(2 x)-(sin(2 x) (a^2 cos(2 x)+1))/(sqrt(2) sqrt(cos^2(x) (a^2 cos(2 x)-a^2+2))))/sqrt(1-(sqrt(cos^2(x) (a^2 cos^2(x)-a^2+1))-a cos^2(x)+a)^2)
	// approx:
	// (a sin(2 x)-(0.707107 sin(2 x) (a^2 cos(2 x)+1))/(cos^2(x) (a^2 cos(2 x)-a^2+2))^0.5)/(1-((cos^2(x) (a^2 cos^2(x)-a^2+1))^0.5-a cos^2(x)+a)^2)^0.5

	float eta = iorOutgoing/iorIncident;
	float a = eta;
	float cosx = min(odotm, 0.99f);			// c

	float sinx = sqrt(1.f - cosx*cosx);		// b
	float sin2x = 2.f*cosx*sinx;			// d
	float cos2x = Sq(cosx) - Sq(sinx);		// f
	float sqr2 = sqrt(2.f);

	float A = sin2x * (a*a*cos2x+1.0f) / (sqr2 * sqrt(Sq(cosx)*(a*a*cos2x-a*a+2.0f)));
	float B = sqrt(1.f - Sq(sqrt(Sq(cosx)*(a*a*Sq(cosx)-a*a+1.0f))-a*Sq(cosx)+a));
	float angleDev = (a * sin2x - A) / B;

	const bool useApproximation = true;
	if (useApproximation) {
		// This is an approximation of the full equation
		// It was matched by hand. It's not a perfect approximation. But it's
		// pretty close. The error seems visually negligable.
		float p = pow(1.f-a, -.35f);
		float W = 1.f - pow(1.f-cosx, p);
		angleDev = W * a - 1.f;
	}
	return -angleDev;
}

float RefractionIncidentAngleDerivative(float odotm, float iorIncident, float iorOutgoing)
{
	// We want to find the rate of change of the incident angle as
	// the microfacet normal changes. In this case, we are assuming that
	// the outgoing direction is constant.
	// One way to do this is to look at the derivative of idotm with
	// respect to odotm. Given that o is constant, changes in odotm
	// represent changes in the microfacet normal.
	// Of course, the angle between I and M isn't actually what we need,
	// because both I and M are moving.
	// However, there is a simple relationship between these, so it makes
	// the calculations easy. And maybe it's a good approximation.
	//
	// We can relate idotm to odotm...
	// idotm = l - eta * c;
	// 		 = eta * c - flip * sqrt(k) - eta * c
	//		 = -flip * sqrt(1.f + Sq(eta)*(Sq(odotm)-1.f))
	//
	// WolframAlpha derivative:
	// (d)/(dx)(sqrt(1+a^2 (-1+x^2))) = (a^2 x)/sqrt(a^2 (x^2-1)+1)
	//
	// Derivative of angle (as opposed to dot product,
	// assuming x > 0 && a > 0:
	// (a cot(x) abs(sin(x)))/sqrt(1-a^2 sin^2(x))
	// where cos(x) is odotm

	float eta = iorOutgoing/iorIncident;
	float flip = (iorIncident > iorOutgoing)?1.0f:-1.0f;

	float cosx = odotm;
	float sinxSq = 1.f - cosx*cosx;
	float sinx = sqrt(sinxSq);
	float cotx = cosx/sinx;
	float a = eta;
	// float angleDev = a * cotx * sinx / sqrt(1.f - Sq(a) * sinxSq);
	float k = sqrt(1.f + Sq(a) * (Sq(odotm) - 1.f));
	float angleDev = a * odotm / k;
	return flip * 2.f * angleDev;		// here, 2.f is a fudge factor because this is an approximation
}

float3 CalculateTransmissionOutgoing(float3 i, float3 m, float iorIncident, float iorOutgoing)
{
	float c = dot(i, m);
	float eta = iorIncident / iorOutgoing;
	float s = (iorIncident > iorOutgoing)?-1.0f:1.0f; // sign(dot(i, n))
	// return (eta * c - s * sqrt(1.f + eta * (c*c - 1.f))) * m - eta * i;

	// there maybe a small error in the Walter07 paper... Expecting eta^2 here --
	// float k = 1.f + eta*eta*(c*c - 1.f);
	float k = 1.f + Sq(eta)*(c*c - 1.f);
	if (k < 0.f) return float3(0.0f, 0.0f, 0.0f);
	return (eta * c - s * sqrt(k)) * m - eta * i;
}

static const float SpecularParameterRange_RefractiveIndexMin = 1.0f;
static const float SpecularParameterRange_RefractiveIndexMax = 3.0f;
static const float ReflectionBlurrinessFromRoughness = 5.f;

//  For reference -- here are some "F0" values taken from
//  https://seblagarde.wordpress.com/2011/08/17/feeding-a-physical-based-lighting-mode/
//      F0(1.0) = 0
//      F0(1.8) = 0.082
//      F0(2.0) = 0.111
//      F0(2.2) = 0.141
//      F0(2.5) = 0.184
//      F0(3.0) = 0.25
// Quartz    0.045593921
// ice       0.017908907
// Water     0.020373188
// Alcohol   0.01995505
// Glass     0.04
// Milk      0.022181983
// Ruby      0.077271957
// Crystal   0.111111111
// Diamond   0.171968833
// Skin      0.028

// We can choose 2 options for defining the "specular" parameter
//  a) Linear against "refractive index"
//      this is more expensive, and tends to mean that the most
//      useful values of "specular" are clustered around 0 (where there
//      is limited fraction going on)
//  b) Linear against "F0"
//      a little cheaper, and more direct. There is a linear relationship
//      between the "specular" parameter and the brightness of the
//      "center" part of the reflection. So it's easy to understand.
//  Also note that we have limited precision for the specular parameter, if
//  we're reading it from texture maps... So it seems to make sense to make
//  it linear against F0.
#define SPECULAR_LINEAR_AGAINST_F0 1

// Note -- maybe this should vary with the "specular" parameter. The point where there
// is no refraction solution should match the point where the lighting becomes 100% reflection
static const float SpecularTransmissionIndexOfRefraction = 1.5f;

float SpecularParameterToF0(float specularParameter)
{
    #if (SPECULAR_LINEAR_AGAINST_F0!=0)
        const float minF0 = RefractiveIndexToF0(SpecularParameterRange_RefractiveIndexMin);
        const float maxF0 = RefractiveIndexToF0(SpecularParameterRange_RefractiveIndexMax);
        return lerp(minF0, maxF0, specularParameter);
    #else
        return RefractiveIndexToF0(lerp(SpecularParameterRange_RefractiveIndexMin, SpecularParameterRange_RefractiveIndexMax, specularParameter));
    #endif
}

#endif
