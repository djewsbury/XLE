// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "Marker.h"
#include "IAsyncMarker.h"
#include "AssetTraits.h"		// just for InvokeAssetConstructor
#include "../OSServices/Log.h"
#include "thousandeyes/futures/TimedWaitable.h"
#include <memory>
#include <tuple>
#include <functional>
#include <future>
#include <utility>

#define CONTINUATION_DETAILED_LOGGING 1

namespace Assets { namespace Internal
{
	template<size_t I = 0, typename... Tp>
		void CheckAssetState(
			AssetState& currentState, 
			Blob& actualizationBlob, 
			DependencyValidation& exceptionDepVal,
			std::tuple<Tp...>& actualized,
			const std::tuple<std::shared_ptr<Marker<Tp>>...>& futures)
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
		bool AnyForegroundPendingAssets(const std::tuple<std::shared_ptr<Marker<Tp>>...>& futures)
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
		return InvokeAssetConstructor<Ty>(std::get<I>(std::forward<Tuple>(t))...);
	}
	template<typename Ty, typename Tuple>
	auto ApplyConstructFinalAssetObject(Tuple&& t) {
		using Indices = std::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>;
		return ApplyConstructFinalAssetObject_impl<Ty>(std::forward<Tuple>(t), Indices());
	}

	unsigned RegisterFrameBarrierCallback(std::function<void()>&& fn);
	void DeregisterFrameBarrierCallback(unsigned);

	template<typename PromisedType>
		bool TimedWait(const std::shared_ptr<Marker<PromisedType>>& future, std::chrono::microseconds timeout)
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

	#if CONTINUATION_DETAILED_LOGGING 
		template<int I, typename... Types>
			void SerializeNames(std::ostream& str)
		{
			if (I!=0) str << ", ";
			str << typeid(std::tuple_element_t<I, std::tuple<Types...>>).name();
			if constexpr ((I+1) != sizeof...(Types))
				SerializeNames<I+1, Types...>(str);
		}

		template<typename PromisedType, typename... Types>
			void LogBeginWatch()
		{
			Log(Debug) << "BeginWatch {";
			SerializeNames<0, Types...>(Log(Debug));
			Log(Debug) << "} -> " << typeid(PromisedType).name() << std::endl;
		}

		template<typename PromisedType, typename... Types>
			void LogBeginFulfillPromise()
		{
			Log(Debug) << "BeginFulfillPromise {";
			SerializeNames<0, Types...>(Log(Debug));
			Log(Debug) << "} -> " << typeid(PromisedType).name() << std::endl;
		}
	#else
		template<typename PromisedType, typename... Types> inline void LogBeginWatch() {}
		template<typename PromisedType, typename... Types> inline void LogBeginFulfillPromise() {}
	#endif

	template<typename T> static auto FutureResult_(int) -> decltype(std::declval<RemoveSmartPtrType<T>>().get());
	template<typename T> static auto FutureResult_(int) -> typename std::enable_if<std::is_copy_constructible_v<typename RemoveSmartPtrType<T>::PromisedType>, decltype(std::declval<RemoveSmartPtrType<T>>().ActualizeBkgrnd())>::type;
	template<typename...> static auto FutureResult_(...) -> void;

	template<typename T> using FutureResult = decltype(FutureResult_<T>(0));

	////////////////////////////////////////////////////////////////////////////////////////////////
	template<typename Payload>
		static AssetState TryQueryFuture(
			Marker<Payload>& future,
			Payload& actualized,
			Blob& actualizationBlob,
			DependencyValidation& exceptionDepVal)
	{
		return future.CheckStatusBkgrnd(actualized, exceptionDepVal, actualizationBlob);
	}

	template<typename Payload>
		static AssetState TryQueryFuture(
			std::shared_ptr<Marker<Payload>>& future,
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
		static auto QueryFuture(Marker<Payload>& future) -> decltype(future.ActualizeBkgrnd()) { return future.ActualizeBkgrnd(); }

	template<typename Payload>
		static auto QueryFuture(std::shared_ptr<Marker<Payload>>& future) -> decltype(future->ActualizeBkgrnd()) { return future->ActualizeBkgrnd(); }

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
			std::tuple<Futures...>&& completedFutures)
	{
		TRY {
			auto actualized = QueryToTuple(completedFutures, std::make_index_sequence<sizeof...(Futures)>());
			auto finalConstruction = ApplyConstructFinalAssetObject<PromisedAssetType>(std::move(actualized));
			promise.set_value(std::move(finalConstruction));	
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise, typename Fn, typename... Futures>
		static void FulfillContinuationFunction(
			Promise& promise, 
			Fn&& continuationFunction,
			std::tuple<Futures...>&& completedFutures)
	{
		TRY {
			auto actualized = QueryToTuple(completedFutures, std::make_index_sequence<sizeof...(Futures)>());
			auto finalResult = std::apply(std::move(continuationFunction), std::move(actualized));
			promise.set_value(std::move(finalResult));
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise, typename Fn, typename... Futures>
		static void FulfillContinuationFunctionPassPromise(
			Promise&& promise,
			Fn&& continuationFunction,
			std::tuple<Futures...>&& completedFutures)
	{
		// In this variation, the continuation function takes a reference to the promise, and the promise must be fulfilled within that continuation function
		// If the continuation function throws, we still pass that exception to the promise
		TRY {
			auto actualized = QueryToTuple(completedFutures, std::make_index_sequence<sizeof...(Futures)>());
			std::apply(std::move(continuationFunction), std::tuple_cat(std::make_tuple(std::move(promise)), std::move(actualized)));
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<
		typename Promise, typename Fn, typename... Futures,
		typename std::enable_if<!std::is_void_v<std::invoke_result_t<Fn, Futures...>>>::type* =nullptr>
		static void FulfillContinuationFunctionPassFutures(
			Promise& promise, 
			Fn&& continuationFunction,
			std::tuple<Futures...>&& completedFutures)
	{
		TRY {
			auto finalResult = std::apply(std::move(continuationFunction), std::move(completedFutures));
			promise.set_value(std::move(finalResult));
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<
		typename Fn, typename... Futures,
		typename std::enable_if<std::is_void_v<std::invoke_result_t<Fn, Futures...>>>::type* =nullptr>
		static void FulfillContinuationFunctionPassFutures(
			std::promise<void>& promise, 
			Fn&& continuationFunction,
			std::tuple<Futures...>&& completedFutures)
	{
		// this variation is for continuation functions that return void
		TRY {
			std::apply(std::move(continuationFunction), std::move(completedFutures));
			promise.set_value();
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename Promise, typename Fn, typename... Futures>
		static void FulfillContinuationFunctionPassPromisePassFutures(
			Promise&& promise,
			Fn&& continuationFunction,
			std::tuple<Futures...>&& completedFutures)
	{
		// In this variation, we pass both the promise and the (un-queried) futures
		TRY {
			std::apply(std::move(continuationFunction), std::tuple_cat(std::make_tuple(std::move(promise)), std::move(completedFutures)));
		} CATCH(...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	template<typename... Futures>
		static void FulfillOpaquePromise(
			std::promise<void>& promise, 
			std::tuple<Futures...>&& completedFutures)
	{
		// We must query the futures just to see if there's an exception within them
		TRY {
			auto actualized = QueryToTuple(completedFutures, std::make_index_sequence<sizeof...(Futures)>());
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
		static_assert(!std::is_void_v<FutureResult<Test>>, "The given future type can't be used with continuation functions. This can happen when using Asset::Marker<> with a non-copyable object");
		if constexpr ((I+1) != sizeof...(FutureTypes))
			CheckValidForContinuation<I+1, FutureTypes...>();
	}

	template<typename Type> static std::shared_future<Type> GetContinuableFuture(const Marker<Type>& input) { return input.ShareFuture(); }
	template<typename Type> static std::shared_future<Type> GetContinuableFuture(const std::shared_ptr<Marker<Type>>& input) { return input->ShareFuture(); }
	template<typename Type> static std::shared_future<Type> GetContinuableFuture(Marker<Type>&& input) { return input.ShareFuture(); }
	template<typename Type> static std::shared_future<Type> GetContinuableFuture(std::shared_ptr<Marker<Type>>&& input) { return input->ShareFuture(); }

	template<typename Type> static const Type& GetContinuableFuture(const Type& input) { return input; }
	template<typename Type> static Type&& GetContinuableFuture(Type&& input) { return std::move(input); }

	// based on thousandeyes::futures::detail::FutureWithTuple, this generates fullfills a promise
	// with our tuple of futures
	template<typename... FutureTypes>
		struct FlexTimedWaitable : public thousandeyes::futures::TimedWaitable
	{
	public:
		using TupleOfFutures = std::tuple<FutureTypes...>;
		FlexTimedWaitable(
			std::chrono::microseconds waitLimit,
			TupleOfFutures subFutures)
		: TimedWaitable(waitLimit), _subFutures(std::move(subFutures))
		{}

		template<std::size_t I>
			bool timedWait_(const std::chrono::microseconds& timeout)
		{
			if (!TimedWait(std::get<I>(_subFutures), timeout))
				return false;
			if constexpr(I+1 != sizeof...(FutureTypes))
				if (!timedWait_<I+1>(timeout))
					return false;
			return true;
		}

		bool timedWait(const std::chrono::microseconds& timeout) override
		{
			return timedWait_<0>(timeout);
		}

		TupleOfFutures _subFutures;
	};

	template<typename... FutureTypes>
		struct FlexTimedWaitableSimple : public FlexTimedWaitable<FutureTypes...>
	{
		using TupleOfFutures = std::tuple<FutureTypes...>;
		FlexTimedWaitableSimple(
			std::chrono::microseconds waitLimit,
			TupleOfFutures&& subFutures,
			std::promise<TupleOfFutures>&& p)
		: FlexTimedWaitable<FutureTypes...>(waitLimit, std::move(subFutures)), _promise(std::move(p))
		{}

		void dispatch(std::exception_ptr err) override
		{
			if (err) {
				_promise.set_exception(err);
				return;
			}

			TRY {
				_promise.set_value(std::move(FlexTimedWaitable<FutureTypes...>::_subFutures));
			} CATCH (...) {
				_promise.set_exception(std::current_exception());
			} CATCH_END
		}

		std::promise<TupleOfFutures> _promise;
	};

	template<typename ContinuationFn, typename PromisedType, typename... FutureTypes>
		struct FlexTimedWaitableWithContinuation : public FlexTimedWaitable<FutureTypes...>
	{
	public:
		using TupleOfFutures = std::tuple<FutureTypes...>;
		FlexTimedWaitableWithContinuation(
			std::chrono::microseconds waitLimit,
			TupleOfFutures&& subFutures,
			ContinuationFn&& continuation,
			std::promise<PromisedType>&& p)
		: FlexTimedWaitable<FutureTypes...>(waitLimit, std::move(subFutures)), _continuation(std::move(continuation)), _promise(std::move(p))
		{}

		void dispatch(std::exception_ptr err) override
		{
			if (err) {
				_promise.set_exception(err);
				return;
			}

			TRY {
				_continuation(std::move(_promise), std::move(FlexTimedWaitable<FutureTypes...>::_subFutures));
			} CATCH (...) {
				_promise.set_exception(std::current_exception());
			} CATCH_END
		}

		std::promise<PromisedType> _promise;
		ContinuationFn _continuation;
	};
}}
