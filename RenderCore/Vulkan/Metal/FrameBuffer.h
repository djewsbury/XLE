// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../Types_Forward.h"
#include "../../FrameBufferDesc.h"
#include <memory>

namespace RenderCore { namespace Metal_Vulkan
{
    class ObjectFactory;
    class ResourceView;
    class DeviceContext;

    class FrameBuffer
	{
	public:
		VkFramebuffer GetUnderlying() const { return _underlying.get(); }
        VkRenderPass GetLayout() const { return _layout.get(); }

		unsigned GetSubpassCount() const { return _subpassCount; }

		FrameBuffer(
			const ObjectFactory& factory,
            const FrameBufferDesc& desc,
            const INamedAttachments& namedResources);
		FrameBuffer();
		~FrameBuffer();

        struct ClearValueOrdering
        {
            unsigned _originalAttachmentIndex;
            ClearValue _defaultClearValue;
        };
        std::vector<ClearValueOrdering> _clearValuesOrdering;
	private:
		VulkanSharedPtr<VkFramebuffer> _underlying;
        VulkanSharedPtr<VkRenderPass> _layout;
		unsigned _subpassCount;
        VectorPattern<int, 2> _defaultOffset;
        VectorPattern<unsigned, 2> _defaultExtent;

        friend void BeginRenderPass(DeviceContext&, FrameBuffer&, IteratorRange<const ClearValue*>);
	};

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        IteratorRange<const ClearValue*> clearValues = {});

    void BeginNextSubpass(DeviceContext& context, FrameBuffer& frameBuffer);
	void EndSubpass(DeviceContext& context, FrameBuffer& frameBuffer);
    void EndRenderPass(DeviceContext& context);
	unsigned GetCurrentSubpassIndex(DeviceContext& context);

    VulkanUniquePtr<VkRenderPass> CreateVulkanRenderPass(
        const Metal_Vulkan::ObjectFactory& factory,
        const FrameBufferDesc& layout,
        TextureSamples samples);

}}