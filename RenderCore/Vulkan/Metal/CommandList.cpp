// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommandList.h"
#include "AsyncTracker.h"
#include "IncludeVulkan.h"
#include <assert.h>

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

	void CommandList::RequireResourceVisbility(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			uint64_t resourceGuids[resourceGuidsInit.size()];
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);

				// Don't record the guid for any resources that are already marked as becoming visible 
				// during this command list (this is the only way we can check relative ordering of 
				// initialization and use within the same command list)
			size_t mustBeVisibleInitialSize = _resourcesThatMustBeVisible.size();
			auto becomingI = _resourcesBecomingVisible.begin();
			_resourcesThatMustBeVisible.reserve(_resourcesBecomingVisible.size() + resourceGuidsInit.size());
			auto mustBeVisibleI = resourceGuids;
			while (mustBeVisibleI != &resourceGuids[resourceGuidsInit.size()]) {
				while (becomingI != _resourcesBecomingVisible.end() && *becomingI < *mustBeVisibleI) ++becomingI;
				if (becomingI == _resourcesBecomingVisible.end() || *becomingI != *mustBeVisibleI)
					_resourcesThatMustBeVisible.push_back(*mustBeVisibleI);		// we sort using std::inplace_merge just below
				++mustBeVisibleI;
			}
			std::inplace_merge(_resourcesThatMustBeVisible.begin(), _resourcesThatMustBeVisible.begin() + mustBeVisibleInitialSize, _resourcesThatMustBeVisible.end());
		#endif
	}

	void CommandList::MakeResourcesVisible(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			uint64_t resourceGuids[resourceGuidsInit.size()];
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);

			auto mid = _resourcesBecomingVisible.insert(_resourcesBecomingVisible.end(), resourceGuids, &resourceGuids[resourceGuidsInit.size()]);
			std::inplace_merge(_resourcesBecomingVisible.begin(), mid, _resourcesBecomingVisible.end());
		#endif
	}

	auto CommandList::OnSubmitToQueue() -> SubmissionResult
	{
		assert(!_asyncTrackerMarkers.empty());
		_attachedStorage.OnSubmitToQueue(_asyncTrackerMarkers[0]);
		assert(!_asyncTrackerMarkers.empty());
		std::sort(_asyncTrackerMarkers.begin(), _asyncTrackerMarkers.end());
		auto fence = checked_cast<FenceBasedTracker*>(_asyncTracker.get())->OnSubmitToQueue(_asyncTrackerMarkers);

		SubmissionResult result;
		result._cmdBuffer = std::move(_underlying);
		result._asyncTrackerMarkers = std::move(_asyncTrackerMarkers);
		result._fence = fence;
		_asyncTrackerMarkers.clear();
		_asyncTracker = nullptr;
		return result;
	}

	CommandList::CommandList(CommandList&&) = default;
	
	CommandList& CommandList::operator=(CommandList&& moveFrom)
	{
		if (&moveFrom != this) {
			_attachedStorage.AbandonAllocations();
			if (_asyncTracker && !_asyncTrackerMarkers.empty()) {
				for (auto m:_asyncTrackerMarkers)
					checked_cast<FenceBasedTracker*>(_asyncTracker.get())->AbandonMarker(m);
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
		return *this;
	}

	CommandList::CommandList(
		VulkanSharedPtr<VkCommandBuffer> underlying,
		std::shared_ptr<IAsyncTracker> asyncTracker)
	: _underlying(std::move(underlying)) 
	, _asyncTracker(std::move(asyncTracker))
	{
		auto marker = checked_cast<FenceBasedTracker*>(_asyncTracker.get())->IncrementProducerFrame();
		assert(marker != IAsyncTracker::Marker_Invalid);
		_asyncTrackerMarkers.push_back(marker);
	}

	CommandList::~CommandList() 
	{
		_attachedStorage.AbandonAllocations();
		if (_asyncTracker && !_asyncTrackerMarkers.empty()) {
			for (auto m:_asyncTrackerMarkers)
				checked_cast<FenceBasedTracker*>(_asyncTracker.get())->AbandonMarker(m);
		} else {
			assert(!_asyncTracker && _asyncTrackerMarkers.empty());     // should have neither or both
		}
	}

	CommandList::CommandList() {}
}}

