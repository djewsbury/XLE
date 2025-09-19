
#include "AssetHeapNew.h"
#include "ContinuationExecutor.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../Core/Prefix.h"

#include <chrono>
#include <memory>
#include <random>
#include <iostream>

using namespace Utility::Literals;

namespace AssetsNew
{
	auto AssetHeap::VisibilityBarrier() -> VisibilityMarkerId
	{
		std::vector<CheckFuturesHelper::Entry> recentCompletions;
		{
			ScopedLock(_checkFuturesHelper->_lock);
			std::swap(recentCompletions, _checkFuturesHelper->_completedState);
		}

		std::sort(
			recentCompletions.begin(), recentCompletions.end(),
			[](const auto& lhs, const auto& rhs) {
				if (lhs._type < rhs._type) return true;
				if (lhs._type > rhs._type) return false;
				if (lhs._code < rhs._code) return true;
				if (lhs._code > rhs._code) return false;
				return lhs._valIdx < rhs._valIdx;
			});

		ScopedLock(_tableLock);
		
		auto i = recentCompletions.begin();
		while (i!=recentCompletions.end()) {
			auto start = i;
			++i;
			while (i != recentCompletions.end() && i->_type == start->_type) ++i;

			using pm = p<IdentifierCode, unsigned>;
			VLA_UNSAFE_FORCE(pm, ids, i-start);
			for (auto i2=start; i2!=i; ++i2)
				ids[i2-start] = { i2->_code, i2->_valIdx };
			
			auto hc = start->_type.hash_code();
			auto ti = _tables.begin() + (hc >> 56ull) * s_tableSpacing;
			auto tend = ti + s_tableSpacing;
			while (expect_evaluation(ti != tend && ti->first < hc, false)) ++ti;
			assert(ti != tend);
			assert(ti->first == hc);
			if (ti->first != hc) continue;

			ti->second._checkCompletionFn(ti->second._table.get(), MakeIteratorRange(ids, ids+(i-start)), _lastVisibilityMarker+1);
		}

		return ++_lastVisibilityMarker;
	}

	namespace Internal
	{
		struct InvokerImmediate
		{
			T1(Fn) void operator()(Fn&& f) { f(); }
		};
	}

	AssetHeap::AssetHeap(std::shared_ptr<thousandeyes::futures::Executor> continuationExecutor)
	: _continuationExecutor(std::move(continuationExecutor))
	{
		_tables.resize(256u*s_tableSpacing, std::pair<uint64_t, InternalTable>(~uint64_t(0), InternalTable{}));
		
		if (!_continuationExecutor) {
			if (false) {
				_continuationExecutor = std::make_shared<::Assets::ContinuationExecutor>(
					std::chrono::microseconds(500),
					thousandeyes::futures::detail::InvokerWithNewThread{},
					::Assets::InvokerToThreadPool{ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool()});
			} else {
				using SimpleExecutor = thousandeyes::futures::PollingExecutor<
					thousandeyes::futures::detail::InvokerWithNewThread,
					Internal::InvokerImmediate>;
				_continuationExecutor = std::make_shared<SimpleExecutor>(
					std::chrono::microseconds(2000),
					thousandeyes::futures::detail::InvokerWithNewThread{},
					Internal::InvokerImmediate{});
			}
		}

		_checkFuturesHelper = std::make_shared<CheckFuturesHelper>();
	}
	AssetHeap::~AssetHeap() {}


#if defined(_DEBUG)

	struct TypeA { unsigned something[256]; };
	struct TypeB { unsigned something[52]; };
	struct TypeC { unsigned something[43]; };

	void AssetHeapTest()
	{
		AssetHeap assets { ::ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor() };

		const unsigned threadCount = 6;
		vp<std::promise<TypeA>, std::chrono::steady_clock::time_point> toCompleteA[threadCount];
		vp<std::promise<TypeB>, std::chrono::steady_clock::time_point> toCompleteB[threadCount];
		vp<std::promise<TypeC>, std::chrono::steady_clock::time_point> toCompleteC[threadCount];
		std::mt19937_64 rng(0x5c61e93fb63a273ull);
		auto start = std::chrono::steady_clock::now();

		for (unsigned c=0; c<10000; ++c) {
			p<std::promise<TypeA>, std::chrono::steady_clock::time_point> pair;
			pair.second = start + std::chrono::milliseconds(rng()%6000);
			assets.Insert(c+"TypeA"_h, "TypeA_" + std::to_string(c), pair.first.get_future());
			toCompleteA[rng()%threadCount].emplace_back(std::move(pair));
		}

		for (unsigned c=0; c<10000; ++c) {
			p<std::promise<TypeB>, std::chrono::steady_clock::time_point> pair;
			pair.second = start + std::chrono::milliseconds(rng()%6000);
			assets.Insert(c+"TypeB"_h, "TypeB_" + std::to_string(c), pair.first.get_future());
			toCompleteB[rng()%threadCount].emplace_back(std::move(pair));
		}

		for (unsigned c=0; c<10000; ++c) {
			p<std::promise<TypeC>, std::chrono::steady_clock::time_point> pair;
			pair.second = start + std::chrono::milliseconds(rng()%6000);
			assets.Insert(c+"TypeC"_h, "TypeC_" + std::to_string(c), pair.first.get_future());
			toCompleteC[rng()%threadCount].emplace_back(std::move(pair));
		}

		std::atomic<unsigned> completeThreads { 0 };
		std::vector<std::thread> bgThreads;
		for (unsigned c=0; c<threadCount; ++c)
			bgThreads.emplace_back(
				[&, c]() {
		
					while (!toCompleteA[c].empty() || !toCompleteB[c].empty() || !toCompleteC[c].empty()) {
						auto now = std::chrono::steady_clock::now();
						for (auto i=toCompleteA[c].begin(); i!=toCompleteA[c].end();) {
							if (i->second < now) {
								i->first.set_value(TypeA{});
								i = toCompleteA[c].erase(i);
							} else
								++i;
						}
						for (auto i=toCompleteB[c].begin(); i!=toCompleteB[c].end();) {
							if (i->second < now) {
								i->first.set_value(TypeB{});
								i = toCompleteB[c].erase(i);
							} else
								++i;
						}
						for (auto i=toCompleteC[c].begin(); i!=toCompleteC[c].end();) {
							if (i->second < now) {
								i->first.set_value(TypeC{});
								i = toCompleteC[c].erase(i);
							} else
								++i;
						}
					}

					++completeThreads;

				});

		while (completeThreads < threadCount) {
			assets.VisibilityBarrier();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		for (auto& t:bgThreads) t.join();

		auto futureTrickleWaitStart = std::chrono::steady_clock::now();

		// We know at this point that all of the promises have been fulfilled, but it can take a little bit for the future continuations to fire.  
		// We'll need to do a manual search to try to establish when all of the continuations have been handled
		for (;;) {
			assets.VisibilityBarrier();
			unsigned unreadyCount = 0;
			{
				auto endA = assets.End<TypeA>();
				for (auto i=assets.Begin<TypeA>(); i!=endA; ++i)
					unreadyCount += i.GetState() != ::Assets::AssetState::Ready;
				auto endB = assets.End<TypeB>();
				for (auto i=assets.Begin<TypeB>(); i!=endB; ++i)
					unreadyCount += i.GetState() != ::Assets::AssetState::Ready;
				auto endC = assets.End<TypeC>();
				for (auto i=assets.Begin<TypeC>(); i!=endC; ++i)
					unreadyCount += i.GetState() != ::Assets::AssetState::Ready;
			}

			if (!unreadyCount) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		auto futureTrickleWaitEnd = std::chrono::steady_clock::now();

		std::cout << "Pre-trickle: " << std::chrono::duration_cast<std::chrono::milliseconds>(futureTrickleWaitStart-start).count() << "ms Trickle wait: " << std::chrono::duration_cast<std::chrono::milliseconds>(futureTrickleWaitEnd-futureTrickleWaitStart).count() << "ms" << std::endl;

		auto endA = assets.End<TypeA>();
		for (auto i=assets.Begin<TypeA>(); i!=endA; ++i) {
			assert(i.GetState() == ::Assets::AssetState::Ready);
			std::cout << i.Id() << " (" << i.GetInitializer() << ") -- " << unsigned(i.GetState()) << std::endl;
		}

		auto endB = assets.End<TypeB>();
		for (auto i=assets.Begin<TypeB>(); i!=endB; ++i) {
			assert(i.GetState() == ::Assets::AssetState::Ready);
			std::cout << i.Id() << " (" << i.GetInitializer() << ") -- " << unsigned(i.GetState()) << std::endl;
		}

		auto endC = assets.End<TypeC>();
		for (auto i=assets.Begin<TypeC>(); i!=endC; ++i) {
			assert(i.GetState() == ::Assets::AssetState::Ready);
			std::cout << i.Id() << " (" << i.GetInitializer() << ") -- " << unsigned(i.GetState()) << std::endl;
		}

		int c=0;
		(void)c;

	}

#endif

}