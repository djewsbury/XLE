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
		// If we use up all of the markers, we need stall and wait for the GPU to catch up
		for (;;) {
			// If "next' is ready to go for the next producer frame, we'll break out 
			if (_nextProducerFrameToStart->_state == State::Unused) break;

			using namespace std::chrono_literals;
			static std::chrono::steady_clock::time_point lastReport{};		// (defaults to start of epoch)
			auto now = std::chrono::steady_clock::now();
			if ((now - lastReport) > 1s) {
				Log(Verbose) << "Stalling due to insufficient trackers in Vulkan FenceBasedTracker" << std::endl;
				lastReport = now;
			}

			Threading::YieldTimeSlice();
			UpdateConsumer();
		}

        auto result = ++_currentProducerFrameMarker;
		_nextProducerFrameToStart->_frameMarker = result;
		_nextProducerFrameToStart->_state = State::WritingCommands;
        assert(_nextProducerFrameToStart->_fence == nullptr);

		++_nextProducerFrameToStart;
		if (_nextProducerFrameToStart >= AsPointer(_trackers.end()))
			_nextProducerFrameToStart = &_trackers[0];

        return result;
	}

	void FenceBasedTracker::OnSubmitToQueue(Marker marker, VkFence fence)
	{
        unsigned fenceIndex = 0;
        for (; fenceIndex<_fences.size(); ++fenceIndex) if (_fences[fenceIndex].get() == fence) break;
        if (fenceIndex > _fences.size())
            Throw(std::runtime_error("Incorrect fence passed to FenceBasedTracker::OnSubmitToQueue"));

        if (!_fenceAllocationFlags.IsAllocated(fenceIndex))
            _fenceAllocationFlags.Allocate(fenceIndex);

        for (auto& tracker:_trackers) {
            if (tracker._frameMarker != marker) continue;

            assert(tracker._state == State::WritingCommands && tracker._fence == nullptr);
            tracker._state = State::SubmittedToQueue;
            tracker._fence = fence;
            return;
        }

        Throw(std::runtime_error("Could not find marker (" + std::to_string(marker) + ") in tracker records"));
	}

    void FenceBasedTracker::AbandonMarker(Marker marker)
    {
        for (auto& tracker:_trackers) {
            if (tracker._frameMarker != marker) continue;

            assert(tracker._state == State::WritingCommands && tracker._fence == nullptr);
            tracker._state = State::Abandoned;
            return;
        }

        Throw(std::runtime_error("Could not find marker (" + std::to_string(marker) + ") in tracker records"));
    }

	VkFence FenceBasedTracker::FindAvailableFence()
    {
        auto firstAvailable = _fenceAllocationFlags.FirstUnallocated();
        assert(firstAvailable != ~0u);
        return _fences[firstAvailable].get();
    }

    void FenceBasedTracker::CheckFenceReset(VkFence fence)
    {
        for (auto& tracker:_trackers)
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
		for (;;) {
			if (_nextConsumerFrameToComplete == _nextProducerFrameToStart) break;

            if (_nextConsumerFrameToComplete->_state == State::SubmittedToQueue) {
                assert(!_nextConsumerFrameToComplete->_fence);
                auto res = vkGetFenceStatus(_device, _nextConsumerFrameToComplete->_fence);
                if (res == VK_SUCCESS) {
                    assert(_nextConsumerFrameToComplete->_frameMarker > _lastCompletedConsumerFrame);
                    _lastCompletedConsumerFrame = _nextConsumerFrameToComplete->_frameMarker;
                    
                    VkFence fence = _nextConsumerFrameToComplete->_fence;
                    _nextConsumerFrameToComplete->_fence = nullptr;
                    CheckFenceReset(fence);
                    _nextConsumerFrameToComplete->_state = State::Unused;
                    _nextConsumerFrameToComplete->_frameMarker = Marker_Invalid;
                } else {
                    if (res == VK_ERROR_DEVICE_LOST)
                        Throw(std::runtime_error("Vulkan device lost"));
                    assert(res == VK_NOT_READY); (void)res;
                    break;
                }
            } else if (_nextConsumerFrameToComplete->_state == State::Abandoned) {
                assert(_nextConsumerFrameToComplete->_frameMarker > _lastCompletedConsumerFrame);
                _lastCompletedConsumerFrame = _nextConsumerFrameToComplete->_frameMarker;

                assert(_nextConsumerFrameToComplete->_fence == nullptr);
                _nextConsumerFrameToComplete->_state = State::Unused;
                _nextConsumerFrameToComplete->_frameMarker = Marker_Invalid;
            } else {
                break;  // Unusued/WritingCommands -- end looping
            }

            ++_nextConsumerFrameToComplete;
            if (_nextConsumerFrameToComplete >= AsPointer(_trackers.end()))
                _nextConsumerFrameToComplete = &_trackers[0];
		}
	}

	bool FenceBasedTracker::WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout)
	{
		auto start = std::chrono::steady_clock::now();

		if (marker <= _lastCompletedConsumerFrame)
			return true;

		bool foundTheFence = false;
		for (unsigned c=0; c<_trackers.size(); ++c)
			if (_trackers[c]._frameMarker == marker) {
				assert(_trackers[c]._state == State::SubmittedToQueue);
				foundTheFence = true;
				break;
			}

		assert(foundTheFence);
		if (!foundTheFence) return false;

		// Wait in order until we complete the one requested
		while (_lastCompletedConsumerFrame != marker) {
			if (_nextConsumerFrameToComplete == _nextProducerFrameToStart) break;

            if (_nextConsumerFrameToComplete->_state == State::SubmittedToQueue) {
                assert(_nextConsumerFrameToComplete->_fence);
                VkFence fence = _nextConsumerFrameToComplete->_fence;
                VkResult res;
                if (timeout.has_value()) {
                    auto timeoutRemaining = ((start + timeout.value()) - std::chrono::steady_clock::now());
                    if (timeoutRemaining < std::chrono::seconds(0)) return false;
                    res = vkWaitForFences(_device, 1, &fence, true, std::chrono::duration_cast<std::chrono::nanoseconds>(timeoutRemaining).count());
                } else {
                    res = vkWaitForFences(_device, 1, &fence, true, UINT64_MAX);
                }
                if (res == VK_SUCCESS) {
                    assert(_nextConsumerFrameToComplete->_frameMarker > _lastCompletedConsumerFrame);
                    _lastCompletedConsumerFrame = _nextConsumerFrameToComplete->_frameMarker;
                    
                    VkFence fence = _nextConsumerFrameToComplete->_fence;
                    _nextConsumerFrameToComplete->_fence = nullptr;
                    CheckFenceReset(fence);
                    _nextConsumerFrameToComplete->_state = State::Unused;
                    _nextConsumerFrameToComplete->_frameMarker = Marker_Invalid;
                } else {
                    if (res == VK_ERROR_DEVICE_LOST)
                        Throw(std::runtime_error("Vulkan device lost"));
                    break;
                }
            } else if (_nextConsumerFrameToComplete->_state == State::Abandoned) {
                assert(_nextConsumerFrameToComplete->_frameMarker > _lastCompletedConsumerFrame);
                _lastCompletedConsumerFrame = _nextConsumerFrameToComplete->_frameMarker;

                assert(_nextConsumerFrameToComplete->_fence == nullptr);
                _nextConsumerFrameToComplete->_state = State::Unused;
                _nextConsumerFrameToComplete->_frameMarker = Marker_Invalid;
            } else {
                break;  // Unusued/WritingCommands -- end looping
            }

            ++_nextConsumerFrameToComplete;
            if (_nextConsumerFrameToComplete >= AsPointer(_trackers.end()))
                _nextConsumerFrameToComplete = &_trackers[0];
		}

		// completed all pending markers, but still didn't find the one requested (or timed out)
		return false;
	}

	FenceBasedTracker::FenceBasedTracker(ObjectFactory& factory, unsigned queueDepth)
    : _fenceAllocationFlags(queueDepth)
	{
		_trackers.resize(queueDepth);
		for (unsigned c=0; c<queueDepth; ++c) {
			_trackers[c]._fence = nullptr;
			_trackers[c]._frameMarker = 0;
			_trackers[c]._state = State::Unused;
		}
        _fences.resize(queueDepth);
        for (unsigned c=0; c<queueDepth; ++c)
            _fences[c] = factory.CreateFence();

		_nextProducerFrameToStart = &_trackers[1];
		_nextConsumerFrameToComplete = &_trackers[0];
		_lastCompletedConsumerFrame = 0;
        // Start writing frame 1
        _currentProducerFrameMarker = 1;
        _trackers[0]._frameMarker = 1;
        _trackers[0]._state = State::WritingCommands;
		_device = factory.GetDevice().get();
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

