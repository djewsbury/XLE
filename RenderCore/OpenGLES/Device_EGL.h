
#pragma once

#include "../IDevice.h"
#include "IDeviceOpenGLES.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Resource.h"
#include "../../Utility/IntrusivePtr.h"

#include <EGL/egl.h>
#undef None
#undef Bool
#undef Success
#undef Complex

namespace RenderCore { namespace ImplOpenGLES
{
////////////////////////////////////////////////////////////////////////////////

    class PresentationChain : public IPresentationChain
    {
    public:
        virtual void Resize(unsigned newWidth, unsigned newHeight) override;
        virtual const std::shared_ptr<PresentationChainDesc>& GetDesc() const override { return _desc; }

        const std::shared_ptr<Metal_OpenGLES::Resource>& GetTargetRenderbuffer();

        unsigned GetGUID() const { return _guid; }
        EGLSurface GetSurface() const { return _surface; }
        EGLDisplay GetDisplay() const { return _display; }

        PresentationChain(EGLDisplay display, EGLConfig sharedContextCfg, const void *platformValue, const PresentationChainDesc& desc);
        ~PresentationChain();

    private:
        unsigned _guid;
        EGLSurface _surface;
        EGLDisplay _display;
        std::shared_ptr<Metal_OpenGLES::Resource> _targetRenderbuffer;
        std::shared_ptr<PresentationChainDesc> _desc;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device;
    
    class ThreadContext : public IThreadContext, public IThreadContextOpenGLES
    {
    public:
        virtual IResourcePtr BeginFrame(IPresentationChain& presentationChain) override;
        virtual void Present(IPresentationChain& presentationChain) override;
        virtual void CommitCommands(CommitCommandsFlags::BitField=0) override;

        virtual void* QueryInterface(size_t guid) override;
        virtual bool IsImmediate() const override { return false; }
        virtual std::shared_ptr<IDevice> GetDevice() const override;
        virtual void InvalidateCachedState() const override {}

        virtual IAnnotator& GetAnnotator() override;
        virtual ThreadContextStateDesc GetStateDesc() const override { return {}; }

        virtual const std::shared_ptr<Metal_OpenGLES::DeviceContext>& GetDeviceContext() override { return _deviceContext; }
        virtual bool IsBoundToCurrentThread() override;
        virtual bool BindToCurrentThread() override;
        virtual void UnbindFromCurrentThread() override;
        virtual std::shared_ptr<IThreadContext> Clone() override;

        void SetFeatureSet(unsigned featureSet);
        unsigned GetFeatureSet() const;

        EGLContext GetUnderlying() { return _context; }

        ThreadContext(EGLDisplay display, EGLConfig cfgForNewContext, EGLContext rootContext, unsigned featureSet, const std::shared_ptr<Device>& device);
        ~ThreadContext();

        ThreadContext(const ThreadContext&) = delete;
        ThreadContext& operator=(const ThreadContext&) = delete;

        // The following constructor is only used by Clone()
        ThreadContext(EGLDisplay display, EGLContext context, EGLSurface dummySurface, unsigned featureSet, const std::shared_ptr<Device>& device, bool);
    protected:
        EGLContext _context = EGL_NO_CONTEXT;
        EGLSurface _dummySurface = EGL_NO_SURFACE;
        unsigned _currentPresentationChainGUID;
        EGLDisplay _display = EGL_NO_DISPLAY;
        bool _clonedContext = false;

        std::weak_ptr<Device> _device;
        std::unique_ptr<IAnnotator> _annotator;
        std::shared_ptr<Metal_OpenGLES::DeviceContext> _deviceContext;

        std::shared_ptr<Metal_OpenGLES::Resource> _activeTargetRenderbuffer;
        intrusive_ptr<OpenGL::FrameBuffer> _temporaryFramebuffer;
    };

////////////////////////////////////////////////////////////////////////////////

    class Device : public IDevice, public IDeviceOpenGLES, public std::enable_shared_from_this<Device>
    {
    public:
        virtual std::unique_ptr<IPresentationChain> CreatePresentationChain(const void* platformWindowHandle, const PresentationChainDesc& desc) override;

        virtual void* QueryInterface(size_t guid) override;

        virtual std::shared_ptr<IThreadContext> GetImmediateContext() override;
        virtual std::unique_ptr<IThreadContext> CreateDeferredContext() override;

        using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
        virtual IResourcePtr CreateResource(const ResourceDesc& desc, const ResourceInitializer& init) override;
        virtual FormatCapability QueryFormatCapability(Format format, BindFlag::BitField bindingType) override;

        virtual DeviceDesc GetDesc() override { return DeviceDesc { "OpenGLES-EGL", "", "" }; }

        std::shared_ptr<ILowLevelCompiler> CreateShaderCompiler() override;

        virtual void Stall() override;

        EGLDisplay GetDisplay() const { return _display; };
        EGLConfig GetRootContextConfig() const { return _rootContextConfig; }
        unsigned GetGLESVersion() const;

        virtual Metal_OpenGLES::FeatureSet::BitField GetFeatureSet() override;
        virtual unsigned GetNativeFormatCode() override;

        virtual void InitializeRootContextForPresentation(const PresentationChainDesc& desc) override;
        virtual void InitializeRootContextHeadless(Format pbufferFmt, TextureSamples samples) override;

        Device();
        ~Device();

    protected:
        std::shared_ptr<ThreadContext> _rootContext;
        std::shared_ptr<Metal_OpenGLES::ObjectFactory> _objectFactory;

        EGLConfig _rootContextConfig = nullptr;
        EGLConfig _deferredContextConfig = nullptr;
        EGLDisplay _display = EGL_NO_DISPLAY;
        unsigned _glesVersion = 0;

        std::shared_ptr<Metal_OpenGLES::ObjectFactory> GetObjectFactory();

        void InitializeRootContext(EGLConfig presentationCfg, EGLConfig deferredCfg);
    };

////////////////////////////////////////////////////////////////////////////////
} }
