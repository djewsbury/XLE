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

		VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo;
		VLA(VkSemaphore, semaphoreBuffer, completionSignals.size()+1);
		VLA(uint64_t, semaphoreValueBuffer, completionSignals.size()+1);
		if (submitResult._timelineSemaphoreToSignal) {

			timelineSemaphoreSubmitInfo = {};
			timelineSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
			timelineSemaphoreSubmitInfo.pNext = nullptr;
			timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = 0;
			timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = nullptr;
			timelineSemaphoreSubmitInfo.signalSemaphoreValueCount = submitInfo.signalSemaphoreCount+1;
			timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = semaphoreValueBuffer;

			// prepend submitResult._timelineSemaphoreOnComplete to the list of semaphores
			// (also timelineSemaphoreSubmitInfo.pSignalSemaphoreValues must be parallel and of the same length)
			++submitInfo.signalSemaphoreCount;
			submitInfo.pSignalSemaphores = semaphoreBuffer;
			*semaphoreBuffer++ = submitResult._timelineSemaphoreToSignal;
			*semaphoreValueBuffer++ = submitResult._timelineSemphoreValue;
			for (auto s:completionSignals) { *semaphoreBuffer++ = s; *semaphoreValueBuffer++ = 0; }

			submitInfo.pNext = &timelineSemaphoreSubmitInfo;
		}

		VkCommandBuffer rawCmdBuffers[] = { submitResult._cmdBuffer.get() };
		submitInfo.commandBufferCount = dimof(rawCmdBuffers);
		submitInfo.pCommandBuffers = rawCmdBuffers;

		ScopedLock(_queueLock);
		auto res = vkQueueSubmit(_underlying, 1, &submitInfo, submitResult._fence);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while queuing command list"));

		if (submitResult._timelineSemaphoreToSignal) {
			// _maxMarkerActuallySubmitted -> max(_maxMarkerActuallySubmitted, submitResult._timelineSemphoreValue), lockless
			auto maxSubmitted = _maxMarkerActuallySubmitted.load();
			for (;;) {
				if (maxSubmitted >= submitResult._timelineSemphoreValue) break;
				if (_maxMarkerActuallySubmitted.compare_exchange_weak(maxSubmitted, submitResult._timelineSemphoreValue)) break;
			}
			return submitResult._timelineSemphoreValue;
		} else {
			assert(!submitResult._asyncTrackerMarkers.empty());
			for (auto i=submitResult._asyncTrackerMarkers.begin(); i<submitResult._asyncTrackerMarkers.end()-1; ++i)
				assert(*i <= *(submitResult._asyncTrackerMarkers.end()-1));
			return *(submitResult._asyncTrackerMarkers.end()-1);
		}
	}

	void SubmissionQueue::WaitForFence(IAsyncTracker::Marker marker, std::optional<std::chrono::nanoseconds> timeout)
	{
		if (auto* ft = dynamic_cast<FenceBasedTracker*>(_gpuTracker.get())) {
			ft->WaitForFence(marker, timeout);
		} else {
			assert(marker <= _maxMarkerActuallySubmitted.load());
			checked_cast<SemaphoreBasedTracker*>(_gpuTracker.get())->WaitForMarker(marker, timeout);
		}
	}

	void SubmissionQueue::Present(
		VkSwapchainKHR swapChain, unsigned imageIndex, 
		IteratorRange<const VkSemaphore*> waitBeforePresent)
	{
		const VkSwapchainKHR swapChains[] = { swapChain };
		uint32_t imageIndices[] = { imageIndex };

		VkPresentInfoKHR present;
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.pNext = NULL;
		present.swapchainCount = dimof(swapChains);
		present.pSwapchains = swapChains;
		present.pImageIndices = imageIndices;
		present.pWaitSemaphores = waitBeforePresent.begin();
		present.waitSemaphoreCount = waitBeforePresent.size();
		present.pResults = NULL;

		ScopedLock(_queueLock);
		auto res = vkQueuePresentKHR(_underlying, &present);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while queuing present"));
	}

	SubmissionQueue::SubmissionQueue(
		ObjectFactory& factory,
		VkQueue queue,
		unsigned queueFamilyIndex)
	: _underlying(queue) 
	, _factory(&factory)
	, _queueFamilyIndex(queueFamilyIndex)
	, _maxMarkerActuallySubmitted(0)
	{
		if (factory.GetXLEFeatures()._timelineSemaphore) {
			_gpuTracker = std::make_shared<Metal_Vulkan::SemaphoreBasedTracker>(*_factory);
		} else
			_gpuTracker = std::make_shared<Metal_Vulkan::FenceBasedTracker>(*_factory, 32);
	}

	SubmissionQueue::~SubmissionQueue()
	{
	}

}}

