// Copyright 2016 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <stdint.h> // for uint32_t, uint64_t

extern "C" 
{
    using VkResult_ = uint32_t;

    #define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;

    #if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
        #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
    #else
        #define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
    #endif

    typedef uint32_t VkFlags;
    typedef uint32_t VkBool32;
    typedef uint64_t VkDeviceSize;
    typedef uint32_t VkSampleMask;

    VK_DEFINE_HANDLE(VkInstance)
    VK_DEFINE_HANDLE(VkPhysicalDevice)
    VK_DEFINE_HANDLE(VkDevice)
    VK_DEFINE_HANDLE(VkQueue)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSemaphore)
    VK_DEFINE_HANDLE(VkCommandBuffer)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFence)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDeviceMemory)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBuffer)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImage)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkEvent)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkQueryPool)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkBufferView)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkImageView)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkShaderModule)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineCache)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipelineLayout)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkRenderPass)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkPipeline)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSetLayout)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSampler)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorPool)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkDescriptorSet)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkFramebuffer)
    VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkCommandPool)

    #undef VK_DEFINE_NON_DISPATCHABLE_HANDLE
    #undef VK_DEFINE_HANDLE

    typedef VkFlags VkCommandPoolCreateFlags;
    typedef VkFlags VkFenceCreateFlags;
    typedef VkFlags VkSemaphoreCreateFlags;
    typedef VkFlags VkShaderModuleCreateFlags;
    typedef VkFlags VkPipelineLayoutCreateFlags;
    typedef VkFlags VkPipelineCacheCreateFlags;
    typedef VkFlags VkImageAspectFlags;
    typedef VkFlags VkMemoryPropertyFlags;
    typedef VkFlags VkShaderStageFlags;
	typedef VkFlags VkQueryPipelineStatisticFlags;

    typedef struct VkRenderPassCreateInfo VkRenderPassCreateInfo;
    typedef struct VkImageCreateInfo VkImageCreateInfo;
    typedef struct VkImageViewCreateInfo VkImageViewCreateInfo;
    typedef struct VkBufferViewCreateInfo VkBufferViewCreateInfo;
    typedef struct VkGraphicsPipelineCreateInfo VkGraphicsPipelineCreateInfo;
    typedef struct VkDescriptorPoolCreateInfo VkDescriptorPoolCreateInfo;
    typedef struct VkBufferCreateInfo VkBufferCreateInfo;
    typedef struct VkSamplerCreateInfo VkSamplerCreateInfo;
    typedef struct VkFramebufferCreateInfo VkFramebufferCreateInfo;
    typedef struct VkComputePipelineCreateInfo VkComputePipelineCreateInfo;
    typedef struct VkDescriptorSetLayoutBinding VkDescriptorSetLayoutBinding;
    typedef struct VkPushConstantRange VkPushConstantRange;
    typedef struct VkFormatProperties VkFormatProperties;

    typedef struct VkVertexInputAttributeDescription VkVertexInputAttributeDescription;
	typedef struct VkVertexInputBindingDescription VkVertexInputBindingDescription;

    typedef struct VkPhysicalDeviceMemoryProperties VkPhysicalDeviceMemoryProperties;
    typedef struct VkPhysicalDeviceFeatures VkPhysicalDeviceFeatures;
    typedef struct VkPhysicalDeviceProperties VkPhysicalDeviceProperties;
    typedef struct VkAllocationCallbacks VkAllocationCallbacks;

    typedef struct VkImageSubresourceRange VkImageSubresourceRange;

    using VkFormat_ = uint32_t;
    using VkDescriptorType_ = uint32_t;
	using VkQueryType_ = uint32_t;
}

