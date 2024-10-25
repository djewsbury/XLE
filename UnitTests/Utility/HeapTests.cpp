
#include "../../Utility/HeapUtils.h"
#include <random>
#include <iostream>
#include <chrono>

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

namespace UnitTests
{
	struct MoveableObj
	{
		uint64_t _v = 0;
		std::unique_ptr<MoveableObj> _ptrs[16];	// emulating a complex object
	};

	TEST_CASE( "Utilities-CircularPageHeap", "[utility]" )
	{
		std::vector<MoveableObj> manyObjsInAVector;
		CircularPagedHeap<MoveableObj> manyObjs;
		std::mt19937_64 rng(60254046252957);

		const unsigned emplaceCount = 100000;
		for (unsigned c=0; c<emplaceCount; ++c) {
			MoveableObj a, b;
			a._v = b._v = rng();
			manyObjsInAVector.emplace_back(std::move(a));
			manyObjs.emplace_back(std::move(b));
		}

		// worse-case addition adds
		const unsigned randomAdditionalAdds = 10000;
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			MoveableObj a, b;
			a._v = b._v = rng();
			auto idx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng);
			manyObjsInAVector.emplace(manyObjsInAVector.begin()+idx, std::move(a));
			manyObjs.emplace(manyObjs.begin()+idx, std::move(b));
		}

		REQUIRE(manyObjs.size() == manyObjsInAVector.size());

		unsigned randomRemoveCount = manyObjs.size() / 2;
		for (unsigned c=0; c<randomRemoveCount; ++c) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng);
			manyObjsInAVector.erase(manyObjsInAVector.begin()+removeIdx);
			manyObjs.erase(manyObjs.begin()+removeIdx);
		}

		// more additional adds, after we've create some holes in the arrays
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			MoveableObj a, b;
			a._v = b._v = rng();
			auto idx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng);
			manyObjsInAVector.emplace(manyObjsInAVector.begin()+idx, std::move(a));
			manyObjs.emplace(manyObjs.begin()+idx, std::move(b));
		}

		// check that we get the same results when iterating through
		REQUIRE(manyObjs.size() == manyObjsInAVector.size());
		auto i = manyObjsInAVector.begin();
		auto i2 = manyObjs.begin();
		while (i2 != manyObjs.end()) {
			REQUIRE(i->_v == i2->_v);
			++i; ++i2;
		}

		uint64_t t=0;
		for (auto& q:manyObjs) t += q._v;		// ensure this compiles

		// remove remaining objects
		while (!manyObjs.empty()) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng);
			manyObjsInAVector.erase(manyObjsInAVector.begin()+removeIdx);
			manyObjs.erase(manyObjs.begin()+removeIdx);
		}
	}

	TEST_CASE( "Utilities-CircularPageHeap-Performance", "[utility]" )
	{
		std::vector<MoveableObj> manyObjsInAVector;
		CircularPagedHeap<MoveableObj> manyObjs;
		std::mt19937_64 rng0(60254046252957);
		std::mt19937_64 rng1(60254046252957);

		const unsigned emplaceCount = 100000;
		auto v0 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<emplaceCount; ++c)
			manyObjsInAVector.emplace_back(MoveableObj{});
		auto m0 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<emplaceCount; ++c)
			manyObjs.emplace_back(MoveableObj{});
		auto h0 = std::chrono::steady_clock::now();

		// worse-case addition adds
		const unsigned randomAdditionalAdds = 10000;
		auto v1 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjsInAVector.size()-1)(rng0);
			manyObjsInAVector.emplace(manyObjsInAVector.begin()+idx, MoveableObj{});
		}
		auto m1 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng1);
			manyObjs.emplace(manyObjs.begin()+idx, MoveableObj{});
		}
		auto h1 = std::chrono::steady_clock::now();

		const unsigned randomRemoveCount = manyObjs.size() / 2;

		auto v2 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomRemoveCount; ++c) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjsInAVector.size()-1)(rng0);
			manyObjsInAVector.erase(manyObjsInAVector.begin()+removeIdx);
		}
		auto m2 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomRemoveCount; ++c) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng1);
			manyObjs.erase(manyObjs.begin()+removeIdx);
		}
		auto h2 = std::chrono::steady_clock::now();

		// more additional adds, after we've create some holes in the arrays
		auto v3 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjsInAVector.size()-1)(rng0);
			manyObjsInAVector.emplace(manyObjsInAVector.begin()+idx, MoveableObj{});
		}
		auto m3 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomAdditionalAdds; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng1);
			manyObjs.emplace(manyObjs.begin()+idx, MoveableObj{});
		}
		auto h3 = std::chrono::steady_clock::now();

		// random lookup
		const unsigned randomLookupCount = 100000;
		uint64_t t0=0, t1=0;
		auto v4 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomLookupCount; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjsInAVector.size()-1)(rng0);
			t0 += (manyObjsInAVector.begin()+idx)->_v;
		}
		auto m4 = std::chrono::steady_clock::now();
		for (unsigned c=0; c<randomLookupCount; ++c) {
			auto idx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng1);
			t1 += (manyObjs.begin()+idx)->_v;
		}
		auto h4 = std::chrono::steady_clock::now();

		// remove remaining objects
		auto v5 = std::chrono::steady_clock::now();
		while (!manyObjsInAVector.empty()) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjsInAVector.size()-1)(rng0);
			manyObjsInAVector.erase(manyObjsInAVector.begin()+removeIdx);
		}
		auto m5 = std::chrono::steady_clock::now();
		while (!manyObjs.empty()) {
			auto removeIdx = std::uniform_int_distribution<>(0, manyObjs.size()-1)(rng1);
			manyObjs.erase(manyObjs.begin()+removeIdx);
		}
		auto h5 = std::chrono::steady_clock::now();


		std::cout << "0 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m0-v0).count() << std::endl;
		std::cout << "0 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h0-m0).count() << std::endl;
		std::cout << "0 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h0-m0).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m0-v0).count()) << "%" << std::endl;

		std::cout << "1 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m1-v1).count() << std::endl;
		std::cout << "1 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h1-m1).count() << std::endl;
		std::cout << "1 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h1-m1).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m1-v1).count()) << "%" << std::endl;

		std::cout << "2 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m2-v2).count() << std::endl;
		std::cout << "2 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h2-m2).count() << std::endl;
		std::cout << "2 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h2-m2).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m2-v2).count()) << "%" << std::endl;

		std::cout << "3 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m3-v3).count() << std::endl;
		std::cout << "3 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h3-m3).count() << std::endl;
		std::cout << "3 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h3-m3).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m3-v3).count()) << "%" << std::endl;

		std::cout << "4 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m4-v4).count() << std::endl;
		std::cout << "4 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h4-m4).count() << std::endl;
		std::cout << "4 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h4-m4).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m4-v4).count()) << "%" << std::endl;

		std::cout << "5 Vector test: " << std::chrono::duration_cast<std::chrono::milliseconds>(m5-v5).count() << std::endl;
		std::cout << "5 Heap test: " << std::chrono::duration_cast<std::chrono::milliseconds>(h5-m5).count() << std::endl;
		std::cout << "5 Diff: " << 100.f * std::chrono::duration_cast<std::chrono::nanoseconds>(h5-m5).count() / float(std::chrono::duration_cast<std::chrono::nanoseconds>(m5-v5).count()) << "%" << std::endl;
	}

	TEST_CASE( "Utilities-RemappingBitHeap", "[utility]" )
	{
		RemappingBitHeap<unsigned> heap;
		heap.Allocate(5);
		heap.Allocate(385);
		heap.Allocate(32);
		heap.Allocate(100);
		heap.Allocate(64);
		heap.Allocate(6);

		auto i = heap.begin();
		REQUIRE(i != heap.end());
		REQUIRE(*i == 5);
		++i;
		REQUIRE(i != heap.end());
		REQUIRE(*i == 6);
		++i;
		REQUIRE(i != heap.end());
		REQUIRE(*i == 32);
		++i;
		REQUIRE(i != heap.end());
		REQUIRE(*i == 64);
		++i;
		REQUIRE(i != heap.end());
		REQUIRE(*i == 100);
		++i;
		REQUIRE(i != heap.end());
		REQUIRE(*i == 385);
		++i;
		REQUIRE(i == heap.end());
		REQUIRE(heap.size() == 6);

		REQUIRE(*(heap.begin()+0) == 5);
		REQUIRE(*(heap.begin()+1) == 6);
		REQUIRE(*(heap.begin()+2) == 32);
		REQUIRE(*(heap.begin()+3) == 64);
		REQUIRE(*(heap.begin()+4) == 100);
		REQUIRE(*(heap.begin()+5) == 385);
		REQUIRE((heap.begin()+6) == heap.end());

		REQUIRE(heap.Remap(5).DenseSequenceValue() == 0);
		REQUIRE(heap.Remap(6).DenseSequenceValue() == 1);
		REQUIRE(heap.Remap(32).DenseSequenceValue() == 2);
		REQUIRE(heap.Remap(64).DenseSequenceValue() == 3);
		REQUIRE(heap.Remap(100).DenseSequenceValue() == 4);
		REQUIRE(heap.Remap(385).DenseSequenceValue() == 5);

		REQUIRE(heap.IsAllocated(32));
		heap.Deallocate(heap.begin()+2);
		REQUIRE(!heap.IsAllocated(32));

		REQUIRE(heap.IsAllocated(100));
		heap.Deallocate(heap.Remap(100));
		REQUIRE(!heap.IsAllocated(100));

		REQUIRE(heap.IsAllocated(64));
		heap.Deallocate(heap.Remap(64));
		REQUIRE(!heap.IsAllocated(64));

		REQUIRE(*(heap.begin()+0) == 5);
		REQUIRE(*(heap.begin()+1) == 6);
		REQUIRE(*(heap.begin()+2) == 385);
		REQUIRE((heap.begin()+3) == heap.end());
		REQUIRE(heap.size() == 3);
	}


    #if 1
		// (this implementation does not function the same as the others)

        // carry-less multiplication; will compile to PCLMULQDQ on x86
        static uint64_t clmul (uint64_t n, uint64_t m)
        {
            __v2di a, b;
            a[0] = n;
            b[0] = m;
            auto r = __builtin_ia32_pclmulqdq128(a,b,0);
            return r[0];
        }

        static uint64_t nth_set_fast (uint64_t m, int n)
        {

            // count set bits in every block of 7
            uint64_t pc = (m &~0xAA54A952A54A952Aull) + ((m &0xAA54A952A54A952Aull)>>1ull);
                    pc = (pc&~0xCC993264C993264Cull) + ((pc&0xCC993264C993264Cull)>>2ull);
                    pc = (pc&~0xF0E1C3870E1C3870ull) + ((pc&0xF0E1C3870E1C3870ull)>>4ull);

            // prefix scan partial sums
            pc *= 0x0102040810204081ull<<7ull;

            // copy n to all blocks
            uint64_t nn = uint64_t(n)* 0x0102040810204081ull;

            // substract nn-pc for each block without carry
            uint64_t ss = nn + (~pc & ~(0x8102040810204081ull>>1)) + 0x8102040810204081ull;

            // find correct block
            uint64_t cc= ss & ~(ss>>7ull) & (0x8102040810204081ull>>1); cc>>=6ull;

            // block mask
            uint64_t bb = (cc<<8ull) -cc; 

            m &= bb; // zero all other blocks

            // xor-prefix scan; select odd/even depending on remainder bit
            uint64_t m0 = clmul(m ,0xFF) & m ; m0 ^=  m  & ( -(ss&cc));
            uint64_t m1 = clmul(m0,0xFF) & m0; m1 ^=  m0 & ( -((ss>>1ull)&cc));
            uint64_t m2 = clmul(m1,0xFF) & m1; m2 ^=  m1 & ( -((ss>>2ull)&cc));
            uint64_t m3 = clmul(m2,0xFF) & m2; m3 ^=  m2 & ( -((ss>>3ull)&cc)); // last step needed because of leftover bit at index 63

            return m3 & bb;
        }
    #endif

    static uint64_t pos_of_nth_bit2(uint64_t X, uint64_t bit)
    {
        // Requires that __builtin_popcountll(X) > bit.
        if (__builtin_popcountll(X) <= bit) return 64u;

        // https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
        // relatively easy to understand solution: we binary search down to a 4 bit range
        // and then use a lookup table
        // We could also use non-lookup table approaches such as binary searching down to
        // 2 bits

        int32_t testx, pos, pop;
        int8_t lut[4][16] = {{0,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0},
                            {0,0,0,1,0,2,2,1,0,3,3,1,3,2,2,1},
                            {0,0,0,0,0,0,0,2,0,0,0,3,0,3,3,2},
                            {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3}};
        bool test;
        pos = 0;
        pop = __builtin_popcount(X & 0xffffffffUL);
        test = pop <= bit;
        bit -= test*pop;
        testx = test*32;
        X >>= testx;
        pos += testx;
        pop = __builtin_popcount(X & 0xffffUL);
        test = pop <= bit;
        bit -= test*pop;
        testx = test*16;
        X >>= testx;
        pos += testx;
        pop = __builtin_popcount(X & 0xffUL);
        test = pop <= bit;
        bit -= test*pop;
        testx = test*8;
        X >>= testx;
        pos += testx;
        pop = __builtin_popcount(X & 0xfUL);
        test = pop <= bit;
        bit -= test*pop;
        testx = test*4;
        X >>= testx;
        pos += testx;
        return pos + lut[bit][X & 0xf];
    }

    static unsigned int nth_bit_set_parallelpopcount(uint64_t value, unsigned int n)
    {
        auto t = __builtin_popcountll(value);

        // Adapted for 64 bits from https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
        // Here, we're using an approach from Bit Twiddling Hacks to find population count in blocks
        /// This, in turn, is adapted by https://graphics.stanford.edu/~seander/bithacks.html#SelectPosFromMSBRank
        // (though that code uses a different method to express the constants here)
        const uint64_t  pop2  = (value & 0x5555555555555555ull) + ((value >> 1ull) & 0x5555555555555555ull);
        const uint64_t  pop4  = (pop2  & 0x3333333333333333ull) + ((pop2  >> 2ull) & 0x3333333333333333ull);
        const uint64_t  pop8  = (pop4  & 0x0f0f0f0f0f0f0f0full) + ((pop4  >> 4ull) & 0x0f0f0f0f0f0f0f0full);
        const uint64_t  pop16 = (pop8  & 0x00ff00ff00ff00ffull) + ((pop8  >> 8ull) & 0x00ff00ff00ff00ffull);
        const uint64_t  pop32 = (pop16 & 0x0000ffff0000ffffull) + ((pop16 >>16ull) & 0x0000ffff0000ffffull);
        const uint64_t  pop64 = (pop32 & 0x00000000ffffffffull) + ((pop32 >>32ull) & 0x00000000ffffffffull);
        unsigned int    rank  = 0;
        uint64_t    temp;

        if (n++ >= pop64)
            return 64;

        temp = pop32 & 0xffu;
        /* if (n > temp) { n -= temp; rank += 32; } */
        rank += ((temp - n) & 256) >> 3;
        n -= temp & ((temp - n) >> 8);

        temp = (pop16 >> rank) & 0xffu;
        /* if (n > temp) { n -= temp; rank += 16; } */
        rank += ((temp - n) & 256) >> 4;
        n -= temp & ((temp - n) >> 8);

        temp = (pop8 >> rank) & 0xffu;
        /* if (n > temp) { n -= temp; rank += 8; } */
        rank += ((temp - n) & 256) >> 5;
        n -= temp & ((temp - n) >> 8);

        temp = (pop4 >> rank) & 0x0fu;
        /* if (n > temp) { n -= temp; rank += 4; } */
        rank += ((temp - n) & 256) >> 6;
        n -= temp & ((temp - n) >> 8);

        temp = (pop2 >> rank) & 0x03u;
        /* if (n > temp) { n -= temp; rank += 2; } */
        rank += ((temp - n) & 256) >> 7;
        n -= temp & ((temp - n) >> 8);

        temp = (value >> rank) & 0x01u;
        /* if (n > temp) rank += 1; */
        rank += ((temp - n) & 256) >> 8;

        return rank;
    }

    static unsigned selectByte(uint8_t m, unsigned n)
    {
		#if 0
			const uint64_t ones = 0x0101010101010101ull;
			const uint64_t unique_bytes = 0x8040201008040201ull;
			const uint64_t unique_bytes_diff_from_msb = (ones * 0x80ull) - unique_bytes;
			const uint64_t prefix_sums = (((((m * ones) & unique_bytes) + unique_bytes_diff_from_msb) >> 7) & ones) * ones;

			const uint64_t broadcasted = ones * (uint64_t(n) | 0x80ull);
			const uint64_t bit_isolate = ones * 0x80;
			const uint64_t mask = (broadcasted - prefix_sums) & bit_isolate;

			if (mask == bit_isolate) return 8;

			return __builtin_popcountll(mask);
		#else

			int8_t lut[4][16] = {{0,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0},
								{0,0,0,1,0,2,2,1,0,3,3,1,3,2,2,1},
								{0,0,0,0,0,0,0,2,0,0,0,3,0,3,3,2},
								{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3}};
			auto pop = __builtin_popcount(m & 0xfUL);
			auto test = pop <= n;
			n -= test*pop;
			m >>= test*4;
			return test*4 + lut[n][m & 0xf];

		#endif
    }

    static unsigned nth_bit_set_SWAR(uint64_t m, unsigned int n)
    {
		if (__builtin_popcountll(m) <= n) return 64u;

        // Adapted from Validark's solution in https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
        // this is a little more evolved and starting to get pretty complicated
        const uint64_t ones = 0x0101010101010101ull;

        uint64_t i = m;
        i -= (i >> 1ull) & 0x5555555555555555ull;
        i = (i & 0x3333333333333333ull) + ((i >> 2ull) & 0x3333333333333333ull);
        const uint64_t prefix_sums = ((i + (i >> 4ull)) & 0x0F0F0F0F0F0F0F0Full) * ones;
        assert((prefix_sums & 0x8080808080808080ull) == 0);

        const uint64_t broadcasted = ones * (uint64_t(n) | 0x80ull);
        const uint64_t bit_isolate = ones * 0x80;
        const uint64_t mask = (broadcasted - prefix_sums) & bit_isolate;

        if (mask == bit_isolate) return 64;

        const uint64_t byte_index = __builtin_popcountll(mask) << 3;

        const unsigned prefix_sum = (prefix_sums << 8 >> byte_index) & 0x3f;
        const unsigned target_byte = (m >> byte_index) & 0xff;
        const unsigned n_for_target_byte = n - prefix_sum;
        assert(byte_index <= 7*8);
        assert(n_for_target_byte <= 8);

        return selectByte(target_byte, n_for_target_byte) + byte_index;
    }

    static unsigned nthset_pdep(uint64_t x, unsigned n)
    {
        // https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
        // Note that _pdep_u64 uses BMI2 instruction set
        // Intel: introduced in Haswell
        // AMD: before Zen3, _pdep_u64 is microcode and so may not be optimal
        // REQUIRE(n < 64);
        return _tzcnt_u64(_pdep_u64(1ull << n, x));
    }

    TEST_CASE( "Utilities-NthBitSet", "[utility]" )
    {
        std::mt19937_64 rng(0x5c6163f846a298e7ull);
        const unsigned tests = 500000;
        for (unsigned c2=0; c2<tests; ++c2) {
            auto v = rng();
            auto comparison = nthset_pdep(v, 0);
            REQUIRE(nth_bit_set_SWAR(v, 0) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 0) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 0) == comparison);
            
            comparison = nthset_pdep(v, 3);
            REQUIRE(nth_bit_set_SWAR(v, 3) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 3) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 3) == comparison);

            comparison = nthset_pdep(v, 5);
            REQUIRE(nth_bit_set_SWAR(v, 5) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 5) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 5) == comparison);

            comparison = nthset_pdep(v, 12);
            REQUIRE(nth_bit_set_SWAR(v, 12) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 12) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 12) == comparison);

            comparison = nthset_pdep(v, 29);
            REQUIRE(nth_bit_set_SWAR(v, 29) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 29) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 29) == comparison);

            comparison = nthset_pdep(v, 45);
            REQUIRE(nth_bit_set_SWAR(v, 45) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 45) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 45) == comparison);

            comparison = nthset_pdep(v, 62);
            REQUIRE(nth_bit_set_SWAR(v, 62) == comparison);
            REQUIRE(pos_of_nth_bit2(v, 62) == comparison);
            REQUIRE(nth_bit_set_parallelpopcount(v, 62) == comparison);
        }
    }

	namespace xoshiro
	{
		static inline uint32_t rotl32(uint32_t x, uint32_t k) { return (x << k) | (x >> (32u - k)); }

		struct RNGState { uint32_t s[4]; };

		uint32_t RNGNext(RNGState& state)
		{
			const uint32_t result = state.s[0] + state.s[3];
			const uint32_t t = state.s[1] << 9u;

			state.s[2] ^= state.s[0];
			state.s[3] ^= state.s[1];
			state.s[1] ^= state.s[2];
			state.s[0] ^= state.s[3];

			state.s[2] ^= t;

			state.s[3] = rotl32(state.s[3], 11u);

			return result;
		}

		void RNGInitialize(RNGState& state, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
		{
			state.s[0] = a;
			state.s[1] = b;
			state.s[2] = c;
			state.s[3] = d;
		}
	}

	TEST_CASE( "Utilities-NthBitSet-Performance", "[utility]" )
    {
		const unsigned testCount = 5000000;
		const auto rngSeed = 0x5c6163f846a298e7ull, rngSeed2 = 0xa149f46c3e6be525ull;

		for (unsigned outer=0; outer<5; ++outer) {
			{
				xoshiro::RNGState s; RNGInitialize(s, uint32_t(rngSeed), rngSeed>>32ull, uint32_t(rngSeed2), rngSeed2>>32ull);
				auto start = std::chrono::steady_clock::now();
				unsigned counter = 0;
				for (unsigned c=0; c<testCount; ++c)
					counter += nthset_pdep(xoshiro::RNGNext(s), c%32);		// low n reduces impact of early outs within the methods
				auto end = std::chrono::steady_clock::now();

				std::cout << "nthset_pdep: " << std::chrono::duration_cast<std::chrono::microseconds>(end-start).count() << " micros" << "                      (" << counter << ")" << std::endl;
			}

			{
				xoshiro::RNGState s; RNGInitialize(s, uint32_t(rngSeed), rngSeed>>32ull, uint32_t(rngSeed2), rngSeed2>>32ull);
				auto start = std::chrono::steady_clock::now();
				unsigned counter = 0;
				for (unsigned c=0; c<testCount; ++c)
					counter += nth_bit_set_SWAR(xoshiro::RNGNext(s), c%32);
				auto end = std::chrono::steady_clock::now();

				std::cout << "nth_bit_set_SWAR: " << std::chrono::duration_cast<std::chrono::microseconds>(end-start).count() << " micros" << "                      (" << counter << ")" << std::endl;
			}

			{
				xoshiro::RNGState s; RNGInitialize(s, uint32_t(rngSeed), rngSeed>>32ull, uint32_t(rngSeed2), rngSeed2>>32ull);
				auto start = std::chrono::steady_clock::now();
				unsigned counter = 0;
				for (unsigned c=0; c<testCount; ++c)
					counter += pos_of_nth_bit2(xoshiro::RNGNext(s), c%32);
				auto end = std::chrono::steady_clock::now();

				std::cout << "pos_of_nth_bit2: " << std::chrono::duration_cast<std::chrono::microseconds>(end-start).count() << " micros" << "                      (" << counter << ")" << std::endl;
			}

			{
				xoshiro::RNGState s; RNGInitialize(s, uint32_t(rngSeed), rngSeed>>32ull, uint32_t(rngSeed2), rngSeed2>>32ull);
				auto start = std::chrono::steady_clock::now();
				unsigned counter = 0;
				for (unsigned c=0; c<testCount; ++c)
					counter += nth_bit_set_parallelpopcount(xoshiro::RNGNext(s), c%32);
				auto end = std::chrono::steady_clock::now();

				std::cout << "nth_bit_set_parallelpopcount: " << std::chrono::duration_cast<std::chrono::microseconds>(end-start).count() << " micros" << "                      (" << counter << ")" << std::endl;
			}

			{
				xoshiro::RNGState s; RNGInitialize(s, uint32_t(rngSeed), rngSeed>>32ull, uint32_t(rngSeed2), rngSeed2>>32ull);
				auto start = std::chrono::steady_clock::now();
				unsigned counter = 0;
				for (unsigned c=0; c<testCount; ++c)
					counter += nth_set_fast(xoshiro::RNGNext(s), c%32);
				auto end = std::chrono::steady_clock::now();

				std::cout << "nth_set_fast: " << std::chrono::duration_cast<std::chrono::microseconds>(end-start).count() << " micros" << "                      (" << counter << ")" << std::endl;
			}
		}
	}

}

