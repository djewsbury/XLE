// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "DeviceContext.h"

#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    void FrameBuffer::BindSubpass(DeviceContext& context, unsigned subpassIndex, IteratorRange<const ClearValue*> clearValues) const
    {
        MTLRenderPassDescriptor* desc = _subpasses[subpassIndex]._renderPassDescriptor.get();

        /* Metal TODO -- this is a partial implementation of clear colors; it works for a single color attachment
         * and assumes that depth/stencil clear values are after color attachment clear values, if any */
        unsigned clearValueIterator = 0;

        if (desc.colorAttachments[0].texture && desc.colorAttachments[0].loadAction == MTLLoadActionClear) {
            if (clearValueIterator < clearValues.size()) {
                auto* clear = clearValues[clearValueIterator]._float;
                desc.colorAttachments[0].clearColor = MTLClearColorMake(clear[0], clear[1], clear[2], clear[3]);
            } else {
                desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
            }
        }
        if (desc.depthAttachment.texture && desc.depthAttachment.loadAction == MTLLoadActionClear) {
            if (clearValueIterator < clearValues.size()) {
                desc.depthAttachment.clearDepth = clearValues[clearValueIterator]._depthStencil._depth;
            } else {
                desc.depthAttachment.clearDepth = 1.0f;
            }
        }
        if (desc.stencilAttachment.texture && desc.stencilAttachment.loadAction == MTLLoadActionClear) {
            if (clearValueIterator < clearValues.size()) {
                desc.stencilAttachment.clearStencil = clearValues[clearValueIterator]._depthStencil._stencil;
            } else {
                desc.stencilAttachment.clearStencil = 0;
            }
        }

        /* Each subpass of the frame will have a RenderCommandEncoder with a different render pass descriptor. */
        context.CreateRenderCommandEncoder(desc);

        context.SetRenderPassConfiguration(desc, _subpasses[subpassIndex]._rasterCount);

        // At the start of a subpass, we set the viewport and scissor rect to full-size (based on color or depth attachment)
        {
            float width = 0.f;
            float height = 0.f;
            if (desc.colorAttachments[0].texture) {
                width = desc.colorAttachments[0].texture.width;
                height = desc.colorAttachments[0].texture.height;
            } else if (desc.depthAttachment.texture) {
                width = desc.depthAttachment.texture.width;
                height = desc.depthAttachment.texture.height;
            } else if (desc.stencilAttachment.texture) {
                width = desc.stencilAttachment.texture.width;
                height = desc.stencilAttachment.texture.height;
            }

            Viewport viewports[1];
            viewports[0] = Viewport{0.f, 0.f, width, height};
            // origin of viewport doesn't matter because it is full-size
            ScissorRect scissorRects[1];
            scissorRects[0] = ScissorRect{0, 0, (unsigned)width, (unsigned)height};
            // origin of scissor rect doesn't matter because it is full-size
            context.SetViewportAndScissorRects(MakeIteratorRange(viewports), MakeIteratorRange(scissorRects));
        }
    }

    MTLLoadAction NonStencilLoadActionFromRenderCore(RenderCore::LoadStore load)
    {
        switch (load) {
            case RenderCore::LoadStore::DontCare:
            case RenderCore::LoadStore::DontCare_StencilRetain:
            case RenderCore::LoadStore::DontCare_StencilClear:
                return MTLLoadActionDontCare;
            case RenderCore::LoadStore::Retain:
            case RenderCore::LoadStore::Retain_StencilDontCare:
            case RenderCore::LoadStore::Retain_StencilClear:
                return MTLLoadActionLoad;
            case RenderCore::LoadStore::Clear:
            case RenderCore::LoadStore::Clear_StencilDontCare:
            case RenderCore::LoadStore::Clear_StencilRetain:
                return MTLLoadActionClear;
        }
    }

    MTLStoreAction NonStencilStoreActionFromRenderCore(RenderCore::LoadStore store)
    {
        switch (store) {
            case RenderCore::LoadStore::Retain:
            case RenderCore::LoadStore::Retain_StencilDontCare:
            case RenderCore::LoadStore::Retain_StencilClear:
                return MTLStoreActionStore;
            default:
                return MTLStoreActionDontCare;
        }
    }

    MTLLoadAction StencilLoadActionFromRenderCore(RenderCore::LoadStore load)
    {
        switch (load) {
            case RenderCore::LoadStore::Retain:
            case RenderCore::LoadStore::DontCare_StencilRetain:
            case RenderCore::LoadStore::Clear_StencilRetain:
                return MTLLoadActionLoad;
            case RenderCore::LoadStore::Clear:
            case RenderCore::LoadStore::DontCare_StencilClear:
            case RenderCore::LoadStore::Retain_StencilClear:
                return MTLLoadActionClear;
            case RenderCore::LoadStore::DontCare:
            case RenderCore::LoadStore::Retain_StencilDontCare:
            case RenderCore::LoadStore::Clear_StencilDontCare:
                return MTLLoadActionDontCare;
        }
    }

    MTLStoreAction StencilStoreActionFromRenderCore(RenderCore::LoadStore store)
    {
        switch (store) {
            case RenderCore::LoadStore::Retain:
            case RenderCore::LoadStore::DontCare_StencilRetain:
            case RenderCore::LoadStore::Clear_StencilRetain:
                return MTLStoreActionStore;
            default:
                return MTLStoreActionDontCare;
        }
    }

    static const Resource& AsResource(const IResourcePtr rp)
    {
        static Resource dummy;
        auto* res = (Resource*)rp->QueryInterface(typeid(Resource).hash_code());
        if (res)
            return *res;
        return dummy;
    }

    static bool HasRetain(LoadStore loadStore)
    {
        return  loadStore == LoadStore::Retain
            ||  loadStore == LoadStore::DontCare_StencilRetain
            ||  loadStore == LoadStore::Clear_StencilRetain
            ||  loadStore == LoadStore::Retain_StencilDontCare
            ||  loadStore == LoadStore::Retain_StencilClear
            ;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    FrameBuffer::FrameBuffer(ObjectFactory& factory, const FrameBufferDesc& fbDesc, const INamedAttachments& namedResources)
    {
        auto subpasses = fbDesc.GetSubpasses();

        _subpasses.resize(subpasses.size());
        for (unsigned p=0; p<(unsigned)subpasses.size(); ++p) {
            _subpasses[p]._renderPassDescriptor = moveptr([[MTLRenderPassDescriptor alloc] init]);
            _subpasses[p]._rasterCount = 1;

            auto* desc = _subpasses[p]._renderPassDescriptor.get();
            const auto& spDesc = subpasses[p];
            const unsigned maxColorAttachments = 4u;
            assert(spDesc.GetOutputs().size() <= maxColorAttachments); // MTLRenderPassDescriptor supports up to four color attachments
            auto colorAttachmentsCount = (unsigned)std::min((unsigned)spDesc.GetOutputs().size(), maxColorAttachments);
            for (unsigned o=0; o<colorAttachmentsCount; ++o) {
                const auto& attachmentView = spDesc.GetOutputs()[o];
                auto& attachmentDesc = fbDesc.GetAttachments()[attachmentView._resourceName]._desc;
                auto resource = namedResources.GetResource(attachmentView._resourceName, attachmentDesc);
                if (!resource)
                    Throw(::Exceptions::BasicLabel("Could not find attachment texture for color attachment in FrameBuffer::FrameBuffer"));

                // Configure MTLRenderPassColorAttachmentDescriptor
                desc.colorAttachments[o].texture = AsResource(resource).GetTexture();
                desc.colorAttachments[o].loadAction = NonStencilLoadActionFromRenderCore(attachmentView._loadFromPreviousPhase);
                desc.colorAttachments[o].storeAction = NonStencilStoreActionFromRenderCore(attachmentView._storeToNextPhase);
                // clearColor is set when binding subpass

                _subpasses[p]._rasterCount = std::max(
                    _subpasses[p]._rasterCount,
                    (unsigned)resource->GetDesc()._textureDesc._samples._sampleCount);

                if (o < spDesc.GetResolveOutputs().size() && spDesc.GetResolveOutputs()[o]._resourceName != ~0u) {
                    auto& attachmentDesc = fbDesc.GetAttachments()[spDesc.GetResolveOutputs()[o]._resourceName]._desc;
                    auto resolveResource = namedResources.GetResource(spDesc.GetResolveOutputs()[o]._resourceName, attachmentDesc);
                    if (!resolveResource)
                        Throw(::Exceptions::BasicLabel("Could not find resolve texture for color attachment in FrameBuffer::FrameBuffer"));

                    assert(AsResource(resolveResource).GetTexture().get().textureType != MTLTextureType2DMultisample);     // don't resolve into a multisample destination
                    assert(AsResource(resolveResource).GetTexture().get().pixelFormat == AsResource(resource).GetTexture().get().pixelFormat);

                    desc.colorAttachments[o].resolveTexture = AsResource(resolveResource).GetTexture();
                    if (HasRetain(attachmentView._storeToNextPhase)) {
                        desc.colorAttachments[o].storeAction = MTLStoreActionStoreAndMultisampleResolve;
                    } else {
                        desc.colorAttachments[o].storeAction = MTLStoreActionMultisampleResolve;
                    }
                }
            }

            if (spDesc.GetDepthStencil()._resourceName != ~0u) {
                auto& attachmentDesc = fbDesc.GetAttachments()[spDesc.GetDepthStencil()._resourceName]._desc;
                auto resource = namedResources.GetResource(spDesc.GetDepthStencil()._resourceName, attachmentDesc);
                if (!resource)
                    Throw(::Exceptions::BasicLabel("Could not find attachment texture for depth/stencil attachment in FrameBuffer::FrameBuffer"));

                auto& res = AsResource(resource);
                auto format = res.GetDesc()._textureDesc._format;
                auto resolvedFormat = ResolveFormat(format, {}, FormatUsage::DSV);
                auto components = GetComponents(resolvedFormat);

                if (components == FormatComponents::Depth || components == FormatComponents::DepthStencil) {
                    desc.depthAttachment.texture = res.GetTexture();
                    desc.depthAttachment.loadAction = NonStencilLoadActionFromRenderCore(spDesc.GetDepthStencil()._loadFromPreviousPhase);
                    desc.depthAttachment.storeAction = NonStencilStoreActionFromRenderCore(spDesc.GetDepthStencil()._storeToNextPhase);
                    // clearDepth is set when binding subpass
                }

                if (components == FormatComponents::Stencil || components == FormatComponents::DepthStencil) {
                    desc.stencilAttachment.texture = res.GetTexture();
                    desc.stencilAttachment.loadAction = StencilLoadActionFromRenderCore(spDesc.GetDepthStencil()._loadFromPreviousPhase);
                    desc.stencilAttachment.storeAction = StencilStoreActionFromRenderCore(spDesc.GetDepthStencil()._storeToNextPhase);
                    // clearStencil is set when binding subpass
                }

                _subpasses[p]._rasterCount = std::max(
                    _subpasses[p]._rasterCount,
                    (unsigned)resource->GetDesc()._textureDesc._samples._sampleCount);

                if (spDesc.GetResolveDepthStencil()._resourceName != ~0u) {
                    auto& attachmentDesc = fbDesc.GetAttachments()[spDesc.GetResolveDepthStencil()._resourceName]._desc;
                    auto resolveResource = namedResources.GetResource(spDesc.GetResolveDepthStencil()._resourceName, attachmentDesc);
                    if (!resolveResource)
                        Throw(::Exceptions::BasicLabel("Could not find attachment texture for depth/stencil resolve attachment in FrameBuffer::FrameBuffer"));

                    assert(AsResource(resolveResource).GetTexture().get().textureType != MTLTextureType2DMultisample);     // don't resolve into a multisample destination
                    assert(AsResource(resolveResource).GetTexture().get().pixelFormat == AsResource(resource).GetTexture().get().pixelFormat);

                    desc.depthAttachment.resolveTexture = AsResource(resolveResource).GetTexture();
                    if (HasRetain(spDesc.GetResolveDepthStencil()._storeToNextPhase)) {
                        desc.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
                    } else {
                        desc.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
                    }
                }
            }
        }
    }

    FrameBuffer::FrameBuffer() {}
    FrameBuffer::~FrameBuffer() {}

    static unsigned s_nextSubpass = 0;
    static std::vector<ClearValue> s_clearValues;

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        IteratorRange<const ClearValue*> clearValues)
    {
        s_nextSubpass = 0;
        s_clearValues.clear();
        s_clearValues.insert(s_clearValues.end(), clearValues.begin(), clearValues.end());
        context.BeginRenderPass();
        BeginNextSubpass(context, frameBuffer);
    }

    /* KenD -- I'd prefer not to have this check, but it keeps balance of
     * creating/destroying the encoder when beginNextSubpass is called although
     * there isn't another subpass descriptor.
     */
    static bool didBindSubpass = false;

    void BeginNextSubpass(
        DeviceContext& context,
        FrameBuffer& frameBuffer)
    {
        // Queue up the next render targets
        auto subpassIndex = s_nextSubpass;
        if (subpassIndex < frameBuffer.GetSubpassCount()) {
            frameBuffer.BindSubpass(context, subpassIndex, MakeIteratorRange(s_clearValues));
            didBindSubpass = true;
        }
        ++s_nextSubpass;
    }

    /* KenD -- Implementation note
     * At the end of a subpass, we should end encoding and destroy the encoder.
     * For now, in RenderPassInstance, when beginning a new subpass, we end the current subpass.
     * And, when ending the render pass, we end the subpass.
     *
     * Clients must balance creating and destroying the encoder.
     * They must also end encoding only once per encoder.
     */

    void EndSubpass(DeviceContext& context, FrameBuffer& frameBuffer)
    {
        if (didBindSubpass) {
            context.EndEncoding();
            context.DestroyRenderCommandEncoder();
        }
        didBindSubpass = false;
    }

    void EndRenderPass(DeviceContext& context)
    {
        // For compatibility with Vulkan, it makes sense to unbind render targets here. This is important
        // if the render targets will be used as compute shader outputs in follow up steps. It also prevents
        // rendering outside of render passes. But sometimes it will produce redundant calls to OMSetRenderTargets().
        context.EndRenderPass();
    }

    unsigned GetCurrentSubpassIndex(DeviceContext& context)
    {
        return s_nextSubpass-1;
    }

}}
