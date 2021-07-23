// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SubmissionQueue.h"

namespace RenderCore { namespace Metal_Vulkan
{
	IAsyncTracker::Marker SubmissionQueue::Submit(
        Metal_Vulkan::CommandList& cmdList,
        IteratorRange<const VkSemaphore*> completionSignals,
        IteratorRange<const VkSemaphore*> waitBeforeBegin,
		IteratorRange<const VkPipelineStageFlags*> waitBeforeBeginStages)
	{
		assert(&cmdList.GetAsyncTracker() == _gpuTracker.get());
        assert(waitBeforeBegin.size() == waitBeforeBeginStages.size());
		cmdList.ValidateCommitToQueue(*_factory);
		auto submitResult = cmdList.OnSubmitToQueue();

		VkSubmitInfo submitInfo;
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext = nullptr;

		submitInfo.waitSemaphoreCount = waitBeforeBegin.size();
		submitInfo.pWaitSemaphores = waitBeforeBegin.begin();
		submitInfo.pWaitDstStageMask = waitBeforeBeginStages.begin();
		submitInfo.signalSemaphoreCount = completionSignals.size();
		submitInfo.pSignalSemaphores = completionSignals.begin();

		VkCommandBuffer rawCmdBuffers[] = { submitResult._cmdBuffer.get() };
		submitInfo.commandBufferCount = dimof(rawCmdBuffers);
		submitInfo.pCommandBuffers = rawCmdBuffers;

		ScopedLock(_queueLock);
		auto res = vkQueueSubmit(_underlying, 1, &submitInfo, submitResult._fence);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while queuing semaphore signal"));

        assert(!submitResult._asyncTrackerMarkers.empty());
		return *(submitResult._asyncTrackerMarkers.end()-1);
	}

    void SubmissionQueue::WaitForFence(IAsyncTracker::Marker marker, std::optional<std::chrono::nanoseconds> timeout)
    {
        _gpuTracker->WaitForFence(marker, timeout);
    }

    SubmissionQueue::SubmissionQueue(
        ObjectFactory& factory,
		VkQueue queue)
    : _underlying(queue) 
    , _factory(&factory)
    {
        _gpuTracker = std::make_shared<Metal_Vulkan::FenceBasedTracker>(*_factory, 32);
    }

    SubmissionQueue::~SubmissionQueue()
    {
    }

}}

