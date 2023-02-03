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

static float RadicalInverseBase2(uint a)
{
    // non ideal for GPU usage, don't use when performance is a concern
    uint Base = 2;
    uint reversedDigits = 0;
    uint divisor = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        divisor *= Base;
        a = next;
    }
    return reversedDigits / float(divisor);
}

static float RadicalInverseBase3(uint a)
{
    // non ideal for GPU usage, don't use when performance is a concern
    uint Base = 3;
    uint reversedDigits = 0;
    uint divisor = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        divisor *= Base;
        a = next;
    }
    return reversedDigits / float(divisor);
}

static float RadicalInverseBase5(uint a)
{
    // non ideal for GPU usage, don't use when performance is a concern
    uint Base = 5;
    uint reversedDigits = 0;
    uint divisor = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        divisor *= Base;
        a = next;
    }
    return reversedDigits / float(divisor);
}

static float RadicalInverseBase7(uint a)
{
    // non ideal for GPU usage, don't use when performance is a concern
    uint Base = 7;
    uint reversedDigits = 0;
    uint divisor = 1;
    while (a) {
        uint next = a / Base;
        uint digit = a - next * Base;
        reversedDigits = reversedDigits * Base + digit;
        divisor *= Base;
        a = next;
    }
    return reversedDigits / float(divisor);
}

float2 HammersleyPt(uint i, uint N)
{
    // note --  we can avoid the M==N case by using the equation below. Since
    //          M==N can lead to singularities, perhaps this give better sampling?
    // return float2(float(i+1)/float(N+1), VanderCorputRadicalInverse(i));
    return float2(float(i)/float(N), VanderCorputRadicalInverse(i));
}

float3 TransformByArbitraryTangentFrame(float3 tangentSpaceInput, float3 normalInDestinationSpace)
{
    // we're just building a tangent frame to give meaning to theta and phi...
    float3 up = (abs(normalInDestinationSpace).y < 0.5f) ? float3(0,1,0) : float3(1,0,0);
    float3 tangentX = normalize(cross(up, normalInDestinationSpace));
    float3 tangentY = cross(normalInDestinationSpace, tangentX);
    return tangentX * tangentSpaceInput.x 
         + tangentY * tangentSpaceInput.y
         + normalInDestinationSpace * tangentSpaceInput.z;
}

float3 SamplerGGXHalfVector_Pick(float2 xi, float alphad)
{
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
    // See https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
    // for more information about normalized "PDFs" in this context
    alphad = max(MinSamplingAlpha, alphad);

    // This is the distribution functions from Walter07 --
    // It was intended for both reflection and transmision
    //
    // This is a distribution directly built from D(...) * abs(dot(m, n))
    // It doesn't consider G, F, or other potentially useful factors
    // As mentioned above, the PDF (respecting solid angle) is p(m) = D(M) * abs(dot(m, n))
    //
    //  theta = arctan(q)
    //  phi = 2 * pi * xi.y
    //  where q = alphad * sqrt(xi.x) / sqrt(1.f - xi.x)
    // So, cos(theta) = cos(arctan(q))
    //  = 1.f / sqrt(1.f + q*q) (from trig)
    //
    // Note that the math here is actually mathematically identical to
    //      float cosTheta = sqrt((1.f - xi.x) / (1.f + (alphad*alphad - 1.f) * xi.x));
    // However, the above form is not evaluating correctly in some cases. Since this
    // is only used for reference and precalculation steps, let's just use the unoptimized
    // form below.
    //
    // See more reference (including working out) at https://agraphicsguynotes.com/posts/sample_microfacet_brdf/
    // float cosTheta = 1.f / sqrt(1.f + q*q);
    // float q = alphad * sqrt(xi.x) / sqrt(1.f - xi.x);
    float cosTheta = sqrt((1.f - xi.x) / (1.f + (alphad*alphad - 1.f) * xi.x));

    float sinTheta = sqrt(1.f - cosTheta * cosTheta);
    float phi = 2.f * pi * xi.y;

    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;
    return H;
}

float SamplerGGXHalfVector_PDFh(float3 tangentSpaceHalfVector, float alphad)
{
    // Used alongside SamplerGGXHalfVector_Pick
    // Gives the pdf respecting solid angle for the half vector distribution (ie, ph(omega))
    //
    // This is D(...) * abs(dot(m, n))
    // But it must be in exactly the same form used in the distribution in SamplerGGXHalfVector_Pick
    // (ie, separating from our TrowReitzD for this purpose)
    //
    // Nice reference with working out from https://agraphicsguynotes.com/posts/sample_microfacet_brdf/

    float cosTheta = tangentSpaceHalfVector.z;
    float denomSqrt = (alphad * alphad - 1) * cosTheta * cosTheta + 1;
    return alphad * alphad * cosTheta / (pi*denomSqrt*denomSqrt);
}

float SamplerGGXHalfVector_PDF(float3 tangentSpaceHalfVector, float3 tangentSpaceViewVector, float alphad)
{
    // We require a change-of-variables term to convert from a pdf describing the distribution of
    // half vectors, to a pdf describing a distribution of incident light vectors (since the integral we're solving
    // is over incident light vectors respecting solid angle)
    // fortunately it's a pretty easy conversion
    // See PBR book chapter 14.1.1
    return SamplerGGXHalfVector_PDFh(tangentSpaceHalfVector, alphad) / (4 * dot(tangentSpaceViewVector, tangentSpaceHalfVector));
}

float3 SamplerHeitzGGXVNDF_Pick(float3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
    // See https://jcgt.org/published/0007/04/01/paper.pdf
    // "Sampling the GGX Distribution of Visible Normals"

    // Section 3.2: transforming the view direction to the hemisphere configuration
    float3 Vh = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    float3 T1 = lensq > 0 ? float3(-Vh.y, Vh.x, 0) * rsqrt(lensq) : float3(1,0,0);
    float3 T2 = cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * pi * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s)*sqrt(1.0 - t1*t1) + s*t2;
    // Section 4.3: reprojection onto hemisphere
    float3 Nh = t1*T1 + t2*T2 + sqrt(max(0.0, 1.0 - t1*t1 - t2*t2))*Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    float3 Ne = normalize(float3(alpha_x * Nh.x, alpha_y * Nh.y, max(0.0, Nh.z)));
    return Ne;
}

float SamplerHeitzGGXVNDF_PDFh(float3 M, float3 V_, float alpha)
{
    // See https://jcgt.org/published/0007/04/01/paper.pdf
    // "Sampling the GGX Distribution of Visible Normals"
    // V_ must be in tangent space (ie, V_.z is VdotN)
    //
    // note that this returns the pdf for the distribution of half vectors w.r.t solid angle
    float D = TrowReitzD(M.z, alpha);
    float G = SmithG(V_.z, alpha);  // G only in one direction
    return G * D * saturate(dot(V_, M)) / V_.z;
}

float2 ConcentricSampleDisk(float2 xi)
{
	// See pbr-book chapter 13.6.2
	// Very snazzy method that projects 8 triangular octants onto slices of the disk
	// it's a little like the geodesic sphere method
	// this creates a nicer distribution relative to our evenly space xi input coords
	// than a basic polar coordinate method would

	float2 xiOffset = 2 * xi - 1.0.xx;
	if (all(xiOffset == 0)) return 0;

	float theta, r;
	if (abs(xiOffset.x) > abs(xiOffset.y)) {
		r = xiOffset.x;
		theta = pi / 4.0 * (xiOffset.y / xiOffset.x);
	} else {
		r = xiOffset.y;
		theta = pi / 2.0 - pi / 4.0 * (xiOffset.x / xiOffset.y);
	}
	float2 result;
	sincos(theta, result.x, result.y);
	return r * result;
}

float3 SamplerCosineHemisphere_Pick(out float pdf, float2 xi)
{
    // using distribution trick from pbr-book Chapter 13.6.3
    float2 disk = ConcentricSampleDisk(xi);
    float z = sqrt(max(0, 1-dot(disk, disk)));
    pdf = z * reciprocalPi;     // pdf w.r.t solid angle
    return float3(disk.x, disk.y, z);
}

float SamplerCosineHemisphere_PDF(float cosTheta)     // cosTheta is NdotL in typical cases
{
    return cosTheta * reciprocalPi; // pdf w.r.t solid angle
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
