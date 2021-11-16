// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDeviceOpenGLES.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "Metal/ObjectFactory.h"
#include "../../Utility/IntrusivePtr.h"
#include "../../../Utility/OCUtils.h"

namespace RenderCore { namespace Metal_OpenGLES { class ObjectFactory; class Resource; } }

@class NSOpenGLContext;
typedef struct _CGLContextObject *CGLContextObj;
typedef struct _CGLPixelFormatObject *CGLPixelFormatObj;

namespace RenderCore { namespace ImplOpenGLES
{
////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        virtual void Resize(unsigned newWidth, unsigned newHeight) override;
        virtual const std::shared_ptr<PresentationChainDesc>& GetDesc() const override { return _desc; }

        const OCPtr<NSOpenGLContext>& GetUnderlying() { return _nsContext; }

        PresentationChain(Metal_OpenGLES::ObjectFactory& objFactory, CGLContextObj sharedContext, const void* platformValue, const PresentationChainDesc& desc);
        ~PresentationChain();

    private:
        OCPtr<NSOpenGLContext> _nsContext;
        CGLContextObj _sharedContext;
        const void* _platformValue;
        std::shared_ptr<PresentationChainDesc> _desc;
        ResourceDesc _backBufferDesc;

        std::shared_ptr<Metal_OpenGLES::Resource> _backBufferResource;
        std::shared_ptr<Metal_OpenGLES::Resource> _fakeBackBuffer;
        std::shared_ptr<Metal_OpenGLES::Resource> _fakeBackBufferResolveBuffer;
        intrusive_ptr<OpenGL::FrameBuffer> _fakeBackBufferFrameBuffer;
        intrusive_ptr<OpenGL::FrameBuffer> _fakeBackBufferResolveFrameBuffer;

        void CreateUnderlyingContext(Metal_OpenGLES::ObjectFactory& objFactory);
        void CreateUnderlyingBuffers(Metal_OpenGLES::ObjectFactory& objFactory);

        friend class ThreadContext;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device;

    class ThreadContext : public IThreadContext
    {
    public:
        virtual IResourcePtr BeginFrame(IPresentationChain& presentationChain) override;
        virtual void Present(IPresentationChain& presentationChain) override;
        virtual void CommitCommands(CommitCommandsFlags::BitField=0) override;

        virtual bool IsImmediate() const override { return false; }
        virtual std::shared_ptr<IDevice> GetDevice() const override;
        virtual void InvalidateCachedState() const override {}

        virtual IAnnotator& GetAnnotator() override;
        virtual ThreadContextStateDesc GetStateDesc() const override { return {}; }

        CGLContextObj GetSharedContext() { return _sharedContext; }
        CGLContextObj GetActiveFrameContext() { return _activeFrameContext; }

        ThreadContext(CGLContextObj sharedContext, const std::shared_ptr<Device>& device);
        ~ThreadContext();

    protected:
        std::weak_ptr<Device> _device; // (must be weak, because Device holds a shared_ptr to the immediate context)
        std::unique_ptr<IAnnotator> _annotator;

        CGLContextObj _activeFrameContext;
        CGLContextObj _sharedContext;
    };

    class ThreadContextOpenGLES : public ThreadContext, public IThreadContextOpenGLES
    {
    public:
        virtual const std::shared_ptr<Metal_OpenGLES::DeviceContext>& GetDeviceContext() override { return _deviceContext; }
        virtual bool IsBoundToCurrentThread() override;
        virtual bool BindToCurrentThread() override;
        virtual void UnbindFromCurrentThread() override;
        virtual void* QueryInterface(size_t guid) override;
        virtual std::shared_ptr<IThreadContext> Clone() override;

        ThreadContextOpenGLES(CGLContextObj sharedContext, const std::shared_ptr<Device>& device);
        ~ThreadContextOpenGLES();

    private:
        std::shared_ptr<Metal_OpenGLES::DeviceContext> _deviceContext;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device : public IDevice, public std::enable_shared_from_this<Device>
    {
    public:
        virtual std::unique_ptr<IPresentationChain> CreatePresentationChain(const void* platformWindowHandle, const PresentationChainDesc& desc) override;

        virtual void* QueryInterface(size_t guid) override;

        virtual std::shared_ptr<IThreadContext> GetImmediateContext() override;
        virtual std::unique_ptr<IThreadContext> CreateDeferredContext() override;

        using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
        virtual IResourcePtr CreateResource(const ResourceDesc& desc, const ResourceInitializer& init) override;
        virtual FormatCapability QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        virtual void Stall() override;

        virtual DeviceDesc GetDesc() override { return DeviceDesc { "OpenGLES-CGL", "", "" }; }

        virtual std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler() override;

        Device();
        ~Device();

    protected:
        std::shared_ptr<ThreadContextOpenGLES> _immediateContext;
        std::shared_ptr<Metal_OpenGLES::ObjectFactory> _objectFactory;
        CGLContextObj _sharedContext;

        CGLPixelFormatObj _mainPixelFormat;
    };

    class DeviceOpenGLES : public Device, public IDeviceOpenGLES
    {
    public:
        virtual Metal_OpenGLES::FeatureSet::BitField GetFeatureSet() override;
        virtual unsigned GetNativeFormatCode() override;
        virtual void* QueryInterface(size_t guid) override;

        DeviceOpenGLES();
        ~DeviceOpenGLES();
    };

////////////////////////////////////////////////////////////////////////////////
}}
