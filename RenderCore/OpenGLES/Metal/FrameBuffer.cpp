// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FrameBuffer.h"
#include "Format.h"
#include "Resource.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "../../Format.h"
#include "../../FrameBufferDesc.h"
#include "../../../Utility/MemoryUtils.h"

#include "IncludeGLES.h"

namespace RenderCore { namespace Metal_OpenGLES
{
	class TextureViewPool
	{
	public:
		ShaderResourceView GetSRV(const IResourcePtr& resource, const TextureViewDesc& view);
		RenderTargetView GetRTV(const IResourcePtr& resource, const TextureViewDesc& view);
		DepthStencilView GetDSV(const IResourcePtr& resource, const TextureViewDesc& view);

		TextureViewPool();
		~TextureViewPool();
	private:
		std::vector<std::pair<uint64, RenderTargetView>> _rtvs;
		std::vector<std::pair<uint64, DepthStencilView>> _dsvs;
		std::vector<std::pair<uint64, ShaderResourceView>> _srvs;
	};

    FrameBuffer::FrameBuffer(
		ObjectFactory& factory,
        const FrameBufferDesc& fbDesc,
        const INamedAttachments& namedResources)
    {
        // We must create the frame buffer, including all resources and views required.
        // Here, some resources can come from the presentation chain. But other resources will
        // be created an attached to this object.
        auto subpasses = fbDesc.GetSubpasses();

		TextureViewPool viewPool;
		unsigned clearValueIterator = 0;
        
        assert(subpasses.size() < s_maxSubpasses);
        for (unsigned c=0; c<(unsigned)subpasses.size(); ++c) {
			const auto& spDesc = subpasses[c];
			auto& sp = _subpasses[c];
			sp._rtvCount = std::min((unsigned)spDesc._output.size(), s_maxMRTs);
			for (unsigned r = 0; r<sp._rtvCount; ++r) {
				const auto& attachmentView = spDesc._output[r];
				auto resource = namedResources.GetResource(attachmentView._resourceName);
				if (!resource)
					Throw(::Exceptions::BasicLabel("Could not find attachment resource for RTV in FrameBuffer::FrameBuffer"));
				sp._rtvs[r] = viewPool.GetRTV(resource, attachmentView._window);
				sp._rtvLoad[r] = attachmentView._loadFromPreviousPhase;
				sp._rtvClearValue[r] = clearValueIterator++;
			}

			if (spDesc._depthStencil._resourceName != ~0u) {
				auto resource = namedResources.GetResource(spDesc._depthStencil._resourceName);
				if (!resource)
					Throw(::Exceptions::BasicLabel("Could not find attachment resource for DSV in FrameBuffer::FrameBuffer"));
				sp._dsv = viewPool.GetDSV(resource, spDesc._depthStencil._window);
				sp._dsvLoad = spDesc._depthStencil._loadFromPreviousPhase;
				sp._dsvClearValue = clearValueIterator++;
			}

            sp._frameBuffer = factory.CreateFrameBuffer();
            glBindFramebuffer(GL_FRAMEBUFFER, sp._frameBuffer->AsRawGLHandle());
            for (unsigned rtv=0; rtv<sp._rtvCount; ++rtv) {
                auto& res = sp._rtvs[rtv];

                auto desc = ExtractDesc(res);
                assert(desc._type == ResourceDesc::Type::Texture);

                if (    desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D
                    ||  desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D) {
                    if (res.GetRenderBuffer()) {
                        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+rtv, GL_RENDERBUFFER, res.GetRenderBuffer()->AsRawGLHandle());
                    } else {
                        if (desc._textureDesc._arrayCount > 1u) {
                            glFramebufferTextureLayer(
                                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+rtv,
                                res.GetTexture()->AsRawGLHandle(),
                                res._window._mipRange._min,
                                res._window._arrayLayerRange._min);
                        } else {
                            glFramebufferTexture2D(
                                GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+rtv, GL_TEXTURE_2D,
                                res.GetTexture()->AsRawGLHandle(),
                                res._window._mipRange._min);
                        }
                    }
                } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D) {
                    assert(!res.GetRenderBuffer());     // not rational in this case
                    glFramebufferTextureLayer(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+rtv,
                        res.GetTexture()->AsRawGLHandle(),
                        res._window._mipRange._min,
                        res._window._arrayLayerRange._min);
                } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
                    assert(!res.GetRenderBuffer());     // not rational in this case
                    assert(desc._textureDesc._arrayCount <= 1u);    // cannot render to arrays of cubemaps
                    glFramebufferTexture2D(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0+rtv, GL_TEXTURE_CUBE_MAP_POSITIVE_X + res._window._arrayLayerRange._min,
                        res.GetTexture()->AsRawGLHandle(),
                        res._window._mipRange._min);
                } else {
                    assert(0);
                }
            }
        }
        _subpassCount = (unsigned)subpasses.size();
    }

    void FrameBuffer::BindSubpass(DeviceContext& context, unsigned subpassIndex, IteratorRange<const ClearValue*> clearValues) const
    {
        if (subpassIndex >= _subpassCount)
            Throw(::Exceptions::BasicLabel("Attempting to set invalid subpass"));

        const auto& s = _subpasses[subpassIndex];
        glBindFramebuffer(GL_FRAMEBUFFER, s._frameBuffer->AsRawGLHandle());

        for (unsigned rtv=0; rtv<s._rtvCount; ++rtv) {
            auto attachmentIdx = s._rtvs[rtv];
            auto load = s._rtvLoad[rtv];
            if (load == AttachmentViewDesc::LoadStore::Clear) {
                glClearBufferfv(GL_COLOR, GL_DRAW_BUFFER0 + rtv, clearValues[attachmentIdx]._float);
            }
        }

        if (s._dsvLoad == LoadStore::Clear_ClearStencil) {
            glClearBufferfi(GL_DEPTH_STENCIL, 0, clearValues[s._dsv]._depthStencil._depth, clearValues[s._dsv]._depthStencil._stencil);
        } else if (s._dsvLoad == LoadStore::Clear || s._dsvLoad == LoadStore::Clear_RetainStencil) {
            glClearBufferfi(GL_DEPTH, 0, clearValues[s._dsv]._depthStencil._depth, clearValues[s._dsv]._depthStencil._stencil);
        } else if (s._dsvLoad == LoadStore::DontCare_ClearStencil || s._dsvLoad == LoadStore::Retain_ClearStencil) {
            glClearBufferfi(GL_STENCIL, 0, clearValues[s._dsv]._depthStencil._depth, clearValues[s._dsv]._depthStencil._stencil);
        }
    }

    RenderTargetView& FrameBuffer::GetRTV(unsigned index) { assert(index < _attachmentCount); return _rtvs[index]; }
    DepthStencilView& FrameBuffer::GetDSV(unsigned index) { assert(index < _attachmentCount); return _dsvs[index]; }
    
	FrameBuffer::FrameBuffer() {}
    FrameBuffer::~FrameBuffer() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static unsigned s_nextSubpass = 0;
    static std::vector<ClearValue> s_clearValues;

    void BeginRenderPass(
        DeviceContext& context,
        FrameBuffer& frameBuffer,
        const FrameBufferDesc& layout,
        const FrameBufferProperties& props,
        IteratorRange<const ClearValue*> clearValues)
    {
        s_nextSubpass = 0;
        s_clearValues.clear();
        s_clearValues.insert(s_clearValues.end(), clearValues.begin(), clearValues.end());
        BeginNextSubpass(context, frameBuffer);
    }

    void BeginNextSubpass(
        DeviceContext& context,
        FrameBuffer& frameBuffer)
    {
        // Queue up the next render targets
        auto subpassIndex = s_nextSubpass;
        frameBuffer.BindSubpass(context, subpassIndex, MakeIteratorRange(s_clearValues));
        ++s_nextSubpass;
    }

    void EndRenderPass(DeviceContext& context)
    {
        // For compatibility with Vulkan, it makes sense to unbind render targets here. This is important
        // if the render targets will be used as compute shader outputs in follow up steps. It also prevents
        // rendering outside of render passes. But sometimes it will produce redundant calls to OMSetRenderTargets().
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class FrameBufferPool::Pimpl 
    {
    public:
    };

    std::shared_ptr<FrameBuffer> FrameBufferPool::BuildFrameBuffer(
		ObjectFactory& factory,
		const FrameBufferDesc& desc,
        const FrameBufferProperties& props,
        const INamedAttachments& namedResources,
        uint64 hashName)
    {
        return std::make_shared<FrameBuffer>(factory, desc, namedResources);
    }

    FrameBufferPool::FrameBufferPool()
    {
        _pimpl = std::make_unique<Pimpl>();
    }

    FrameBufferPool::~FrameBufferPool()
    {}



}}
