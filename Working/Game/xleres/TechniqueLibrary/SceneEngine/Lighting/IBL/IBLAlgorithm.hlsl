// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(IBL_ALGORITHM_H)
#define IBL_ALGORITHM_H

#if !defined(LIGHTING_ALGORITHM_H)
    #error Include LightingAlgorithm.hlsl before inlcuding this file
#endif

static const float MinSamplingAlpha = 0.001f;
static const float MinSamplingRoughness = 0.03f;
static const float SpecularIBLMipMapCount = 9.f;

// #define OLD_M_DISTRIBUTION_FN

float VanderCorputRadicalInverse(uint bits)
{
    // This is the "Van der Corput radical inverse" function
    // see source:
    //      http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
    // The result is intended to reflect the bits of the input around the decimal
    // place. So we take an integer as input and return a 0-1 value. So the constant
    // here is to transform from (0, max int] back into the (0-1] range.
    // 
    return float(reversebits(bits)) * 2.3283064365386963e-10f;
    // bits = (bits << 16u) | (bits >> 16u);
    // bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    // bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    // bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    // bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    // return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
 }

float2 HammersleyPt(uint i, uint N)
{
    // note --  we can avoid the M==N case by using the equation below. Since
    //          M==N can lead to singularities, perhaps this give better sampling?
    // return float2(float(i+1)/float(N+1), VanderCorputRadicalInverse(i));
    return float2(float(i)/float(N), VanderCorputRadicalInverse(i));
}

float3 SampleMicrofacetNormalGGX(uint i, uint sampleCount, float3 normal, float alphad)
{
        // Very similar to the unreal course notes implementation here
        // Note that "HammersleyPt" always produces (0,0) as the first points
        //      -- this will become a direction equal to "normal"
    float2 xi = HammersleyPt(i, sampleCount);

    // The following will attempt to select points that are
    // well distributed for the GGX highlight
    // See a version of this equation in
    //  http://igorsklyar.com/system/documents/papers/28/Schlick94.pdf
    //  under the Monte Carlo Techniques heading
    // Note that we may consider remapping 'a' here to take into account
    // the remapping we do for the G and D terms in our GGX implementation...?
    //
    // Also note because we're not taking a even distribution of points, we
    // have to be careful to make sure the final equation is properly normalized
    // (in other words, if we had a full dome of incoming white light,
    // we don't want changes to the roughness to change the result).
    // See http://http.developer.nvidia.com/GPUGems3/gpugems3_ch20.html
    // for more information about normalized "PDFs" in this context
    //
    // Note that I've swapped xi.x & xi.y from the Unreal implementation. Maybe
    // not a massive change, but it seems to help the distribution of the output
    // samples significantly. xi.x is evenly distributed, while xi.y is not
    alphad = max(MinSamplingAlpha, alphad);

    #if !defined(OLD_M_DISTRIBUTION_FN)
        // This is the distribution functions from Walter07 --
        // It was intended for both reflection and transmision
        //  theta = arctan(q)
        //  phi = 2 * pi * xi.y
        //  where q = alphad * sqrt(xi.x) / sqrt(1.f - xi.x)
        // So, cos(theta) = cos(arctan(q))
        //  = 1.f / sqrt(1.f + q*q) (from trig)
        //
        // These functions are designed to work with GGX specifically.
        // The probability function is p(m) = D(M) * abs(dot(m, n))
        //
        // Note that the math here is actually mathematically identical to
        //      float cosTheta = sqrt((1.f - xi.x) / (1.f + (alphad*alphad - 1.f) * xi.x));
        // However, the above form is not evaluating correctly in some cases. Since this
        // is only used for reference and precalculation steps, let's just use the unoptimized
        // form below.
        float q = alphad * sqrt(xi.x) / sqrt(1.f - xi.x);
        float cosTheta = 1.f / sqrt(1.f + q*q);
    #else
        // This is the distribution function from the unreal course notes
        // They say the pdf is as below, but I haven't checked that.
        //      pdf  = (D * NdotH) / 4.f * VdotH
        float cosTheta = sqrt((1.f - xi.x) / (1.f + (alphad*alphad - 1.f) * xi.x));
    #endif

    float sinTheta = sqrt(1.f - cosTheta * cosTheta);
    float phi = 2.f * pi * xi.y;

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

    // we're just building a tangent frame to give meaning to theta and phi...
    float3 up = (abs(normal.x) < 0.5f) ? float3(1,0,0) : float3(0,1,0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = cross(normal, tangentX);
    return tangentX * H.x + tangentY * H.y + normal * H.z;
}

float InversePDFWeight(float3 H, float3 N, float3 V, float alphad)
{
    float NdotH = abs(dot(H, N));
    precise float D = TrowReitzD(NdotH, max(MinSamplingAlpha, alphad));

    #if !defined(OLD_M_DISTRIBUTION_FN)
        // note -- this will only work correctly for any implementation
        // of TrowReitzD that exactly matches the Walter07 paper
        // (because we're using his distribution function in SampleMicrofacetNormalGGX)
        // Note that this can return very large values for grazing angles. This
        // might be over-exaggerating these samples?
        return 1.f / (D * NdotH);
    #else
        float VdotH = abs(dot(V, H));
        return (4.f * VdotH) / (D * NdotH);
    #endif
}

float3 CosWeightedDirection(uint i, uint sampleCount, float3 normal)
{
    // Derived from:
    //  https://pathtracing.wordpress.com/2011/03/03/cosine-weighted-hemisphere/
    // (with some redundant calculations removed)
    float2 xi = HammersleyPt(i, sampleCount);

    float cosTheta = sqrt(1.0f-xi.x);
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    float phi = 2.0f * pi * xi.y;

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;

#if 1
    float3 up = abs(normal.z) < 0.999f ? float3(0,0,1) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = cross(normal, tangentX);
    return tangentX * H.x + tangentY * H.y + normal * H.z;
#else
    float3 z = normal;
    float3 h = z;
    if (abs(h.x)<=abs(h.y) && abs(h.x)<=abs(h.z))
        h.x = 1.0f;
    else if (abs(h.y)<=abs(h.x) && abs(h.y)<=abs(h.z))
        h.y = 1.0f;
    else
        h.z = 1.0f;

    float3 x = normalize(cross(h, z));
    float3 y = cross(x, z);
    return H.x * x + H.y * y + H.z * z;
#endif
}

float MipmapToRoughness(uint mipIndex)
{
    // We can adjust the mapping between roughness and the mipmaps as needed...
    // Each successive mipmap is smaller, so we loose resolution linearly against
    // roughness (even though the blurring amount is not actually linear against roughness)
    // We could use the inverse of the GGX function to calculate something that is
    // more linear against the sample cone size, perhaps...?
    // Does it make sense to offset by .5 to get a value in the middle of the range? We
    // will be using trilinear filtering to get a value between 2 mipmaps.
    // Arguably a roughness of "0.0" is not very interesting -- but we commit our
    // highest resolution mipmap to that.
    return 0.08f + 0.33f * saturate(float(mipIndex) / float(SpecularIBLMipMapCount));
}

float RoughnessToMipmap(float roughness)
{
    return saturate(3.0f * roughness - 0.08f) * SpecularIBLMipMapCount;
}


#endif
