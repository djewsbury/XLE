// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING	// warning generated inside of thousandeyes/futures/then.h

#include "ContinuationInternal.h"
#include "../ConsoleRig/GlobalServices.h"
#include "thousandeyes/futures/Default.h"
#include "thousandeyes/futures/Executor.h"
#include <functional>

namespace Assets
{
	template<typename... FutureTypes>
		class MultiAssetFuture
	{
	public:
		template<typename PromisedType>
			void ThenConstructToPromise(std::promise<PromisedType>&& promise)
		{
			if (_checkImmediatelyFulfilled && Internal::CanBeFulfilledImmediately(_futures, std::move(promise)))
				return;
			Internal::LogBeginWatch<PromisedType, Internal::FutureResult<FutureTypes>...>();
			MakeContinuation(
				std::move(promise),
				[](std::promise<PromisedType>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<PromisedType, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillPromise(promise, std::move(completedFutures));
				});
		}

		template<typename PromisedType, typename Fn, typename std::enable_if<!std::is_void_v<std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>>>::type* =nullptr>
			void ThenConstructToPromise(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			if (_checkImmediatelyFulfilled && Internal::CanBeFulfilledImmediately(_futures, std::move(promise), std::move(fn)))
				return;
			Internal::LogBeginWatch<PromisedType, Internal::FutureResult<FutureTypes>...>();
			using FunctionResult = std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>;
			static_assert(std::is_constructible_v<std::decay_t<PromisedType>, std::decay_t<FunctionResult>>, "Mismatch between function result and promise type");
			MakeContinuation(
				std::move(promise),
				[func=std::move(fn)](std::promise<PromisedType>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<PromisedType, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillContinuationFunction(promise, std::move(func), std::move(completedFutures));
				});
		}

		template<typename PromisedType, typename Fn, typename std::enable_if<std::is_void_v<std::invoke_result_t<Fn, std::promise<PromisedType>&&, Internal::FutureResult<FutureTypes>...>>>::type* =nullptr>
			void ThenConstructToPromise(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			if (_checkImmediatelyFulfilled && Internal::CanBeFulfilledImmediately(_futures, std::move(promise), std::move(fn)))
				return;
			Internal::LogBeginWatch<PromisedType, Internal::FutureResult<FutureTypes>...>();
			MakeContinuation(
				std::move(promise),
				[func=std::move(fn)](std::promise<PromisedType>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<PromisedType, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillContinuationFunctionPassPromise(std::move(promise), std::move(func), std::move(completedFutures));
				});
		}

		template<typename PromisedType, typename Fn, typename std::enable_if<!std::is_void_v<std::invoke_result_t<Fn, FutureTypes...>>>::type* =nullptr>
			void ThenConstructToPromiseWithFutures(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			assert(!_checkImmediatelyFulfilled);		// this variation not implemented
			Internal::LogBeginWatch<PromisedType, Internal::FutureResult<FutureTypes>...>();
			using FunctionResult = std::invoke_result_t<Fn, FutureTypes...>;
			static_assert(std::is_constructible_v<std::decay_t<PromisedType>, std::decay_t<FunctionResult>>, "Mismatch between function result and promise type");
			MakeContinuation(
				std::move(promise),
				[func=std::move(fn)](std::promise<PromisedType>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<PromisedType, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillContinuationFunctionPassFutures(promise, std::move(func), std::move(completedFutures));
				});
		}

		template<typename PromisedType, typename Fn, typename std::enable_if<std::is_void_v<std::invoke_result_t<Fn, std::promise<PromisedType>&&, FutureTypes...>>>::type* =nullptr>
			void ThenConstructToPromiseWithFutures(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			assert(!_checkImmediatelyFulfilled);		// this variation not implemented
			Internal::LogBeginWatch<PromisedType, Internal::FutureResult<FutureTypes>...>();
			MakeContinuation(
				std::move(promise),
				[func=std::move(fn)](std::promise<PromisedType>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<PromisedType, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillContinuationFunctionPassPromisePassFutures(std::move(promise), std::move(func), std::move(completedFutures));
				});
		}

		template<typename Fn>
			std::future<std::invoke_result_t<Fn, FutureTypes...>> Then(Fn&& fn)
		{
			assert(!_checkImmediatelyFulfilled);		// this variation not implemented
			using FunctionResult = std::invoke_result_t<Fn, FutureTypes...>;
			Internal::LogBeginWatch<FunctionResult, Internal::FutureResult<FutureTypes>...>();
			std::promise<FunctionResult> promise;
			auto result = promise.get_future();
			MakeContinuation(
				std::move(promise),
				[func=std::move(fn)](std::promise<FunctionResult>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<FunctionResult, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillContinuationFunctionPassFutures(promise, std::move(func), std::move(completedFutures));
				});
			return result;
		}

		std::future<void> ThenOpaqueFuture()
		{
			assert(!_checkImmediatelyFulfilled);		// this variation not implemented
			Internal::LogBeginWatch<void, Internal::FutureResult<FutureTypes>...>();
			std::promise<void> promise;
			auto result = promise.get_future();
			MakeContinuation(
				std::move(promise),
				[](std::promise<void>&& promise, std::tuple<FutureTypes...>&& completedFutures) mutable {
					Internal::LogBeginFulfillPromise<void, Internal::FutureResult<FutureTypes>...>();
					Internal::FulfillOpaquePromise(promise, std::move(completedFutures));
				});
			return result;
		}

		std::future<std::tuple<FutureTypes...>> AsCombinedFuture()
		{
			return MakeFuture();
		}

		MultiAssetFuture& CheckImmediately() { _checkImmediatelyFulfilled = true; return *this; }

		MultiAssetFuture(FutureTypes... subFutures) 
		: _futures{std::forward<FutureTypes>(subFutures)...}
		{
		}

	private:
		std::tuple<FutureTypes...> _futures;
		bool _checkImmediatelyFulfilled = false;

		std::future<std::tuple<FutureTypes...>> MakeFuture()
		{
			std::promise<std::tuple<FutureTypes...>> mergedPromise;
			auto mergedFuture = mergedPromise.get_future();

			auto* executor = ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor().get();
			if (!executor) {
				// might happen during shutdown
				mergedPromise.set_exception(std::make_exception_ptr(std::runtime_error("Continuation executor has expired")));
				return mergedFuture;
			}
			executor->watch(std::make_unique<Internal::FlexTimedWaitableSimple<FutureTypes...>>(
				std::chrono::hours(1),		// timeout for FlexTimedWaitable from now, following the patterns present in thousandeyes::futures
				std::move(_futures),
				std::move(mergedPromise)
			));

			return mergedFuture;
		}

		template<typename PromisedType, typename ContinuationFn>
			void MakeContinuation(std::promise<PromisedType>&& promise, ContinuationFn&& continuation)
		{
			auto* executor = ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor().get();
			if (!executor) {
				// might happen during shutdown
				promise.set_exception(std::make_exception_ptr(std::runtime_error("Continuation executor has expired")));
				return;
			}

			executor->watch(std::make_unique<Internal::FlexTimedWaitableWithContinuation<ContinuationFn, PromisedType, FutureTypes...>>(
				std::chrono::hours(1),		// timeout for FlexTimedWaitable from now, following the patterns present in thousandeyes::futures
				std::move(_futures),
				std::move(continuation),
				std::move(promise)
			));
		}
	};

	template<typename... FutureTypes>
		MultiAssetFuture<std::decay_t<decltype(Internal::GetContinuableFuture(std::declval<FutureTypes>()))>...> WhenAll(FutureTypes... subFutures)
	{
		Internal::CheckValidForContinuation<0, FutureTypes...>();
		return MultiAssetFuture<std::decay_t<decltype(Internal::GetContinuableFuture(std::declval<FutureTypes>()))>...>{
			Internal::GetContinuableFuture(std::forward<FutureTypes>(subFutures))...
		};
	}

}
