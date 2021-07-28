// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IncludeVulkan.h"

namespace RenderCore { namespace Metal_Vulkan
{
    class ExtensionFunctions
    {
    public:
        PFN_vkCmdBeginTransformFeedbackEXT _beginTransformFeedback = nullptr;
        PFN_vkCmdBindTransformFeedbackBuffersEXT _bindTransformFeedbackBuffers = nullptr;
        PFN_vkCmdEndTransformFeedbackEXT _endTransformFeedback = nullptr;

        PFN_vkSetDebugUtilsObjectNameEXT _setObjectName = nullptr;
        PFN_vkCmdBeginDebugUtilsLabelEXT _beginLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT _endLabel = nullptr;

        ExtensionFunctions(VkInstance instance);
    };
}}
