// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../FrameBufferDesc.h"
#include "../../StateDesc.h"
#include <memory>

namespace RenderCore { class IResourceView; class ViewportDesc; }

namespace RenderCore { namespace Metal_Vulkan
{
    class ObjectFactory;
    class ResourceView;
    class DeviceContext;

    class FrameBuffer
	{
	public:
        ViewportDesc GetDefaultViewport() const { return _defaultViewport; }

		VkFramebuffer GetUnderlying() const { return _underlying.get(); }
        VkRenderPass GetLayout() const { return _layout.get(); }

		unsigned GetSubpassCount() const { return _subpassCount; }

        VectorPattern<int, 2> GetDefaultOffset() const { return _defaultOffset; };
        VectorPattern<unsigned, 2> GetDefaultExtent() const { return _defaultExtent; }

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
        ViewportDesc _defaultViewport;
        std::vector<std::shared_ptr<IResourceView>> _retainedViews;

        friend void BeginRenderPass(DeviceContext&, FrameBuffer&, IteratorRange<const ClearValue*>);
	};

    VulkanUniquePtr<VkRenderPass> CreateVulkanRenderPass(
        const Metal_Vulkan::ObjectFactory& factory,
        const FrameBufferDesc& layout,
        TextureSamples samples);

}}