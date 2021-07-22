// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "IncludeVulkan.h"      // required because we're deriving from Vulkan types
#include "../../StateDesc.h"
#include "../../IDevice.h"
#include "../../../Utility/IteratorUtils.h"
#include <utility>

namespace RenderCore { namespace Metal_Vulkan
{
    class DeviceContext;
    class ObjectFactory;

    static const unsigned s_mrtLimit = 4;

////////////////////////////////////////////////////////////////////////////////////////////////

    class SamplerState : public ISampler
    {
    public:
        SamplerState(ObjectFactory&, const SamplerDesc&);
		~SamplerState();

        using UnderlyingType = VkSampler;
        VkSampler GetUnderlying() const { return _sampler.get(); }
        const VulkanSharedPtr<VkSampler>& ShareSampler() const { return _sampler; }

        SamplerDesc GetDesc() const override;
    private:
        VulkanSharedPtr<VkSampler> _sampler;
        SamplerDesc _desc;
    };

    namespace Internal
    {

            ////////////////////////////////////////////////////////////////////////////////////////////////

        class VulkanRasterizerState : public VkPipelineRasterizationStateCreateInfo
        {
        public:
            VulkanRasterizerState(const RasterizationDesc& desc = {});
            VulkanRasterizerState(const VulkanRasterizerState&);
            VulkanRasterizerState& operator=(const VulkanRasterizerState&);
        private:
            VkPipelineRasterizationConservativeStateCreateInfoEXT _conservativeRasterExt;
        };

            ////////////////////////////////////////////////////////////////////////////////////////////////

        class VulkanBlendState : public VkPipelineColorBlendStateCreateInfo
        {
        public:
            VulkanBlendState(IteratorRange<const AttachmentBlendDesc*> blendStates);
            VulkanBlendState();
            VulkanBlendState(const VulkanBlendState& cloneFrom);
            VulkanBlendState& operator=(const VulkanBlendState& cloneFrom);
        private:
            VkPipelineColorBlendAttachmentState _attachments[s_mrtLimit];
        };

            ////////////////////////////////////////////////////////////////////////////////////////////////

        class VulkanDepthStencilState : public VkPipelineDepthStencilStateCreateInfo
        {
        public:
            VulkanDepthStencilState(const DepthStencilDesc& depthStencilState = {});
        };

    }
}}

