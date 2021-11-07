// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING	// warning generated inside of thousandeyes/futures/then.h

#include "AssetFuture.h"
#include "AssetUtils.h"
#include "AssetTraits.h"		// just for ConstructFinalAssetObject
#include "../Utility/Threading/Mutex.h"
#include "thousandeyes/futures/then.h"
#include <tuple>
#include <utility>

namespace Assets
{
	namespace Internal
	{
		template<size_t I = 0, typename... Tp>
			void CheckAssetState(
				AssetState& currentState, 
				Blob& actualizationBlob, 
				DependencyValidation& exceptionDepVal,
				std::tuple<Tp...>& actualized,
				const std::tuple<std::shared_ptr<Future<Tp>>...>& futures)
		{
			Blob queriedLog;
			DependencyValidation queriedDepVal;
			auto state = std::get<I>(futures)->CheckStatusBkgrnd(std::get<I>(actualized), queriedDepVal, queriedLog);
			if (state != AssetState::Ready)
				currentState = state;

			if (state != AssetState::Invalid) {	// (on first invalid, stop looking any further)
				if constexpr(I+1 != sizeof...(Tp))
					CheckAssetState<I+1>(currentState, actualizationBlob, exceptionDepVal, actualized, futures);
			} else {
				std::stringstream str;
				str << "Failed to actualize subasset number (" << I << "): ";
				if (queriedLog) { str << AsString(queriedLog); } else { str << std::string("<<no log>>"); }
				actualizationBlob = AsBlob(str.str());
				exceptionDepVal = queriedDepVal;
			}
		}

		template<size_t I = 0, typename... Tp>
			bool AnyForegroundPendingAssets(const std::tuple<std::shared_ptr<Future<Tp>>...>& futures)
		{
			if (std::get<I>(futures)->GetAssetState() == AssetState::Pending)
				return true;
			if constexpr(I+1 != sizeof...(Tp))
				return AnyForegroundPendingAssets<I+1>(futures);
			return false;
		}

		// Thanks to https://stackoverflow.com/questions/687490/how-do-i-expand-a-tuple-into-variadic-template-functions-arguments for this 
		// pattern. Using std::make_index_sequence to expand out a sequence of integers in a parameter pack, and then using this to
		// index the tuple
		template<typename Ty, typename Tuple, std::size_t ... I>
		auto ApplyConstructFinalAssetObject_impl(Tuple&& t, std::index_sequence<I...>) {
			return ConstructFinalAssetObject<Ty>(std::get<I>(std::forward<Tuple>(t))...);
		}
		template<typename Ty, typename Tuple>
		auto ApplyConstructFinalAssetObject(Tuple&& t) {
			using Indices = std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>;
			return ApplyConstructFinalAssetObject_impl<Ty>(std::forward<Tuple>(t), Indices());
		}

		unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn);
		void DeregisterFrameBarrierCallback(unsigned);

		template<typename PromisedType>
			bool TimedWait(const std::shared_ptr<Future<PromisedType>>& future, std::chrono::microseconds timeout)
		{
			auto stallResult = future->StallWhilePending(timeout);
			return stallResult.value_or(AssetState::Pending) != AssetState::Pending;
		}
		
		static bool TimedWait(const IAsyncMarker& future, std::chrono::microseconds timeout)
		{
			auto stallResult = future.StallWhilePending(timeout);
			return stallResult.value_or(AssetState::Pending) != AssetState::Pending;
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

		template<typename T> static auto FutureResult_(int) -> decltype(std::declval<RemoveSmartPtrType<T>>().get());
		template<typename T> static auto FutureResult_(int) -> typename std::enable_if<std::is_copy_constructible_v<typename RemoveSmartPtrType<T>::PromisedType>, decltype(std::declval<RemoveSmartPtrType<T>>().ActualizeBkgrnd())>::type;
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
			TryGetAssetFromFuture(future, state, actualized, actualizationBlob, exceptionDepVal);
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
			TryGetAssetFromFuture(future, state, actualized, actualizationBlob, exceptionDepVal);
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
				if (queriedLog) { str << AsString(queriedLog); } else { str << std::string("<<no log>>"); }
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
				auto finalConstruction = ApplyConstructFinalAssetObject<PromisedAssetType>(std::move(actualized));
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
				auto finalConstruction = ApplyConstructFinalAssetObject<PromisedAssetType>(std::move(actualized));
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

		template<typename Promise, typename Fn, typename... Futures>
			static void FulfillContinuationFunction2(
				Promise&& promise,
				Fn&& continuationFunction,
				std::future<std::tuple<Futures...>>& completedFutures)
		{
			// In this variation, the continuation function takes a reference to the promise, and the promise must be fulfilled within that continuation function
			// If the continuation function throws, we still pass that exception to the promise
			TRY {
				auto completed = completedFutures.get();
				auto actualized = QueryToTuple(completed, std::make_index_sequence<sizeof...(Futures)>());
				std::apply(std::move(continuationFunction), std::tuple_cat(std::make_tuple(std::move(promise)), std::move(actualized)));
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

		template<typename Type> static std::shared_future<Type> GetContinuableFuture(const Future<Type>& input) { return input.ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(const std::shared_ptr<Future<Type>>& input) { return input->ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(Future<Type>&& input) { return input.ShareFuture(); }
		template<typename Type> static std::shared_future<Type> GetContinuableFuture(std::shared_ptr<Future<Type>>&& input) { return input->ShareFuture(); }

		template<typename Type> static const Type& GetContinuableFuture(const Type& input) { return input; }
		template<typename Type> static Type&& GetContinuableFuture(Type&& input) { return std::move(input); }
	}

	template<typename... FutureTypes>
		class MultiAssetFuture : public std::future<std::tuple<FutureTypes...>>
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

		template<typename PromisedType, typename Fn, typename std::enable_if<!std::is_void_v<std::invoke_result_t<Fn, Internal::FutureResult<FutureTypes>...>>>::type* =nullptr>
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

		template<typename PromisedType, typename Fn, typename std::enable_if<std::is_void_v<std::invoke_result_t<Fn, std::promise<PromisedType>&&, Internal::FutureResult<FutureTypes>...>>>::type* =nullptr>
			void ThenConstructToPromise(
				std::promise<PromisedType>&& promise,
				Fn&& fn)
		{
			thousandeyes::futures::then(
				std::move(*this),
				[promise=std::move(promise), func=std::move(fn)](std::future<std::tuple<FutureTypes...>>&& completedFutures) mutable {
					Internal::FulfillContinuationFunction2(std::move(promise), std::move(func), completedFutures);
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

		MultiAssetFuture(FutureTypes... subFutures) 
		: std::future<std::tuple<FutureTypes...>>{MakeFuture(std::forward<FutureTypes>(subFutures)...)}
		{
		}

		static std::future<std::tuple<FutureTypes...>> MakeFuture(FutureTypes... subFutures)
		{
			std::promise<std::tuple<FutureTypes...>> mergedPromise;
			auto mergedFuture = mergedPromise.get_future();

			std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();
			executor->watch(std::make_unique<Internal::FlexTimedWaitable<FutureTypes...>>(
				std::chrono::hours(1),		// timeout for FlexTimedWaitable from now, following the patterns present in thousandeyes::futures
				std::make_tuple(std::forward<FutureTypes>(subFutures)...),
				std::move(mergedPromise)
			));

			return mergedFuture;
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
