// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../IDevice_Forward.h"
#include "../../../Utility/IteratorUtils.h"
#include "../../../Core/Types.h"
#include <memory>
#include "IncludeVulkan.h"
#include "Foreign/VulkanMemoryAllocator/include/vk_mem_alloc.h"

namespace RenderCore { namespace Metal_Vulkan
{
	class DeviceContext;
    class ExtensionFunctions;

	class IAsyncTracker
	{
	public:
		using Marker = unsigned;
		static const Marker Marker_Invalid = ~0u;
        static const Marker Marker_FrameContainsNoData = ~0u - 1u;

		virtual Marker GetConsumerMarker() const = 0;
		virtual Marker GetProducerMarker() const = 0;
	};

    class IDestructionQueue
    {
    public:
        virtual void    Destroy(VkCommandPool) = 0;
        virtual void    Destroy(VkSemaphore) = 0;
		virtual void    Destroy(VkEvent) = 0;
        virtual void    Destroy(VkDeviceMemory) = 0;
        virtual void    Destroy(VkRenderPass) = 0;
        virtual void    Destroy(VkImage) = 0;
        virtual void    Destroy(VkImageView) = 0;
        virtual void    Destroy(VkBufferView) = 0;
        virtual void    Destroy(VkFramebuffer) = 0;
        virtual void    Destroy(VkShaderModule) = 0;
        virtual void    Destroy(VkDescriptorSetLayout) = 0;
        virtual void    Destroy(VkDescriptorPool) = 0;
        virtual void    Destroy(VkPipeline) = 0;
        virtual void    Destroy(VkPipelineCache) = 0;
        virtual void    Destroy(VkPipelineLayout) = 0;
        virtual void    Destroy(VkBuffer) = 0;
        virtual void    Destroy(VkFence) = 0;
        virtual void    Destroy(VkSampler) = 0;
		virtual void	Destroy(VkQueryPool) = 0;
        virtual void	Destroy(VkImage, VmaAllocation) = 0;
        virtual void	Destroy(VkBuffer, VmaAllocation) = 0;

		struct FlushFlags
		{
			enum { DestroyAll = 1<<1 };
			using BitField = unsigned;
		};
        virtual void    Flush(FlushFlags::BitField = 0) = 0;
        virtual ~IDestructionQueue();
    };

	class ObjectFactory
	{
	public:
        // main resources
        VulkanUniquePtr<VkImage> CreateImage(
            const VkImageCreateInfo& createInfo,
            uint64_t guidForVisibilityTracking = 0ull) const;
        VulkanUniquePtr<VkBuffer> CreateBuffer(const VkBufferCreateInfo& createInfo) const;
        VulkanUniquePtr<VkSampler> CreateSampler(const VkSamplerCreateInfo& createInfo) const;
        VulkanUniquePtr<VkFramebuffer> CreateFramebuffer(const VkFramebufferCreateInfo& createInfo) const;
        VulkanUniquePtr<VkRenderPass> CreateRenderPass(const VkRenderPassCreateInfo2& createInfo) const;

        VulkanUniquePtr<VkBuffer> CreateBufferWithAutoMemory(const VkBufferCreateInfo& createInfo, VmaAllocation& allocationResult) const;
        VulkanUniquePtr<VkImage> CreateImageWithAutoMemory(const VkImageCreateInfo& createInfo, VmaAllocation& allocationResult, uint64_t guidForVisibilityTracking = 0ull) const;

        // resource views
        VulkanUniquePtr<VkImageView> CreateImageView(const VkImageViewCreateInfo& createInfo) const;
        VulkanUniquePtr<VkBufferView> CreateBufferView(const VkBufferViewCreateInfo& createInfo) const;

        // pipelines / shaders
        VulkanUniquePtr<VkShaderModule> CreateShaderModule(
            IteratorRange<const void*> byteCode,
            VkShaderModuleCreateFlags flags = 0) const;

        VulkanUniquePtr<VkPipeline> CreateGraphicsPipeline(
            VkPipelineCache pipelineCache,
            const VkGraphicsPipelineCreateInfo& createInfo) const;

        VulkanUniquePtr<VkPipeline> CreateComputePipeline(
            VkPipelineCache pipelineCache,
            const VkComputePipelineCreateInfo& createInfo) const;

        VulkanUniquePtr<VkPipelineCache> CreatePipelineCache(
            const void* initialData = nullptr, size_t initialDataSize = 0,
            VkPipelineCacheCreateFlags flags = 0) const;

        VulkanUniquePtr<VkPipelineLayout> CreatePipelineLayout(
            IteratorRange<const VkDescriptorSetLayout*> setLayouts,
            IteratorRange<const VkPushConstantRange*> pushConstants = IteratorRange<const VkPushConstantRange*>(),
            VkPipelineLayoutCreateFlags flags = 0) const;

        // desc sets
        VulkanUniquePtr<VkDescriptorSetLayout> CreateDescriptorSetLayout(
            IteratorRange<const VkDescriptorSetLayoutBinding*> bindings) const;

        VulkanUniquePtr<VkDescriptorPool> CreateDescriptorPool(
            const VkDescriptorPoolCreateInfo& createInfo) const;

        // misc
        VulkanUniquePtr<VkCommandPool> CreateCommandPool(
            unsigned queueFamilyIndex, VkCommandPoolCreateFlags flags = 0) const;

        // sync
        VulkanUniquePtr<VkFence> CreateFence(VkFenceCreateFlags flags = 0) const;
        VulkanUniquePtr<VkSemaphore> CreateSemaphore(VkSemaphoreCreateFlags flags = 0) const;
		VulkanUniquePtr<VkEvent> CreateEvent() const;
        VulkanUniquePtr<VkQueryPool> CreateQueryPool(
			VkQueryType_ type, unsigned count, 
			VkQueryPipelineStatisticFlags pipelineStats = 0) const;

        // memory direct from Vulkan
        VulkanUniquePtr<VkDeviceMemory> AllocateMemoryDirectFromVulkan(
            VkDeviceSize allocationSize, unsigned memoryTypeIndex) const;
		unsigned FindMemoryType(
            VkFlags memoryTypeBits, 
            VkMemoryPropertyFlags requirementsMask = 0) const;

        // device & capabilities query
        VkPhysicalDevice GetPhysicalDevice() const { return _physDev; }
		const VulkanSharedPtr<VkDevice>& GetDevice() const { return _device; }
		VkFormatProperties GetFormatProperties(VkFormat_ fmt) const;
        const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return *_physDevProperties; }
        const VkPhysicalDeviceFeatures& GetPhysicalDeviceFeatures() const { return *_physDevFeatures; }
        ExtensionFunctions& GetExtensionFunctions() { return *_extensionFunctions; }

		std::shared_ptr<IDestructionQueue> CreateMarkerTrackingDestroyer(const std::shared_ptr<IAsyncTracker>&);
		void SetDefaultDestroyer(const std::shared_ptr<IDestructionQueue>&);
        VmaAllocator GetVMAAllocator() { return _vmaAllocator; }

        #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
            void ForgetResource(uint64_t resourceGuid) const;
            mutable std::vector<uint64_t> _resourcesVisibleToQueue;
        #endif

		ObjectFactory(
            VkInstance instance,
            VkPhysicalDevice physDev, VulkanSharedPtr<VkDevice> device, 
            std::shared_ptr<ExtensionFunctions> extensionFunctions);
		ObjectFactory();
		~ObjectFactory();

        ObjectFactory(const ObjectFactory&) = delete;
		ObjectFactory& operator=(const ObjectFactory&) = delete;

        ObjectFactory(ObjectFactory&&) never_throws;
		ObjectFactory& operator=(ObjectFactory&&) never_throws;

	private:
        VkPhysicalDevice            _physDev;
		VulkanSharedPtr<VkDevice>   _device;
        VmaAllocator _vmaAllocator;
        
		std::shared_ptr<IDestructionQueue> _immediateDestruction; 
		std::shared_ptr<IDestructionQueue> _destruction;

        std::unique_ptr<VkPhysicalDeviceMemoryProperties> _memProps;
        std::unique_ptr<VkPhysicalDeviceProperties> _physDevProperties;
        std::unique_ptr<VkPhysicalDeviceFeatures> _physDevFeatures;
        std::shared_ptr<ExtensionFunctions> _extensionFunctions;

        #if defined(_DEBUG)
            std::vector<std::weak_ptr<IDestructionQueue>> _associatedDestructionQueues;
        #endif
	};

	ObjectFactory& GetObjectFactory(IDevice& device);
	ObjectFactory& GetObjectFactory(DeviceContext&);
	ObjectFactory& GetObjectFactory(IResource&);
	ObjectFactory& GetObjectFactory();

    extern const VkAllocationCallbacks* g_allocationCallbacks;
}}
