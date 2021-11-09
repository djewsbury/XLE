// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../UnitTestHelper.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/MemoryFile.h"
#include "../../Assets/Assets.h"
#include "../../Assets/MountingTree.h"
#include "../../OSServices/Log.h"
#include <stdexcept>
#include <random>
#include <future>
#include <unordered_map>
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include "thousandeyes/futures/Executor.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class AssetWithRandomConstructionTime
	{
	public:
		static void ConstructToPromise(
			std::promise<AssetWithRandomConstructionTime>&& promise,
			std::chrono::nanoseconds constructionTime,
			Assets::AssetState finalState)
		{
			std::this_thread::sleep_for(constructionTime);
			if (finalState == ::Assets::AssetState::Ready) {
				promise.set_value(AssetWithRandomConstructionTime{});
			} else {
				promise.set_exception(std::make_exception_ptr(std::runtime_error("Invalid AssetWithRandomConstructionTime")));
			}
		}
	};

	TEST_CASE( "AssetSetManager-ThrashFutures", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

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
					::Assets::WhenAll(futuresInFlight[0], futuresInFlight[1]).ThenConstructToPromise(
						future->AdoptPromise(),
						[invalid, duration](auto zero, auto one) {
							std::this_thread::sleep_for(duration);
							if (invalid)
								Throw(std::runtime_error("Emulating construction error in invalid asset"));
							return AssetWithRandomConstructionTime{}; 
						});
				} else if (futuresInFlight.size() >= 2 && std::uniform_int_distribution<>(0, 10)(rng) == 0) {
					::Assets::WhenAll(futuresInFlight[0], futuresInFlight[1]).ThenConstructToPromise(
						future->AdoptPromise(),
						[globalServices=globalServices.get(), duration, invalid](std::promise<AssetWithRandomConstructionTime>&& promiseToFulfill, auto zero, auto one) { 
							globalServices->GetLongTaskThreadPool().Enqueue(
								[promise=std::move(promiseToFulfill), duration, invalid]() mutable {
									AssetWithRandomConstructionTime::ConstructToPromise(
										std::move(promise),
										duration,
										invalid ? ::Assets::AssetState::Invalid : ::Assets::AssetState::Ready);
								});
						});
				} else {
					globalServices->GetLongTaskThreadPool().Enqueue(
						[future, duration, invalid]() {
							auto promise = future->AdoptPromise();
							AssetWithRandomConstructionTime::ConstructToPromise(
								std::move(promise),
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
			bkThread.Enqueue(
				[newAssets, &futuresInFlight, &futuresInFlight2, &rng, &bkCounter]() {
					TRY {
						for (unsigned c=0; c<newAssets; ++c) {
							assert(futuresInFlight.size() > 2);
							auto invalid = std::uniform_int_distribution<>(0, 1)(rng) == 0;
							TestFuture future;
							auto duration = std::chrono::nanoseconds((int)std::uniform_real_distribution<float>(0.f, 10*1000.f)(rng));
							auto zero = std::uniform_int_distribution<>(0, futuresInFlight.size()-1)(rng);
							auto one = std::uniform_int_distribution<>(0, futuresInFlight.size()-1)(rng);
							::Assets::WhenAll(futuresInFlight[zero], futuresInFlight[one]).ThenConstructToPromise(
								future.AdoptPromise(),
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

	TEST_CASE( "AssetFuture-Continuation", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

		auto futureZero = std::make_shared<::Assets::Future<unsigned>>();
		auto futureOne = std::make_shared<::Assets::Future<unsigned>>();
		auto futureTwo = std::make_shared<::Assets::Future<unsigned>>();
		
		futureZero->SetAsset(0);
		futureOne->SetAsset(1);
		futureTwo->SetAsset(2);

		std::atomic<bool> test = false;
		thousandeyes::futures::then(
			::Assets::WhenAll(futureZero, futureOne, futureTwo),
			[&test](auto futureTuple) {

				auto tuple = futureTuple.get();

				auto resultZero = std::get<0>(tuple).get();
				auto resultOne = std::get<1>(tuple).get();
				auto resultTwo = std::get<2>(tuple).get();

				int c=0;
				(void)c;
				test = true;
			});

		while (!test) { std::this_thread::sleep_for(0s); }

		struct TripleConstructor
		{
			unsigned _result = 26394629;
			TripleConstructor(unsigned zero, unsigned one, unsigned two)
			:_result(zero+one+two)
			{
				int c=0;
				(void)c;
			}
			TripleConstructor() {}
			TripleConstructor(TripleConstructor&&) = default;
			TripleConstructor& operator=(TripleConstructor&&) = default;
		};
		::Assets::Future<TripleConstructor> finalFuture;

		::Assets::WhenAll(futureZero, futureOne, futureTwo).ThenConstructToPromise(finalFuture.AdoptPromise());
		finalFuture.StallWhilePending();
		REQUIRE(finalFuture.Actualize()._result == 3);

		::Assets::Future<unsigned> finalFuture2;
		::Assets::WhenAll(futureZero, futureOne, futureTwo).ThenConstructToPromise(
			finalFuture2.AdoptPromise(),
			[](auto one, auto zero, auto three) { return one+zero+three; });
		finalFuture2.StallWhilePending();
		REQUIRE(finalFuture2.Actualize() == 3);

		auto continuation = ::Assets::WhenAll(futureZero, futureOne, futureTwo).Then(
			[](auto zero, auto one, auto two) { return zero.get()+one.get()+two.get(); });
		continuation.wait();
		REQUIRE(continuation.get() == 3);

		std::promise<unsigned> basicPromise;
		auto basicFuture = basicPromise.get_future();
		basicPromise.set_value(3);

		std::promise<unsigned> basicPromise2;
		auto basicFuture2 = basicPromise2.get_future().share();
		basicPromise2.set_value(4);

		::Assets::Future<unsigned> futureThree;
		futureThree.SetAsset(5);

		auto continuation2 = ::Assets::WhenAll(futureZero, futureOne, futureTwo, std::move(basicFuture), basicFuture2, std::move(futureThree)).Then(
			[](auto zero, auto one, auto two, auto three, auto four, auto five) {
				return zero.get()+one.get()+two.get()+three.get()+four.get()+five.get(); 
			});
		continuation2.wait();
		REQUIRE(continuation2.get() == 15);

		// Moving Asset::Future after registering a continuation 
		::Assets::Future<unsigned> futureFour;
		auto continuation3 = ::Assets::WhenAll(futureFour.ShareFuture()).ThenOpaqueFuture();
		::Assets::Future<unsigned> movedFutureFour = std::move(futureFour);
		movedFutureFour.SetAsset(4);
		continuation3.wait();
		continuation3.get();
	}

	TEST_CASE( "AssetFuture-ContinuationException", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

		struct AssetTypeOne
		{
			std::string _value;
			AssetTypeOne() {}
			AssetTypeOne(std::string v) : _value(v) {}
			/*AssetTypeOne(AssetTypeOne&&) = default;
			AssetTypeOne& operator=(AssetTypeOne&&) = default;
			AssetTypeOne(const AssetTypeOne&) = delete;
			AssetTypeOne& operator=(const AssetTypeOne&) = delete;*/

			static ::Assets::Future<AssetTypeOne> SuccessfulAssetFuture(std::string v)
			{
				::Assets::Future<AssetTypeOne> result;
				result.SetAsset(v);
				return result;
			}

			static ::Assets::Future<AssetTypeOne> FailedAssetFuture(::Assets::Blob blob)
			{
				::Assets::Future<AssetTypeOne> result;
				result.SetInvalidAsset({}, blob);
				return result;
			}

			static std::future<AssetTypeOne> SuccessfulStdFuture(std::string v)
			{
				std::promise<AssetTypeOne> promise;
				promise.set_value(v);
				return promise.get_future();
			}

			static std::future<AssetTypeOne> FailedStdFuture(std::exception_ptr eptr)
			{
				std::promise<AssetTypeOne> promise;
				promise.set_exception(std::move(eptr));
				return promise.get_future();
			}
		};

		static_assert(std::is_same_v<::Assets::Internal::FutureResult<::Assets::Future<unsigned>>, const unsigned&>);
		static_assert(std::is_same_v<::Assets::Internal::FutureResult<std::future<AssetTypeOne>>, AssetTypeOne>);
		static_assert(std::is_same_v<::Assets::Internal::FutureResult<std::shared_future<AssetTypeOne>>, const AssetTypeOne&>);

		auto successfulChain = ::Assets::WhenAll(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::SuccessfulStdFuture(" one"))
			.Then([](auto zero, auto one) { return zero.get()._value + one.get()._value; });
		successfulChain.wait();
		REQUIRE(successfulChain.get() == "zero one");

		auto successfulChainVoidReturn = ::Assets::WhenAll(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::SuccessfulStdFuture(" one"))
			.Then([](auto zero, auto one) { zero.get(); one.get(); });
		successfulChainVoidReturn.wait();

		auto failedChain = ::Assets::WhenAll(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedAssetFuture(::Assets::AsBlob("Failed asset")))
			.Then([](auto zero, auto one) { return zero.get()._value + one.get()._value; });
		failedChain.wait();
		{
			::Assets::AssetState state;
			::Assets::Blob blob;
			::Assets::DependencyValidation depVal;
			std::string actualized;
			::Assets::Internal::TryGetAssetFromFuture(failedChain, state, actualized, blob, depVal);
			REQUIRE(state == ::Assets::AssetState::Invalid);
			REQUIRE(::Assets::AsString(blob) == "Failed asset");
		}

		auto failedChain2 = ::Assets::WhenAll(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedStdFuture(std::make_exception_ptr(std::runtime_error("runtime_error"))))
			.Then([](auto zero, auto one) { return zero.get()._value + one.get()._value; });
		failedChain2.wait();
		{
			::Assets::AssetState state;
			::Assets::Blob blob;
			::Assets::DependencyValidation depVal;
			std::string actualized;
			::Assets::Internal::TryGetAssetFromFuture(failedChain2, state, actualized, blob, depVal);
			REQUIRE(state == ::Assets::AssetState::Invalid);
			REQUIRE(::Assets::AsString(blob) == "runtime_error");
		}

		::Assets::Future<std::string> failedChain3;
		::Assets::WhenAll(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedStdFuture(std::make_exception_ptr(std::runtime_error("runtime_error"))))
			.ThenConstructToPromise(failedChain3.AdoptPromise(), [](auto zero, auto one) { return zero._value + one._value; });
		failedChain3.StallWhilePending();
		REQUIRE(failedChain3.GetAssetState() == ::Assets::AssetState::Invalid);
		REQUIRE(::Assets::AsString(failedChain3.GetActualizationLog()) == "runtime_error");
	}

	class ExampleAsset
	{
	public:
		std::string _contents;
		static void ConstructToPromise(
			std::promise<std::shared_ptr<ExampleAsset>>&& promise,
			StringSection<> filename)
		{
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[promise=std::move(promise), fn=filename.AsString()]() mutable {
					TRY {
						auto file = ::Assets::MainFileSystem::OpenFileInterface(fn, "rb");
						auto size = file->GetSize();
						std::string contents;
						contents.resize(size);
						file->Read(contents.data(), size);
						auto res = std::make_shared<ExampleAsset>();
						res->_contents = std::move(contents);
						promise.set_value(res);
					} CATCH(...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}
	};

	TEST_CASE( "Assets-ConstructToPromise", "[assets]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

		std::string fileContents = "This is the contents of the file";
		static std::unordered_map<std::string, ::Assets::Blob> s_utData {
			std::make_pair("file.dat", ::Assets::AsBlob(fileContents))
		};
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));

		auto futureAsset = ::Assets::MakeAsset<ExampleAsset>("ut-data/file.dat");
		futureAsset->StallWhilePending();
		auto actualAsset = futureAsset->Actualize();
		REQUIRE(actualAsset->_contents == fileContents);

		// Expecting a failure for this one -- we should get InvalidAsset with something useful in the actualization log 
		auto failedAsset = ::Assets::MakeAsset<ExampleAsset>("ut-data/no-file.data");
		failedAsset->StallWhilePending();
		REQUIRE(failedAsset->GetAssetState() == ::Assets::AssetState::Invalid);
		auto log = ::Assets::AsString(failedAsset->GetActualizationLog());
		REQUIRE(!log.empty());
		Log(Debug) << "Failed MakeAsset<> reported: " << log << std::endl;
	}

	TEST_CASE( "General-StandardFutures", "[assets]" )
	{
		// Testing some of the edge cases and wierder ways we're using std::future<>,
		// to help with compatibility testing

		struct PromisedType
		{
			std::shared_ptr<void> _asset;
			::Assets::Blob _actualizationLog;
		};
		std::promise<PromisedType> promise;
		auto future = promise.get_future();

		promise.set_value(PromisedType{});
		REQUIRE(future.wait_for(0s) == std::future_status::ready);
		auto gotValue = future.get();

		// we can't query or wait for the future after query
		REQUIRE_THROWS([&]() {future.wait_for(0s);}());
		REQUIRE_THROWS([&]() {future.get();}());

		// we can share after query, but we end up with a useless shared_future<>
		auto sharedAfterQuery = future.share();
		REQUIRE_THROWS([&]() {sharedAfterQuery.wait_for(0s);}());
		REQUIRE_THROWS([&]() {sharedAfterQuery.get();}());

		// we can't get a second future from promise
		// and we can't call set_value() a second time
		REQUIRE_THROWS([&]() {(void)promise.get_future();}());
		REQUIRE_THROWS([&]() {promise.set_value(PromisedType{});}());

		// however we can reset and reuse the same promise
		promise = {};
		promise.set_value(PromisedType{});

		// get future from promise after fullfilling it
		auto secondFuture = promise.get_future();
		REQUIRE(secondFuture.wait_for(0s) == std::future_status::ready);
		gotValue = secondFuture.get();

		// shared future hyjinks
		std::promise<PromisedType> promiseForSharedFuture;
		promiseForSharedFuture.set_value(PromisedType{});
		auto sharedFuture = promiseForSharedFuture.get_future().share();
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		// waiting for and calling get() on a shared_future is valid even after the
		// first query
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		std::shared_future<PromisedType> secondSharedFuture = sharedFuture;
		REQUIRE(secondSharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = secondSharedFuture.get();

		// copy constructor off the copied future
		std::shared_future<PromisedType> thirdSharedFuture = secondSharedFuture;
		REQUIRE(thirdSharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = thirdSharedFuture.get();

		// waiting for the original shared future still valid
		REQUIRE(sharedFuture.wait_for(0s) == std::future_status::ready);
		gotValue = sharedFuture.get();

		// does a promise loose contact with it's futures after it's moved?
		promise = {};
		auto futureToExplode = promise.get_future();
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::timeout);
		std::promise<PromisedType> moveDstPromise = std::move(promise);

		// we can't use a promise that was just used as a move src
		REQUIRE_THROWS([&]() {promise.set_value(PromisedType{});}());
		
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::timeout);
		moveDstPromise.set_value(PromisedType{});
		REQUIRE(futureToExplode.wait_for(0s) == std::future_status::ready);

		// Internally std::promise<> holds a pointer to another object. In the VS libraries, it's called _Associated_state
		// This contains a mutex and condition variable. The promised type is stored within the same heap block
		// calling wait_for() always invokes a mutex lock/unlock and std::condition_variable::wait_for combo 
	}

}

