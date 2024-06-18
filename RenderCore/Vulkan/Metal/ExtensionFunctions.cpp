// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExtensionFunctions.h"
#include "../../DeviceInitialization.h"

namespace RenderCore { namespace Metal_Vulkan
{
    ExtensionFunctions::ExtensionFunctions(VkInstance instance, const DeviceFeatures& xleFeatures)
    : _instance(instance)
    {
        _beginTransformFeedback = (PFN_vkCmdBeginTransformFeedbackEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginTransformFeedbackEXT");
		_bindTransformFeedbackBuffers = (PFN_vkCmdBindTransformFeedbackBuffersEXT)vkGetInstanceProcAddr(instance, "vkCmdBindTransformFeedbackBuffersEXT");
        _endTransformFeedback = (PFN_vkCmdEndTransformFeedbackEXT)vkGetInstanceProcAddr(instance, "vkCmdEndTransformFeedbackEXT");

        #if defined(VULKAN_ENABLE_DEBUG_EXTENSIONS)
            _setObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
            _beginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdBeginDebugUtilsLabelEXT");
            _endLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(instance, "vkCmdEndDebugUtilsLabelEXT");
        #endif

        _getSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValueKHR)vkGetInstanceProcAddr(instance, "vkGetSemaphoreCounterValueKHR");
        _signalSemaphore = (PFN_vkSignalSemaphoreKHR)vkGetInstanceProcAddr(instance, "vkSignalSemaphoreKHR");
        _waitSemaphores = (PFN_vkWaitSemaphoresKHR)vkGetInstanceProcAddr(instance, "vkWaitSemaphoresKHR");

        if (xleFeatures._vulkanRenderPass2)
            _createRenderPass2 = (PFN_vkCreateRenderPass2KHR)vkGetInstanceProcAddr(instance, "vkCreateRenderPass2KHR");
        else
            _createRenderPass2 = nullptr;
    }
}}
