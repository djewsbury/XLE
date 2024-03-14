
#include "../../Utility/HeapUtils.h"
#include <random>
#include <iostream>

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

		REQUIRE(*(heap.begin()+0) == 5);
		REQUIRE(*(heap.begin()+1) == 6);
		REQUIRE(*(heap.begin()+2) == 32);
		REQUIRE(*(heap.begin()+3) == 64);
		REQUIRE(*(heap.begin()+4) == 100);
		REQUIRE(*(heap.begin()+5) == 385);
		REQUIRE((heap.begin()+6) == heap.end());

		REQUIRE(heap.IsAllocated(32));
		heap.Deallocate(heap.begin()+2);
		REQUIRE(!heap.IsAllocated(32));

		REQUIRE(heap.IsAllocated(64));
		heap.Deallocate(heap.Remap(64));
		REQUIRE(!heap.IsAllocated(64));

		REQUIRE(heap.IsAllocated(100));
		heap.Deallocate(heap.Remap(100));
		REQUIRE(!heap.IsAllocated(100));

		REQUIRE(*(heap.begin()+0) == 5);
		REQUIRE(*(heap.begin()+1) == 6);
		REQUIRE(*(heap.begin()+2) == 385);
		REQUIRE((heap.begin()+3) == heap.end());
	}

}

