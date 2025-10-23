// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define VMA_IMPLEMENTATION		// function implementations get compiled in this translation unit
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "Foreign/VulkanMemoryAllocator/include/vk_mem_alloc.h"
#pragma clang diagnostic pop