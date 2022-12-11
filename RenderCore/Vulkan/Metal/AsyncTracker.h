// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ObjectFactory.h"
#include "VulkanCore.h"
#include "../../../Utility/BitUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include <optional>
#include <chrono>
#include <vector>
#include <deque>
#include <thread>

namespace RenderCore { namespace Metal_Vulkan
{
	class ExtensionFunctions;

	class FenceBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const override { return _lastCompletedConsumerFrameMarker; }
		virtual Marker GetProducerMarker() const override { return _currentProducerFrameMarker; }
		virtual MarkerStatus GetSpecificMarkerStatus(Marker) const override;

		Marker AllocateMarkerForNewCmdList();
		VkFence OnSubmitToQueue(IteratorRange<const Marker*> markers);
		void AbandonMarkers(IteratorRange<const Marker*> markers);

		void UpdateConsumer();
		bool WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout = {});	///< returns true iff the marker has completed, or false if we timed out waiting for it

		float GetThreadingPressure();
		void AttachName(Marker marker, std::string name);

		FenceBasedTracker(ObjectFactory& factory, unsigned queueDepth);
		~FenceBasedTracker();
	private:
		enum class State { SubmittedToQueue, Abandoned, Unused }; 
		struct Tracker
		{
			VkFence _fence = nullptr;
			Marker _frameMarker = Marker_Invalid;
			State _state = State::Unused;
		};
		std::vector<Tracker> _trackersSubmittedToQueue;				// protected by _trackersSubmittedToQueueLock
		std::vector<Tracker> _trackersSubmittedPendingOrdering;		// protected by _trackersSubmittedToQueueLock
		std::vector<VulkanUniquePtr<VkFence>> _fences;				// protected by _trackersSubmittedToQueueLock
		std::vector<VkFence> _fencesCurrentlyInWaitOperation;		// protected by _trackersSubmittedToQueueLock
		BitHeap _fenceAllocationFlags;								// protected by _trackersSubmittedToQueueLock
		Marker _nextSubmittedToQueueMarker = Marker_Invalid;		// protected by _trackersSubmittedToQueueLock
		Threading::Mutex _trackersSubmittedToQueueLock;

		struct TrackerWritingCommands
		{
			Marker _marker;
			std::chrono::steady_clock::time_point _beginTime;
			std::thread::id _threadInitiated;
			std::string _name;
		};
		std::vector<TrackerWritingCommands> _trackersWritingCommands;				// protected by _trackersWritingCommandsLock
		std::vector<Marker> _trackersPendingAbandon;				// protected by _trackersWritingCommandsLock
		bool _initialMarker = false;								// protected by _trackersWritingCommandsLock
		Threading::Mutex _trackersWritingCommandsLock;

		std::atomic<Marker> _currentProducerFrameMarker = Marker_Invalid;
		std::atomic<Marker> _lastCompletedConsumerFrameMarker = Marker_Invalid;
		VkDevice _device;

		std::thread::id _queueThreadId;
		unsigned _requestedQueueDepth;

		void CheckFenceResetAlreadyLocked(VkFence fence);
		VkFence FindAvailableFence(IteratorRange<const Marker*> marker, std::unique_lock<Threading::Mutex>& lock);
	};

	class SemaphoreBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const override { return _lastCompletedConsumerFrameMarker; }
		virtual Marker GetProducerMarker() const override { return _currentProducerFrameMarker; }
		virtual MarkerStatus GetSpecificMarkerStatus(Marker) const override;

		Marker AllocateMarkerForNewCmdList();
		void AbandonMarkers(IteratorRange<const Marker*> markers);

		void UpdateConsumer();

		bool WaitForSpecificMarker(Marker marker, std::optional<std::chrono::nanoseconds> timeout = {});	///< returns true iff the marker has completed, or false if we timed out waiting for it
		float GetThreadingPressure();
		void AttachName(Marker marker, std::string name);

		VkSemaphore GetSemaphore() const { return _semaphore.get(); }
		VkSemaphore GetSubmitSemaphore() const { return _submitSemaphore.get(); }

		SemaphoreBasedTracker(ObjectFactory& factory);
		~SemaphoreBasedTracker();
	private:
		VulkanUniquePtr<VkSemaphore> _semaphore;
		VulkanUniquePtr<VkSemaphore> _submitSemaphore;
		ExtensionFunctions* _extFn;

		enum class State { SubmittedToQueue, Abandoned, Unused }; 
		struct Tracker
		{
			Marker _frameMarker = Marker_Invalid;
			State _state = State::Unused;
		};
		std::vector<Tracker> _trackersSubmittedPendingOrdering;		// protected by _trackersSubmittedToQueueLock
		mutable Threading::Mutex _trackersSubmittedToQueueLock;

		struct TrackerWritingCommands
		{
			Marker _marker;
			std::chrono::steady_clock::time_point _beginTime;
			std::thread::id _threadInitiated;
			std::string _name;
		};
		std::vector<TrackerWritingCommands> _trackersWritingCommands;		// protected by _trackersWritingCommandsLock
		bool _currentProducerFrameMarkerAdvancedBeforeAllocation = false;	// protected by _trackersWritingCommandsLock
		Threading::Mutex _trackersWritingCommandsLock;

		// [1, _lastCompletedConsumerFrameMarker] -> GPU finished all processing already
		// (_lastCompletedConsumerFrameMarker, _lastQueuedInOrder] -> all queued or abandoned (though _lastSubmittedInOrder must be queued)
		// (_lastQueuedInOrder, _trailingAbandons] ->  all abandoned, not expecting any GPU updates on these
		// (_lastTrailingAbandons, _currentProducerFrameMarker] -> CPU writing commands, or queued out of order

		std::atomic<uint64_t> _currentProducerFrameMarker;
		std::atomic<uint64_t> _lastCompletedConsumerFrameMarker;
		std::atomic<uint64_t> _lastCompletedSubmitSemaphoreValue;
		std::atomic<uint64_t> _nextSubmitSemaphoreValue;

		uint64_t _lastQueuedInOrder;		// protected by _trackersSubmittedToQueueLock
		uint64_t _trailingAbandons;			// protected by _trackersSubmittedToQueueLock
		VkDevice _device;

		uint64_t ProcessMarkers(IteratorRange<const Marker*> markers, State newState, uint64_t newSubmitSemaphoreValue);

		using MarkerToSubmitSemaphore = std::pair<Marker, uint64_t>;
		std::deque<MarkerToSubmitSemaphore> _submitSemaphoreValuesAndMarkers;		// protected by _trackersSubmittedToQueueLock

		// SubmissionQueue related interface
		friend class SubmissionQueue;
		struct SubmissionInfo
		{
			Marker _maxInorderMarker = 0;
			uint64_t _submitSemaphoreValue = 0;
		};
		SubmissionInfo OnSubmitToQueue(IteratorRange<const Marker*> markers);
	};

	class EventBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const { return _lastConsumerFrame; }
		virtual Marker GetProducerMarker() const { return _currentProducerFrame; }

		void AllocateMarkerForNewCmdList();
		void SetConsumerEndOfFrame(DeviceContext&);
		void UpdateConsumer();

		EventBasedTracker(ObjectFactory& factory, unsigned queueDepth);
		~EventBasedTracker();
	private:
		struct Tracker
		{
			VulkanUniquePtr<VkEvent> _event;
			Marker _producerFrameMarker;
			Marker _consumerFrameMarker;
		};
		std::vector<Tracker> _trackers;
		unsigned _bufferCount;
		unsigned _producerBufferIndex;
		unsigned _consumerBufferIndex;
		Marker _currentProducerFrame;
		Marker _lastConsumerFrame;
		VkDevice _device;
	};
}}
