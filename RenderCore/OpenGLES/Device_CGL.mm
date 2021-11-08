// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Device_CGL.h"
#include "Metal/DeviceContext.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Resource.h"
#include "Metal/QueryPool.h"
#include "Metal/Shader.h"
#include "../Init.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Exceptions.h"
#include "../../OSServices/Log.h"
#include <type_traits>
#include <iostream>
#include <assert.h>

#include <OpenGL/OpenGL.h>
#include <AppKit/NSOpenGL.h>
#include <AppKit/NSOpenGLView.h>

#pragma GCC diagnostic ignored "-Wunused-value"

namespace RenderCore { namespace ImplOpenGLES
{
    static Metal_OpenGLES::FeatureSet::BitField GetFeatureSet()
    {
        Metal_OpenGLES::FeatureSet::BitField featureSet = Metal_OpenGLES::FeatureSet::GLES200; // for now, fake with gles2 feature set

        const char* extensionsString = (const char*)glGetString(GL_EXTENSIONS);
        if (extensionsString) {
            if (strstr(extensionsString, "AMD_compressed_ATC_texture") || strstr(extensionsString, "ATI_texture_compression_atitc")) {
                featureSet |= Metal_OpenGLES::FeatureSet::ATITC;
            }

            if (strstr(extensionsString, "GL_EXT_debug_label")) {
                featureSet |= Metal_OpenGLES::FeatureSet::LabelObject;
            }
        }

        return featureSet;
    }

    static const CGLPixelFormatAttribute* GetPixelFormatAttributes()
    {
        static unsigned pixelAttrs[] = {
            kCGLPFADoubleBuffer,
            kCGLPFAAccelerated,
            kCGLPFAClosestPolicy,
            // kCGLPFAOpenGLProfile, kCGLOGLPVersion_GL4_Core, // TODO: one day!
            kCGLPFAColorSize, 24,
            kCGLPFAAlphaSize, 8,
            kCGLPFADepthSize, 24,
            kCGLPFAStencilSize, 8,
            0,
        };
        return (const CGLPixelFormatAttribute*)pixelAttrs;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    Device::Device()
    {
        int virtualScreenCount;
        auto error = CGLChoosePixelFormat(GetPixelFormatAttributes(), &_mainPixelFormat, &virtualScreenCount);
        assert(!error);
        assert(_mainPixelFormat);
        (void)virtualScreenCount;

        error = CGLCreateContext(_mainPixelFormat, nullptr, &_sharedContext);

        assert(!error);
        assert(_sharedContext);

        CGLSetCurrentContext(_sharedContext);

        _objectFactory = std::make_shared<Metal_OpenGLES::ObjectFactory>(GetFeatureSet());
    }

    Device::~Device()
    {
        _objectFactory.reset();
        CGLSetCurrentContext(nullptr);
        CGLReleaseContext(_sharedContext);
        CGLReleasePixelFormat(_mainPixelFormat);
    }

    std::unique_ptr<IPresentationChain> Device::CreatePresentationChain(const void* platformWindowHandle, const PresentationChainDesc& desc)
    {
        return std::make_unique<PresentationChain>(*_objectFactory, _sharedContext, platformWindowHandle, desc);
    }

    std::shared_ptr<IThreadContext> Device::GetImmediateContext()
    {
        if (!_immediateContext) {
            _immediateContext = std::make_shared<ThreadContextOpenGLES>(_sharedContext, shared_from_this());
        }
        return _immediateContext;
    }

    std::unique_ptr<IThreadContext> Device::CreateDeferredContext()
    {
        CGLContextObj deferredContext = 0;
        auto error = CGLCreateContext(_mainPixelFormat, _sharedContext, &deferredContext);
        assert(!error); (void)error;
        return std::make_unique<ThreadContextOpenGLES>(deferredContext, shared_from_this());
    }

    FormatCapability Device::QueryFormatCapability(Format format, BindFlag::BitField bindingType)
    {
        auto activeFeatureSet = _objectFactory->GetFeatureSet();
        auto glFmt = Metal_OpenGLES::AsTexelFormatType(format);
        if (glFmt._internalFormat == GL_NONE) {
            return FormatCapability::NotSupported;
        }

        bool supported = true;
        if (bindingType & BindFlag::DepthStencil) {
            return FormatCapability::Supported;
        } else if (bindingType & BindFlag::ShaderResource) {
            supported &= !!(activeFeatureSet & glFmt._textureFeatureSet);
        } else if (bindingType & BindFlag::RenderTarget) {
            supported &= !!(activeFeatureSet & glFmt._renderbufferFeatureSet);
        }

        return supported ? FormatCapability::Supported : FormatCapability::NotSupported;
    }

    void* Device::QueryInterface(size_t guid)
    {
        return nullptr;
    }
    
    IResourcePtr Device::CreateResource(const ResourceDesc& desc, const ResourceInitializer& init)
    {
        return Metal_OpenGLES::CreateResource(*_objectFactory, desc, init);
    }

    std::shared_ptr<ILowLevelCompiler>        Device::CreateShaderCompiler()
    {
        return Metal_OpenGLES::CreateLowLevelShaderCompiler(*this);
    }

    void Device::Stall()
    {
        glFinish();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    Metal_OpenGLES::FeatureSet::BitField DeviceOpenGLES::GetFeatureSet()
    {
        return _objectFactory->GetFeatureSet();
    }

    unsigned DeviceOpenGLES::GetNativeFormatCode()
    {
        return 0;
    }

    void* DeviceOpenGLES::QueryInterface(size_t guid)
    {
        if (guid == typeid(IDeviceOpenGLES).hash_code()) {
            return (IDeviceOpenGLES*)this;
        }
        return nullptr;
    }

    DeviceOpenGLES::DeviceOpenGLES() {}
    DeviceOpenGLES::~DeviceOpenGLES() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    PresentationChain::PresentationChain(Metal_OpenGLES::ObjectFactory& objFactory, CGLContextObj sharedContext, const void* platformValue, const PresentationChainDesc& pDesc)
    {
        auto desc = pDesc;
        desc._format = Format::R8G8B8A8_UNORM_SRGB; // we can't get the actual format from the _CGL device easily, so just put in a dummy value here
        auto textureDesc = TextureDesc::Plain2D(desc._width, desc._height, desc._format, 1, 0, desc._samples);
        if (desc._bindFlags & BindFlag::ShaderResource) {
            _backBufferDesc = CreateDesc(BindFlag::ShaderResource | BindFlag::RenderTarget, 0, GPUAccess::Read | GPUAccess::Write, textureDesc, "backbuffer");
        } else {
            _backBufferDesc = CreateDesc(BindFlag::RenderTarget, 0, GPUAccess::Write, textureDesc, "backbuffer");
        }

        _desc = std::make_shared<PresentationChainDesc>(desc);
        _sharedContext = sharedContext;
        _platformValue = platformValue;
        CreateUnderlyingContext(objFactory);
    }

    PresentationChain::~PresentationChain() {}

    void PresentationChain::CreateUnderlyingContext(Metal_OpenGLES::ObjectFactory& objFactory)
    {
        // destroy the previous context before we started creating the new one
        _nsContext = nullptr;

        id objCObj = (id)_platformValue;
        if ([objCObj isKindOfClass:NSOpenGLView.class]) {
            _nsContext = ((NSOpenGLView*)objCObj).openGLContext;
        } else {
            int virtualScreenCount;
            CGLPixelFormatObj pixelFormat;
            auto error = CGLChoosePixelFormat(GetPixelFormatAttributes(), &pixelFormat, &virtualScreenCount);
            assert(!error);
            assert(pixelFormat);
            (void)virtualScreenCount;

            CGLContextObj context;
            error = CGLCreateContext(pixelFormat, _sharedContext, &context);
            assert(!error);
            assert(context);
            CGLReleasePixelFormat(pixelFormat);

            _nsContext = moveptr([[NSOpenGLContext alloc] initWithCGLContextObj:context]);
            CGLReleaseContext(context);

            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wdeprecated-declarations"
            _nsContext.get().view = (NSView*)_platformValue;
            #pragma clang diagnostic pop
        }

        CreateUnderlyingBuffers(objFactory);
    }

    void PresentationChain::CreateUnderlyingBuffers(Metal_OpenGLES::ObjectFactory& objFactory)
    {
        // the correct context must be bound
        CGLSetCurrentContext(_nsContext.get().CGLContextObj);

        // destroy all existing buffers before (re-)creating them
        _backBufferResource.reset();
        _fakeBackBuffer.reset();
        _fakeBackBufferResolveBuffer.reset();
        _fakeBackBufferFrameBuffer.reset();
        _fakeBackBufferResolveFrameBuffer.reset();

        const bool useFakeBackbuffer = false;
        if (useFakeBackbuffer || (_backBufferDesc._bindFlags & BindFlag::ShaderResource)) {
            _fakeBackBuffer = std::make_shared<Metal_OpenGLES::Resource>(objFactory, _backBufferDesc);

            if (_backBufferDesc._bindFlags & BindFlag::ShaderResource) {
                if (_backBufferDesc._textureDesc._samples._sampleCount > 1) {
                    Log(Error) << "Requested back buffer MSAA as well as readable main color buffer, MSAA samples is ignored" << std::endl;
                }

                _fakeBackBufferFrameBuffer = objFactory.CreateFrameBuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _fakeBackBufferFrameBuffer->AsRawGLHandle());
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _fakeBackBuffer->GetTexture()->AsRawGLHandle(), 0);
                GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
                glDrawBuffers(1, drawBuffers);
            } else {
                // create MSAA resolve buffer if needed
                if (_backBufferDesc._textureDesc._samples._sampleCount > 1) {
                    auto resolveBufferDesc = _backBufferDesc;
                    resolveBufferDesc._textureDesc._samples._sampleCount = 0;
                    _fakeBackBufferResolveBuffer = std::make_shared<Metal_OpenGLES::Resource>(objFactory, resolveBufferDesc);

                    _fakeBackBufferResolveFrameBuffer = objFactory.CreateFrameBuffer();
                    glBindFramebuffer(GL_FRAMEBUFFER, _fakeBackBufferResolveFrameBuffer->AsRawGLHandle());
                    glBindRenderbuffer(GL_RENDERBUFFER, _fakeBackBufferResolveBuffer->GetRenderBuffer()->AsRawGLHandle());
                    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _fakeBackBufferResolveBuffer->GetRenderBuffer()->AsRawGLHandle());
                    GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
                    glDrawBuffers(1, drawBuffers);
                }

                _fakeBackBufferFrameBuffer = objFactory.CreateFrameBuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _fakeBackBufferFrameBuffer->AsRawGLHandle());
                glBindRenderbuffer(GL_RENDERBUFFER, _fakeBackBuffer->GetRenderBuffer()->AsRawGLHandle());
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _fakeBackBuffer->GetRenderBuffer()->AsRawGLHandle());
                GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
                glDrawBuffers(1, drawBuffers);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            _backBufferResource = std::make_shared<Metal_OpenGLES::Resource>(Metal_OpenGLES::Resource::CreateBackBuffer(_backBufferDesc));
        }
    }

    void PresentationChain::Resize(unsigned newWidth, unsigned newHeight)
    {
        if (newWidth == _backBufferDesc._textureDesc._width && newHeight == _backBufferDesc._textureDesc._height) {
            return;
        }

        _backBufferDesc._textureDesc._width = newWidth;
        _backBufferDesc._textureDesc._height = newHeight;
        _desc->_width = _backBufferDesc._textureDesc._width;
        _desc->_height = _backBufferDesc._textureDesc._height;

        auto& objFactory = Metal_OpenGLES::GetObjectFactory(*_fakeBackBuffer);
        CreateUnderlyingContext(objFactory); // This shouldn't be required, but Testbed on macOS renders to quarter resolution on 1x displays without it...
        CreateUnderlyingBuffers(objFactory);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContext::ThreadContext(CGLContextObj sharedContext, const std::shared_ptr<Device>& device)
        : _device(device), _sharedContext(sharedContext), _activeFrameContext(nullptr)
    {
        CGLRetainContext(_sharedContext);
    }

    ThreadContext::~ThreadContext()
    {
        if (_activeFrameContext) {
            CGLReleaseContext(_activeFrameContext);
            _activeFrameContext = nullptr;
        }
        if (_sharedContext) {
            CGLReleaseContext(_sharedContext);
            _sharedContext = nullptr;
        }
    }

    IResourcePtr ThreadContext::BeginFrame(IPresentationChain& presentationChain)
    {
        assert(!_activeFrameContext);
        if (_activeFrameContext) {
            CGLReleaseContext(_activeFrameContext);
            _activeFrameContext = nullptr;
        }

        auto& presChain = *checked_cast<PresentationChain*>(&presentationChain);
        _activeFrameContext = presChain.GetUnderlying().get().CGLContextObj;
        CGLRetainContext(_activeFrameContext);
        CGLSetCurrentContext(_activeFrameContext);

        if (presChain._fakeBackBuffer) {
            return presChain._fakeBackBuffer;
        } else {
            return presChain._backBufferResource;
        }
    }

    void ThreadContext::Present(IPresentationChain& presentationChain)
    {
        auto& presChain = *checked_cast<PresentationChain*>(&presentationChain);
        assert(presChain.GetUnderlying().get().CGLContextObj == _activeFrameContext);

        if (_activeFrameContext) {
            // If using "fake back buffer" mode, blit into the true back buffer
            if (presChain._fakeBackBuffer) {
                GLuint srcFramebuffer = presChain._fakeBackBufferFrameBuffer->AsRawGLHandle();
                NSSize srcSize = NSMakeSize(presChain._backBufferDesc._textureDesc._width, presChain._backBufferDesc._textureDesc._height);

                // If using MSAA, blit into a non-multisampling buffer (resolve buffer) first
                if (presChain._fakeBackBufferResolveFrameBuffer) {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, presChain._fakeBackBufferFrameBuffer->AsRawGLHandle());
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, presChain._fakeBackBufferResolveFrameBuffer->AsRawGLHandle());
                    glBlitFramebuffer(0, 0, (GLint)srcSize.width, (GLint)srcSize.height, 0, 0, (GLint)srcSize.width, (GLint)srcSize.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    srcFramebuffer = presChain._fakeBackBufferResolveFrameBuffer->AsRawGLHandle();
                }
                
                glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFramebuffer);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, (GLint)srcSize.width, (GLint)srcSize.height, 0, 0, (GLint)srcSize.width, (GLint)srcSize.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }

            CGLFlushDrawable(_activeFrameContext);
            CGLReleaseContext(_activeFrameContext);
            _activeFrameContext = nullptr;
        }

        CGLSetCurrentContext(_sharedContext);
    }

    void ThreadContext::CommitCommands(CommitCommandsFlags::BitField flags)
    {
        assert(flags==0);
        assert(!_activeFrameContext); // If you're actively rendering, you need Present instead
        if (CGLGetCurrentContext()) {
            glFlush();
        }
    }

    std::shared_ptr<IDevice> ThreadContext::GetDevice() const
    {
        return _device.lock();
    }

    IAnnotator& ThreadContext::GetAnnotator()
    {
        if (!_annotator) {
            auto d = _device.lock();
            assert(d);
            _annotator = std::make_unique<Metal_OpenGLES::Annotator>();
        }
        return *_annotator;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContextOpenGLES::ThreadContextOpenGLES(CGLContextObj sharedContext, const std::shared_ptr<Device>& device)
        : ThreadContext(sharedContext, device)
    {
        _deviceContext = std::make_shared<Metal_OpenGLES::DeviceContext>(device, GetFeatureSet());
    }

    ThreadContextOpenGLES::~ThreadContextOpenGLES() {}

    void* ThreadContextOpenGLES::QueryInterface(size_t guid)
    {
        if (guid == typeid(IThreadContextOpenGLES).hash_code()) {
            return (IThreadContextOpenGLES*)this;
        }
        return nullptr;
    }

    bool ThreadContextOpenGLES::IsBoundToCurrentThread()
    {
        CGLContextObj currentContext = CGLGetCurrentContext();
        if (_activeFrameContext) {
            return _activeFrameContext == currentContext;
        }
        return _sharedContext == currentContext;
    }

    bool ThreadContextOpenGLES::BindToCurrentThread()
    {
        auto error = CGLSetCurrentContext(_activeFrameContext ? _activeFrameContext : _sharedContext);
        return error == kCGLNoError;
    }

    void ThreadContextOpenGLES::UnbindFromCurrentThread()
    {
        glFlush();
        CGLSetCurrentContext(nullptr);
    }

    std::shared_ptr<IThreadContext> ThreadContextOpenGLES::Clone()
    {
        return nullptr;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    render_dll_export std::shared_ptr<IDevice> CreateDevice()
    {
        return std::make_shared<DeviceOpenGLES>();
    }

    void RegisterCreation()
    {
        (void)static_constructor<&RegisterCreation>::c;
        RegisterDeviceCreationFunction(UnderlyingAPI::OpenGLES, &CreateDevice);
    }

} }
