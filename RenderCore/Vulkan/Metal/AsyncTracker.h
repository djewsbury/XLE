// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

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
		virtual Marker GetConsumerMarker() const { return _lastCompletedConsumerFrameMarker; }
		virtual Marker GetProducerMarker() const { return _currentProducerFrameMarker; }

		Marker IncrementProducerFrame();
		void OnSubmitToQueue(Marker, VkFence);
		void AbandonMarker(Marker);

		VkFence FindAvailableFence();

		void UpdateConsumer();
		bool WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout = {});

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
		std::vector<Tracker> _trackersSubmittedToQueue;				// protected by _queueThreadId
		std::vector<Tracker> _trackersSubmittedPendingOrdering;		// protected by _queueThreadId
		std::vector<VulkanUniquePtr<VkFence>> _fences;				// protected by _queueThreadId
		BitHeap _fenceAllocationFlags;								// protected by _queueThreadId
		Marker _nextSubmittedToQueueMarker = Marker_Invalid;		// protected by _queueThreadId

		std::vector<unsigned> _trackersWritingCommands;				// protected by _trackersWritingCommandsLock
		std::vector<unsigned> _trackersPendingAbandon;				// protected by _trackersWritingCommandsLock
		bool _initialMarker = false;								// protected by _trackersWritingCommandsLock
		Threading::Mutex _trackersWritingCommandsLock;

		std::atomic<Marker> _currentProducerFrameMarker = Marker_Invalid;
		std::atomic<Marker> _lastCompletedConsumerFrameMarker = Marker_Invalid;
		VkDevice _device;

		std::thread::id _queueThreadId;

		void CheckFenceReset(VkFence fence);
		void FlushTrackersPendingAbandon();
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
