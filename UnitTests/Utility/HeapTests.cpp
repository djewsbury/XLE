
#include "../../Utility/HeapUtils.h"
#include <random>

#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace Utility::Literals;

namespace UnitTests
{
	struct MoveableObj
	{
		uint64_t _v;
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

}

