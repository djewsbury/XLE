// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Math/MathConstants.hlsl"
#include "../Math/Misc.hlsl"

////////////////////////////////////////////////////////////////////////////////////////////////////

float3 ResolveSH_Opt(float3 premulCoefficients[9], float3 dir)
{
    // 3-band spherical harmonic resolve
    // This call expects that the input spherical harmonic coefficients have been
    // pre-multiplied by the scalars in ShPremultipliers.
    // Also, subtract value in [6] from the value in [0], and then triple the value
    // in [6].
    // To resolve it, we just sum a weighted contribution for each input value
    //
    // Note that this is optimized for scalar based GPUs, but not vector based GPUs
    // For old vector based GPUs, we might get better results by using more 4D vector
    // operations (see implementation, for example, from Peter-Pike Sloan)
    float3 result = premulCoefficients[0];

    result += premulCoefficients[1] * dir.y;
    result += premulCoefficients[2] * dir.z;
    result += premulCoefficients[3] * dir.x;

    float3 dirSq = dir * dir;
    result += premulCoefficients[4] * (dir.x * dir.y);
    result += premulCoefficients[5] * (dir.z * dir.y);
    result += premulCoefficients[6] * dirSq.z;              // note that we simplify this from (dirSq.z * 3.0f - 1.f) by modifying the input values
    result += premulCoefficients[7] * (dir.x * dir.z);
    result += premulCoefficients[8] * (dirSq.x - dirSq.y);
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//      R E F E R E N C E   I M P L E M E N T A T I O N S

float EvalSHBasis(uint index, float3 dir)
{
    float xx = dir.x, yy = dir.y, zz = dir.z;

    float x[5], y[5], z[5];
    x[0] = y[0] = z[0] = 1.;
    for (uint i=1u; i<5u; ++i) {
        x[i] = xx * x[i-1u];
        y[i] = yy * y[i-1u];
        z[i] = zz * z[i-1u];
    }

    // note -- these equations are assuming that "dir" is unit length
    //      Also, these equations include the Condon-Shortley phase,
    //      which is not used in all representations of the spherical harmonics equations.
    //      See:
    //          http://mathworld.wolfram.com/Condon-ShortleyPhase.html
    //      (note that Robin Green's "Spherical Harmonic Lighting: The Gritty Details"
    //      doesn't include this extra term, but most other references do)

    float value;
    switch (index) {
    case  0u: value = 1.0/(2.f*sqrt(pi)); break;

    case  1u: value = -sqrt(3.f/(4.f*pi)) * dir.y; break;
    case  2u: value =  sqrt(3.f/(4.f*pi)) * dir.z; break;
    case  3u: value = -sqrt(3.f/(4.f*pi)) * dir.x; break;

    case  4u: value =  0.5f  * sqrt(15.f/pi) * dir.x * dir.y; break;
    case  5u: value = -0.5f  * sqrt(15.f/pi) * dir.y * dir.z; break;
    case  6u: value =  0.25f * sqrt( 5.f/pi) * (3.f * Sq(dir.z) - 1.0f); break; // -Sq(dir.x) - Sq(dir.y) + 2*Sq(dir.z)); break;
    case  7u: value = -0.5f  * sqrt(15.f/pi) * dir.x * dir.z; break;
    case  8u: value =  0.25f * sqrt(15.f/pi) * (Sq(dir.x) - Sq(dir.y)); break;

    case  9u: value = (sqrt(35.f/(2.f*pi))*(-3.f*x[2]*yy + y[3]))/4.f; break;
    case 10u: value = (sqrt(105.f/pi)*xx*yy*zz)/2.f; break;
    case 11u: value = -(sqrt(21.f/(2.f*pi))*yy*(-1.f + 5.f*z[2]))/4.f; break;
    case 12u: value = (sqrt(7.f/pi)*zz*(-3.f + 5.f*z[2]))/4.f; break;
    case 13u: value = -(sqrt(21.f/(2.f*pi))*xx*(-1.f + 5.f*z[2]))/4.f; break;
    case 14u: value = (sqrt(105.f/pi)*(x[2] - y[2])*zz)/4.f; break;
    case 15u: value = -(sqrt(35.f/(2.f*pi))*(x[3] - 3.f*xx*y[2]))/4.f; break;

    case 16u: value = (3.f*sqrt(35.f/pi)*xx*yy*(x[2] - y[2]))/4.f; break;
    case 17u: value = (-3.f*sqrt(35.f/(2.f*pi))*(3.f*x[2]*yy - y[3])*zz)/4.f; break;
    case 18u: value = (3.f*sqrt(5.f/pi)*xx*yy*(-1.f + 7.f*z[2]))/4.f; break;
    case 19u: value = (-3.f*sqrt(5.f/(2.*pi))*yy*zz*(-3.f + 7.f*z[2]))/4.f; break;
    case 20u: value = (3.f*(3.f - 30.f*z[2] + 35.f*z[4]))/(16.f*sqrt(pi)); break;
    case 21u: value = (-3.f*sqrt(5.f/(2.f*pi))*xx*zz*(-3.f + 7.f*z[2]))/4.f; break;
    case 22u: value = (3.f*sqrt(5.f/pi)*(x[2] - y[2])*(-1.f + 7.f*z[2]))/8.f; break;
    case 23u: value = (-3.f*sqrt(35.f/(2.f*pi))*(x[3] - 3.f*xx*y[2])*zz)/4.f; break;
    case 24u: value = (3.f*sqrt(35.f/pi)*(x[4] - 6.f*x[2]*y[2] + y[4]))/16.f; break;
    }
    return value;
}

static const uint SHCoefficientCount = 9u;

float3 ResolveSH_Reference(float3 coefficients[SHCoefficientCount], float3 dir)
{
    float3 result = float3(0,0,0);
    for (uint c=0u; c<SHCoefficientCount; ++c) {
        float rsqrtPi = rsqrt(pi);
        float z[5];  // these constants come from the formula for modulating a spherical harmonic by a rotated zonal harmonic
        z[0] = .5f * rsqrtPi;
        z[1] = sqrt(3.f)/3.0f * rsqrtPi;
        z[2] = sqrt(5.f)/8.0f * rsqrtPi;
        z[3] = 0.f;
        z[4] = -1.f/16.0f * rsqrtPi;
        uint l = (c>=16u) ? 4u : ((c>=9u) ? 3u : ((c>=4u) ? 2u : ((c>=1u) ? 1u : 0u)));
        float A = sqrt(4.f * pi / (2.f*float(l)+1.f));
        float f = A * z[l] * EvalSHBasis(c, dir);
        result += coefficients[c] * f;
    }
    return result;
}

#if defined(__VERSION__)
    static const float ShBandFactors[] = float[] (1.0f, 2.0f / 3.0f, 1.0f / 4.0f);
#else
    static const float ShBandFactors[] = {1.0f, 2.0f / 3.0f, 1.0f / 4.0f};
#endif
static const float rsqrtPi = rsqrt(pi);

#if defined(__VERSION__)
static const float ShPremultipliers[] = float[]
(
#else
static const float ShPremultipliers[] = {
#endif
     ShBandFactors[0] * .5f * rsqrtPi,

    -ShBandFactors[1] * rsqrt(4.f/3.f) * rsqrtPi,
     ShBandFactors[1] * rsqrt(4.f/3.f) * rsqrtPi,
    -ShBandFactors[1] * rsqrt(4.f/3.f) * rsqrtPi,

     ShBandFactors[2] * .5f  * sqrt(15.f) * rsqrtPi,
    -ShBandFactors[2] * .5f  * sqrt(15.f) * rsqrtPi,
     ShBandFactors[2] * .25f * sqrt( 5.f) * rsqrtPi,
    -ShBandFactors[2] * .5f  * sqrt(15.f) * rsqrtPi,
     ShBandFactors[2] * .25f * sqrt(15.f) * rsqrtPi
#if defined(__VERSION__)
);
#else
};
#endif
