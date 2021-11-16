// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "IDeviceOpenGLES.h"
#include "ObjectFactory.h"
#include "../../Utility/IntrusivePtr.h"
#include "../../../Utility/OCUtils.h"
#include <memory>

@class EAGLContext;

namespace RenderCore { namespace Metal_OpenGLES { class ObjectFactory; class Resource; } }

namespace RenderCore { namespace ImplOpenGLES
{
////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        virtual void Resize(unsigned newWidth, unsigned newHeight) override;
        virtual const std::shared_ptr<PresentationChainDesc>& GetDesc() const override { return _desc; }

        EAGLContext* GetEAGLContext() { return _eaglContext.get(); }
        const std::shared_ptr<Metal_OpenGLES::Resource>& GetFrameRenderbuffer() const { return _frameRenderbuffer; }

        PresentationChain(Metal_OpenGLES::ObjectFactory& objFactory, EAGLContext* sharedContext, const void* platformValue, const PresentationChainDesc& desc);
        ~PresentationChain();

    private:
        OCPtr<EAGLContext> _eaglContext;
        std::shared_ptr<Metal_OpenGLES::Resource> _frameRenderbuffer;
        std::shared_ptr<PresentationChainDesc> _desc;
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

        virtual void* QueryInterface(size_t guid) override;

        void MakeDeferredContext();

        ThreadContext(EAGLContext* sharedContext, const std::shared_ptr<Device>& device);
        ~ThreadContext();

    protected:
        std::weak_ptr<Device> _device; // (must be weak, because Device holds a shared_ptr to the immediate context)
        std::unique_ptr<IAnnotator> _annotator;

        OCPtr<EAGLContext> _activeFrameContext;
        OCPtr<EAGLContext> _deferredContext;
        OCPtr<EAGLContext> _sharedContext;

        std::shared_ptr<Metal_OpenGLES::Resource> _activeFrameRenderbuffer;
        intrusive_ptr<OpenGL::FrameBuffer> _activeFrameBuffer;
    };

    class ThreadContextOpenGLES : public ThreadContext, public IThreadContextOpenGLES
    {
    public:
        virtual const std::shared_ptr<Metal_OpenGLES::DeviceContext>&  GetDeviceContext() override { return _deviceContext; }
        virtual bool IsBoundToCurrentThread() override;
        virtual bool BindToCurrentThread() override;
        virtual void UnbindFromCurrentThread() override;
        virtual void* QueryInterface(size_t guid) override;
        virtual std::shared_ptr<IThreadContext> Clone() override;
        
        ThreadContextOpenGLES(EAGLContext* sharedContext, const std::shared_ptr<Device>& device);
        ~ThreadContextOpenGLES();
        
    private:
        std::shared_ptr<Metal_OpenGLES::DeviceContext> _deviceContext;
    };

////////////////////////////////////////////////////////////////////////////////

    class BoundContextVerification;

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

        virtual DeviceDesc GetDesc() override { return DeviceDesc { "OpenGLES-EAGL", "", "" }; }
        virtual std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler() override;

        virtual void Stall() override;

        Device();
        ~Device();

    protected:
        std::shared_ptr<ThreadContextOpenGLES> _immediateContext;
        std::shared_ptr<Metal_OpenGLES::ObjectFactory> _objectFactory;
        OCPtr<EAGLContext> _sharedContext;

        #if defined(_DEBUG)
            std::unique_ptr<BoundContextVerification> _boundContextVerification;
        #endif
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

