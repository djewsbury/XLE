// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING

#include "../UnitTestHelper.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../OSServices/Log.h"
#include <stdexcept>
#include <random>
#include <future>
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
		static void ConstructToFuture(
			::Assets::Future<AssetWithRandomConstructionTime>& future,
			std::chrono::nanoseconds constructionTime,
			Assets::AssetState finalState)
		{
			std::this_thread::sleep_for(constructionTime);
			if (finalState == ::Assets::AssetState::Ready) {
				future.SetAsset(AssetWithRandomConstructionTime{});
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

	using namespace Assets;

	namespace Internal
	{
		template<typename PromisedType>
			bool TimedWait(const std::shared_ptr<::Assets::Future<PromisedType>>& future, std::chrono::microseconds timeout)
		{
			auto stallResult = future->StallWhilePending(timeout);
			return stallResult.value_or(::Assets::AssetState::Pending) != ::Assets::AssetState::Pending;
		}
		
		static bool TimedWait(const ::Assets::IAsyncMarker& future, std::chrono::microseconds timeout)
		{
			auto stallResult = future.StallWhilePending(timeout);
			return stallResult.value_or(::Assets::AssetState::Pending) != ::Assets::AssetState::Pending;
		}

		template<typename PromisedType>
			bool TimedWait(const std::future<PromisedType>& future, std::chrono::microseconds timeout)
		{
			return future.wait_for(timeout) == std::future_status::ready;
		}

		template<typename PromisedType>
			bool TimedWait(const std::shared_future<PromisedType>& future, std::chrono::microseconds timeout)
		{
			return future.wait_for(timeout) == std::future_status::ready;
		}

		// based on thousandeyes::futures::detail::FutureWithTuple, this generates fullfills a promise
		// with our tuple of futures
		template<typename... FutureTypes>
			struct FlexTimedWaitable : public thousandeyes::futures::TimedWaitable
		{
		public:
			using TupleOfFutures = std::tuple<FutureTypes...>;
			FlexTimedWaitable(
				std::chrono::microseconds waitLimit,
				TupleOfFutures subFutures,
				std::promise<TupleOfFutures> p)
			: TimedWaitable(std::move(waitLimit)), _subFutures(std::move(subFutures)), _promise(std::move(p))
			{}

			template<std::size_t I>
				bool timedWait_(const std::chrono::microseconds& timeout)
			{
				if (!TimedWait(std::get<I>(_subFutures), timeout))
					return false;
				if constexpr(I+1 != sizeof...(FutureTypes))
					return timedWait_<I+1>(timeout);
				return true;
			}

			bool timedWait(const std::chrono::microseconds& timeout) override
			{
				return timedWait_<0>(timeout);
			}

			void dispatch(std::exception_ptr err) override
			{
				if (err) {
					_promise.set_exception(err);
					return;
				}

				TRY {
					_promise.set_value(std::move(_subFutures));
				} CATCH (...) {
					_promise.set_exception(std::current_exception());
				} CATCH_END
			}

			TupleOfFutures _subFutures;
			std::promise<TupleOfFutures> _promise;
		};

		template<typename... FutureTypes>
			std::future<std::tuple<std::decay_t<FutureTypes>...>> WhenAll3(FutureTypes... subFutures)
		{
			std::promise<std::tuple<std::decay_t<FutureTypes>...>> mergedPromise;
			auto mergedFuture = mergedPromise.get_future();
			std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();

			std::chrono::microseconds timeLimit { 0 };
			executor->watch(std::make_unique<FlexTimedWaitable<FutureTypes...>>(
				timeLimit,
				std::make_tuple(std::forward<FutureTypes>(subFutures)...),
				std::move(mergedPromise)
			));

			return mergedFuture;
		}

		template<typename T> static auto FutureResult_(int) -> decltype(std::declval<::Assets::Internal::RemoveSmartPtrType<T>>().get());
		template<typename T> static auto FutureResult_(int) -> typename std::enable_if<std::is_copy_constructible_v<typename ::Assets::Internal::RemoveSmartPtrType<T>::PromisedType>, decltype(std::declval<::Assets::Internal::RemoveSmartPtrType<T>>().ActualizeBkgrnd())>::type;
		template<typename...> static auto FutureResult_(...) -> void;

		template<typename T> using FutureResult = decltype(FutureResult_<T>(0));

		////////////////////////////////////////////////////////////////////////////////////////////////
		template<typename Payload>
			static AssetState TryQueryFuture(
				Future<Payload>& future,
				Payload& actualized,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal)
		{
			return future.CheckStatusBkgrnd(actualized, exceptionDepVal, actualizationBlob);
		}

		template<typename Payload>
			static AssetState TryQueryFuture(
				std::shared_ptr<Future<Payload>>& future,
				Payload& actualized,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal)
		{
			return future->CheckStatusBkgrnd(actualized, exceptionDepVal, actualizationBlob);
		}

		template<typename Payload>
			static AssetState TryQueryFuture(
				std::future<Payload>& future,
				Payload& actualized,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal)
		{
			AssetState state;
			::Assets::Internal::TryGetAssetFromFuture(future, state, actualized, actualizationBlob, exceptionDepVal);
			return state;
		}

		template<typename Payload>
			static AssetState TryQueryFuture(
				std::shared_future<Payload>& future,
				Payload& actualized,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal)
		{
			AssetState state;
			::Assets::Internal::TryGetAssetFromFuture(future, state, actualized, actualizationBlob, exceptionDepVal);
			return state;
		}

		template<size_t I = 0, typename... Futures>
			static void TryQueryFutures_(
				AssetState& currentState,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal,
				std::tuple<Futures...>& completedFutures,
				std::tuple<FutureResult<Futures>...>& actualized)
		{
			Blob queriedLog;
			DependencyValidation queriedDepVal;
			auto state = TryQueryFuture(std::get<I>(completedFutures), std::get<I>(actualized), queriedLog, queriedDepVal);
			if (state != AssetState::Ready)
				currentState = state;

			if (state != AssetState::Invalid) {	// (on first invalid, stop looking any further)
				if constexpr(I+1 != sizeof...(Futures))
					TryQueryFutures_<I+1>(currentState, actualizationBlob, exceptionDepVal, completedFutures, actualized);
			} else {
				std::stringstream str;
				str << "Failed to actualize subasset number (" << I << "): ";
				if (queriedLog) { str << ::Assets::AsString(queriedLog); } else { str << std::string("<<no log>>"); }
				actualizationBlob = AsBlob(str.str());
				exceptionDepVal = queriedDepVal;
			}
		}

		template<typename... Futures>
			static std::tuple<FutureResult<Futures>...> TryQueryFutures(
				AssetState& currentState,
				Blob& actualizationBlob,
				DependencyValidation& exceptionDepVal,
				std::tuple<Futures...>& completedFutures)
		{
			std::tuple<FutureResult<Futures>...> result;
			TryQueryFutures_(currentState, actualizationBlob, exceptionDepVal, completedFutures, result);
			return result;
		}

		////////////////////////////////////////////////////////////////////////////////////////////////
		template<typename Payload>
			static auto QueryFuture(Future<Payload>& future) -> decltype(future.ActualizeBkgrnd()) { return future.ActualizeBkgrnd(); }

		template<typename Payload>
			static auto QueryFuture(std::shared_ptr<Future<Payload>>& future) -> decltype(future->ActualizeBkgrnd()) { return future->ActualizeBkgrnd(); }

		template<typename Payload>
			static Payload QueryFuture(std::future<Payload>& future) { return future.get(); }

		template<typename Payload>
			static const Payload& QueryFuture(std::shared_future<Payload>& future) { return future.get(); }

		template<size_t I = 0, typename... Futures>
			static void QueryFutures(
				std::tuple<FutureResult<Futures>...>& actualized,
				std::tuple<Futures...>& completedFutures)
		{
			// note -- this won't work with futures that return references. However "QueryToTuple" will
			// This is because QueryToTuple doesn't require the default constructor for "actualized" to be called
			std::get<I>(actualized) = QueryFuture(std::get<I>(completedFutures));
			if constexpr(I+1 != sizeof...(Futures))
				QueryFutures<I+1>(actualized, completedFutures);
		}

		template<typename Tuple, std::size_t ... I>
			static std::tuple<FutureResult<std::tuple_element_t<I, Tuple>>...> QueryToTuple(Tuple& completedFutures, std::index_sequence<I...>)
		{
			return {
				QueryFuture(std::get<I>(completedFutures))...
			};
		}

		////////////////////////////////////////////////////////////////////////////////////////////////

		template<typename PromisedAssetType, typename... Futures>
			static void FulfillPromise(
				std::promise<PromisedAssetType>& promise, 
				std::tuple<Futures...>& completedFutures)
		{
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				auto finalConstruction = ::Assets::Internal::ApplyConstructFinalAssetObject<PromisedAssetType>(std::move(actualized));
				promise.set_value(std::move(finalConstruction));	
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}

		template<typename PromisedAssetType, typename... Futures>
			static void FulfillPromise(
				std::promise<PromisedAssetType>& promise, 
				std::future<std::tuple<Futures...>>& completedFutures)
		{
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				auto finalConstruction = ::Assets::Internal::ApplyConstructFinalAssetObject<PromisedAssetType>(std::move(actualized));
				promise.set_value(std::move(finalConstruction));	
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}

		template<typename Fn, typename... Futures>
			static void FulfillContinuationFunction(
				std::promise<std::invoke_result_t<Fn, FutureResult<Futures>...>>& promise, 
				Fn&& continuationFunction,
				std::tuple<Futures...>& completedFutures)
		{
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				auto finalResult = std::apply(std::move(continuationFunction), std::move(actualized));
				promise.set_value(std::move(finalResult));
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}

		template<typename Fn, typename... Futures>
			static void FulfillContinuationFunction(
				std::promise<std::invoke_result_t<Fn, FutureResult<Futures>...>>& promise, 
				Fn&& continuationFunction,
				std::future<std::tuple<Futures...>>& completedFutures)
		{
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				auto finalResult = std::apply(std::move(continuationFunction), std::move(actualized));
				promise.set_value(std::move(finalResult));
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}

		template<typename... Futures>
			static void FulfillOpaquePromise(
				std::promise<void>& promise, 
				std::future<std::tuple<Futures...>>& completedFutures)
		{
			// We must query the futures just to see if there's an exception within them
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				(void)actualized;
				promise.set_value();
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}

		template<int I, typename... FutureTypes>
			static void CheckValidForContinuation()
		{
			using Test = std::tuple_element_t<I, std::tuple<FutureTypes...>>;
			static_assert(!std::is_void_v<FutureResult<Test>>, "The given future type can't be used with continuation functions. This can happen when using Asset::Future<> with a non-copyable object");
			if constexpr ((I+1) != sizeof...(FutureTypes))
				CheckValidForContinuation<I+1, FutureTypes...>();
		}

		template<typename Type> static std::shared_future<Type> GetContinuableFuture(const ::Assets::Future<Type>& input) { return input.ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(const std::shared_ptr<::Assets::Future<Type>>& input) { return input->ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(::Assets::Future<Type>&& input) { return input.ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(std::shared_ptr<::Assets::Future<Type>>&& input) { return input->ShareFuture(); }

		template<typename Type> static const Type& GetContinuableFuture(const Type& input) { return input; }
		template<typename Type> static Type&& GetContinuableFuture(Type&& input) { return std::move(input); }
	}

	template<typename... FutureTypes>
		class MultiAssetFuture4 : public std::future<std::tuple<FutureTypes...>>
	{
	public:
		template<typename FinalPromiseType>
			void ThenConstructToPromise(FinalPromiseType&& promise)
		{
			thousandeyes::futures::then(
				std::move(*this),
				[promise=std::move(promise)](std::future<std::tuple<FutureTypes...>>&& completedFutures) mutable {
					Internal::FulfillPromise(promise, completedFutures);
				});
		}

		template<typename PromisedType, typename Fn>
			void ThenConstructToPromise(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			using FunctionResult = std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>;
			static_assert(std::is_same_v<std::decay_t<PromisedType>, std::decay_t<FunctionResult>>, "Mismatch between function result and promise type");
			thousandeyes::futures::then(
				std::move(*this),
				[promise=std::move(promise), func=std::move(fn)](std::future<std::tuple<FutureTypes...>>&& completedFutures) mutable {
					Internal::FulfillContinuationFunction(promise, std::move(func), completedFutures);
				});
		}

		template<typename Fn>
			std::future<std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>> Then(Fn&& continuationFunction)
		{
			using FunctionResult = std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>;
			std::promise<FunctionResult> promise;
			auto result = promise.get_future();
			thousandeyes::futures::then(
				std::move(*this),
				[promise=std::move(promise), func=std::move(continuationFunction)](std::future<std::tuple<FutureTypes...>>&& completedFutures) mutable {
					Internal::FulfillContinuationFunction(promise, std::move(func), completedFutures);
				});
			return result;
		}

		std::future<void> ThenOpaqueFuture()
		{
			std::promise<void> promise;
			auto result = promise.get_future();
			thousandeyes::futures::then(
				std::move(*this),
				[promise=std::move(promise)](std::future<std::tuple<FutureTypes...>>&& completedFutures) mutable {
					Internal::FulfillOpaquePromise(promise, completedFutures);
				});
			return result;
		}

		MultiAssetFuture4(FutureTypes... subFutures) 
		: std::future<std::tuple<FutureTypes...>>{MakeFuture(std::forward<FutureTypes>(subFutures)...)}
		{
		}

		static std::future<std::tuple<FutureTypes...>> MakeFuture(FutureTypes... subFutures)
		{
			std::promise<std::tuple<FutureTypes...>> mergedPromise;
			auto mergedFuture = mergedPromise.get_future();

			std::chrono::microseconds timeLimit { 0 };
			std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();
			executor->watch(std::make_unique<Internal::FlexTimedWaitable<FutureTypes...>>(
				timeLimit,
				std::make_tuple(std::forward<FutureTypes>(subFutures)...),
				std::move(mergedPromise)
			));

			return mergedFuture;
		}		
	};

	template<typename... FutureTypes>
		MultiAssetFuture4<std::decay_t<decltype(Internal::GetContinuableFuture(std::declval<FutureTypes>()))>...> WhenAll4(FutureTypes... subFutures)
	{
		Internal::CheckValidForContinuation<0, FutureTypes...>();
		return MultiAssetFuture4<std::decay_t<decltype(Internal::GetContinuableFuture(std::declval<FutureTypes>()))>...>{
			Internal::GetContinuableFuture(std::forward<FutureTypes>(subFutures))...
		};
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
			WhenAll4(futureZero, futureOne, futureTwo),
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

		WhenAll4(futureZero, futureOne, futureTwo).ThenConstructToPromise(finalFuture.AdoptPromise());
		finalFuture.StallWhilePending();
		REQUIRE(finalFuture.Actualize()._result == 3);

		::Assets::Future<unsigned> finalFuture2;
		WhenAll4(futureZero, futureOne, futureTwo).ThenConstructToPromise(
			finalFuture2.AdoptPromise(),
			[](auto one, auto zero, auto three) { return one+zero+three; });
		finalFuture2.StallWhilePending();
		REQUIRE(finalFuture2.Actualize() == 3);

		auto continuation = WhenAll4(futureZero, futureOne, futureTwo).Then(
			[](auto zero, auto one, auto two) { return zero+one+two; });
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

		auto continuation2 = WhenAll4(futureZero, futureOne, futureTwo, std::move(basicFuture), basicFuture2, std::move(futureThree)).Then(
			[](auto zero, auto one, auto two, auto three, auto four, auto five) {
				return zero+one+two+three+four+five; 
			});
		continuation2.wait();
		REQUIRE(continuation2.get() == 15);

		// Moving Asset::Future after registering a continuation 
		::Assets::Future<unsigned> futureFour;
		auto continuation3 = WhenAll4(futureFour.ShareFuture()).ThenOpaqueFuture();
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

		static_assert(std::is_same_v<Internal::FutureResult<Future<unsigned>>, const unsigned&>);
		static_assert(std::is_same_v<Internal::FutureResult<std::future<AssetTypeOne>>, AssetTypeOne>);
		static_assert(std::is_same_v<Internal::FutureResult<std::shared_future<AssetTypeOne>>, const AssetTypeOne&>);

		auto successfulChain = WhenAll4(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::SuccessfulStdFuture(" one"))
			.Then([](auto zero, auto one) { return zero._value + one._value; });
		successfulChain.wait();
		REQUIRE(successfulChain.get() == "zero one");

		auto failedChain = WhenAll4(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedAssetFuture(::Assets::AsBlob("Failed asset")))
			.Then([](auto zero, auto one) { return zero._value + one._value; });
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

		auto failedChain2 = WhenAll4(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedStdFuture(std::make_exception_ptr(std::runtime_error("runtime_error"))))
			.Then([](auto zero, auto one) { return zero._value + one._value; });
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
		WhenAll4(
			AssetTypeOne::SuccessfulAssetFuture("zero"), 
			AssetTypeOne::FailedStdFuture(std::make_exception_ptr(std::runtime_error("runtime_error"))))
			.ThenConstructToPromise(failedChain3.AdoptPromise(), [](auto zero, auto one) { return zero._value + one._value; });
		failedChain3.StallWhilePending();
		REQUIRE(failedChain3.GetAssetState() == AssetState::Invalid);
		REQUIRE(AsString(failedChain3.GetActualizationLog()) == "runtime_error");
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

