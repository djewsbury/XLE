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
	static VulkanUniquePtr<VkFence> CreateFenceOutsideOfFactory(VkDevice device)
	{
		// We can't use ObjectFactory::CreateFence(), because the object factory uses FenceBasedTracker internally
		// for it's destruction tracking
		VkFenceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = 0;		// can onlt be 0 or VK_FENCE_CREATE_SIGNALED_BIT

		VkFence rawFence = nullptr;
        auto res = vkCreateFence(device, &createInfo, g_allocationCallbacks, &rawFence);
        return VulkanUniquePtr<VkFence>(
            rawFence,
            [device](VkFence fence) { vkDestroyFence(device, fence, g_allocationCallbacks ); });
	}

	auto FenceBasedTracker::IncrementProducerFrame() -> Marker
	{
		ScopedLock(_trackersWritingCommandsLock);
		if (_initialMarker) {
			// special case to ensure that the initial marker actually gets submitted (or abandoned) by something
			assert(_currentProducerFrameMarker == 1);
			_trackersWritingCommands.push_back({_currentProducerFrameMarker, std::chrono::steady_clock::now()});
			_initialMarker = false;
			return _currentProducerFrameMarker;
		} else {
			auto result = ++_currentProducerFrameMarker;
			_trackersWritingCommands.push_back({result, std::chrono::steady_clock::now()});
			return result;
		}
	}

	VkFence FenceBasedTracker::FindAvailableFence(IteratorRange<const Marker*> markers, std::unique_lock<Threading::Mutex>& lock)
	{
		if (_fenceAllocationFlags.AllocatedCount() > _requestedQueueDepth) {
			// we incur this penalty on all threads, however it will have greated impact on threads that are submitting frequently -- which is why it can help correct the situation
			lock.unlock();
			auto pressure = GetThreadingPressure();
			Log(Warning) << "Stalling due to too many fences in FenceBasedTracker (pressure: " << pressure << "). This happens when a command list submitted in the background takes longer than multiple frames. Slowing down to try to reduce pressure" << std::endl;
			Threading::Sleep(4 * pressure);
			if (std::this_thread::get_id() == _queueThreadId)
				UpdateConsumer();
			lock.lock();
		}

		assert(!markers.empty());
		auto highestMarker = *(markers.end()-1);
		auto firstAvailable = _fenceAllocationFlags.Allocate();
		if (firstAvailable == ~0u)
			Throw(std::runtime_error("Failed to allocate fence in FenceBaseTracker"));

		if (firstAvailable >= _fences.size()) {
			_fences.reserve(firstAvailable+1);
			auto& factory = GetObjectFactory();
			while (firstAvailable >= _fences.size())
				_fences.push_back(CreateFenceOutsideOfFactory(factory.GetDevice().get()));
		}

		assert(_fences[firstAvailable]);
		return _fences[firstAvailable].get();
	}

	float FenceBasedTracker::GetThreadingPressure()
	{
		// When there are multiple CPU threads generating cmdlists, and one or more of those cmdlists are kept
		// alive for multiple frames, we end up in a situation where the tracker can never advance. This is
		// very bad for the system, and it probably means that objects are never being destroyed.
		// The "pressure" value returned here represents how bad this has gotten. We compare the number of 
		// current allocated fences to the requested "queueDepth" provided at startup. 
		//  less than queueDepth is pressure 0
		//	between 1*queueDepth to 2*queueDepth is pressure 0 to 1
		//	more than 2*queueDepth , the pressure will increase continually
		auto allocated = _fenceAllocationFlags.AllocatedCount();
		if (allocated < _requestedQueueDepth) return 0.f;
		return std::pow((allocated-_requestedQueueDepth) / float(_requestedQueueDepth), 4.f);
	}

	VkFence FenceBasedTracker::OnSubmitToQueue(IteratorRange<const Marker*> markers)
	{
		assert(!markers.empty());
		for (auto i=markers.begin()+1; i!=markers.end(); ++i) assert(*(i-1) < *i);		// we need the markers to be in sorted order here

		VkFence fence = nullptr;
		// Now allocate a fence and update _trackersSubmittedToQueue & _trackersSubmittedPendingOrdering
		{
			std::unique_lock<Threading::Mutex> lock(_trackersSubmittedToQueueLock);

			fence = FindAvailableFence(markers, lock);
			assert(fence);

			for (auto marker:markers) {
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
					if (_trackersSubmittedPendingOrdering.size() > 16) {
						Log(Warning) << "There are " << _trackersSubmittedPendingOrdering.size() << " pending command lists. This will slow now destruction queue efficiency because destructions related to the pending command lists won't be processed until the missing one is submitted or abandoned." << std::endl;
					}
					_trackersSubmittedPendingOrdering.push_back(tracker);
				}
			}
		}

		// update _trackersWritingCommands (we need to do this after FindAvailableFence(), because we use the _trackersWritingCommands for the number of 
		// fences to reserve)
		std::vector<unsigned> trackersPendingAbandon;
		{
			ScopedLock(_trackersWritingCommandsLock);
			auto i = std::remove_if(
				_trackersWritingCommands.begin(), _trackersWritingCommands.end(),
				[markers](const auto& c) { return std::find(markers.begin(), markers.end(), c.first) != markers.end(); });
			_trackersWritingCommands.erase(i, _trackersWritingCommands.end());

			// process _trackersPendingAbandon now
			trackersPendingAbandon = std::move(_trackersPendingAbandon);
		}

		// flush trackers pending abandon if there are any...
		if (!trackersPendingAbandon.empty()) {
			std::sort(trackersPendingAbandon.begin(), trackersPendingAbandon.end());
			ScopedLock(_trackersSubmittedToQueueLock);
			for (const auto& marker:trackersPendingAbandon) {
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
					if (_trackersSubmittedPendingOrdering.size() > 16) {
						Log(Warning) << "Large number of command lists pending ordering in async tracker. Marker (" << _nextSubmittedToQueueMarker << ") has not be submitted" << std::endl;
						Log(Warning) << "There are " << _trackersSubmittedPendingOrdering.size() << " pending command lists. This will slow now destruction queue efficiency because destructions related to the pending command lists won't be processed until the missing one is submitted or abandoned." << std::endl;
					}
					_trackersSubmittedPendingOrdering.push_back(tracker);
				}
			}
			trackersPendingAbandon.clear();
		}

		return fence;
	}

	void FenceBasedTracker::AbandonMarker(Marker marker)
	{
		ScopedLock(_trackersWritingCommandsLock);
		auto i = std::find_if(
			_trackersWritingCommands.begin(), _trackersWritingCommands.end(),
			[marker](const auto& c) { return c.first == marker; });
		assert(i != _trackersWritingCommands.end());
		_trackersWritingCommands.erase(i);
		_trackersPendingAbandon.push_back(marker);
	}

	void FenceBasedTracker::CheckFenceResetAlreadyLocked(VkFence fence)
	{
		for (auto& tracker:_trackersSubmittedToQueue)
			if (tracker._fence == fence)
				return;
		for (auto& tracker:_trackersSubmittedPendingOrdering)
			if (tracker._fence == fence)
				return;
		for (auto f:_fencesCurrentlyInWaitOperation)	// Another thread is in a vkWaitForFences for this very marker. We can't reset/reuse it right now
			if (f == fence)
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

		{
			ScopedLock(_trackersSubmittedToQueueLock);
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
						CheckFenceResetAlreadyLocked(fence);
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

		// Check for command lists in the "writing commands" state for a long period 
		{
			ScopedLock(_trackersWritingCommandsLock);
			const auto warningAge = std::chrono::seconds(1);
			if (!_trackersWritingCommands.empty()) {
				auto age = std::chrono::steady_clock::now() - _trackersWritingCommands.begin()->second;
				if (age > warningAge) {
					Log(Warning) << "Command list (" << _trackersWritingCommands.begin()->first << ") has been in the writing state for (" << std::chrono::duration_cast<std::chrono::seconds>(age).count() << ") seconds." << std::endl;
					Log(Warning) << "Command lists that say in this state for a long time reduce destruction queue efficiency. GPU objects cannot be destroyed until all command lists that were present during the object's lifetime have completed" << std::endl;
					Log(Warning) << "So even single command lists that stay in the writing state will prevent all objects from being destroyed" << std::endl;
				}
			}
		}
	}

	auto FenceBasedTracker::GetSpecificMarkerStatus(Marker marker) const -> MarkerStatus
	{
		if (marker <= GetConsumerMarker())
			return MarkerStatus::ConsumerCompleted;

		{
			ScopedLock(const_cast<FenceBasedTracker*>(this)->_trackersSubmittedToQueueLock);
			for (const auto& next:_trackersSubmittedToQueue) {
				if (next._frameMarker == marker) {
					if (next._state == State::SubmittedToQueue) {
						assert(next._fence);
						auto res = vkGetFenceStatus(_device, next._fence);
						if (res == VK_SUCCESS) {
							return MarkerStatus::ConsumerCompleted;
						} else {
							if (res == VK_ERROR_DEVICE_LOST)
								Throw(std::runtime_error("Vulkan device lost"));
							assert(res == VK_NOT_READY);
							return MarkerStatus::ConsumerPending;
						}
					} else if (next._state == State::Abandoned) {
						return MarkerStatus::ConsumerCompleted;
					} else {
						break;
					}
				}
			}

			for (const auto& next:_trackersSubmittedPendingOrdering) {
				if (next._frameMarker == marker) {
					if (next._state == State::SubmittedToQueue) {
						assert(next._fence);
						auto res = vkGetFenceStatus(_device, next._fence);
						if (res == VK_SUCCESS) {
							return MarkerStatus::ConsumerCompleted;
						} else {
							if (res == VK_ERROR_DEVICE_LOST)
								Throw(std::runtime_error("Vulkan device lost"));
							assert(res == VK_NOT_READY);
							return MarkerStatus::ConsumerPending;
						}
					} else if (next._state == State::Abandoned) {
						return MarkerStatus::ConsumerCompleted;
					} else {
						break;
					}
				}
			}

			for (const auto& next:_trackersWritingCommands)
				if (next.first == marker)
					return MarkerStatus::NotSubmitted;
		}

		return MarkerStatus::Unknown;
	}

	bool FenceBasedTracker::WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout)
	{
		auto start = std::chrono::steady_clock::now();
		if (marker <= _lastCompletedConsumerFrameMarker.load())
			return true;

		VkFence fence = nullptr;
		{
			ScopedLock(_trackersSubmittedToQueueLock);
			for (auto& tracker:_trackersSubmittedToQueue)
				if (tracker._frameMarker == marker) {
					assert(tracker._state == State::SubmittedToQueue);
					fence = tracker._fence;
					break;
				}
			if (!fence)
				for (auto& tracker:_trackersSubmittedPendingOrdering)
					if (tracker._frameMarker == marker) {
						assert(tracker._state == State::SubmittedToQueue);
						fence = tracker._fence;
						break;
					}
			if (fence)
				_fencesCurrentlyInWaitOperation.push_back(fence);
		}

		// If we didn't find the fence, we must assume that it's already been waited for/completed
		if (!fence) return true;

		// Stall for this specific fence now. But do this outside of a lock so that we won't interfer with
		// other threads. Note that other threads can interact with the queue and submit or wait for fences
		// at the same time
		bool timedOut = false;
		if (timeout.has_value()) {
			// note -- set timeout to zero to query the current state without stalling
			auto timeoutCount = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout.value()).count();
			auto res = vkWaitForFences(_device, 1, &fence, true, timeoutCount);
			assert(res == VK_SUCCESS || res == VK_TIMEOUT); (void)res;
			timedOut = res == VK_TIMEOUT;
		} else {
			auto res = vkWaitForFences(_device, 1, &fence, true, UINT64_MAX);
			assert(res == VK_SUCCESS); (void)res;
		}

		{
			ScopedLock(_trackersSubmittedToQueueLock);
			auto i = std::find(_fencesCurrentlyInWaitOperation.begin(), _fencesCurrentlyInWaitOperation.end(), fence);
			assert(i!=_fencesCurrentlyInWaitOperation.end());
			_fencesCurrentlyInWaitOperation.erase(i);
			// Another thread may have already removed references to this fence from the trackers, but didn't reset
			// it because we're waiting on it. If so, we should reset it now:
			CheckFenceResetAlreadyLocked(fence);
		}

		return !timedOut;
	}

	FenceBasedTracker::FenceBasedTracker(ObjectFactory& factory, unsigned queueDepth)
	: _fenceAllocationFlags(2*queueDepth)
	, _requestedQueueDepth(queueDepth)
	{
		_trackersSubmittedToQueue.reserve(queueDepth);
		_trackersSubmittedPendingOrdering.reserve(queueDepth);
		_trackersWritingCommands.reserve(queueDepth);
		_trackersPendingAbandon.reserve(queueDepth);

		_fences.resize(2*queueDepth);
		for (unsigned c=0; c<(2*queueDepth); ++c)
			_fences[c] = CreateFenceOutsideOfFactory(factory.GetDevice().get());

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

