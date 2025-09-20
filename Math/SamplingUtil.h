// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <random>
#include <vector>

namespace XLEMath
{
	template <int Base>
		inline float CalculateHaltonNumber(unsigned index)
	{
		// See https://pbr-book.org/3ed-2018/Sampling_and_Reconstruction/The_Halton_Sampler
		// AMD's capsaicin implementation does not seem perfect. Instead, let's take some cures from the pbr-book
		// Note not bothering with the reverse bit trick for base 2
		float reciprocalBaseN = 1.0f, result = 0.0f;
		float reciprocalBase = 1.f / float(Base);
		while (index) {
			auto next = index / Base;
			auto digit = index - next * Base;
			result = result * Base + digit;
			reciprocalBaseN *= reciprocalBase;
			index = next;
		}
		return result * reciprocalBaseN;
	}

	template <int BaseIdx>
		inline float CalculateScrambledHaltonNumber(unsigned index)
	{
		static constexpr unsigned Primes[] = { 2, 3, 5, 7, 11 };
		static unsigned primeSums[dimof(Primes)];
		static std::vector<uint16_t> digitPerms;
		if (digitPerms.empty()) {
			std::mt19937_64 rng(6294384621946ull);
			unsigned accumulator = 0;
			for (unsigned c=0; c<dimof(Primes); ++c) {
				primeSums[c] = accumulator;
				accumulator += Primes[c];
			}
			digitPerms.reserve(accumulator);
			for (unsigned c=0; c<dimof(Primes); ++c) {
				auto start = digitPerms.size();
				for (unsigned q=0; q<Primes[c]; ++q)
					digitPerms.push_back(q);
				std::shuffle(digitPerms.begin()+start, digitPerms.end(), rng);
			}
		}

		assert(BaseIdx < dimof(Primes));
		assert((primeSums[BaseIdx] + BaseIdx) <= digitPerms.size());
		uint16_t* perm = digitPerms.data() + primeSums[BaseIdx];

		int Base = Primes[BaseIdx];
		float reciprocalBaseN = 1.0f, result = 0.0f;
		float reciprocalBase = 1.f / float(Base);
		while (index) {
			auto next = index / Base;
			auto digit = index - next * Base;
			result = result * Base + perm[digit];
			reciprocalBaseN *= reciprocalBase;
			index = next;
		}
		return reciprocalBaseN * (result + reciprocalBase * perm[0] / (1 - reciprocalBase));
	}

}

