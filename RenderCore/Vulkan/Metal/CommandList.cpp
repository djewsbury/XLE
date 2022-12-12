// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommandList.h"
#include "AsyncTracker.h"
#include "Pools.h"
#include "IncludeVulkan.h"
#include "../../../OSServices/Log.h"
#include <assert.h>

// #define SUBMISSION_LOG_SPAM 1

namespace RenderCore { namespace Metal_Vulkan
{
	void CommandList::UpdateBuffer(
		VkBuffer buffer, VkDeviceSize offset, 
		VkDeviceSize byteCount, const void* data)
	{
		assert(byteCount <= 65536); // this restriction is imposed by Vulkan
		assert((byteCount & (4 - 1)) == 0);  // must be a multiple of 4
		assert(byteCount > 0 && data);
		vkCmdUpdateBuffer(
			_underlying.get(),
			buffer, 0,
			byteCount, (const uint32_t*)data);
	}

	void CommandList::BindDescriptorSets(
		VkPipelineBindPoint pipelineBindPoint,
		VkPipelineLayout layout,
		uint32_t firstSet,
		uint32_t descriptorSetCount,
		const VkDescriptorSet* pDescriptorSets,
		uint32_t dynamicOffsetCount,
		const uint32_t* pDynamicOffsets)
	{
		vkCmdBindDescriptorSets(
			_underlying.get(),
			pipelineBindPoint, layout, firstSet, 
			descriptorSetCount, pDescriptorSets,
			dynamicOffsetCount, pDynamicOffsets);
	}

	void CommandList::CopyBuffer(
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		uint32_t regionCount,
		const VkBufferCopy* pRegions)
	{
		vkCmdCopyBuffer(_underlying.get(), srcBuffer, dstBuffer, regionCount, pRegions);
	}

	void CommandList::CopyImage(
		VkImage srcImage,
		VkImageLayout srcImageLayout,
		VkImage dstImage,
		VkImageLayout dstImageLayout,
		uint32_t regionCount,
		const VkImageCopy* pRegions)
	{
		vkCmdCopyImage(
			_underlying.get(), 
			srcImage, srcImageLayout, 
			dstImage, dstImageLayout, 
			regionCount, pRegions);
	}

	void CommandList::CopyBufferToImage(
		VkBuffer srcBuffer,
		VkImage dstImage,
		VkImageLayout dstImageLayout,
		uint32_t regionCount,
		const VkBufferImageCopy* pRegions)
	{
		vkCmdCopyBufferToImage(
			_underlying.get(),
			srcBuffer, 
			dstImage, dstImageLayout,
			regionCount, pRegions);
	}

	void CommandList::CopyImageToBuffer(
		VkImage srcImage,
		VkImageLayout srcImageLayout,
		VkBuffer dstBuffer,
		uint32_t regionCount,
		const VkBufferImageCopy* pRegions)
	{
		vkCmdCopyImageToBuffer(
			_underlying.get(),
			srcImage, srcImageLayout,
			dstBuffer,
			regionCount, pRegions);
	}

	void CommandList::ClearColorImage(
		VkImage image,
		VkImageLayout imageLayout,
		const VkClearColorValue* pColor,
		uint32_t rangeCount,
		const VkImageSubresourceRange* pRanges)
	{
		vkCmdClearColorImage(
			_underlying.get(),
			image, imageLayout, 
			pColor, rangeCount, pRanges);
	}

	void CommandList::ClearDepthStencilImage(
		VkImage image,
		VkImageLayout imageLayout,
		const VkClearDepthStencilValue* pDepthStencil,
		uint32_t rangeCount,
		const VkImageSubresourceRange* pRanges)
	{
		vkCmdClearDepthStencilImage(
			_underlying.get(),
			image, imageLayout, 
			pDepthStencil, rangeCount, pRanges);
	}

	void CommandList::PipelineBarrier(
		VkPipelineStageFlags            srcStageMask,
		VkPipelineStageFlags            dstStageMask,
		VkDependencyFlags               dependencyFlags,
		uint32_t                        memoryBarrierCount,
		const VkMemoryBarrier*          pMemoryBarriers,
		uint32_t                        bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier*    pBufferMemoryBarriers,
		uint32_t                        imageMemoryBarrierCount,
		const VkImageMemoryBarrier*     pImageMemoryBarriers)
	{
		vkCmdPipelineBarrier(
			_underlying.get(),
			srcStageMask, dstStageMask,
			dependencyFlags, 
			memoryBarrierCount, pMemoryBarriers,
			bufferMemoryBarrierCount, pBufferMemoryBarriers,
			imageMemoryBarrierCount, pImageMemoryBarriers);
	}

	void CommandList::PushConstants(
		VkPipelineLayout layout,
		VkShaderStageFlags stageFlags,
		uint32_t offset,
		uint32_t size,
		const void* pValues)
	{
		vkCmdPushConstants(
			_underlying.get(),
			layout, stageFlags,
			offset, size, pValues);
	}

	void CommandList::WriteTimestamp(
		VkPipelineStageFlagBits pipelineStage,
		VkQueryPool queryPool, uint32_t query)
	{
		vkCmdWriteTimestamp(_underlying.get(), pipelineStage, queryPool, query);
	}

	void CommandList::BeginQuery(VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
	{
		vkCmdBeginQuery(_underlying.get(), queryPool, query, flags);
	}

	void CommandList::EndQuery(VkQueryPool queryPool, uint32_t query)
	{
		vkCmdEndQuery(_underlying.get(), queryPool, query);
	}

	void CommandList::ResetQueryPool(VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
	{
		vkCmdResetQueryPool(_underlying.get(), queryPool, firstQuery, queryCount);
	}

	void CommandList::SetEvent(VkEvent evnt, VkPipelineStageFlags stageMask)
	{
		vkCmdSetEvent(_underlying.get(), evnt, stageMask);
	}

	void CommandList::ExecuteSecondaryCommandList(CommandList&& cmdList)
	{
		const VkCommandBuffer buffers[] = { cmdList.GetUnderlying().get() };
		vkCmdExecuteCommands(
			_underlying.get(),
			dimof(buffers), buffers);

		_attachedStorage.MergeIn(std::move(cmdList._attachedStorage));
		assert(_asyncTracker == cmdList._asyncTracker);
		_asyncTrackerMarkers.insert(_asyncTrackerMarkers.end(), cmdList._asyncTrackerMarkers.begin(), cmdList._asyncTrackerMarkers.end());

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)

			// Merge in the list of "must be visible" resources and "becoming visible resources
			// However, note:
			//		- input and output arrays should be sorted -- so we can use merge sort approach for this
			//		- any new "must be visible" resources that are already present in our "becoming visible" list 
			//			be filtered out (ie, we're merging in use of a resource that was made visible previously on this cmd list) 
			RequireResourceVisbility(cmdList._resourcesThatMustBeVisible);
			MakeResourcesVisible(cmdList._resourcesBecomingVisible);
		#endif

		cmdList._underlying.reset();
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			cmdList._resourcesBecomingVisible.clear();
			cmdList._resourcesThatMustBeVisible.clear();
		#endif
		cmdList._attachedStorage = {};
		cmdList._asyncTracker = nullptr;
		cmdList._asyncTrackerMarkers.clear();
	}

	void CommandList::ValidateCommitToQueue(ObjectFactory& factory)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			// We're going to commit the current command list to the queue. Let's validate resource visibility
			// All resources in _resourcesBecomingVisible must be on the "_resourcesVisibleToQueue" list in ObjectFactory
			// If they are not, it means one of the following:
			//   - that the resource was never made visible on a command list
			//   - the command list in which it was made visible hasn't yet been commited to the queue
			//   - it's made visible after it was used on this command list
			ScopedLock(factory._resourcesVisibleToQueueLock);
			auto factoryi = factory._resourcesVisibleToQueue.begin();
			auto searchi = _resourcesThatMustBeVisible.begin();
			while (searchi != _resourcesThatMustBeVisible.end()) {
				while (factoryi != factory._resourcesVisibleToQueue.end() && *factoryi < *searchi)
					++factoryi;

				if (factoryi == factory._resourcesVisibleToQueue.end() || *factoryi != *searchi)
					Throw(std::runtime_error("Attempting to use resource that hasn't been made visible. Ensure that all used resources have had Metal::CompleteInitialization() called on them"));

				++searchi;
			}
			_resourcesThatMustBeVisible.clear();

			// Now register the resources in _resourcesBecomingVisible as visible to the queue
			auto becomingVisibleEnd = std::unique(_resourcesBecomingVisible.begin(), _resourcesBecomingVisible.end());
			if (_resourcesBecomingVisible.begin() != becomingVisibleEnd) {
				std::vector<uint64_t> newVisibleToQueue;
				newVisibleToQueue.reserve(becomingVisibleEnd - _resourcesBecomingVisible.begin() + factory._resourcesVisibleToQueue.size());
				std::set_union(
					factory._resourcesVisibleToQueue.begin(), factory._resourcesVisibleToQueue.end(),
					_resourcesBecomingVisible.begin(), becomingVisibleEnd,
					std::back_inserter(newVisibleToQueue));

				std::swap(newVisibleToQueue, factory._resourcesVisibleToQueue);
			}
		#endif
	}

	void CommandList::RequireResourceVisbilityAlreadySorted(IteratorRange<const uint64_t*> resourceGuids)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
				// Don't record the guid for any resources that are already marked as becoming visible 
				// during this command list (this is the only way we can check relative ordering of 
				// initialization and use within the same command list)
			size_t mustBeVisibleInitialSize = _resourcesThatMustBeVisible.size();
			auto becomingI = _resourcesBecomingVisible.begin();
			_resourcesThatMustBeVisible.reserve(_resourcesBecomingVisible.size() + resourceGuids.size());
			auto mustBeVisibleI = resourceGuids.begin();
			while (mustBeVisibleI != resourceGuids.end()) {
				while (becomingI != _resourcesBecomingVisible.end() && *becomingI < *mustBeVisibleI) ++becomingI;
				if (becomingI == _resourcesBecomingVisible.end() || *becomingI != *mustBeVisibleI)
					_resourcesThatMustBeVisible.push_back(*mustBeVisibleI);		// we sort using std::inplace_merge just below
				++mustBeVisibleI;
			}
			std::inplace_merge(_resourcesThatMustBeVisible.begin(), _resourcesThatMustBeVisible.begin() + mustBeVisibleInitialSize, _resourcesThatMustBeVisible.end());
			auto i = std::unique(_resourcesThatMustBeVisible.begin(), _resourcesThatMustBeVisible.end());
			_resourcesThatMustBeVisible.erase(i, _resourcesThatMustBeVisible.end());
		#endif
	}

	void CommandList::RequireResourceVisbility(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			VLA(uint64_t, resourceGuids, resourceGuidsInit.size());
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);
			RequireResourceVisbilityAlreadySorted(MakeIteratorRange(resourceGuids, &resourceGuids[resourceGuidsInit.size()]));	// inline please
		#endif
	}

	void CommandList::MakeResourcesVisible(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			VLA(uint64_t, resourceGuids, resourceGuidsInit.size());
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);

			auto mid = _resourcesBecomingVisible.insert(_resourcesBecomingVisible.end(), resourceGuids, &resourceGuids[resourceGuidsInit.size()]);
			std::inplace_merge(_resourcesBecomingVisible.begin(), mid, _resourcesBecomingVisible.end());
		#endif
	}

	IAsyncTracker::Marker CommandList::GetPrimaryTrackerMarker() const
	{
		assert(!_asyncTrackerMarkers.empty());
		// the first one should be the main tracker associated with this cmd list -- additionals come from cmd lists executed via ExecuteSecondaryCommandList
		return *_asyncTrackerMarkers.begin();
	}

	void CommandList::AddWaitBeforeBegin(VulkanSharedPtr<VkSemaphore> semaphore, uint64_t value)
	{
		#if defined(_DEBUG)
			for (auto& s:_signalOnCompletion) 
				assert(s.first != semaphore || s.second > value);		// cmd list both waits and signals the same semaphore
		#endif
		for (auto& s:_waitBeforeBegin) {
			if (s.first == semaphore) {
				s.second = std::max(s.second, value);
				return;
			}
		}
		_waitBeforeBegin.emplace_back(std::move(semaphore), value);
	}

	void CommandList::AddSignalOnCompletion(VulkanSharedPtr<VkSemaphore> semaphore, uint64_t value)
	{
		#if defined(_DEBUG)
			for (auto& s:_waitBeforeBegin) 
				assert(s.first != semaphore || s.second < value);		// cmd list both waits and signals the same semaphore
		#endif
		for (auto& s:_signalOnCompletion) {
			if (s.first == semaphore) {
				s.second = std::max(s.second, value);
				return;
			}
		}
		_signalOnCompletion.emplace_back(std::move(semaphore), value);
	}

	CommandList::CommandList(CommandList&&) = default;
	
	CommandList& CommandList::operator=(CommandList&& moveFrom)
	{
		if (&moveFrom != this) {
			_attachedStorage.AbandonAllocations();
			if (_asyncTracker && !_asyncTrackerMarkers.empty()) {
				std::sort(_asyncTrackerMarkers.begin(), _asyncTrackerMarkers.end());
				_asyncTracker->AbandonMarkers(_asyncTrackerMarkers);
			} else {
				assert(!_asyncTracker && _asyncTrackerMarkers.empty());     // should have neither or both
			}
		}

		_underlying = std::move(moveFrom._underlying);
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			_resourcesBecomingVisible = std::move(moveFrom._resourcesBecomingVisible);
			_resourcesThatMustBeVisible = std::move(moveFrom._resourcesThatMustBeVisible);
		#endif
		_attachedStorage = std::move(moveFrom._attachedStorage);
		_asyncTracker = std::move(moveFrom._asyncTracker);
		_asyncTrackerMarkers = std::move(moveFrom._asyncTrackerMarkers);
		_waitBeforeBegin = std::move(moveFrom._waitBeforeBegin);
		_signalOnCompletion = std::move(moveFrom._signalOnCompletion);
		return *this;
	}

	CommandList::CommandList(
		VulkanSharedPtr<VkCommandBuffer> underlying,
		std::shared_ptr<IAsyncTrackerVulkan> asyncTracker)
	: _underlying(std::move(underlying)) 
	, _asyncTracker(std::move(asyncTracker))
	{
		auto marker = _asyncTracker->AllocateMarkerForNewCmdList();
		assert(marker != IAsyncTracker::Marker_Invalid);
		_asyncTrackerMarkers.push_back(marker);
	}

	CommandList::~CommandList() 
	{
		_attachedStorage.AbandonAllocations();
		if (_asyncTracker && !_asyncTrackerMarkers.empty()) {
			std::sort(_asyncTrackerMarkers.begin(), _asyncTrackerMarkers.end());
			_asyncTracker->AbandonMarkers(_asyncTrackerMarkers);
		} else {
			assert(!_asyncTracker && _asyncTrackerMarkers.empty());     // should have neither or both
		}
	}

	CommandList::CommandList() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SubmissionQueue::Submit(
		IteratorRange<Metal_Vulkan::CommandList* const*> cmdLists,
		IteratorRange<const std::pair<VkSemaphore, uint64_t>*> waitBeforeBegin,
		IteratorRange<const VkPipelineStageFlags*> waitBeforeBeginStagesInit,
		IteratorRange<const std::pair<VkSemaphore, uint64_t>*> signalOnCompletion)
	{
		assert(waitBeforeBegin.size() == waitBeforeBeginStagesInit.size());
		size_t trackerMarkersCount = 0;
		for (auto* cmdList:cmdLists) {
			assert(&cmdList->GetAsyncTracker() == _gpuTracker.get());
			// We don't call ValidateCommitToQueue for transfer queues so that resources aren't marked visible to object factory until they are transfered to graphics queues
			if (_queueFamilyIndex != _factory->_dedicatedTransferQueueFamily)
				cmdList->ValidateCommitToQueue(*_factory);
			cmdList->_attachedStorage.OnSubmitToQueue(cmdList->GetPrimaryTrackerMarker());
			trackerMarkersCount += cmdList->_asyncTrackerMarkers.size();
		}

		std::vector<IAsyncTracker::Marker> asyncTrackerMarkers;
		asyncTrackerMarkers.reserve(trackerMarkersCount);
		VLA(VkCommandBuffer, rawCmdBuffers, cmdLists.size());
		std::vector<VulkanSharedPtr<VkCommandBuffer>> capturedCmdBuffers;
		capturedCmdBuffers.reserve(cmdLists.size());
		unsigned c=0;
		unsigned waitBeforeBeginInCmdListCount = 0, signalOnCompletionInCmdListCount = 0;
		for (auto* cmdList:cmdLists) {
			assert(cmdList->_asyncTracker.get() == _gpuTracker.get());
			std::sort(cmdList->_asyncTrackerMarkers.begin(), cmdList->_asyncTrackerMarkers.end());
			auto midpoint = asyncTrackerMarkers.insert(asyncTrackerMarkers.end(), cmdList->_asyncTrackerMarkers.begin(), cmdList->_asyncTrackerMarkers.end());
			std::inplace_merge(asyncTrackerMarkers.begin(), midpoint, asyncTrackerMarkers.end());

			cmdList->_asyncTrackerMarkers.clear();
			cmdList->_asyncTracker = nullptr;
			rawCmdBuffers[c++] = cmdList->_underlying.get();
			capturedCmdBuffers.emplace_back(std::move(cmdList->_underlying));
			waitBeforeBeginInCmdListCount += cmdList->_waitBeforeBegin.size();
			signalOnCompletionInCmdListCount += cmdList->_signalOnCompletion.size();
		}

		// Tell the tracker we're submitting the markers
		auto trackerSubmitInfo = checked_cast<SemaphoreBasedTracker*>(_gpuTracker.get())->OnSubmitToQueue(asyncTrackerMarkers);

		////////////////////////////////////////
		VLA(VkSemaphore, waitBeforeBeginSemaphores, waitBeforeBegin.size()+waitBeforeBeginInCmdListCount);
		VLA(VkPipelineStageFlags, waitBeforeBeginStages, waitBeforeBegin.size()+waitBeforeBeginInCmdListCount);
		VLA(uint64_t, waitBeforeBeginValues, waitBeforeBegin.size()+waitBeforeBeginInCmdListCount);
		VLA(VkSemaphore, signalOnCompletionSemaphores, signalOnCompletion.size()+signalOnCompletionInCmdListCount+2);
		VLA(uint64_t, signalOnCompletionValues, signalOnCompletion.size()+signalOnCompletionInCmdListCount+2);
		unsigned waitBeforeBeginCount = 0, signalOnCompletionCount = 0;

		for (auto s:waitBeforeBegin) {
			waitBeforeBeginSemaphores[waitBeforeBeginCount] = s.first;
			waitBeforeBeginValues[waitBeforeBeginCount] = s.second;
			waitBeforeBeginStages[waitBeforeBeginCount] = waitBeforeBeginStagesInit[waitBeforeBeginCount];
			++waitBeforeBeginCount;
		}

		for (auto s:signalOnCompletion) {
			signalOnCompletionSemaphores[signalOnCompletionCount] = s.first;
			signalOnCompletionValues[signalOnCompletionCount] = s.second;
			++signalOnCompletionCount;
		}

		for (auto* cmdList:cmdLists) {
			for (const auto& s:cmdList->_waitBeforeBegin) {
				// if the same semaphore is actually signalled by an earlier cmd list in part of the same commit, we can omit it
				unsigned c=0;
				for (; c<signalOnCompletionCount; ++c)
					if (signalOnCompletionSemaphores[c] == s.first.get() && signalOnCompletionValues[c] <= s.second)
						break;
				if (c != signalOnCompletionCount) continue;	// omitted

				waitBeforeBeginSemaphores[waitBeforeBeginCount] = s.first.get();
				waitBeforeBeginValues[waitBeforeBeginCount] = s.second;
				waitBeforeBeginStages[waitBeforeBeginCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				++waitBeforeBeginCount;
			}

			for (const auto& s:cmdList->_signalOnCompletion) {
				signalOnCompletionSemaphores[signalOnCompletionCount] = s.first.get();
				signalOnCompletionValues[signalOnCompletionCount] = s.second;
				++signalOnCompletionCount;
			}

			cmdList->_waitBeforeBegin.clear();
			cmdList->_signalOnCompletion.clear();
		}

		ScopedLock(_queueLock);

		// Note that we have to ignore timeline semaphore values that are the same as previously submitted cmd lists (otherwise it triggers errors inside of Vulkan)
		// This happens when there are out-of-order markers queued up (ie, the current semaphore value is actually out of date)
		assert(!trackerSubmitInfo._maxInorderMarker || trackerSubmitInfo._maxInorderMarker >= _maxInorderActuallySubmitted);
		if (trackerSubmitInfo._maxInorderMarker > _maxInorderActuallySubmitted) {
			signalOnCompletionSemaphores[signalOnCompletionCount] = checked_cast<SemaphoreBasedTracker*>(_gpuTracker.get())->GetSemaphore();
			signalOnCompletionValues[signalOnCompletionCount] = trackerSubmitInfo._maxInorderMarker;
			++signalOnCompletionCount;
		}

		signalOnCompletionSemaphores[signalOnCompletionCount] = checked_cast<SemaphoreBasedTracker*>(_gpuTracker.get())->GetSubmitSemaphore();
		signalOnCompletionValues[signalOnCompletionCount] = trackerSubmitInfo._submitSemaphoreValue;
		++signalOnCompletionCount;

		VkSubmitInfo submitInfo;
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.pNext = nullptr;

		submitInfo.waitSemaphoreCount = waitBeforeBeginCount;
		submitInfo.pWaitSemaphores = waitBeforeBeginSemaphores;
		submitInfo.pWaitDstStageMask = waitBeforeBeginStages;
		submitInfo.signalSemaphoreCount = signalOnCompletionCount;
		submitInfo.pSignalSemaphores = signalOnCompletionSemaphores;

		VkTimelineSemaphoreSubmitInfo timelineSemaphoreSubmitInfo;
		if (_factory->GetXLEFeatures()._timelineSemaphore) {
			timelineSemaphoreSubmitInfo = {};
			timelineSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
			timelineSemaphoreSubmitInfo.pNext = nullptr;
			timelineSemaphoreSubmitInfo.waitSemaphoreValueCount = waitBeforeBeginCount;
			timelineSemaphoreSubmitInfo.pWaitSemaphoreValues = waitBeforeBeginValues;
			timelineSemaphoreSubmitInfo.signalSemaphoreValueCount = signalOnCompletionCount;
			timelineSemaphoreSubmitInfo.pSignalSemaphoreValues = signalOnCompletionValues;
			submitInfo.pNext = &timelineSemaphoreSubmitInfo;
		}

		submitInfo.commandBufferCount = cmdLists.size();
		submitInfo.pCommandBuffers = rawCmdBuffers;

		#if defined(SUBMISSION_LOG_SPAM)
			Log(Verbose) << "[q] Submitting " << submitInfo.commandBufferCount << " cmd buffers" << std::endl;
			for (unsigned c=0; c<waitBeforeBeginCount; ++c)
				Log(Verbose) << "[q]  wait on 0x" << std::hex << waitBeforeBeginSemaphores[c] << std::dec << " for value " << waitBeforeBeginValues[c] << std::endl;
			for (unsigned c=0; c<waitBeforeBeginCount; ++c)
				Log(Verbose) << "[q]  signal 0x" << std::hex << signalOnCompletionSemaphores[c] << std::dec << " for value " << signalOnCompletionValues[c] << std::endl;
		#endif

		auto res = vkQueueSubmit(_underlying, 1, &submitInfo, nullptr);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while queuing command list"));

		_maxInorderActuallySubmitted = std::max(_maxInorderActuallySubmitted, (uint64_t)trackerSubmitInfo._maxInorderMarker);
		if (!asyncTrackerMarkers.empty())
			_maxOutOfOrderActuallySubmitted = std::max(_maxOutOfOrderActuallySubmitted, (uint64_t)asyncTrackerMarkers.back());
	}

	void SubmissionQueue::WaitForFence(IAsyncTracker::Marker marker, std::optional<std::chrono::nanoseconds> timeout)
	{
		_gpuTracker->WaitForSpecificMarker(marker, timeout);
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
	, _maxInorderActuallySubmitted(0)
	, _maxOutOfOrderActuallySubmitted(0)
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

