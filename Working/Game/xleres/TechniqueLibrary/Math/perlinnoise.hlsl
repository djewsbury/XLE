// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(PERLIN_NOISE_H)
#define PERLIN_NOISE_H

#include "../Framework/CommonResources.hlsl"

Texture1D<float3>       GradTexture;
Texture1D<float>        PermTexture;

    //
    //      Improved Perlin noise calculation
    //      See GPU Gems 2
    //          http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter26.html
    //
    //      are fade/perm/grad names too generic for top level scope...?
    //

float3 fade(float3 t)
{
    return t * t * t * (t * (t * 6.f - 15.f) + 10.f);   // new curve
//  return t * t * (3 - 2 * t);                         // old curve
}

float3 fadeDev(float3 t)
{
    // return 30.f * t * t * (t - 1.0.xxx) * (t - 1.0.xxx);
    return 30.f * t * t * (t * (t - 2.0.xxx) + 1.0.xxx);
}

float2 fade(float2 t)
{
    return t * t * t * (t * (t * 6 - 15) + 10); // new curve
//  return t * t * (3 - 2 * t);                 // old curve
}

float perm(float x)
{
        // nvidia implementation always uses wrapping point sampler lookups for perm & grad
    // return PermTexture.SampleLevel(DefaultSampler, x / 256.f, 0) * 256.f;
    return PermTexture[uint(x)&0xff] * 256.f;
}

float3 sampleGrad(float x)
{
        // DavidJ -- there might be a problem here. try "frac(x/16.f)*16.f"
    // float t = frac(x)*16.f;
    float t = frac(x/16.f)*16.f;
    uint i = uint(t);
    return GradTexture[i];
    // return GradTexture.SampleLevel(DefaultSampler, x, 0);
}

float grad(float x, float3 p)
{
    return dot(sampleGrad(x), p);
}

float grad(float x, float2 p)
{
    return dot(sampleGrad(x).xy, p);
}

    // 3D version
float PerlinNoise3D(float3 p)
{
    float3 P = fmod(floor(p), 256.f);
    if (P.x < 0.0) P.x += 256.f;
    if (P.y < 0.0) P.y += 256.f;
    if (P.z < 0.0) P.z += 256.f;
    p -= floor(p);
    float3 f = fade(p);

        // HASH COORDINATES FOR 6 OF THE 8 CUBE CORNERS
    float A  = perm(P.x) + P.y;
    float AA = perm(A) + P.z;
    float AB = perm(A + 1) + P.z;
    float B  = perm(P.x + 1) + P.y;
    float BA = perm(B) + P.z;
    float BB = perm(B + 1) + P.z;

        // AND ADD BLENDED RESULTS FROM 8 CORNERS OF CUBE
    return
        lerp(
            lerp(   lerp(   grad(perm(AA), p),
                            grad(perm(BA), p + float3(-1, 0, 0)), f.x),
                    lerp(   grad(perm(AB), p + float3(0, -1, 0)),
                            grad(perm(BB), p + float3(-1, -1, 0)), f.x), f.y),
            lerp(   lerp(   grad(perm(AA + 1), p + float3(0, 0, -1)),
                            grad(perm(BA + 1), p + float3(-1, 0, -1)), f.x),
                    lerp(   grad(perm(AB + 1), p + float3(0, -1, -1)),
                            grad(perm(BB + 1), p + float3(-1, -1, -1)), f.x), f.y),
            f.z);
}

float PerlinNoise3DDev(float3 p, out float3 derivative)
{
    #if 0

        float3 P = fmod(floor(p), 256.f);
        p -= floor(p);
        float3 f = fade(p);

            // HASH COORDINATES FOR 6 OF THE 8 CUBE CORNERS
        float A  = perm(P.x) + P.y;
        float AA = perm(A) + P.z;
        float AB = perm(A + 1) + P.z;
        float B  = perm(P.x + 1) + P.y;
        float BA = perm(B) + P.z;
        float BB = perm(B + 1) + P.z;

        derivative = float3(0,0,1);

            // AND ADD BLENDED RESULTS FROM 8 CORNERS OF CUBE
        return
            lerp(
                lerp(   lerp(   grad(perm(AA), p),
                                grad(perm(BA), p + float3(-1, 0, 0)), f.x),
                        lerp(   grad(perm(AB), p + float3(0, -1, 0)),
                                grad(perm(BB), p + float3(-1, -1, 0)), f.x), f.y),
                lerp(   lerp(   grad(perm(AA + 1), p + float3(0, 0, -1)),
                                grad(perm(BA + 1), p + float3(-1, 0, -1)), f.x),
                        lerp(   grad(perm(AB + 1), p + float3(0, -1, -1)),
                                grad(perm(BB + 1), p + float3(-1, -1, -1)), f.x), f.y),
                f.z);

    #else

        float3 P = fmod(floor(p), 256.f);
        p -= floor(p);
        float3 f = fade(p);

            // HASH COORDINATES FOR 6 OF THE 8 CUBE CORNERS
        float A  = perm(P.x) + P.y;
        float AA = perm(A) + P.z;
        float AB = perm(A + 1) + P.z;
        float B  = perm(P.x + 1) + P.y;
        float BA = perm(B) + P.z;
        float BB = perm(B + 1) + P.z;

        float3 g000 = sampleGrad(perm(AA));
        float3 g100 = sampleGrad(perm(BA));
        float3 g010 = sampleGrad(perm(AB));
        float3 g110 = sampleGrad(perm(BB));

        float3 g001 = sampleGrad(perm(AA+1));
        float3 g101 = sampleGrad(perm(BA+1));
        float3 g011 = sampleGrad(perm(AB+1));
        float3 g111 = sampleGrad(perm(BB+1));

        float dot000 = dot(g000, p);
        float dot100 = dot(g100, p + float3(-1, 0, 0));
        float dot010 = dot(g010, p + float3(0, -1, 0));
        float dot110 = dot(g110, p + float3(-1, -1, 0));

        float dot001 = dot(g001, p + float3(0, 0, -1));
        float dot101 = dot(g101, p + float3(-1, 0, -1));
        float dot011 = dot(g011, p + float3(0, -1, -1));
        float dot111 = dot(g111, p + float3(-1, -1, -1));

        float noiseResult =
            lerp(
                lerp(lerp(dot000, dot100, f.x), lerp(dot010, dot110, f.x), f.y),
                lerp(lerp(dot001, dot101, f.x), lerp(dot011, dot111, f.x), f.y),
                f.z);

        float3 fDev = fadeDev(p);

        derivative.x =
              g000.x
            + fDev.x *  (dot100 - dot000)

            + f.x *     (g100.x - g000.x)
            + f.y *     (g010.x - g000.x)
            + f.z *     (g001.x - g000.x)

            + fDev.x * f.y * (dot110 - dot010 - dot100 + dot000)
            + f.x    * f.y * (g110.x - g010.x - g100.x + g000.x)
            + fDev.x * f.z * (dot101 - dot001 - dot100 + dot000)

            + f.x * f.z * (g101.x - g001.x - g100.x - g000.x)
            + f.y * f.z * (g011.x - g001.x - g010.x + g000.x)

            + fDev.x * f.y * f.z * (dot111 - dot011 - dot101 + dot001 - dot110 + dot010 + dot100 - dot000)
            + f.x * f.y * f.z *    (g111.x - g011.x - g101.x + g001.x - g110.x + g010.x + g100.x - g000.x)
            ;

        derivative.y =
              g000.y
            + f.x * (g100.y - g000.y)

            + fDev.y * (dot010 - dot000)
            + f.y * (g010.y - g000.y)
            + f.z * (g001.y - g000.y)

            + f.x * fDev.y * (dot110 - dot010 - dot100 + dot000)
            + f.x * f.y * (g110.y - g010.y - g100.y + g000.y)
            + f.x * f.z * (g101.y - g001.y - g100.y + g000.y)

            + fDev.y * f.z * (dot011 - dot001 - dot010 + dot000)
            + f.y * f.z * (g011.y - g001.y - g010.y + g000.y)

            + f.x * fDev.y * f.z * (dot111 - dot011 - dot101 + dot001 - dot110 + dot010 + dot100 - dot000)
            + f.x * f.y * f.z * (g111.y - g011.y - g101.y + g001.y - g110.y + g010.y + g100.y - g000.y)
            ;

        derivative.z =
              g000.z
            + f.x * (g100.z - g000.z)

            + f.y * (g010.z - g000.z)
            + fDev.z * (dot001 - dot000)
            + f.z * (g001.z - g000.z)

            + f.x * f.y * (g110.z - g010.z - g100.z + g000.z)
            + f.x * fDev.z * (dot101 - dot001 - dot110 + dot000)
            + f.x * f.z * (g101.z - g001.z - g100.z + g000.z)

            + f.y * fDev.z * (dot011 - dot001 - dot010 + dot000)
            + f.y * f.z * (g011.z - g001.z - g010.z + g000.z)

            + f.x * f.y * fDev.z * (dot111 - dot011 - dot101 + dot001 - dot110 + dot010 + dot100 - dot000)
            + f.x * f.y * f.z * (g111.z - g011.z - g101.z + g001.z - g110.z + g010.z + g100.z - g000.z)
            ;

        return noiseResult;

    #endif
}

    // 2D version
float PerlinNoise2D(float2 p)
{
    float2 P = fmod(floor(p), 256.f);
    if (P.x < 0.0) P.x += 256.f;
    if (P.y < 0.0) P.y += 256.f;
    p -= floor(p);
    float2 f = fade(p);

        // HASH COORDINATES FOR 6 OF THE 8 CUBE CORNERS
    float A  = perm(P.x) + P.y;
    float AA = perm(A);
    float AB = perm(A + 1);
    float B  = perm(P.x + 1) + P.y;
    float BA = perm(B);
    float BB = perm(B + 1);

        // AND ADD BLENDED RESULTS FROM 8 CORNERS OF CUBE
    return
        lerp(   lerp(   grad(perm(AA), p),
                        grad(perm(BA), p + float2(-1, 0)), f.x),
                lerp(   grad(perm(AB), p + float2(0, -1)),
                        grad(perm(BB), p + float2(-1, -1)), f.x), f.y);
}

float fbmNoise2D(float2 position, float hgrid, float gain, float lacunarity, int octaves)
{
		// standard fbm noise method
	float total = 0.0f;
	float frequency = 1.0f/(float)hgrid;
	float amplitude = 1.f;

	for (int i = 0; i < octaves; ++i)
	{
		total += PerlinNoise2D(position * frequency) * amplitude;
		frequency *= lacunarity;
		amplitude *= gain;
	}

	return total;
}

float fbmNoise3D(float3 position, float hgrid, float gain, float lacunarity, int octaves)
{
		// standard fbm noise method
	float total = 0.0f;
	float frequency = 1.0f/(float)hgrid;
	float amplitude = 1.f;

	for (int i = 0; i < octaves; ++i)
	{
		total += PerlinNoise3D(position * frequency) * amplitude;
		frequency *= lacunarity;
		amplitude *= gain;
	}

	return total;
}

float fbmNoise3DZeroToOne(float3 position, float hgrid, float gain, float lacunarity, int octaves)
{
		// standard fbm noise method
	float total = 0.0f;
	float frequency = 1.0f/(float)hgrid;
	float amplitude = 1.f;

	for (int i = 0; i < octaves; ++i)
	{
		total += (0.5f + 0.5f * PerlinNoise3D(position * frequency)) * amplitude;
		frequency *= lacunarity;
		amplitude *= gain;
	}

	return total;
}

#endif
