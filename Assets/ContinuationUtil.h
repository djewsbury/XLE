// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING	// warning generated inside of thousandeyes/futures/then.h

#include "AssetFutureContinuation.h"
#include "IAsyncMarker.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/TimedWaitable.h"
#include "thousandeyes/futures/Executor.h"

namespace Assets
{
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

		template<typename Promise, typename Fn>
			class PollingFunctionBridge : public thousandeyes::futures::TimedWaitable
		{
		public:
			bool timedWait(const std::chrono::microseconds&) override
			{
				_pollingCompleted |= !_fn(std::move(_promise));
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
			}

			PollingFunctionBridge(Promise&& promise, Fn&& fn)
			: TimedWaitable(std::chrono::hours(1))
			, _promise(std::move(promise))
			, _fn(std::move(fn)) {}

			Fn _fn;
			Promise _promise;
			bool _pollingCompleted = false;
		};
	}

	template<typename Marker>
		static std::future<Marker> MakeASyncMarkerBridge(Marker&& marker)
	{
		auto bridge = std::make_unique<Internal::ASyncMarkerBridge<Marker>>(std::move(marker));
		auto result = bridge->_promise.get_future();

		std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();
		executor->watch(std::move(bridge));

		return result;
	}

	inline std::future<std::shared_ptr<IAsyncMarker>> MakeASyncMarkerBridge(std::shared_ptr<IAsyncMarker> marker)
	{
		auto bridge = std::make_unique<Internal::ASyncMarkerPtrBridge>(std::move(marker));
		auto result = bridge->_promise.get_future();

		std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();
		executor->watch(std::move(bridge));

		return result;
	}

	template<typename Promise, typename Fn>
		static void PollToPromise(
			Promise&& promise,
			Fn&& fn)
	{
		auto bridge = std::make_unique<Internal::PollingFunctionBridge<Promise, Fn>>(std::move(promise), std::move(fn));
		std::shared_ptr<thousandeyes::futures::Executor> executor = thousandeyes::futures::Default<thousandeyes::futures::Executor>();
		executor->watch(std::move(bridge));
	}
}
