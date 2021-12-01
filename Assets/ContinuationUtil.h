// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING	// warning generated inside of thousandeyes/futures/then.h

#include "Continuation.h"
#include "IAsyncMarker.h"
#include "../ConsoleRig/GlobalServices.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/TimedWaitable.h"
#include "thousandeyes/futures/Executor.h"

namespace Assets
{
	enum PollStatus { Continue, Finish };

	namespace Internal
	{
		template<typename Marker>
			class ASyncMarkerBridge : public thousandeyes::futures::TimedWaitable
		{
		public:
			bool timedWait(const std::chrono::microseconds& timeout) override
			{
				if (timeout.count() == 0) {
					return _marker.GetAssetState() != ::Assets::AssetState::Pending;
				} else {
					return _marker.StallWhilePending(timeout).value_or(::Assets::AssetState::Pending) != ::Assets::AssetState::Pending;
				}
				return true;
			}

			void dispatch(std::exception_ptr err) override
			{
				if (err) {
					_promise.set_exception(err);
					return;
				}

				_promise.set_value(std::move(_marker));
			}

			ASyncMarkerBridge(Marker&& marker) : TimedWaitable(std::chrono::hours(1)), _marker(std::move(marker)) {}
			std::promise<Marker> _promise;
			Marker _marker;
		};

		class ASyncMarkerPtrBridge : public thousandeyes::futures::TimedWaitable
		{
		public:
			bool timedWait(const std::chrono::microseconds& timeout) override
			{
				assert(_marker);
				if (timeout.count() == 0) {
					return _marker->GetAssetState() != ::Assets::AssetState::Pending;
				} else {
					return _marker->StallWhilePending(timeout).value_or(::Assets::AssetState::Pending) != ::Assets::AssetState::Pending;
				}
				return true;
			}

			void dispatch(std::exception_ptr err) override
			{
				assert(_marker);
				if (err) {
					_promise.set_exception(err);
					_marker = nullptr;
					return;
				}

				_promise.set_value(std::move(_marker));
			}

			ASyncMarkerPtrBridge(std::shared_ptr<IAsyncMarker> marker) : TimedWaitable(std::chrono::hours(1)), _marker(std::move(marker)) {}
			std::promise<std::shared_ptr<IAsyncMarker>> _promise;
			std::shared_ptr<IAsyncMarker> _marker;
		};

		template<typename CheckFn> static auto CheckFnTakesTimeout(int) -> decltype(std::declval<std::invoke_result_t<CheckFn, std::chrono::microseconds>>(), std::true_type{});
		template<typename...> static auto CheckFnTakesTimeout(...) -> std::false_type;

		template<typename Promise, typename CheckFn, typename DispatchFn>
			class PollingFunctionBridge : public thousandeyes::futures::TimedWaitable
		{
		public:
			bool timedWait(const std::chrono::microseconds& timeout) override
			{
				if constexpr (decltype(CheckFnTakesTimeout<CheckFn>(0))::value) {
					_pollingCompleted |= (_checkFn(timeout) == PollStatus::Finish);
				} else {
					auto timeoutTime = std::chrono::steady_clock::now() + timeout;
					_pollingCompleted |= (_checkFn() == PollStatus::Finish);
					if (!_pollingCompleted) {
						// thousandeyes::futures will busy-loop if we don't actually yield the thread at all
						// So let's make sure we sleep at least for a bit here
						std::this_thread::sleep_until(timeoutTime);
					}
				}
				return _pollingCompleted;
			}

			void dispatch(std::exception_ptr err) override
			{
				if (err) {
					assert(!_pollingCompleted);
					_promise.set_exception(err);
					return;
				}

				assert(_pollingCompleted);
				if constexpr (!std::is_void_v<FutureResult<decltype(_promise.get_future())>>) {
					TRY {
						_promise.set_value(_dispatchFn());
					} CATCH(...) {
						_promise.set_exception(std::current_exception());
					} CATCH_END
				} else {
					TRY {
						_dispatchFn();
						_promise.set_value();
					} CATCH(...) {
						_promise.set_exception(std::current_exception());
					} CATCH_END

				}
			}

			PollingFunctionBridge(Promise&& promise, CheckFn&& checkFn, DispatchFn&& dispatchFn)
			: TimedWaitable(std::chrono::hours(1))
			, _promise(std::move(promise))
			, _checkFn(std::move(checkFn))
			, _dispatchFn(std::move(dispatchFn)) {}

			CheckFn _checkFn;
			DispatchFn _dispatchFn;
			Promise _promise;
			bool _pollingCompleted = false;
		};
	}

	template<typename Marker>
		static std::future<Marker> MakeASyncMarkerBridge(Marker&& marker)
	{
		auto bridge = std::make_unique<Internal::ASyncMarkerBridge<Marker>>(std::move(marker));
		auto result = bridge->_promise.get_future();

		auto* executor = ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor().get();
		if (executor) {
			executor->watch(std::move(bridge));
		} else {
			// might happen during shutdown
			bridge->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Continuation executor has expired")));
		}

		return result;
	}

	inline std::future<std::shared_ptr<IAsyncMarker>> MakeASyncMarkerBridge(std::shared_ptr<IAsyncMarker> marker)
	{
		auto bridge = std::make_unique<Internal::ASyncMarkerPtrBridge>(std::move(marker));
		auto result = bridge->_promise.get_future();

		auto* executor = ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor().get();
		if (executor) {
			executor->watch(std::move(bridge));
		} else {
			// might happen during shutdown
			bridge->_promise.set_exception(std::make_exception_ptr(std::runtime_error("Continuation executor has expired")));
		}

		return result;
	}

	template<typename Promise, typename CheckFn, typename DispatchFn>
		static void PollToPromise(
			Promise&& promise,
			CheckFn&& checkFn,
			DispatchFn&& dispatchFn)
	{
		auto bridge = std::make_unique<Internal::PollingFunctionBridge<Promise, CheckFn, DispatchFn>>(std::move(promise), std::move(checkFn), std::move(dispatchFn));
		auto* executor = ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor().get();
		if (executor) {
			executor->watch(std::move(bridge));
		} else {
			// might happen during shutdown
			promise.set_exception(std::make_exception_ptr(std::runtime_error("Continuation executor has expired")));
		}
	}
}
