// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectFactory.h"
#include "Resource.h"
#include "ExtensionFunctions.h"
#include "IncludeVulkan.h"
#include "../../../OSServices/Log.h"
#include "../../../Core/Prefix.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/HeapUtils.h"
#include <queue>
#include <deque>

namespace RenderCore { namespace Metal_Vulkan
{
    const VkAllocationCallbacks* g_allocationCallbacks = nullptr;

	static std::shared_ptr<IDestructionQueue> CreateImmediateDestroyer(VulkanSharedPtr<VkDevice> device, VmaAllocator vmaAllocator);

    VulkanUniquePtr<VkCommandPool> ObjectFactory::CreateCommandPool(
        unsigned queueFamilyIndex, VkCommandPoolCreateFlags flags) const
    {
        VkCommandPoolCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		createInfo.pNext = nullptr;
		createInfo.queueFamilyIndex = queueFamilyIndex;
		createInfo.flags = flags;

        auto d = _destruction.get();
		VkCommandPool rawPool = nullptr;
		auto res = vkCreateCommandPool(_device.get(), &createInfo, g_allocationCallbacks, &rawPool);
		auto pool = VulkanUniquePtr<VkCommandPool>(
			rawPool,
			[d](VkCommandPool pool) { d->Destroy(pool); });
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while creating command pool"));
        return std::move(pool);
    }

    VulkanUniquePtr<VkSemaphore> ObjectFactory::CreateSemaphore(
        VkSemaphoreCreateFlags flags) const
    {
        VkSemaphoreCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = flags;

        auto d = _destruction.get();
        VkSemaphore rawPtr = nullptr;
        auto res = vkCreateSemaphore(
            _device.get(), &createInfo,
            g_allocationCallbacks, &rawPtr);
        VulkanUniquePtr<VkSemaphore> result(
            rawPtr,
            [d](VkSemaphore sem) { d->Destroy(sem); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failure while creating Vulkan semaphore"));
        return std::move(result);
    }

	VulkanUniquePtr<VkEvent> ObjectFactory::CreateEvent() const
	{
		VkEventCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
		createInfo.pNext = nullptr;
		createInfo.flags = 0;

		auto d = _destruction.get();
		VkEvent rawPtr = nullptr;
		auto res = vkCreateEvent(
			_device.get(), &createInfo,
			g_allocationCallbacks, &rawPtr);
		VulkanUniquePtr<VkEvent> result(
			rawPtr,
			[d](VkEvent sem) { d->Destroy(sem); });
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while creating Vulkan event"));
		return std::move(result);
	}

    VulkanUniquePtr<VkDeviceMemory> ObjectFactory::AllocateMemoryDirectFromVulkan(
        VkDeviceSize allocationSize, unsigned memoryTypeIndex) const
    {
        VkMemoryAllocateInfo mem_alloc = {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.pNext = nullptr;
        mem_alloc.allocationSize = allocationSize;
        mem_alloc.memoryTypeIndex = memoryTypeIndex;

        auto d = _destruction.get();
        VkDeviceMemory rawMem = nullptr;
        auto res = vkAllocateMemory(_device.get(), &mem_alloc, g_allocationCallbacks, &rawMem);
        auto mem = VulkanUniquePtr<VkDeviceMemory>(
            rawMem,
            [d](VkDeviceMemory mem) { d->Destroy(mem); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while allocating device memory for image"));

        return std::move(mem);
    }

    VulkanUniquePtr<VkRenderPass> ObjectFactory::CreateRenderPass(
        const VkRenderPassCreateInfo2& createInfo) const
    {
        auto d = _destruction.get();
        VkRenderPass rawPtr = nullptr;
        auto res = vkCreateRenderPass2(_device.get(), &createInfo, g_allocationCallbacks, &rawPtr);
        auto renderPass = VulkanUniquePtr<VkRenderPass>(
            rawPtr,
            [d](VkRenderPass pass) { d->Destroy(pass); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failure while creating render pass"));
        return std::move(renderPass);
    }

    VulkanUniquePtr<VkImage> ObjectFactory::CreateImage(
        const VkImageCreateInfo& createInfo,
        uint64_t guidForVisibilityTracking) const
    {
        auto d = _destruction.get();
        VkImage rawImage = nullptr;
        auto res = vkCreateImage(_device.get(), &createInfo, g_allocationCallbacks, &rawImage);
        VulkanUniquePtr<VkImage> image;
        #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
            if (guidForVisibilityTracking) {
                image = VulkanUniquePtr<VkImage>(
                    rawImage,
                    [d, guidForVisibilityTracking, this](VkImage image) { d->Destroy(image); this->ForgetResource(guidForVisibilityTracking); });
            } else
        #endif
        {
            image = VulkanUniquePtr<VkImage>(
                rawImage,
                [d](VkImage image) { d->Destroy(image); });
        }
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating image"));
        return std::move(image);
    }

    VulkanUniquePtr<VkImageView> ObjectFactory::CreateImageView(
        const VkImageViewCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkImageView viewRaw = nullptr;
        auto result = vkCreateImageView(_device.get(), &createInfo, g_allocationCallbacks, &viewRaw);
        auto imageView = VulkanUniquePtr<VkImageView>(
            viewRaw,
            [d](VkImageView view) { d->Destroy(view); });
        if (result != VK_SUCCESS)
            Throw(VulkanAPIFailure(result, "Failed while creating image view of resource"));
        return std::move(imageView);
    }

    VulkanUniquePtr<VkBufferView> ObjectFactory::CreateBufferView(
        const VkBufferViewCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkBufferView viewRaw = nullptr;
        auto result = vkCreateBufferView(_device.get(), &createInfo, g_allocationCallbacks, &viewRaw);
        auto bufferView = VulkanUniquePtr<VkBufferView>(
            viewRaw,
            [d](VkBufferView view) { d->Destroy(view); });
        if (result != VK_SUCCESS)
            Throw(VulkanAPIFailure(result, "Failed while creating buffer view of resource"));
        return std::move(bufferView);
    }

    VulkanUniquePtr<VkFramebuffer> ObjectFactory::CreateFramebuffer(
        const VkFramebufferCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkFramebuffer rawFB = nullptr;
        auto res = vkCreateFramebuffer(_device.get(), &createInfo, g_allocationCallbacks, &rawFB);
        auto framebuffer = VulkanUniquePtr<VkFramebuffer>(
            rawFB,
            [d](VkFramebuffer fb) { d->Destroy(fb); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while allocating frame buffer"));
        return std::move(framebuffer);
    }

    VulkanUniquePtr<VkShaderModule> ObjectFactory::CreateShaderModule(
        IteratorRange<const void*> byteCode,
        VkShaderModuleCreateFlags flags) const
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = flags;
        createInfo.codeSize = byteCode.size();
        createInfo.pCode = (const uint32_t*)byteCode.begin();

        auto d = _destruction.get();
        VkShaderModule rawShader = nullptr;
        auto res = vkCreateShaderModule(_device.get(), &createInfo, g_allocationCallbacks, &rawShader);
        auto shader = VulkanUniquePtr<VkShaderModule>(
            rawShader,
            [d](VkShaderModule shdr) { d->Destroy(shdr); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating shader module"));
        return std::move(shader);
    }

    VulkanUniquePtr<VkDescriptorSetLayout> ObjectFactory::CreateDescriptorSetLayout(
        IteratorRange<const VkDescriptorSetLayoutBinding*> bindings) const
    {
        VkDescriptorSetLayoutCreateInfo createInfo = {};
        createInfo.sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.bindingCount = (uint32)bindings.size();
        createInfo.pBindings = bindings.begin();

        auto d = _destruction.get();
        VkDescriptorSetLayout rawLayout = nullptr;
        auto res = vkCreateDescriptorSetLayout(_device.get(), &createInfo, g_allocationCallbacks, &rawLayout);
        auto shader = VulkanUniquePtr<VkDescriptorSetLayout>(
            rawLayout,
            [d](VkDescriptorSetLayout layout) { d->Destroy(layout); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating descriptor set layout"));
        return std::move(shader);
    }

    VulkanUniquePtr<VkDescriptorPool> ObjectFactory::CreateDescriptorPool(
        const VkDescriptorPoolCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkDescriptorPool rawPool = nullptr;
        auto res = vkCreateDescriptorPool(_device.get(), &createInfo, g_allocationCallbacks, &rawPool);
        auto pool = VulkanUniquePtr<VkDescriptorPool>(
            rawPool,
            [d](VkDescriptorPool pool) { d->Destroy(pool); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating descriptor pool"));
        return std::move(pool);
    }

    VulkanUniquePtr<VkPipeline> ObjectFactory::CreateGraphicsPipeline(
        VkPipelineCache pipelineCache,
        const VkGraphicsPipelineCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkPipeline rawPipeline = nullptr;
        auto res = vkCreateGraphicsPipelines(_device.get(), pipelineCache, 1, &createInfo, g_allocationCallbacks, &rawPipeline);
        auto pipeline = VulkanUniquePtr<VkPipeline>(
            rawPipeline,
            [d](VkPipeline pipeline) { d->Destroy(pipeline); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating graphics pipeline"));
        return std::move(pipeline);
    }

    VulkanUniquePtr<VkPipeline> ObjectFactory::CreateComputePipeline(
        VkPipelineCache pipelineCache,
        const VkComputePipelineCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkPipeline rawPipeline = nullptr;
        auto res = vkCreateComputePipelines(_device.get(), pipelineCache, 1, &createInfo, g_allocationCallbacks, &rawPipeline);
        auto pipeline = VulkanUniquePtr<VkPipeline>(
            rawPipeline,
            [d](VkPipeline pipeline) { d->Destroy(pipeline); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating compute pipeline"));
        return std::move(pipeline);
    }

    VulkanUniquePtr<VkPipelineCache> ObjectFactory::CreatePipelineCache(
        const void* initialData, size_t initialDataSize,
        VkPipelineCacheCreateFlags flags) const
    {
        VkPipelineCacheCreateInfo createInfo;
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.initialDataSize = initialDataSize;
        createInfo.pInitialData = initialData;
        createInfo.flags = flags;

        auto d = _destruction.get();
        VkPipelineCache rawCache = nullptr;
        auto res = vkCreatePipelineCache(_device.get(), &createInfo, g_allocationCallbacks, &rawCache);
        auto cache = VulkanUniquePtr<VkPipelineCache>(
            rawCache,
            [d](VkPipelineCache cache) { d->Destroy(cache); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating descriptor set layout"));
        return std::move(cache);
    }

    VulkanUniquePtr<VkPipelineLayout> ObjectFactory::CreatePipelineLayout(
        IteratorRange<const VkDescriptorSetLayout*> setLayouts,
        IteratorRange<const VkPushConstantRange*> pushConstants,
        VkPipelineLayoutCreateFlags flags) const
    {
        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
        pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pPipelineLayoutCreateInfo.pNext = nullptr;
        pPipelineLayoutCreateInfo.flags = flags;
        pPipelineLayoutCreateInfo.setLayoutCount = (uint32_t)setLayouts.size();
        pPipelineLayoutCreateInfo.pSetLayouts = setLayouts.begin();
        pPipelineLayoutCreateInfo.pushConstantRangeCount = (uint32_t)pushConstants.size();
        pPipelineLayoutCreateInfo.pPushConstantRanges = pushConstants.begin();

        auto d = _destruction.get();
        VkPipelineLayout rawPipelineLayout = nullptr;
        auto res = vkCreatePipelineLayout(_device.get(), &pPipelineLayoutCreateInfo, g_allocationCallbacks, &rawPipelineLayout);
        auto pipelineLayout = VulkanUniquePtr<VkPipelineLayout>(
            rawPipelineLayout,
            [d](VkPipelineLayout layout) { d->Destroy(layout); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating descriptor set layout"));
        return std::move(pipelineLayout);
    }

    VulkanUniquePtr<VkBuffer> ObjectFactory::CreateBuffer(const VkBufferCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkBuffer rawBuffer = nullptr;
        auto res = vkCreateBuffer(_device.get(), &createInfo, g_allocationCallbacks, &rawBuffer);
        auto buffer = VulkanUniquePtr<VkBuffer>(
            rawBuffer,
            [d](VkBuffer buffer) { d->Destroy(buffer); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating buffer"));
        return std::move(buffer);
    }

    static VmaAllocationCreateInfo SetupAllocationCreateInfo(AllocationRules::BitField allocationRules)
    {
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.flags = 0;

		if (allocationRules & AllocationRules::HostVisibleRandomAccess) allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		else if (allocationRules & AllocationRules::HostVisibleSequentialWrite) allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        if ((allocCreateInfo.flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) || (allocCreateInfo.flags & VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT)) {
            allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            if (!(allocationRules & AllocationRules::DisableAutoCacheCoherency))
                allocCreateInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        if (allocationRules & AllocationRules::PermanentlyMapped)
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        if (allocationRules & AllocationRules::DedicatedPage)
            allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        return allocCreateInfo;
    }

    VulkanUniquePtr<VkBuffer> ObjectFactory::CreateBufferWithAutoMemory(
        /* out */ VmaAllocation& allocationResult,
        /* out */ VmaAllocationInfo& allocInfoResult,
        const VkBufferCreateInfo& createInfo, AllocationRules::BitField allocationRules) const
    {
        // Using vma, create a buffer and automatically allocate the memory
        // doesn't seem to be any particular benefit to separating buffer & memory allocation with this library, so let's
        // go ahead and simplify it
        auto allocCreateInfo = SetupAllocationCreateInfo(allocationRules);
        VkBuffer rawBuffer = nullptr;
        auto res = vmaCreateBuffer(_vmaAllocator, &createInfo, &allocCreateInfo, &rawBuffer, &allocationResult, &allocInfoResult);
        auto* d = (allocationRules & AllocationRules::DisableSafeDestruction) ? _immediateDestruction.get() : _destruction.get();
        auto buffer = VulkanUniquePtr<VkBuffer>(
            rawBuffer,
            [d, allocationResult](VkBuffer buffer)
            {
                d->Destroy(buffer, allocationResult); 
            });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating buffer"));
        return std::move(buffer);
    }

    VulkanUniquePtr<VkImage> ObjectFactory::CreateImageWithAutoMemory(
        /* out */ VmaAllocation& allocationResult,
        /* out */ VmaAllocationInfo& allocInfoResult,
        const VkImageCreateInfo& createInfo, AllocationRules::BitField allocationRules, uint64_t guidForVisibilityTracking) const
    {
        auto allocCreateInfo = SetupAllocationCreateInfo(allocationRules);
        VkImage rawImage = nullptr;
        auto res = vmaCreateImage(_vmaAllocator, &createInfo, &allocCreateInfo, &rawImage, &allocationResult, &allocInfoResult);
        VulkanUniquePtr<VkImage> image;
        auto* d = (allocationRules & AllocationRules::DisableSafeDestruction) ? _immediateDestruction.get() : _destruction.get();
        #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
            if (guidForVisibilityTracking) {
                image = VulkanUniquePtr<VkImage>(
                    rawImage,
                    [d, allocationResult, guidForVisibilityTracking, this](VkImage image) { d->Destroy(image, allocationResult); this->ForgetResource(guidForVisibilityTracking); });
            } else
        #endif
        {
            image = VulkanUniquePtr<VkImage>(
                rawImage,
                [d, allocationResult](VkImage image) { d->Destroy(image, allocationResult); });
        }
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating image"));
        return std::move(image);
    }

    VulkanUniquePtr<VkFence> ObjectFactory::CreateFence(VkFenceCreateFlags flags) const
    {
        VkFenceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        createInfo.pNext = nullptr;
        createInfo.flags = flags;		// can onlt be 0 or VK_FENCE_CREATE_SIGNALED_BIT

        auto d = _destruction.get();
        VkFence rawFence = nullptr;
        auto res = vkCreateFence(_device.get(), &createInfo, g_allocationCallbacks, &rawFence);
        auto fence = VulkanUniquePtr<VkFence>(
            rawFence,
            [d](VkFence fence) { d->Destroy(fence); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating fence"));
        return std::move(fence);
    }

    VulkanUniquePtr<VkSampler> ObjectFactory::CreateSampler(const VkSamplerCreateInfo& createInfo) const
    {
        auto d = _destruction.get();
        VkSampler rawSampler = nullptr;
        auto res = vkCreateSampler(_device.get(), &createInfo, g_allocationCallbacks, &rawSampler);
        auto sampler = VulkanUniquePtr<VkSampler>(
            rawSampler,
            [d](VkSampler sampler) { d->Destroy(sampler); });
        if (res != VK_SUCCESS)
            Throw(VulkanAPIFailure(res, "Failed while creating sampler"));
        return std::move(sampler);
    }

	VulkanUniquePtr<VkQueryPool> ObjectFactory::CreateQueryPool(
		VkQueryType_ type, unsigned count,
		VkQueryPipelineStatisticFlags pipelineStats) const
	{
		VkQueryPoolCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		createInfo.pNext = nullptr;
		createInfo.flags = 0;
		createInfo.queryType = (VkQueryType)type;
		createInfo.queryCount = count;
		createInfo.pipelineStatistics = pipelineStats;

		auto d = _destruction.get();
		VkQueryPool rawQueryPool = nullptr;
		auto res = vkCreateQueryPool(_device.get(), &createInfo, g_allocationCallbacks, &rawQueryPool);
		auto queryPool = VulkanUniquePtr<VkQueryPool>(
			rawQueryPool,
			[d](VkQueryPool qp) { d->Destroy(qp); });
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failed while creating query pool"));
		return std::move(queryPool);
	}

    unsigned ObjectFactory::FindMemoryType(VkFlags memoryTypeBits, VkMemoryPropertyFlags requirementsMask) const
    {
        // Search memtypes to find first index with those properties
        for (uint32_t i=0; i<dimof(_memProps->memoryTypes); i++) {
            if ((memoryTypeBits & 1) == 1) {
                // Type is available, does it match user properties?
                if ((_memProps->memoryTypes[i].propertyFlags & requirementsMask) == requirementsMask)
                    return i;
            }
            memoryTypeBits >>= 1;
        }
        return ~0x0u;
    }

    const VkMemoryType* ObjectFactory::GetMemoryTypeInfo(unsigned memoryType) const
    {
        if (memoryType >= dimof(_memProps->memoryTypes))
            return nullptr;
        return &_memProps->memoryTypes[memoryType];
    }

	VkFormatProperties ObjectFactory::GetFormatProperties(VkFormat_ fmt) const
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(_physDev, (VkFormat)fmt, &formatProps);
		return formatProps;
	}

	void ObjectFactory::SetDefaultDestroyer(const std::shared_ptr<IDestructionQueue>& destruction)
	{
		_destruction = destruction;
	}

    #if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
        void ObjectFactory::ForgetResource(uint64_t resourceGuid) const
        {
            auto i = std::lower_bound(_resourcesVisibleToQueue.begin(), _resourcesVisibleToQueue.end(), resourceGuid);
            if (i != _resourcesVisibleToQueue.end() && *i == resourceGuid)
                _resourcesVisibleToQueue.erase(i);
        }
    #endif

    ObjectFactory::ObjectFactory(ObjectFactory&& moveFrom) never_throws
    : _physDev(std::move(moveFrom._physDev))
	, _device(std::move(moveFrom._device))
    , _destruction(std::move(moveFrom._destruction))
	, _immediateDestruction(std::move(moveFrom._immediateDestruction))
    , _memProps(std::move(moveFrom._memProps))
    , _physDevProperties(std::move(moveFrom._physDevProperties))
    , _physDevFeatures(std::move(moveFrom._physDevFeatures))
    , _extensionFunctions(std::move(moveFrom._extensionFunctions))
    , _vmaAllocator(std::move(moveFrom._vmaAllocator))
    {
        moveFrom._vmaAllocator = nullptr;
        #if defined(_DEBUG)
            _associatedDestructionQueues = std::move(moveFrom._associatedDestructionQueues);
        #endif
    }

	ObjectFactory& ObjectFactory::operator=(ObjectFactory&& moveFrom) never_throws
    {
        if (&moveFrom == this) return *this;

        #if defined(_DEBUG)
            // Ensure that all destruction queues created with CreateMarkerTrackingDestroyer() have been destroyed before coming here
            // This is important because they hold unprotected references to _vmaAllocator
            for (const auto& q:_associatedDestructionQueues)
                assert(q.expired());
        #endif
        if (_vmaAllocator)
            vmaDestroyAllocator(_vmaAllocator);

        _physDev = std::move(moveFrom._physDev);
		_device = std::move(moveFrom._device);
        _destruction = std::move(moveFrom._destruction);
		_immediateDestruction = std::move(moveFrom._immediateDestruction);
        _memProps = std::move(moveFrom._memProps);
        _physDevProperties = std::move(moveFrom._physDevProperties);
        _physDevFeatures = std::move(moveFrom._physDevFeatures);
        _extensionFunctions = std::move(moveFrom._extensionFunctions);
        _vmaAllocator = std::move(moveFrom._vmaAllocator); moveFrom._vmaAllocator = nullptr;
        #if defined(_DEBUG)
            _associatedDestructionQueues = std::move(moveFrom._associatedDestructionQueues);
        #endif
        return *this;
    }

    ObjectFactory::ObjectFactory(VkInstance instance,  VkPhysicalDevice physDev, VulkanSharedPtr<VkDevice> device, std::shared_ptr<ExtensionFunctions> extensionFunctions)
    : _physDev(physDev), _device(device), _extensionFunctions(extensionFunctions)
    {
        _memProps = std::make_unique<VkPhysicalDeviceMemoryProperties>(VkPhysicalDeviceMemoryProperties{});
        vkGetPhysicalDeviceMemoryProperties(physDev, _memProps.get());

        VkPhysicalDeviceProperties2 physDevProps2 = {};
        physDevProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        VkPhysicalDeviceMultiviewProperties multiViewProps = {};
        multiViewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;
        physDevProps2.pNext = &multiViewProps;
        vkGetPhysicalDeviceProperties2(physDev, &physDevProps2);

        VkPhysicalDeviceFeatures2 physDevFeatures2 = {};
        physDevFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceMultiviewFeatures multiViewFeatures = {};
        multiViewFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
        physDevFeatures2.pNext = &multiViewFeatures;
        vkGetPhysicalDeviceFeatures2(physDev, &physDevFeatures2);

        _physDevProperties = std::make_unique<VkPhysicalDeviceProperties>(VkPhysicalDeviceProperties{});
        *_physDevProperties = physDevProps2.properties;

        _physDevFeatures = std::make_unique<VkPhysicalDeviceFeatures>(VkPhysicalDeviceFeatures{});
        *_physDevFeatures = physDevFeatures2.features;

        // Create the VMA main instance object (_vmaAllocator)
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
        
        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.vulkanApiVersion = VK_HEADER_VERSION_COMPLETE;
        allocatorCreateInfo.physicalDevice = _physDev;
        allocatorCreateInfo.device = _device.get();
        allocatorCreateInfo.instance = instance;
        allocatorCreateInfo.pVulkanFunctions = &vulkanFunctions;
        
        auto allocatorCreationResult = vmaCreateAllocator(&allocatorCreateInfo, &_vmaAllocator);
        if (allocatorCreationResult != VK_SUCCESS)
            Throw(VulkanAPIFailure(allocatorCreationResult, "Failure while creating allocator instance"));

        // default destruction behaviour (should normally be overriden by the device later)
        _immediateDestruction = CreateImmediateDestroyer(_device, _vmaAllocator);
		_destruction = _immediateDestruction;
    }

	ObjectFactory::ObjectFactory() {}
	ObjectFactory::~ObjectFactory() 
    {
        _immediateDestruction.reset();
        _destruction.reset();
        #if defined(_DEBUG)
            // Ensure that all destruction queues created with CreateMarkerTrackingDestroyer() have been destroyed before coming here
            // This is important because they hold unprotected references to _vmaAllocator
            for (const auto& q:_associatedDestructionQueues)
                assert(q.expired());
        #endif
        if (_vmaAllocator)
            vmaDestroyAllocator(_vmaAllocator);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    IDestructionQueue::~IDestructionQueue() {}

    class DeferredDestruction : public IDestructionQueue
    {
    public:
        void    Destroy(VkCommandPool) override;
        void    Destroy(VkSemaphore) override;
		void    Destroy(VkEvent) override;
        void    Destroy(VkDeviceMemory) override;
        void    Destroy(VkRenderPass) override;
        void    Destroy(VkImage) override;
        void    Destroy(VkImageView) override;
        void    Destroy(VkBufferView) override;
        void    Destroy(VkFramebuffer) override;
        void    Destroy(VkShaderModule) override;
        void    Destroy(VkDescriptorSetLayout) override;
        void    Destroy(VkDescriptorPool) override;
        void    Destroy(VkPipeline) override;
        void    Destroy(VkPipelineCache) override;
        void    Destroy(VkPipelineLayout) override;
        void    Destroy(VkBuffer) override;
        void    Destroy(VkFence) override;
        void    Destroy(VkSampler) override;
		void	Destroy(VkQueryPool) override;
        void	Destroy(VkImage, VmaAllocation) override;
        void	Destroy(VkBuffer, VmaAllocation) override;

        void    Flush(FlushFlags::BitField) override;

        DeferredDestruction(VulkanSharedPtr<VkDevice> device, const std::shared_ptr<IAsyncTracker>& tracker, VmaAllocator vmaAllocator);
        ~DeferredDestruction();
    private:
        VulkanSharedPtr<VkDevice> _device;
		std::shared_ptr<IAsyncTracker> _gpuTracker;
        VmaAllocator _vmaAllocator;      // raw reference

		using Marker = IAsyncTracker::Marker;
        template<typename Type>
            class Queue 
			{
			public:
                Threading::Mutex _lock;
				ResizableCircularBuffer<std::pair<Marker, unsigned>, 32> _markerCounts;
				std::deque<Type> _objects;
			};

            // note --  the order here represents the order in  which objects
            //          of each type will be deleted. It can be significant,
            //          for example VkDeviceMemory objects must be deleted after
            //          the VkImage/VkBuffer objects that reference them.
        std::tuple<
              Queue<VkCommandPool>              // 0
            , Queue<VkSemaphore>                // 1
			, Queue<VkFence>                    // 2
            , Queue<VkRenderPass>               // 3
            , Queue<VkImage>                    // 4
            , Queue<VkImageView>                // 5
            , Queue<VkBufferView>               // 6
            , Queue<VkFramebuffer>              // 7
            , Queue<VkShaderModule>             // 8
            , Queue<VkDescriptorSetLayout>      // 9
            , Queue<VkDescriptorPool>           // 10
            , Queue<VkPipeline>                 // 11
            , Queue<VkPipelineCache>            // 12
            , Queue<VkPipelineLayout>           // 13
            , Queue<VkBuffer>                   // 14
            , Queue<VkDeviceMemory>             // 15
            , Queue<VkSampler>                  // 16
			, Queue<VkQueryPool>				// 17
			, Queue<VkEvent>					// 18
            , Queue<std::pair<VkImage, VmaAllocation>>     // 19
            , Queue<std::pair<VkBuffer, VmaAllocation>>     // 20
        > _queues;

        template<int Index, typename Type>
            void DoDestroy(Type obj);

        template<int Index>
            void FlushQueue(Marker marker);
    };

    template<int Index, typename Type>
        inline void DeferredDestruction::DoDestroy(Type obj)
    {
		auto marker = _gpuTracker->GetProducerMarker();
        auto& q = std::get<Index>(_queues);
        ScopedLock(q._lock);
		if (q._markerCounts.empty()) {
			assert(q._objects.empty());
			q._markerCounts.emplace_back(std::make_pair(marker, 1u));
		} else if (q._markerCounts.back().first == marker) {
			++q._markerCounts.back().second;
		} else {
			assert(q._markerCounts.front().first < marker);
			q._markerCounts.emplace_back(std::make_pair(marker, 1u));
		}

		q._objects.push_back(obj);
    }

    template<typename Type> void DestroyObjectImmediate(VkDevice device, VmaAllocator, Type obj);
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkCommandPool obj)           { vkDestroyCommandPool(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkSemaphore obj)             { vkDestroySemaphore(device, obj, g_allocationCallbacks ); }
	template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkEvent obj)					{ vkDestroyEvent(device, obj, g_allocationCallbacks); }
	template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkFence obj)                 { vkDestroyFence(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkRenderPass obj)            { vkDestroyRenderPass(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkImage obj)                 { vkDestroyImage(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkImageView obj)             { vkDestroyImageView(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkBufferView obj)            { vkDestroyBufferView(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkFramebuffer obj)           { vkDestroyFramebuffer(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkShaderModule obj)          { vkDestroyShaderModule(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkDescriptorSetLayout obj)   { vkDestroyDescriptorSetLayout(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkDescriptorPool obj)        { vkDestroyDescriptorPool(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkPipeline obj)              { vkDestroyPipeline(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkPipelineCache obj)         { vkDestroyPipelineCache(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkPipelineLayout obj)        { vkDestroyPipelineLayout(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkBuffer obj)                { vkDestroyBuffer(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkDeviceMemory obj)          { vkFreeMemory(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkSampler obj)               { vkDestroySampler(device, obj, g_allocationCallbacks ); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator, VkQueryPool obj)             { vkDestroyQueryPool(device, obj, g_allocationCallbacks); }

	template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator allocator, std::pair<VkImage, VmaAllocation> p)     { vmaDestroyImage(allocator, p.first, p.second); }
    template<> inline void DestroyObjectImmediate(VkDevice device, VmaAllocator allocator, std::pair<VkBuffer, VmaAllocation> p)    { vmaDestroyBuffer(allocator, p.first, p.second); }

    template<int Index>
        inline void DeferredDestruction::FlushQueue(Marker marker)
    {
		// destroy up to and including the given marker
        auto& q = std::get<Index>(_queues);
        ScopedLock(q._lock);
		while (!q._markerCounts.empty() && q._markerCounts.front().first <= marker) {
			auto countToDelete = (size_t)q._markerCounts.front().second;

			assert(countToDelete <= q._objects.size());
			auto endi = q._objects.begin() + std::min(q._objects.size(), countToDelete);
			for (auto i=q._objects.begin(); i!=endi; ++i)
				DestroyObjectImmediate(_device.get(), _vmaAllocator, *i);
			
			q._markerCounts.pop_front(); 
			q._objects.erase(q._objects.begin(), endi);
		}
    }

    void    DeferredDestruction::Destroy(VkCommandPool obj) { DoDestroy<0>(obj); }
    void    DeferredDestruction::Destroy(VkSemaphore obj) { DoDestroy<1>(obj); }
    void    DeferredDestruction::Destroy(VkFence obj) { DoDestroy<2>(obj); }
    void    DeferredDestruction::Destroy(VkRenderPass obj) { DoDestroy<3>(obj); }
    void    DeferredDestruction::Destroy(VkImage obj) { DoDestroy<4>(obj); }
    void    DeferredDestruction::Destroy(VkImageView obj) { DoDestroy<5>(obj); }
    void    DeferredDestruction::Destroy(VkBufferView obj) { DoDestroy<6>(obj); }
    void    DeferredDestruction::Destroy(VkFramebuffer obj) { DoDestroy<7>(obj); }
    void    DeferredDestruction::Destroy(VkShaderModule obj) { DoDestroy<8>(obj); }
    void    DeferredDestruction::Destroy(VkDescriptorSetLayout obj) { DoDestroy<9>(obj); }
    void    DeferredDestruction::Destroy(VkDescriptorPool obj) { DoDestroy<10>(obj); }
    void    DeferredDestruction::Destroy(VkPipeline obj) { DoDestroy<11>(obj); }
    void    DeferredDestruction::Destroy(VkPipelineCache obj) { DoDestroy<12>(obj); }
    void    DeferredDestruction::Destroy(VkPipelineLayout obj) { DoDestroy<13>(obj); }
    void    DeferredDestruction::Destroy(VkBuffer obj) { DoDestroy<14>(obj); }
    void    DeferredDestruction::Destroy(VkDeviceMemory obj) { DoDestroy<15>(obj); }
    void    DeferredDestruction::Destroy(VkSampler obj) { DoDestroy<16>(obj); }
	void    DeferredDestruction::Destroy(VkQueryPool obj) { DoDestroy<17>(obj); }
	void    DeferredDestruction::Destroy(VkEvent obj) { DoDestroy<18>(obj); }
    void    DeferredDestruction::Destroy(VkImage image, VmaAllocation allocation) { DoDestroy<19>(std::make_pair(image, allocation)); }
    void    DeferredDestruction::Destroy(VkBuffer buffer, VmaAllocation allocation) { DoDestroy<20>(std::make_pair(buffer, allocation)); }

    void    DeferredDestruction::Flush(FlushFlags::BitField flags)
    {
		auto marker = (flags & FlushFlags::DestroyAll) ? ~0u : _gpuTracker->GetConsumerMarker();
        FlushQueue<0>(marker);
        FlushQueue<1>(marker);
        FlushQueue<2>(marker);
        FlushQueue<3>(marker);
        FlushQueue<4>(marker);
        FlushQueue<5>(marker);
        FlushQueue<6>(marker);
        FlushQueue<7>(marker);
        FlushQueue<8>(marker);
        FlushQueue<9>(marker);
        FlushQueue<10>(marker);
        FlushQueue<11>(marker);
        FlushQueue<12>(marker);
        FlushQueue<13>(marker);
        FlushQueue<14>(marker);
        FlushQueue<15>(marker);
		FlushQueue<16>(marker);
		FlushQueue<17>(marker);
        FlushQueue<18>(marker);
        FlushQueue<19>(marker);
        FlushQueue<20>(marker);
    }

    DeferredDestruction::DeferredDestruction(VulkanSharedPtr<VkDevice> device, const std::shared_ptr<IAsyncTracker>& tracker, VmaAllocator vmaAllocator)
    : _device(device), _gpuTracker(tracker), _vmaAllocator(vmaAllocator)
    {
    }

    DeferredDestruction::~DeferredDestruction() 
    {
        Flush(IDestructionQueue::FlushFlags::DestroyAll);
    }

	std::shared_ptr<IDestructionQueue> ObjectFactory::CreateMarkerTrackingDestroyer(const std::shared_ptr<IAsyncTracker>& tracker)
	{
		auto result = std::make_shared<DeferredDestruction>(_device, tracker, _vmaAllocator);
        #if defined(_DEBUG)
            _associatedDestructionQueues.emplace_back(result);
        #endif
        return result;
	}

	class ImmediateDestruction : public IDestructionQueue
	{
	public:
		void    Destroy(VkCommandPool) override;
		void    Destroy(VkSemaphore) override;
		void    Destroy(VkEvent) override;
		void    Destroy(VkDeviceMemory) override;
		void    Destroy(VkRenderPass) override;
		void    Destroy(VkImage) override;
		void    Destroy(VkImageView) override;
        void    Destroy(VkBufferView) override;
		void    Destroy(VkFramebuffer) override;
		void    Destroy(VkShaderModule) override;
		void    Destroy(VkDescriptorSetLayout) override;
		void    Destroy(VkDescriptorPool) override;
		void    Destroy(VkPipeline) override;
		void    Destroy(VkPipelineCache) override;
		void    Destroy(VkPipelineLayout) override;
		void    Destroy(VkBuffer) override;
		void    Destroy(VkFence) override;
		void    Destroy(VkSampler) override;
		void	Destroy(VkQueryPool) override;
        void	Destroy(VkImage, VmaAllocation) override;
        void	Destroy(VkBuffer, VmaAllocation) override;

		void    Flush(FlushFlags::BitField) override;

		ImmediateDestruction(VulkanSharedPtr<VkDevice> device, VmaAllocator allocator);
		~ImmediateDestruction();
	private:
		VulkanSharedPtr<VkDevice> _device;
        VmaAllocator _allocator;        // raw reference
	};

	void    ImmediateDestruction::Destroy(VkCommandPool obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkSemaphore obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkEvent obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkDeviceMemory obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkRenderPass obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkImage obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkImageView obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
    void    ImmediateDestruction::Destroy(VkBufferView obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkFramebuffer obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkShaderModule obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkDescriptorSetLayout obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkDescriptorPool obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkPipeline obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkPipelineCache obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkPipelineLayout obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkBuffer obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkFence obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void    ImmediateDestruction::Destroy(VkSampler obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
	void	ImmediateDestruction::Destroy(VkQueryPool obj) { DestroyObjectImmediate(_device.get(), _allocator, obj); }
    void	ImmediateDestruction::Destroy(VkImage obj, VmaAllocation allocation) { DestroyObjectImmediate(_device.get(), _allocator, std::make_pair(obj, allocation)); }
    void	ImmediateDestruction::Destroy(VkBuffer obj, VmaAllocation allocation) { DestroyObjectImmediate(_device.get(), _allocator, std::make_pair(obj, allocation)); }

	void    ImmediateDestruction::Flush(FlushFlags::BitField) {}

	ImmediateDestruction::ImmediateDestruction(VulkanSharedPtr<VkDevice> device, VmaAllocator allocator)
	: _device(device), _allocator(allocator) {}
	ImmediateDestruction::~ImmediateDestruction() {}

	static std::shared_ptr<IDestructionQueue> CreateImmediateDestroyer(VulkanSharedPtr<VkDevice> device, VmaAllocator vmaAllocator)
	{
		return std::make_shared<ImmediateDestruction>(std::move(device), vmaAllocator);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static const char* AsString(VkResult res)
    {
        // String names for standard vulkan error codes
        switch (res)
        {
                // success codes
            case VK_SUCCESS: return "Success";
            case VK_NOT_READY: return "Not Ready";
            case VK_TIMEOUT: return "Timeout";
            case VK_EVENT_SET: return "Event Set";
            case VK_EVENT_RESET: return "Event Reset";
            case VK_INCOMPLETE: return "Incomplete";

                // error codes
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "Out of host memory";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "Out of device memory";
            case VK_ERROR_INITIALIZATION_FAILED: return "Initialization failed";
            case VK_ERROR_DEVICE_LOST: return "Device lost";
            case VK_ERROR_MEMORY_MAP_FAILED: return "Memory map failed";
            case VK_ERROR_LAYER_NOT_PRESENT: return "Layer not present";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "Extension not present";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "Feature not present";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "Incompatible driver";
            case VK_ERROR_TOO_MANY_OBJECTS: return "Too many objects";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "Format not supported";

                // kronos extensions
            case VK_ERROR_SURFACE_LOST_KHR: return "[KHR] Surface lost";
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "[KHR] Native window in use";
            case VK_SUBOPTIMAL_KHR: return "[KHR] Suboptimal";
            case VK_ERROR_OUT_OF_DATE_KHR: return "[KHR] Out of date";
            case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "[KHR] Incompatible display";
            case VK_ERROR_VALIDATION_FAILED_EXT: return "[KHR] Validation failed";

                // NV extensions
            case VK_ERROR_INVALID_SHADER_NV: return "[NV] Invalid shader";

            default: return "<<unknown>>";
        }
    }

    VulkanAPIFailure::VulkanAPIFailure(VkResult_ res, const char message[])
        : Exceptions::BasicLabel("%s [%s, %i]", message, AsString((VkResult)res), res) {}
}}

