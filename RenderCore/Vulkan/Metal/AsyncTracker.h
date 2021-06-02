// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectFactory.h"
#include "VulkanCore.h"
#include "../../../Utility/BitUtils.h"
#include <optional>
#include <chrono>
#include <vector>

namespace RenderCore { namespace Metal_Vulkan
{
	class FenceBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const { return _lastCompletedConsumerFrame; }
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
		enum class State { Unused, WritingCommands, SubmittedToQueue, Abandoned }; 
		struct Tracker
		{
			VkFence _fence = nullptr;
			Marker _frameMarker = Marker_Invalid;
			State _state = State::Unused;
		};
		std::vector<Tracker> _trackers;
		std::vector<VulkanUniquePtr<VkFence>> _fences;
		BitHeap _fenceAllocationFlags;

		Tracker* _nextProducerFrameToStart = nullptr;
		Tracker* _nextConsumerFrameToComplete = nullptr;
		Marker _currentProducerFrameMarker = Marker_Invalid;
		Marker _lastCompletedConsumerFrame = Marker_Invalid;
		VkDevice _device;

		void CheckFenceReset(VkFence fence);
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
