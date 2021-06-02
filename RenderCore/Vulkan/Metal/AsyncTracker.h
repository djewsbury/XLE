// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectFactory.h"
#include "VulkanCore.h"
#include <optional>
#include <chrono>
#include <vector>

namespace RenderCore { namespace Metal_Vulkan
{
	class FenceBasedTracker : public IAsyncTracker
	{
	public:
		virtual Marker GetConsumerMarker() const { return _lastCompletedConsumerFrame; }
		virtual Marker GetProducerMarker() const { return _currentProducerFrame->_frameMarker; }

		void IncrementProducerFrame();
		VkFence GetFenceForCurrentFrame();
		void UpdateConsumer();
		bool WaitForFence(Marker marker, std::optional<std::chrono::nanoseconds> timeout = {});

		FenceBasedTracker(ObjectFactory& factory, unsigned queueDepth);
		~FenceBasedTracker();
	private:
		struct Tracker
		{
			VulkanUniquePtr<VkFence> _fence;
			Marker _frameMarker;
			bool _submittedToGPU = false;
			bool _gotGPUCompletion = false;
		};
		std::vector<Tracker> _trackers;
		Tracker* _currentProducerFrame = nullptr;
		Tracker* _nextConsumerFrameToComplete = nullptr;
		Marker _lastCompletedConsumerFrame;
		VkDevice _device;
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
