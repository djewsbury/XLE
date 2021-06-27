// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ObjectFactory.h"
#include "../../ResourceDesc.h"
#include "../../IDevice.h"
#include "../../../Utility/IteratorUtils.h"
#include <vector>
#include <memory>

namespace RenderCore
{
    class SubResourceInitData;
}

namespace RenderCore { namespace Metal_AppleMetal
{
    class ObjectFactory;

    class Resource : public IResource, public std::enable_shared_from_this<Resource>
    {
    public:
        // --------------- Cross-GFX-API interface ---------------
        using Desc = ResourceDesc;
        virtual Desc        GetDesc() const override;
        virtual void*       QueryInterface(size_t guid) override;
        virtual uint64_t    GetGUID() const override;
        virtual std::vector<uint8_t>    ReadBackSynchronized(IThreadContext& context, SubResourceId subRes) const override;
        virtual std::shared_ptr<IResourceView>  CreateTextureView(BindFlag::Enum usage, const TextureViewDesc& window) override;
        virtual std::shared_ptr<IResourceView>  CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize) override;

        // --------------- Apple Metal specific interface ---------------
        AplMtlTexture* GetTexture() const { return _underlyingTexture; }; // <MTLTexture>
        AplMtlBuffer* GetBuffer() const { return _underlyingBuffer; }; // <MTLBuffer>

        Resource(
            ObjectFactory& factory, const Desc& desc,
            const SubResourceInitData& initData = SubResourceInitData{});
        Resource(
            ObjectFactory& factory, const Desc& desc,
            const IDevice::ResourceInitializer& initData);

        Resource(const id<MTLTexture>&, const ResourceDesc& = {});
        Resource(const id<MTLTexture>&, const ResourceDesc&, uint64_t guidOverride);

        Resource();
        ~Resource();

        static uint64_t ReserveGUID();

    protected:
        OCPtr<AplMtlBuffer> _underlyingBuffer; // id<MTLBuffer>
        OCPtr<AplMtlTexture> _underlyingTexture; // id<MTLTexture>
        Desc _desc;
        uint64_t _guid;
    };

    inline void CompleteInitialization(
		DeviceContext& context,
		IteratorRange<IResource* const*> resources) {}

    class BlitEncoder
    {
    public:
        class CopyPartial_Dest
        {
        public:
            IResource*          _resource;
            SubResourceId       _subResource;
            VectorPattern<unsigned, 3>      _leftTopFront;
        };

        class CopyPartial_Src
        {
        public:
            IResource*          _resource;
            SubResourceId       _subResource;
            VectorPattern<unsigned, 3>      _leftTopFront;
            VectorPattern<unsigned, 3>      _rightBottomBack;
        };

        void    Write(
            const CopyPartial_Dest& dst,
            const SubResourceInitData& srcData,
            Format srcDataFormat,
            VectorPattern<unsigned, 3> srcDataDimensions);

        void    Copy(
            const CopyPartial_Dest& dst,
            const CopyPartial_Src& src);

        void    Copy(
			IResource& dst,
			IResource& src);

        ~BlitEncoder();

        BlitEncoder(const BlitEncoder&) = delete;
		BlitEncoder& operator=(const BlitEncoder&) = delete;
    private:
        BlitEncoder(DeviceContext& devContext);
        DeviceContext* _devContext = nullptr;
        bool _openedEncoder = false;
        friend class DeviceContext;
    };

    namespace Internal
	{
        ResourceDesc ExtractDesc(const IResource& input);
        ResourceDesc ExtractRenderBufferDesc(const id<MTLTexture>& texture);
        RawMTLHandle GetBufferRawMTLHandle(const IResource& resource);
    }
}}
