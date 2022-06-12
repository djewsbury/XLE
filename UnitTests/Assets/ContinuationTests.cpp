// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/ContinuationExecutor.h"
#include "../../ConsoleRig/GlobalServices.h"
#include <stdexcept>
#include <chrono>
#include <random>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <iostream>

using namespace Catch::literals;
using namespace std::literals;
namespace UnitTests
{
	class FirstOrderPromises
	{
	public:
		std::future<unsigned> CreatePromise(std::chrono::steady_clock::time_point triggerTime)
		{
			std::promise<unsigned> newPromise;
			auto result = newPromise.get_future();
			ScopedLock(_lock);
			// if we happen to be inserting in the front of the queue, then it's not guaranteed that the promise will be fullfilled at the correct time
			_promises.insert(
				LowerBound(_promises, triggerTime),
				std::make_pair(triggerTime, std::move(newPromise)));
			return result;
		}

		std::optional<std::chrono::steady_clock::time_point> LastScheduledPromise()
		{
			ScopedLock(_lock);
			if (_promises.empty()) return {};
			return (_promises.end()-1)->first;
		}

		FirstOrderPromises()
		{
			_promises.reserve(4196);

			_bkThread = std::thread{[this]() {
				while (!this->_stop.load()) {
					std::unique_lock<Threading::Mutex> lock{_lock};
					if (_promises.empty()) {
						lock = {};
						std::this_thread::sleep_for(500us);
						continue;
					}
					auto sleepUntil = _promises.begin()->first;
					lock = {};
					std::this_thread::sleep_until(sleepUntil);
					lock = std::unique_lock<Threading::Mutex>{_lock};
					auto now = std::chrono::steady_clock::now();
					_promises.begin()->second.set_value(0);
					_promises.erase(_promises.begin());
					// finish any others that happen to be complete now
					while (!_promises.empty() && _promises.begin()->first <= now) {
						_promises.begin()->second.set_value(0);
						_promises.erase(_promises.begin());
					}
				}
			}};
		}

		~FirstOrderPromises()
		{
			_stop = true;
			_bkThread.join();
		}

		Threading::Mutex _lock;
		std::vector<std::pair<std::chrono::steady_clock::time_point, std::promise<unsigned>>> _promises;
		std::thread _bkThread;
		std::atomic<bool> _stop;
	};

	static std::shared_future<unsigned> SelectFuture(std::vector<std::shared_future<unsigned>>& allFutures, std::mt19937_64& rng)
	{
		// always pick from one of the last 500 futures
		unsigned min = allFutures.size() - std::min((unsigned)allFutures.size(), 500u);
		return allFutures[std::uniform_int_distribution<>(min, allFutures.size()-1)(rng)];
	}

	TEST_CASE( "Continuation-ThrashTest", "[assets]" )
	{
		// Let's create a scenario with a large network of continuation futures, with most futures waiting on 
		// other futures
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>();
		std::mt19937_64 rng{785129462};
		FirstOrderPromises firstOrderPromises;
		const unsigned targetFutureCount = 3000;
		std::vector<std::shared_future<unsigned>> allFutures;
		allFutures.reserve(targetFutureCount);

		auto queuingTimeStart = std::chrono::steady_clock::now();
		for (unsigned c=0; c<targetFutureCount; ++c) {
			if ((c%5)==0) {
				// first order promise
				auto duration = std::chrono::microseconds(std::uniform_int_distribution<>(5000, 8000)(rng));
				allFutures.emplace_back(firstOrderPromises.CreatePromise(std::chrono::steady_clock::now() + duration));
			} else {
				// higher order promise
				unsigned childCount = std::uniform_int_distribution<>(1, 5)(rng);
				childCount = std::min(childCount, (unsigned)allFutures.size());
				std::promise<unsigned> newPromise;
				auto newFuture = newPromise.get_future();
				if (childCount == 1) {
					::Assets::WhenAll(SelectFuture(allFutures, rng)).ThenConstructToPromise(
						std::move(newPromise),
						[](auto zero) { return 0; });
				} else if (childCount == 2) {
					::Assets::WhenAll(
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng))
						.ThenConstructToPromise(std::move(newPromise), [](auto zero, auto one) { return 0; });
				} else if (childCount == 3) {
					::Assets::WhenAll(
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng))
						.ThenConstructToPromise(std::move(newPromise), [](auto zero, auto one, auto two) { return 0; });
				} else if (childCount == 4) {
					::Assets::WhenAll(
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng))
						.ThenConstructToPromise(std::move(newPromise), [](auto zero, auto one, auto two, auto three) { return 0; });
				} else if (childCount == 5) {
					::Assets::WhenAll(
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng),
							SelectFuture(allFutures, rng))
						.ThenConstructToPromise(std::move(newPromise), [](auto zero, auto one, auto two, auto three, auto four) { return 0; });
				} else {
					assert(0);
				}
				allFutures.emplace_back(std::move(newFuture));
			}

			if ((c % 64) == 0) std::this_thread::sleep_for(100us);
		}

		auto lastScheduled = firstOrderPromises.LastScheduledPromise();
		auto now = std::chrono::steady_clock::now();
		std::cout << "Beginning wait for futures. Took " << std::chrono::duration_cast<std::chrono::milliseconds>(now-queuingTimeStart).count() << " milliseconds to queue futures" << std::endl;
		if (lastScheduled) std::cout << "Final first order promise will trigger in: " << std::chrono::duration_cast<std::chrono::milliseconds>(lastScheduled.value()-now).count() << " milliseconds" << std::endl;
		else std::cout << "All first order promises already triggered" << std::endl;
		for (auto& f:allFutures) f.get();
		now = std::chrono::steady_clock::now();
		if (lastScheduled)
			std::cout << "Final future completed " << std::chrono::duration_cast<std::chrono::milliseconds>(now-lastScheduled.value()).count() << " milliseconds after final first order promise" << std::endl;
	}
}
