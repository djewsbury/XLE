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
#include <thread>

namespace RenderCore { namespace Metal_Vulkan
{
	class FenceBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const override { return _lastCompletedConsumerFrameMarker; }
		virtual Marker GetProducerMarker() const override { return _currentProducerFrameMarker; }
		virtual MarkerStatus GetSpecificMarkerStatus(Marker) const override;

		Marker IncrementProducerFrame();
		VkFence OnSubmitToQueue(IteratorRange<const Marker*> marker);
		void AbandonMarker(Marker);

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
			std::string _name;
		};
		std::vector<TrackerWritingCommands> _trackersWritingCommands;				// protected by _trackersWritingCommandsLock
		std::vector<unsigned> _trackersPendingAbandon;				// protected by _trackersWritingCommandsLock
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

	class EventBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const { return _lastConsumerFrame; }
		virtual Marker GetProducerMarker() const { return _currentProducerFrame; }

		void IncrementProducerFrame();
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
