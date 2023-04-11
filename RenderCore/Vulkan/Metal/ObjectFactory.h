// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../IDevice_Forward.h"
#include "../../DeviceInitialization.h"     // for DeviceFeatures
#include "../../ResourceDesc.h"     // for AllocationRules
#include "../../../Utility/IteratorUtils.h"
#include "../../../Core/Types.h"
#include <memory>
#include "IncludeVulkan.h"
#include "Foreign/VulkanMemoryAllocator/include/vk_mem_alloc.h"

#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
    #include "../../../Utility/Threading/Mutex.h"
#endif

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

        enum class MarkerStatus { Unknown, NotSubmitted, ConsumerPending, ConsumerCompleted, Abandoned };
        virtual MarkerStatus GetSpecificMarkerStatus(Marker) const = 0;

        virtual ~IAsyncTracker();
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
			enum { DestroyAll = 1<<0, ReleaseTracker = 1<<1 };
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

        VulkanUniquePtr<VkBuffer> CreateBufferWithAutoMemory(
            /* out */ VmaAllocation& allocationResult,
            /* out */ VmaAllocationInfo& allocInfoResult,
            const VkBufferCreateInfo& createInfo, AllocationRules::BitField allocationRules) const;
        VulkanUniquePtr<VkImage> CreateImageWithAutoMemory(
            /* out */ VmaAllocation& allocationResult,
            /* out */ VmaAllocationInfo& allocInfoResult,
            const VkImageCreateInfo& createInfo, AllocationRules::BitField allocationRules, 
            uint64_t guidForVisibilityTracking = 0ull) const;

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
        VulkanUniquePtr<VkSemaphore> CreateTimelineSemaphore(uint64_t initialValue = 0) const;
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
        const VkMemoryType* GetMemoryTypeInfo(unsigned memoryType) const;

        // device & capabilities query
        VkPhysicalDevice GetPhysicalDevice() const { return _physDev; }
		const VulkanSharedPtr<VkDevice>& GetDevice() const { return _device; }
		VkFormatProperties GetFormatProperties(VkFormat_ fmt) const;
        const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const { return *_physDevProperties; }
        const VkPhysicalDeviceFeatures& GetPhysicalDeviceFeatures() const { return *_physDevFeatures; }
        ExtensionFunctions& GetExtensionFunctions() { return *_extensionFunctions; }
        const DeviceFeatures& GetXLEFeatures() const { return _xleFeatures; }

		std::shared_ptr<IDestructionQueue> CreateMarkerTrackingDestroyer(const std::shared_ptr<IAsyncTracker>&);
		void SetDefaultDestroyer(const std::shared_ptr<IDestructionQueue>&);
        VmaAllocator GetVmaAllocator() { return _vmaAllocator; }

        #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
            void ForgetResource(uint64_t resourceGuid) const;
            void UpdateForgottenResourcesAlreadyLocked();
            mutable std::vector<uint64_t> _resourcesVisibleToQueue;
            mutable std::vector<uint64_t> _invalidatedResources;
            mutable Threading::Mutex _resourcesVisibleToQueueLock;
        #endif

        unsigned _graphicsQueueFamily = ~0u;
        unsigned _dedicatedTransferQueueFamily = ~0u;
        unsigned _dedicatedComputeQueueFamily = ~0u;

		ObjectFactory(
            VkInstance instance,
            VkPhysicalDevice physDev, VulkanSharedPtr<VkDevice> device,
            const DeviceFeatures& xleFeatures,
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
        DeviceFeatures _xleFeatures;

        #if defined(_DEBUG)
            std::vector<std::weak_ptr<IDestructionQueue>> _associatedDestructionQueues;
        #endif

        #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
            struct ResourceVisibilityHelper;
            std::unique_ptr<ResourceVisibilityHelper> _resourceVisibilityHelper;
        #endif
	};

	ObjectFactory& GetObjectFactory(IDevice& device);
	ObjectFactory& GetObjectFactory(DeviceContext&);
	ObjectFactory& GetObjectFactory(IResource&);
	ObjectFactory& GetObjectFactory();

    extern const VkAllocationCallbacks* g_allocationCallbacks;
}}
