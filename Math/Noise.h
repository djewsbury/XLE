// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"

namespace XLEMath
{
	float SimplexNoise(Float2 input);
	float SimplexNoise(Float3 input);
	float SimplexNoise(Float4 input);

	template<typename Type>
		float SimplexFBM(Type pos, float hgrid, float gain, float lacunarity, int octaves);

	// xoshiro128+ generator. Intended for generating 32 bit floats (since the lowest four bits have low linear complexity)
	// From the authors: We suggest to use a sign test to extract a random Boolean value, and right shifts to extract subsets of bits.
	// See source: https://prng.di.unimi.it/xoshiro128plus.c
	// Note that the authors have placed the above source into the public domain
	// Ideally we should initialize using values produced from another rng (they should never all be zeroes)

	namespace xoshiro
	{
		static inline uint32_t rotl32(uint32_t x, uint32_t k) { return (x << k) | (x >> (32u - k)); }

		inline uint32_t RNGNext(uint32_t state[4])
		{
			const uint32_t result = state[0] + state[3];
			const uint32_t t = state[1] << 9u;

			state[2] ^= state[0];
			state[3] ^= state[1];
			state[1] ^= state[2];
			state[0] ^= state[3];

			state[2] ^= t;

			state[3] = rotl32(state[3], 11u);

			return result;
		}

		struct RNGState { uint32_t _s[4]; };

		inline uint32_t RNGNext(RNGState& state) { return RNGNext(state._s); }
		inline void RNGInitialize(RNGState& state, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
		{
			state._s[0] = a;
			state._s[1] = b;
			state._s[2] = c;
			state._s[3] = d;
		}
	}

	inline float AsFloatBits(uint32_t input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; uint32_t i; };
		Converter c; c.i = input; 
		return c.f;
	}

	inline uint32_t AsUInt32Bits(float input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; uint32_t i; };
		Converter c; c.f = input; 
		return c.i;
	}

	inline double AsFloatBits(uint64_t input)
	{
			// (or just use a reinterpret cast)
		union Converter { double f; uint64_t i; };
		Converter c; c.i = input; 
		return c.f;
	}

	inline uint64_t AsUInt64Bits(double input)
	{
			// (or just use a reinterpret cast)
		union Converter { double f; uint64_t i; };
		Converter c; c.f = input; 
		return c.i;
	}

	inline float ZeroToOneFromBits(uint32_t i)
	{
		// Below uses the simple principle described at the bottom of https://prng.di.unimi.it/
		// Note that "float" must be a 32 bit type for this to work (and at least roughly ieee)
		// If we don't know the precision of float, we must use another method
		return AsFloatBits((0x7fu << 23u) | (i>>9u)) - 1.0;
	}
}
