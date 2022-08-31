// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "CmdListAttachedStorage.h"
#include "ObjectFactory.h"
#include "IncludeVulkan.h"
#include <vector>

namespace RenderCore { namespace Metal_Vulkan
{
	class CommandList
	{
	public:
		// --------------- Vulkan specific interface --------------- 
		void UpdateBuffer(
			VkBuffer buffer, VkDeviceSize offset, 
			VkDeviceSize byteCount, const void* data);
		void BindDescriptorSets(
			VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout layout,
			uint32_t firstSet,
			uint32_t descriptorSetCount,
			const VkDescriptorSet* pDescriptorSets,
			uint32_t dynamicOffsetCount,
			const uint32_t* pDynamicOffsets);
		void CopyBuffer(
			VkBuffer srcBuffer,
			VkBuffer dstBuffer,
			uint32_t regionCount,
			const VkBufferCopy* pRegions);
		void CopyImage(
			VkImage srcImage,
			VkImageLayout srcImageLayout,
			VkImage dstImage,
			VkImageLayout dstImageLayout,
			uint32_t regionCount,
			const VkImageCopy* pRegions);
		void CopyBufferToImage(
			VkBuffer srcBuffer,
			VkImage dstImage,
			VkImageLayout dstImageLayout,
			uint32_t regionCount,
			const VkBufferImageCopy* pRegions);
		void CopyImageToBuffer(
			VkImage srcImage,
			VkImageLayout srcImageLayout,
			VkBuffer dstBuffer,
			uint32_t regionCount,
			const VkBufferImageCopy* pRegions);
		void ClearColorImage(
			VkImage image,
			VkImageLayout imageLayout,
			const VkClearColorValue* pColor,
			uint32_t rangeCount,
			const VkImageSubresourceRange* pRanges);
		void ClearDepthStencilImage(
			VkImage image,
			VkImageLayout imageLayout,
			const VkClearDepthStencilValue* pDepthStencil,
			uint32_t rangeCount,
			const VkImageSubresourceRange* pRanges);
		void PipelineBarrier(
			VkPipelineStageFlags            srcStageMask,
			VkPipelineStageFlags            dstStageMask,
			VkDependencyFlags               dependencyFlags,
			uint32_t                        memoryBarrierCount,
			const VkMemoryBarrier*          pMemoryBarriers,
			uint32_t                        bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier*    pBufferMemoryBarriers,
			uint32_t                        imageMemoryBarrierCount,
			const VkImageMemoryBarrier*     pImageMemoryBarriers);
		void PushConstants(
			VkPipelineLayout layout,
			VkShaderStageFlags stageFlags,
			uint32_t offset,
			uint32_t size,
			const void* pValues);
		void WriteTimestamp(
			VkPipelineStageFlagBits pipelineStage, 
			VkQueryPool queryPool, uint32_t query);
		void BeginQuery(VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags = 0);
		void EndQuery(VkQueryPool queryPool, uint32_t query);
		void ResetQueryPool(
			VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
		void SetEvent(VkEvent evnt, VkPipelineStageFlags stageMask);
		void ResetEvent(VkEvent evnt, VkPipelineStageFlags stageMask);

		const VulkanSharedPtr<VkCommandBuffer>& GetUnderlying() const { return _underlying; }
		CmdListAttachedStorage& GetCmdListAttachedStorage() { return _attachedStorage; }

		struct SubmissionResult
		{
			VkFence _fence;
			VulkanSharedPtr<VkCommandBuffer> _cmdBuffer;
			std::vector<IAsyncTracker::Marker> _asyncTrackerMarkers;
		};
		SubmissionResult OnSubmitToQueue();
		IAsyncTracker& GetAsyncTracker() { return *_asyncTracker; }
		IAsyncTracker::Marker GetPrimaryTrackerMarker() const;

		void RequireResourceVisbility(IteratorRange<const uint64_t*> resourceGuids);
		void RequireResourceVisbilityAlreadySorted(IteratorRange<const uint64_t*> resourceGuids);
		void MakeResourcesVisible(IteratorRange<const uint64_t*> resourceGuids);
		void ValidateCommitToQueue(ObjectFactory& factory);

		void ExecuteSecondaryCommandList(CommandList&& cmdList);

		CommandList();
		CommandList(
			VulkanSharedPtr<VkCommandBuffer> underlying,
			std::shared_ptr<IAsyncTracker> asyncTracker);
		~CommandList();
		CommandList(CommandList&&);
		CommandList& operator=(CommandList&&);
		
	private:
		VulkanSharedPtr<VkCommandBuffer> _underlying;

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			std::vector<uint64_t> _resourcesBecomingVisible;
			std::vector<uint64_t> _resourcesThatMustBeVisible;
		#endif

		CmdListAttachedStorage _attachedStorage;
		std::shared_ptr<IAsyncTracker> _asyncTracker;
		std::vector<IAsyncTracker::Marker> _asyncTrackerMarkers;

		friend class DeviceContext;
		friend class SharedEncoder;
	};
}}

