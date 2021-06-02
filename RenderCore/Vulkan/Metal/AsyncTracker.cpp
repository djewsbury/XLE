// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AsyncTracker.h"
#include "DeviceContext.h"
#include "IncludeVulkan.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include <chrono>

namespace RenderCore { namespace Metal_Vulkan
{
	auto FenceBasedTracker::IncrementProducerFrame() -> Marker
	{
		ScopedLock(_trackersWritingCommandsLock);
		if (_initialMarker) {
			// special case to ensure that the initial marker actually gets submitted (or abandoned) by something
			assert(_currentProducerFrameMarker == 1);
			_trackersWritingCommands.push_back(_currentProducerFrameMarker);
			_initialMarker = false;
			return _currentProducerFrameMarker;
		} else {
			auto result = ++_currentProducerFrameMarker;
			_trackersWritingCommands.push_back(result);
			return result;
		}
	}

	void FenceBasedTracker::OnSubmitToQueue(Marker marker, VkFence fence)
	{
		assert(std::this_thread::get_id() == _queueThreadId);

		unsigned fenceIndex = 0;
		for (; fenceIndex<_fences.size(); ++fenceIndex) if (_fences[fenceIndex].get() == fence) break;
		if (fenceIndex > _fences.size())
			Throw(std::runtime_error("Incorrect fence passed to FenceBasedTracker::OnSubmitToQueue"));

		if (!_fenceAllocationFlags.IsAllocated(fenceIndex))
			_fenceAllocationFlags.Allocate(fenceIndex);

		FlushTrackersPendingAbandon();

		Tracker tracker { fence, marker, State::SubmittedToQueue };
		if (marker == _nextSubmittedToQueueMarker) {
			_trackersSubmittedToQueue.push_back(tracker);
			++_nextSubmittedToQueueMarker;

			for (;;) {
				auto i = std::find_if(
					_trackersSubmittedPendingOrdering.begin(), _trackersSubmittedPendingOrdering.end(),
					[next=_nextSubmittedToQueueMarker](const auto& c) { return c._frameMarker == next; });
				if (i != _trackersSubmittedPendingOrdering.end()) {
					_trackersSubmittedToQueue.push_back(*i);
					++_nextSubmittedToQueueMarker;
					_trackersSubmittedPendingOrdering.erase(i);
				} else
					break;
			}
		} else {
			_trackersSubmittedPendingOrdering.push_back(tracker);
		}
	}

	void FenceBasedTracker::AbandonMarker(Marker marker)
	{
		ScopedLock(_trackersWritingCommandsLock);
		_trackersPendingAbandon.push_back(marker);
	}

	void FenceBasedTracker::FlushTrackersPendingAbandon()
	{
		assert(std::this_thread::get_id() == _queueThreadId);
		ScopedLock(_trackersWritingCommandsLock);
		std::sort(_trackersPendingAbandon.begin(), _trackersPendingAbandon.end());
		for (const auto& marker:_trackersPendingAbandon) {
			Tracker tracker { nullptr, marker, State::Abandoned };
			if (marker == _nextSubmittedToQueueMarker) {
				_trackersSubmittedToQueue.push_back(tracker);
				++_nextSubmittedToQueueMarker;

				for (;;) {
					auto i = std::find_if(
						_trackersSubmittedPendingOrdering.begin(), _trackersSubmittedPendingOrdering.end(),
						[next=_nextSubmittedToQueueMarker](const auto& c) { return c._frameMarker == next; });
					if (i != _trackersSubmittedPendingOrdering.end()) {
						_trackersSubmittedToQueue.push_back(*i);
						++_nextSubmittedToQueueMarker;
						_trackersSubmittedPendingOrdering.erase(i);
					} else
						break;
				}
			} else {
				_trackersSubmittedPendingOrdering.push_back(tracker);
			}
		}
		_trackersPendingAbandon.clear();
	}

	VkFence FenceBasedTracker::FindAvailableFence()
	{
		assert(std::this_thread::get_id() == _queueThreadId);
		auto firstAvailable = _fenceAllocationFlags.FirstUnallocated();
		assert(firstAvailable != ~0u);
		return _fences[firstAvailable].get();
	}

	void FenceBasedTracker::CheckFenceReset(VkFence fence)
	{
		assert(std::this_thread::get_id() == _queueThreadId);
		for (auto& tracker:_trackersSubmittedToQueue)
			if (tracker._fence == fence)
				return;
		for (auto& tracker:_trackersSubmittedPendingOrdering)
			if (tracker._fence == fence)
				return;

		unsigned fenceIndex = 0;
		for (; fenceIndex<_fences.size(); ++fenceIndex) if (_fences[fenceIndex].get() == fence) break;
		assert(fenceIndex < _fences.size());
		assert(_fenceAllocationFlags.IsAllocated(fenceIndex));
		_fenceAllocationFlags.Deallocate(fenceIndex);
		vkResetFences(_device, 1, &fence);
	}

	void FenceBasedTracker::UpdateConsumer()
	{
		assert(std::this_thread::get_id() == _queueThreadId);
		while (!_trackersSubmittedToQueue.empty()) {
			auto next = *_trackersSubmittedToQueue.begin();

			if (next._state == State::SubmittedToQueue) {
				assert(next._fence);
				auto res = vkGetFenceStatus(_device, next._fence);
				if (res == VK_SUCCESS) {
					assert(next._frameMarker == _lastCompletedConsumerFrameMarker+1);
					_lastCompletedConsumerFrameMarker = next._frameMarker;
					
					auto fence = next._fence;
					_trackersSubmittedToQueue.erase(_trackersSubmittedToQueue.begin());
					CheckFenceReset(fence);
				} else {
					if (res == VK_ERROR_DEVICE_LOST)
						Throw(std::runtime_error("Vulkan device lost"));
					assert(res == VK_NOT_READY); (void)res;
					break;
				}
			} else if (next._state == State::Abandoned) {
				assert(next._frameMarker == _lastCompletedConsumerFrameMarker+1);
				_lastCompletedConsumerFrameMarker = next._frameMarker;

				assert(next._fence == nullptr);
				_trackersSubmittedToQueue.erase(_trackersSubmittedToQueue.begin());
			} else {
				assert(0);
				break;
			}
		}
	}

	bool FenceBasedTracker::WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout)
	{
		assert(std::this_thread::get_id() == _queueThreadId);
		auto start = std::chrono::steady_clock::now();

		if (marker <= _lastCompletedConsumerFrameMarker)
			return true;

		bool foundTheFence = false;
		for (unsigned c=0; c<_trackersSubmittedToQueue.size(); ++c)
			if (_trackersSubmittedToQueue[c]._frameMarker == marker) {
				assert(_trackersSubmittedToQueue[c]._state == State::SubmittedToQueue);
				foundTheFence = true;
				break;
			}

		assert(foundTheFence);
		if (!foundTheFence) return false;

		// Wait in order until we complete the one requested
		while (!_trackersSubmittedToQueue.empty()) {
			auto next = *_trackersSubmittedToQueue.begin();

			if (next._state == State::SubmittedToQueue) {
				assert(next._fence);
				VkResult res;
				if (timeout.has_value()) {
					auto timeoutRemaining = ((start + timeout.value()) - std::chrono::steady_clock::now());
					if (timeoutRemaining < std::chrono::seconds(0)) return false;
					res = vkWaitForFences(_device, 1, &next._fence, true, std::chrono::duration_cast<std::chrono::nanoseconds>(timeoutRemaining).count());
				} else {
					res = vkWaitForFences(_device, 1, &next._fence, true, UINT64_MAX);
				}
				if (res == VK_SUCCESS) {
					assert(next._frameMarker == _lastCompletedConsumerFrameMarker+1);
					_lastCompletedConsumerFrameMarker = next._frameMarker;
					
					auto fence = next._fence;
					_trackersSubmittedToQueue.erase(_trackersSubmittedToQueue.begin());
					CheckFenceReset(fence);
				} else {
					if (res == VK_ERROR_DEVICE_LOST)
						Throw(std::runtime_error("Vulkan device lost"));
					break;
				}
			} else if (next._state == State::Abandoned) {
				assert(next._frameMarker == _lastCompletedConsumerFrameMarker+1);
				_lastCompletedConsumerFrameMarker = next._frameMarker;

				assert(next._fence == nullptr);
				_trackersSubmittedToQueue.erase(_trackersSubmittedToQueue.begin());
			} else {
				break;  // Unusued/WritingCommands -- end looping
			}

			if (_lastCompletedConsumerFrameMarker == marker)
				return true;
		}

		// completed all pending markers, but still didn't find the one requested (or timed out)
		return false;
	}

	FenceBasedTracker::FenceBasedTracker(ObjectFactory& factory, unsigned queueDepth)
	: _fenceAllocationFlags(queueDepth)
	{
		_trackersSubmittedToQueue.reserve(queueDepth);
		_trackersSubmittedPendingOrdering.reserve(queueDepth);
		_trackersWritingCommands.reserve(queueDepth);
		_trackersPendingAbandon.reserve(queueDepth);

		_fences.resize(queueDepth);
		for (unsigned c=0; c<queueDepth; ++c)
			_fences[c] = factory.CreateFence();

		// We start with frame 1;
		_currentProducerFrameMarker = 1;
		_lastCompletedConsumerFrameMarker = 0;
		_nextSubmittedToQueueMarker = 1;
		_initialMarker = true;
		_device = factory.GetDevice().get();
		_queueThreadId = std::this_thread::get_id();
	}

	FenceBasedTracker::~FenceBasedTracker() {}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void EventBasedTracker::SetConsumerEndOfFrame(DeviceContext& context)
	{
		// set the marker on the frame that has just finished --
		// Note that if we use VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, this is only going 
		// to be tracking rendering command progress -- not compute shaders!
		// Is ALL_COMMANDS fine?
		if (_trackers[_producerBufferIndex]._consumerFrameMarker != _currentProducerFrame) {
			if (context.HasActiveCommandList()) {
				context.GetActiveCommandList().SetEvent(_trackers[_producerBufferIndex]._event.get(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
				_trackers[_producerBufferIndex]._consumerFrameMarker = _currentProducerFrame;
			} else {
				_trackers[_producerBufferIndex]._consumerFrameMarker = Marker_FrameContainsNoData;
			}
		}
	}

	void EventBasedTracker::IncrementProducerFrame()
	{
		++_currentProducerFrame;
		_producerBufferIndex = (_producerBufferIndex + 1) % _bufferCount; 
		// If we start "eating our tail" (ie, we don't have enough buffers to support the queued GPU frames, we will get an assert
		// here... This needs to be replaced with something more robust
		// Probably higher level code should prevent the CPU from getting too far ahead of the GPU, so as to guarantee we never
		// end up eating our tail here...
		while (_trackers[_producerBufferIndex]._producerFrameMarker != Marker_Invalid) {
			
			using namespace std::chrono_literals;
			static std::chrono::steady_clock::time_point lastReport{};		// (defaults to start of epoch)
			auto now = std::chrono::steady_clock::now();
			if ((now - lastReport) > 1s) {
				Log(Verbose) << "Stalling due to insufficient trackers in Vulkan EventBasedTracker" << std::endl;
				lastReport = now;
			}

			Threading::YieldTimeSlice();
			UpdateConsumer();
		}
		assert(_trackers[_producerBufferIndex]._producerFrameMarker == Marker_Invalid); 
		_trackers[_producerBufferIndex]._producerFrameMarker = _currentProducerFrame;
	}

	void EventBasedTracker::UpdateConsumer()
	{
		for (;;) {
			if (_trackers[_consumerBufferIndex]._consumerFrameMarker == Marker_Invalid)
				break;

			if (_trackers[_consumerBufferIndex]._consumerFrameMarker != Marker_FrameContainsNoData) {
				auto status = vkGetEventStatus(_device, _trackers[_consumerBufferIndex]._event.get());
				if (status == VK_EVENT_RESET)
					break;
				assert(status == VK_EVENT_SET);
			}

			auto res = vkResetEvent(_device, _trackers[_consumerBufferIndex]._event.get());
			assert(res == VK_SUCCESS); (void)res;

			assert(_trackers[_consumerBufferIndex]._consumerFrameMarker == _trackers[_consumerBufferIndex]._producerFrameMarker 
				|| _trackers[_consumerBufferIndex]._consumerFrameMarker == Marker_FrameContainsNoData);
			assert(_trackers[_consumerBufferIndex]._producerFrameMarker > _lastConsumerFrame);
			_lastConsumerFrame = _trackers[_consumerBufferIndex]._producerFrameMarker;
			_trackers[_consumerBufferIndex]._consumerFrameMarker = Marker_Invalid;
			_trackers[_consumerBufferIndex]._producerFrameMarker = Marker_Invalid;
			_consumerBufferIndex = (_consumerBufferIndex + 1) % _bufferCount;
		}
	}

	EventBasedTracker::EventBasedTracker(ObjectFactory& factory, unsigned queueDepth)
	{
		assert(queueDepth > 0);
		_trackers.resize(queueDepth);
		for (unsigned q = 0; q < queueDepth; ++q) {
			_trackers[q]._event = factory.CreateEvent();
			_trackers[q]._consumerFrameMarker = Marker_Invalid;
			_trackers[q]._producerFrameMarker = Marker_Invalid;
		}
		_currentProducerFrame = 1;
		_bufferCount = queueDepth;
		_consumerBufferIndex = 1;
		_producerBufferIndex = 1;
		_trackers[_producerBufferIndex]._producerFrameMarker = _currentProducerFrame;
		_lastConsumerFrame = 0;
		_device = factory.GetDevice().get();
	}

	EventBasedTracker::~EventBasedTracker() {}

}}

