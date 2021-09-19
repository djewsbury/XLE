#pragma once

#include "../../ResourceDesc.h"
#include "../../ResourceUtils.h"        // for CopyPartial_Dest, CopyPartial_Src
#include "../../IDevice.h"
#include "ObjectFactory.h"
#include "../../../Utility/IteratorUtils.h"

namespace RenderCore
{
    class SubResourceInitData;
}

namespace RenderCore { namespace Metal_OpenGLES
{
    class ObjectFactory;

    class Resource : public IResource
    {
    public:
        using Desc = ResourceDesc;

        Desc GetDesc() const override        { return _desc; }

        const intrusive_ptr<OpenGL::Buffer>& GetBuffer() const { return _underlyingBuffer; }
        const intrusive_ptr<OpenGL::Texture>& GetTexture() const { return _underlyingTexture; }
        const intrusive_ptr<OpenGL::RenderBuffer>& GetRenderBuffer() const { return _underlyingRenderBuffer; }
        IteratorRange<const void*> GetConstantBuffer() const { return MakeIteratorRange(_constantBuffer); }
        uint64_t GetConstantBufferHash() const { return _constantBufferHash; }
        const bool IsBackBuffer() { return _isBackBuffer; }

        virtual void*       QueryInterface(size_t guid) override;
        virtual uint64_t    GetGUID() const override;
        virtual std::vector<uint8_t> ReadBack(IThreadContext& context, SubResourceId subRes) const override;

        Resource(
            ObjectFactory& factory, const Desc& desc,
            const SubResourceInitData& initData = SubResourceInitData{});
        Resource(
            ObjectFactory& factory, const Desc& desc,
            const IDevice::ResourceInitializer& initData);

        Resource(const intrusive_ptr<OpenGL::Texture>&, const ResourceDesc& = {});
        Resource(const intrusive_ptr<OpenGL::RenderBuffer>&);
        Resource(const intrusive_ptr<OpenGL::Buffer>&);
        Resource();
        ~Resource();

        mutable uint64_t _lastBoundSamplerState = ~0u;

        static Resource CreateBackBuffer(const Desc& desc);
    protected:
        intrusive_ptr<OpenGL::Buffer> _underlyingBuffer;
        intrusive_ptr<OpenGL::Texture> _underlyingTexture;
        intrusive_ptr<OpenGL::RenderBuffer> _underlyingRenderBuffer;
        std::vector<uint8_t> _constantBuffer;
        uint64_t _constantBufferHash = 0ull;
        bool _isBackBuffer;
        Desc _desc;
        uint64_t _guid;
    };

    GLenum AsBufferTarget(BindFlag::BitField bindFlags);
    GLenum AsUsageMode(CPUAccess::BitField cpuAccess, GPUAccess::BitField gpuAccess);

    inline RawGLHandle GetBufferRawGLHandle(const IResource& resource)
    {
        return static_cast<const Resource&>(resource).GetBuffer()->AsRawGLHandle();
    }

    ResourceDesc ExtractDesc(const IResource& res);
    IResourcePtr CreateResource(
        ObjectFactory& factory, const ResourceDesc& desc,
        const IDevice::ResourceInitializer& initData = nullptr);

    ResourceDesc ExtractDesc(OpenGL::RenderBuffer* renderbuffer);
    ResourceDesc ExtractDesc(OpenGL::Texture* renderbuffer);
    ResourceDesc ExtractDesc(OpenGL::Buffer* renderbuffer);
    std::string DescribeUnknownObject(unsigned glName);

    class BlitPass
    {
    public:
        void    Write(
            const CopyPartial_Dest& dst,
            const SubResourceInitData& srcData,
            Format srcDataFormat,
            VectorPattern<unsigned, 3> srcDataDimensions);

        void    Copy(
            const CopyPartial_Dest& dst,
            const CopyPartial_Src& src);

        BlitPass(IThreadContext& threadContext);
        ~BlitPass();

    private:
        GLint _prevTextureBinding;
        GLint _prevUnpackAlignment, _prevUnpackRowLength;
        bool _boundTexture;
    };
}}


