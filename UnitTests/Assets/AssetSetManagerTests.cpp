// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../OSServices/Log.h"
#include <stdexcept>
#include <random>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class AssetWithRandomConstructionTime
	{
	public:
		static void ConstructToFuture(
			::Assets::Future<AssetWithRandomConstructionTime>& future,
			std::chrono::nanoseconds constructionTime,
			Assets::AssetState finalState)
		{
			std::this_thread::sleep_for(constructionTime);
			if (finalState == ::Assets::AssetState::Ready) {
				future.SetAsset(AssetWithRandomConstructionTime{}, {});
			} else {
				future.SetInvalidAsset({}, {});
			}
		}
	};

	TEST_CASE( "AssetSetManager-ThrashFutures", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());

		const int targetAssetsInFlight = 32;
		int assetsCompleted = 0;
		int targetAssetsCompleted = 10 * 1000;
		using TestFuture = ::Assets::Future<AssetWithRandomConstructionTime>;
		std::vector<std::shared_ptr<TestFuture>> futuresInFlight;
		std::vector<TestFuture> futuresInFlight2;

		std::mt19937_64 rng(6294529472);

		unsigned notCompletedImmediately = 0;
		unsigned assetsAbandoned = 0;

		ThreadPool bkThread(1);
		std::atomic<unsigned> bkCounter(0);

		while (assetsCompleted<targetAssetsCompleted) {
			unsigned newAssets = std::max(targetAssetsInFlight - (int)futuresInFlight.size(), 0);
			for (unsigned c=0; c<newAssets; ++c) {
				auto invalid = std::uniform_int_distribution<>(0, 1)(rng) == 0;
				auto future = std::make_shared<TestFuture>();
				auto duration = std::chrono::nanoseconds((int)std::uniform_real_distribution<float>(0.f, 10*1000.f)(rng));
				if (futuresInFlight.size() >= 2 && std::uniform_int_distribution<>(0, 10)(rng) == 0) {
					::Assets::WhenAll(futuresInFlight[0], futuresInFlight[1]).ThenConstructToFuture(
						*future,
						[invalid, duration](auto zero, auto one) {
							std::this_thread::sleep_for(duration);
							if (invalid)
								Throw(std::runtime_error("Emulating construction error in invalid asset"));
							return AssetWithRandomConstructionTime{}; 
						});
				} else if (futuresInFlight.size() >= 2 && std::uniform_int_distribution<>(0, 10)(rng) == 0) {
					::Assets::WhenAll(futuresInFlight[0], futuresInFlight[1]).ThenConstructToFuture(
						*future,
						[weakFuture=std::weak_ptr<TestFuture>{future}, globalServices=globalServices.get(), duration, invalid](::Assets::Future<AssetWithRandomConstructionTime>& resultFuture, auto zero, auto one) { 
							REQUIRE(&resultFuture == weakFuture.lock().get());
							globalServices->GetLongTaskThreadPool().Enqueue(
								[weakFuture, duration, invalid]() {
									auto l = weakFuture.lock();
									if (!l) return;
									AssetWithRandomConstructionTime::ConstructToFuture(
										*l,
										duration,
										invalid ? ::Assets::AssetState::Invalid : ::Assets::AssetState::Ready);
								});
						});
				} else {
					globalServices->GetLongTaskThreadPool().Enqueue(
						[future, duration, invalid]() {
							AssetWithRandomConstructionTime::ConstructToFuture(
								*future, 
								duration, 
								invalid ? ::Assets::AssetState::Invalid : ::Assets::AssetState::Ready);
						});
				}
				futuresInFlight.push_back(future);
			}

			// similarly queue some assets using Futures as value (ie, not using a shared_ptr<> to the Future)
			// Also create and configure 
			newAssets = std::max(targetAssetsInFlight - (int)futuresInFlight2.size(), 0);
			auto bkCounterInitial = bkCounter.load();
			bkThread.EnqueueBasic(
				[newAssets, &futuresInFlight, &futuresInFlight2, &rng, &bkCounter]() {
					TRY {
						for (unsigned c=0; c<newAssets; ++c) {
							assert(futuresInFlight.size() > 2);
							auto invalid = std::uniform_int_distribution<>(0, 1)(rng) == 0;
							TestFuture future;
							auto duration = std::chrono::nanoseconds((int)std::uniform_real_distribution<float>(0.f, 10*1000.f)(rng));
							auto zero = std::uniform_int_distribution<>(0, futuresInFlight.size()-1)(rng);
							auto one = std::uniform_int_distribution<>(0, futuresInFlight.size()-1)(rng);
							::Assets::WhenAll(futuresInFlight[zero], futuresInFlight[one]).ThenConstructToFuture(
								future,
								[invalid, duration](auto zero, auto one) {
									std::this_thread::sleep_for(duration);
									if (invalid)
										Throw(std::runtime_error("Emulating construction error in invalid asset"));
									return AssetWithRandomConstructionTime{}; 
								});
							futuresInFlight2.push_back(std::move(future));
						}
						std::shuffle(futuresInFlight2.begin(), futuresInFlight2.end(), rng);		// thrash move operator
						++bkCounter;
					} CATCH (const std::exception& e) {
						FAIL(std::string{"Future shuffling failed with exception: "} + e.what());
					} CATCH_END
				});

			std::this_thread::sleep_for(500ns);
			::Assets::Services::GetAssetSets().OnFrameBarrier();
			bkThread.StallAndDrainQueue();	// ensure queued fn is completed before continuing
			REQUIRE(bkCounter.load() == (bkCounterInitial+1));
			if (!futuresInFlight.empty() && std::uniform_int_distribution<>(0, 50)(rng) == 0) {
				auto futureToStallFor = std::uniform_int_distribution<>(0, futuresInFlight.size()-1)(rng);
				futuresInFlight[futureToStallFor]->StallWhilePending();
			}
			for (auto i=futuresInFlight.begin(); i!=futuresInFlight.end();) {
				if ((*i)->GetAssetState() != ::Assets::AssetState::Pending) {
					i = futuresInFlight.erase(i);
					++assetsCompleted;
				} else {
					if (std::uniform_int_distribution<>(0, 100)(rng) == 0) {
						i = futuresInFlight.erase(i);
						++assetsCompleted;
						++assetsAbandoned;
					} else {
						++i;
						++notCompletedImmediately;
					}
				}
			}
			for (auto i=futuresInFlight2.begin(); i!=futuresInFlight2.end();) {
				if (i->GetAssetState() != ::Assets::AssetState::Pending) {
					i = futuresInFlight2.erase(i);
					++assetsCompleted;
				} else {
					if (std::uniform_int_distribution<>(0, 40)(rng) == 0) {
						i = futuresInFlight2.erase(i);
						++assetsCompleted;
						++assetsAbandoned;
					} else {
						++i;
						++notCompletedImmediately;
					}
				}
			}
		}

		Log(Debug) << "Not completed immediately: " << notCompletedImmediately << std::endl;
		Log(Debug) << "Abandoned: " << assetsAbandoned << std::endl;
	}
}

