// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "AsyncTracker.h"
#include "DeviceContext.h"
#include <memory>

namespace RenderCore { namespace Metal_Vulkan
{
	class SubmissionQueue
	{
	public:
		VkQueue GetUnderlying()     { return _underlying; }
		const std::shared_ptr<Metal_Vulkan::FenceBasedTracker>& GetTracker() { return _gpuTracker; }

		IAsyncTracker::Marker Submit(
			CommandList& cmdList,
			IteratorRange<const VkSemaphore*> signalOnCompletion,
			IteratorRange<const VkSemaphore*> waitBeforeBegin,
			IteratorRange<const VkPipelineStageFlags*> waitBeforeBeginStages);
		void WaitForFence(IAsyncTracker::Marker marker, std::optional<std::chrono::nanoseconds> timeout = {});

		SubmissionQueue(
			ObjectFactory& factory,
			VkQueue queue);
		~SubmissionQueue();
		SubmissionQueue(SubmissionQueue&&) = delete;
		SubmissionQueue& operator=(SubmissionQueue&&) = delete;
	private:
		VkQueue _underlying;
		std::shared_ptr<Metal_Vulkan::FenceBasedTracker> _gpuTracker;
		ObjectFactory* _factory;
	};
}}

