// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "DeviceContext.h"
#include "../../../Utility/IteratorUtils.h"
#include "../../../Core/Exceptions.h"

#include "IncludeAppleMetal.h"

namespace RenderCore { namespace Metal_AppleMetal
{
    MTLRenderPassDescriptor* FrameBuffer::GetDescriptor(unsigned subpassIdx) const
    {
        assert(subpassIdx < _subpasses.size());
        return _subpasses[subpassIdx]._renderPassDescriptor;
    }

    unsigned FrameBuffer::GetSampleCount(unsigned subpassIdx) const
    {
        assert(subpassIdx < _subpasses.size());
        return _subpasses[subpassIdx]._rasterCount;
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

    static void ScanForLoads(
        bool& mainAspectLoad, bool& stencilAspectLoad,
        const FrameBufferDesc& fbDesc,
        unsigned subpassStart,
        AttachmentName attachmentName)
    {
        assert(subpassStart<fbDesc.GetSubpasses().size());
        mainAspectLoad = stencilAspectLoad = false;
        for (unsigned s=subpassStart; s!=fbDesc.GetSubpasses().size(); ++s) {
            const auto& subpass = fbDesc.GetSubpasses()[s];
            
            for (auto& view:subpass.GetOutputs())
                mainAspectLoad |= view._resourceName == attachmentName;
            for (auto& view:subpass.GetInputs())
                mainAspectLoad |= view._resourceName == attachmentName;
            for (auto& view:subpass.GetResolveOutputs())
                mainAspectLoad |= view._resourceName == attachmentName;

            if (subpass.GetDepthStencil()._resourceName == attachmentName) {
                auto aspect = subpass.GetDepthStencil()._window._format._aspect;
                mainAspectLoad 
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Depth)
                    ;
                stencilAspectLoad
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Stencil)
                    ;
            }

            if (subpass.GetResolveDepthStencil()._resourceName == attachmentName) {
                auto aspect = subpass.GetResolveDepthStencil()._window._format._aspect;
                mainAspectLoad 
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Depth)
                    ;
                stencilAspectLoad
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Stencil)
                    ;
            }
        }
    }

    static void ScanForStores(
        bool& mainAspectStore, bool& stencilAspectStore,
        const FrameBufferDesc& fbDesc,
        unsigned subpassEnd,
        AttachmentName attachmentName)
    {
        assert(subpassEnd<=fbDesc.GetSubpasses().size());
        mainAspectStore = stencilAspectStore = false;
        for (unsigned s=0; s!=subpassEnd; ++s) {
            const auto& subpass = fbDesc.GetSubpasses()[s];
            
            for (auto& view:subpass.GetOutputs())
                mainAspectStore |= view._resourceName == attachmentName;
            for (auto& view:subpass.GetResolveOutputs())
                mainAspectStore |= view._resourceName == attachmentName;

            if (subpass.GetDepthStencil()._resourceName == attachmentName) {
                auto aspect = subpass.GetDepthStencil()._window._format._aspect;
                stencilAspectStore 
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Depth)
                    ;
                stencilAspectStore
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Stencil)
                    ;
            }

            if (subpass.GetResolveDepthStencil()._resourceName == attachmentName) {
                auto aspect = subpass.GetResolveDepthStencil()._window._format._aspect;
                stencilAspectStore 
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Depth)
                    ;
                stencilAspectStore
                    = (aspect == TextureViewDesc::Aspect::UndefinedAspect) 
                    | (aspect == TextureViewDesc::Aspect::DepthStencil)
                    | (aspect == TextureViewDesc::Aspect::Stencil)
                    ;
            }
        }
    }

    FrameBuffer::FrameBuffer(ObjectFactory& factory, const FrameBufferDesc& fbDesc, const INamedAttachments& namedResources)
    {
        auto subpasses = fbDesc.GetSubpasses();
        unsigned maxWidth = 0, maxHeight = 0;

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
                auto& attachmentDesc = fbDesc.GetAttachments()[attachmentView._resourceName];
                auto resource = namedResources.GetResource(attachmentView._resourceName, attachmentDesc, fbDesc.GetProperties());
                if (!resource)
                    Throw(::Exceptions::BasicLabel("Could not find attachment texture for color attachment in FrameBuffer::FrameBuffer"));

                // Configure MTLRenderPassColorAttachmentDescriptor
                // Consider stores and loads occuring with the same render pass
                // prior writes within the render pass are always loaded; and we will
                // always store for future reads within the render pass
                bool localLoad = false, stencilAspectLoad = false;
                bool localStore = false, stencilAspectStore = false;
                ScanForLoads(localLoad, stencilAspectLoad, fbDesc, p+1, attachmentView._resourceName);
                ScanForStores(localLoad, stencilAspectLoad, fbDesc, p, attachmentView._resourceName);

                desc.colorAttachments[o].texture = checked_cast<Resource*>(resource.get())->GetTexture();
                desc.colorAttachments[o].loadAction = localStore ? MTLLoadActionLoad : NonStencilLoadActionFromRenderCore(attachmentDesc._loadFromPreviousPhase);
                desc.colorAttachments[o].storeAction = localLoad ? MTLStoreActionStore : NonStencilStoreActionFromRenderCore(attachmentDesc._storeToNextPhase);
            
                auto resDesc = resource->GetDesc();
                _subpasses[p]._rasterCount = std::max(
                    _subpasses[p]._rasterCount,
                    (unsigned)resDesc._textureDesc._samples._sampleCount);
                maxWidth = std::max(maxWidth, (unsigned)resDesc._textureDesc._width);
                maxHeight = std::max(maxHeight, (unsigned)resDesc._textureDesc._height);

                if (o < spDesc.GetResolveOutputs().size() && spDesc.GetResolveOutputs()[o]._resourceName != ~0u) {
                    const auto& resolveAttachmentView = spDesc.GetResolveOutputs()[o];
                    auto& attachmentDesc = fbDesc.GetAttachments()[resolveAttachmentView._resourceName];
                    auto resolveResource = namedResources.GetResource(resolveAttachmentView._resourceName, attachmentDesc, fbDesc.GetProperties());
                    if (!resolveResource)
                        Throw(::Exceptions::BasicLabel("Could not find resolve texture for color attachment in FrameBuffer::FrameBuffer"));

                    assert(checked_cast<Resource*>(resolveResource.get())->GetTexture().textureType != MTLTextureType2DMultisample);     // don't resolve into a multisample destination
                    assert(checked_cast<Resource*>(resolveResource.get())->GetTexture().pixelFormat == checked_cast<Resource*>(resource.get())->GetTexture().pixelFormat);

                    desc.colorAttachments[o].resolveTexture = checked_cast<Resource*>(resolveResource.get())->GetTexture();
                    if (localLoad || HasRetain(attachmentDesc._storeToNextPhase)) {
                        desc.colorAttachments[o].storeAction = MTLStoreActionStoreAndMultisampleResolve;
                    } else {
                        desc.colorAttachments[o].storeAction = MTLStoreActionMultisampleResolve;
                    }
                }
            }

            if (spDesc.GetDepthStencil()._resourceName != ~0u) {
                auto& attachmentDesc = fbDesc.GetAttachments()[spDesc.GetDepthStencil()._resourceName];
                auto resource = namedResources.GetResource(spDesc.GetDepthStencil()._resourceName, attachmentDesc, fbDesc.GetProperties());
                if (!resource)
                    Throw(::Exceptions::BasicLabel("Could not find attachment texture for depth/stencil attachment in FrameBuffer::FrameBuffer"));

                auto& res = *checked_cast<Resource*>(resource.get());
                auto format = res.GetDesc()._textureDesc._format;
                auto resolvedFormat = ResolveFormat(format, {}, BindFlag::DepthStencil);
                auto components = GetComponents(resolvedFormat);

                bool localLoad = false, stencilAspectLoad = false;
                bool localStore = false, stencilAspectStore = false;
                ScanForLoads(localLoad, stencilAspectLoad, fbDesc, p+1, spDesc.GetDepthStencil()._resourceName);
                ScanForStores(localLoad, stencilAspectLoad, fbDesc, p, spDesc.GetDepthStencil()._resourceName);

                if (components == FormatComponents::Depth || components == FormatComponents::DepthStencil) {
                    desc.depthAttachment.texture = res.GetTexture();
                    desc.depthAttachment.loadAction = localStore ? MTLLoadActionLoad : NonStencilLoadActionFromRenderCore(attachmentDesc._loadFromPreviousPhase);
                    desc.depthAttachment.storeAction = localLoad ? MTLStoreActionStore : NonStencilStoreActionFromRenderCore(attachmentDesc._storeToNextPhase);
                }

                if (components == FormatComponents::Stencil || components == FormatComponents::DepthStencil) {
                    desc.stencilAttachment.texture = res.GetTexture();
                    desc.stencilAttachment.loadAction = stencilAspectStore ? MTLLoadActionLoad : StencilLoadActionFromRenderCore(attachmentDesc._loadFromPreviousPhase);
                    desc.stencilAttachment.storeAction = stencilAspectLoad ? MTLStoreActionStore : StencilStoreActionFromRenderCore(attachmentDesc._storeToNextPhase);
                }

                auto resDesc = resource->GetDesc();
                _subpasses[p]._rasterCount = std::max(
                    _subpasses[p]._rasterCount,
                    (unsigned)resDesc._textureDesc._samples._sampleCount);
                maxWidth = std::max(maxWidth, (unsigned)resDesc._textureDesc._width);
                maxHeight = std::max(maxHeight, (unsigned)resDesc._textureDesc._height);

                if (spDesc.GetResolveDepthStencil()._resourceName != ~0u) {
                    auto& attachmentDesc = fbDesc.GetAttachments()[spDesc.GetResolveDepthStencil()._resourceName];
                    auto resolveResource = namedResources.GetResource(spDesc.GetResolveDepthStencil()._resourceName, attachmentDesc, fbDesc.GetProperties());
                    if (!resolveResource)
                        Throw(::Exceptions::BasicLabel("Could not find attachment texture for depth/stencil resolve attachment in FrameBuffer::FrameBuffer"));

                    assert(checked_cast<Resource*>(resolveResource.get())->GetTexture().textureType != MTLTextureType2DMultisample);     // don't resolve into a multisample destination
                    assert(checked_cast<Resource*>(resolveResource.get())->GetTexture().pixelFormat == checked_cast<Resource*>(resource.get())->GetTexture().pixelFormat);

                    desc.depthAttachment.resolveTexture = checked_cast<Resource*>(resolveResource.get())->GetTexture();
                    if (localLoad || HasRetain(attachmentDesc._storeToNextPhase)) {
                        desc.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
                    } else {
                        desc.depthAttachment.storeAction = MTLStoreActionMultisampleResolve;
                    }
                }
            }
        }

        // At the start of a render pass, we set the viewport and scissor rect to full-size (based on color or depth attachment)
        {
            ViewportDesc viewports[1];
            viewports[0] = ViewportDesc{0.f, 0.f, (float)maxWidth, (float)maxHeight};
            // origin of viewport doesn't matter because it is full-size
            ScissorRect scissorRects[1];
            scissorRects[0] = ScissorRect{0, 0, maxWidth, maxHeight};
        }
    }

    FrameBuffer::FrameBuffer() {}
    FrameBuffer::~FrameBuffer() {}
}}
