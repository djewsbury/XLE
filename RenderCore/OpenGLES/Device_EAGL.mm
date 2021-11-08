// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Device_EAGL.h"
#include "Metal/DeviceContext.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Resource.h"
#include "Metal/Format.h"
#include "Metal/QueryPool.h"
#include "Metal/Shader.h"
#include "../Init.h"
#include "../../OSServices/Log.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Exceptions.h"
#include <type_traits>
#include <iostream>
#include <assert.h>
#include "IncludeGLES.h"

#include <OpenGLES/EAGL.h>
#include <UIKit/UIView.h>

#pragma GCC diagnostic ignored "-Wunused-value"

namespace RenderCore { namespace ImplOpenGLES
{
    #if defined(_DEBUG)
        class BoundContextVerification
        {
        public:
            void BindToCurrentThread(size_t id);
            void UnbindFromCurrentThread(size_t id);
            void CheckBinding(size_t id);

            BoundContextVerification();
            ~BoundContextVerification();

            static BoundContextVerification& GetInstance();
        private:
            Threading::Mutex _lock;
            std::vector<std::pair<Threading::ThreadId, size_t>> _bindings;

            static BoundContextVerification* s_instance;
        };

        BoundContextVerification* BoundContextVerification::s_instance = nullptr;

        void BoundContextVerification::BindToCurrentThread(size_t id)
        {
            auto currentThreadId = Threading::CurrentThreadId();

            ScopedLock(_lock);
            for (auto&b:_bindings) {
                if (b.second == id && b.first != currentThreadId) {
                    assert(0);
                }
            }

            for (auto&b:_bindings) {
                if (b.first == currentThreadId) {
                    assert(b.second == id);
                    b.second = id;
                    return;
                }
            }

            _bindings.push_back(std::make_pair(currentThreadId, id));
        }

        void BoundContextVerification::UnbindFromCurrentThread(size_t id)
        {
            auto currentThreadId = Threading::CurrentThreadId();

            ScopedLock(_lock);
            for (auto i=_bindings.begin(); i!=_bindings.end(); ++i) {
                if (i->first == currentThreadId) {
                    assert(i->second == id);
                    _bindings.erase(i);
                    return;
                }
            }

            assert(0);
        }

        void BoundContextVerification::CheckBinding(size_t id)
        {
            auto currentThreadId = Threading::CurrentThreadId();

            ScopedLock(_lock);
            for (const auto&b:_bindings) {
                if (b.first == currentThreadId) {
                    assert(b.second == id);
                    return;
                }
            }

            assert(0);
        }

        BoundContextVerification::BoundContextVerification()
        {
            assert(s_instance == nullptr);
            s_instance = this;
        }

        BoundContextVerification::~BoundContextVerification()
        {
            {
                ScopedLock(_lock);
                assert(_bindings.empty());
            }
            assert(s_instance == this);
            s_instance = nullptr;
        }

        BoundContextVerification& BoundContextVerification::GetInstance()
        {
            return *s_instance;
        }

        static EAGLSharegroup * s_mainShareGroup = nil;

        void CheckContextIntegrity()
        {
            assert(EAGLContext.currentContext);
            assert(EAGLContext.currentContext.sharegroup == s_mainShareGroup);
            BoundContextVerification::GetInstance().CheckBinding((size_t)EAGLContext.currentContext);
        }
    #endif

////////////////////////////////////////////////////////////////////////////////////////////////////////

    static Metal_OpenGLES::FeatureSet::BitField AsGLESFeatureSet(EAGLRenderingAPI api)
    {
        Metal_OpenGLES::FeatureSet::BitField featureSet = Metal_OpenGLES::FeatureSet::GLES200;
        switch (api) {
        default:
        case kEAGLRenderingAPIOpenGLES1: assert(0); break;
        case kEAGLRenderingAPIOpenGLES2: featureSet = Metal_OpenGLES::FeatureSet::GLES200 | Metal_OpenGLES::FeatureSet::ETC1TC; break;
        case kEAGLRenderingAPIOpenGLES3: featureSet = Metal_OpenGLES::FeatureSet::GLES200 | Metal_OpenGLES::FeatureSet::GLES300 | Metal_OpenGLES::FeatureSet::ETC1TC | Metal_OpenGLES::FeatureSet::ETC2TC; break;
        }
        // All Apple / EAGL devices can support PVR textures
        featureSet |= Metal_OpenGLES::FeatureSet::PVRTC;

        const char* extensionsString = (const char*)glGetString(GL_EXTENSIONS);
        if (extensionsString) {
            if (strstr(extensionsString, "GL_EXT_debug_label")) {
                featureSet |= Metal_OpenGLES::FeatureSet::LabelObject;
            }
        }

        return featureSet;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    Device::Device()
    {
        auto* t = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!t) {
            Log(Debug) << "Falling back to OpenGLES2.0 because GLES3.0 context failed to be constructed" << std::endl;
            t = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        }
        _sharedContext = moveptr(t);
        [EAGLContext setCurrentContext:_sharedContext.get()];
        #if defined(_DEBUG)
            _boundContextVerification = std::make_unique<BoundContextVerification>();
            _boundContextVerification->BindToCurrentThread((size_t)_sharedContext.get());
        #endif

        #if defined(_DEBUG)
            s_mainShareGroup = _sharedContext.get().sharegroup;
        #endif

        auto featureSet = AsGLESFeatureSet(_sharedContext.get().API);
        _objectFactory = std::make_shared<Metal_OpenGLES::ObjectFactory>(featureSet);
    }

    Device::~Device()
    {
        if (EAGLContext.currentContext == _sharedContext.get()) {
            #if defined(_DEBUG)
                _boundContextVerification->UnbindFromCurrentThread((size_t)_sharedContext.get());
            #endif
            EAGLContext.currentContext = nil;
        }
        #if defined(_DEBUG)
            _boundContextVerification.reset();
        #endif
    }

    std::unique_ptr<IPresentationChain> Device::CreatePresentationChain(const void* platformValue, const PresentationChainDesc& desc)
    {
        return std::make_unique<PresentationChain>(*_objectFactory, _sharedContext.get(), platformValue, desc);
    }

    std::unique_ptr<IThreadContext> Device::CreateDeferredContext()
    {
        auto result = std::make_unique<ThreadContextOpenGLES>(_sharedContext, shared_from_this());
        result->MakeDeferredContext();
        return std::move(result);
    }

    std::shared_ptr<IThreadContext> Device::GetImmediateContext()
    {
        if (!_immediateContext)
            _immediateContext = std::make_shared<ThreadContextOpenGLES>(_sharedContext, shared_from_this());
        return _immediateContext;
    }

    FormatCapability Device::QueryFormatCapability(Format format, BindFlag::BitField bindingType)
    {
        auto activeFeatureSet = _objectFactory->GetFeatureSet();
        auto glFmt = Metal_OpenGLES::AsTexelFormatType(format);
        if (glFmt._internalFormat == GL_NONE)
            return FormatCapability::NotSupported;

        bool supported = true;
        if (bindingType & BindFlag::ShaderResource) {
            supported &= !!(activeFeatureSet & glFmt._textureFeatureSet);
        } else if ((bindingType & BindFlag::RenderTarget) || (bindingType & BindFlag::DepthStencil)) {
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

    PresentationChain::PresentationChain(Metal_OpenGLES::ObjectFactory& objFactory, EAGLContext* eaglContext, const void* platformValue, const PresentationChainDesc& desc)
    : _eaglContext(eaglContext)
    {
        if (desc._bindFlags & BindFlag::ShaderResource) {
            Throw(std::runtime_error("Readable main color buffer is not supported on iOS; need to use fake backbuffer path"));
        }

        if (![((NSObject*)platformValue) isKindOfClass:UIView.class]) {
            Throw(std::runtime_error("Platform value in PresentationChain::PresentationChain is not a UIView"));
        }

        auto* view = (UIView*)platformValue;
        if (![view.layer conformsToProtocol:@protocol(EAGLDrawable)]) {
            Throw(std::runtime_error("Layer in UIView passed to PresentationChain::PresentationChain does not conform to EAGLDrawable protocol"));
        }

        auto eaglDrawable = (id<EAGLDrawable>)view.layer;
        eaglDrawable.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:FALSE], kEAGLDrawablePropertyRetainedBacking,
                                                                                     kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat,
                                                                                     nil];

        auto frameRenderbuffer = objFactory.CreateRenderBuffer();
        glBindRenderbuffer(GL_RENDERBUFFER, frameRenderbuffer->AsRawGLHandle());

        auto res = [eaglContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglDrawable];
        if (!res) {
            Throw(std::runtime_error("Failed to allocate renderbuffer storage for EAGL drawable in PresentationChain::PresentationChain"));
        }

        _frameRenderbuffer = std::make_shared<Metal_OpenGLES::Resource>(frameRenderbuffer);

        auto drawableDesc = ExtractDesc(*_frameRenderbuffer);
        assert(drawableDesc._type == ResourceDesc::Type::Texture);
        if (desc._width != drawableDesc._textureDesc._width || desc._height != drawableDesc._textureDesc._height) {
            Throw(std::runtime_error("EAGLDrawable size is not the same as input in PresentationChain::PresentationChain"));
        }

        _desc = std::make_shared<PresentationChainDesc>();
        _desc->_width = drawableDesc._textureDesc._width;
        _desc->_height = drawableDesc._textureDesc._height;
        _desc->_format = drawableDesc._textureDesc._format;
        _desc->_samples = drawableDesc._textureDesc._samples;
    }

    PresentationChain::~PresentationChain()
    {
    }

    void PresentationChain::Resize(unsigned newWidth, unsigned newHeight)
    {
        assert(0); // unimplemented
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContext::ThreadContext(EAGLContext* sharedContext, const std::shared_ptr<Device>& device)
        : _device(device), _sharedContext(sharedContext)
    {}

    ThreadContext::~ThreadContext() {}

    IResourcePtr ThreadContext::BeginFrame(IPresentationChain& presentationChain)
    {
        assert(!_activeFrameContext);
        _activeFrameContext = nullptr;

        auto& presChain = *checked_cast<PresentationChain*>(&presentationChain);
        _activeFrameContext = presChain.GetEAGLContext();
        _activeFrameRenderbuffer = presChain.GetFrameRenderbuffer();
        if (!_activeFrameContext) {
            _activeFrameContext = _sharedContext;
        }

        if (EAGLContext.currentContext != _activeFrameContext.get()) {
            [EAGLContext setCurrentContext:_activeFrameContext.get()];
            #if defined(_DEBUG)
                BoundContextVerification::GetInstance().BindToCurrentThread((size_t)_activeFrameContext.get());
            #endif
        }

        return _activeFrameRenderbuffer;
    }

    void ThreadContext::Present(IPresentationChain& presentationChain)
    {
        #if !defined(NDEBUG)
            auto& presChain = *checked_cast<PresentationChain*>(&presentationChain);
            assert(!presChain.GetEAGLContext() || presChain.GetEAGLContext() == _activeFrameContext);
        #endif
        if (_activeFrameContext) {
            glBindRenderbuffer(GL_RENDERBUFFER, _activeFrameRenderbuffer->GetRenderBuffer()->AsRawGLHandle());
            [_activeFrameContext.get() presentRenderbuffer:GL_RENDERBUFFER];
        }
        _activeFrameRenderbuffer.reset();
        _activeFrameBuffer.reset();
        _activeFrameContext = nullptr;
        if (EAGLContext.currentContext != _sharedContext.get()) {
            [EAGLContext setCurrentContext:_sharedContext.get()];
            #if defined(_DEBUG)
                BoundContextVerification::GetInstance().UnbindFromCurrentThread((size_t)_activeFrameContext.get());
                BoundContextVerification::GetInstance().BindToCurrentThread((size_t)_sharedContext.get());
            #endif
        }
    }

    void ThreadContext::CommitCommands(CommitCommandsFlags::BitField flags)
    {
        assert(flags==0);
        assert(!_activeFrameContext); // If you're actively rendering, you need Present instead
        if (EAGLContext.currentContext) {
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

    void* ThreadContext::QueryInterface(size_t guid)
    {
        if (guid == typeid(EAGLContext).hash_code()) {
            if (_activeFrameContext) {
                return _activeFrameContext.get();
            }
            if (_deferredContext) {
                return _deferredContext.get();
            }
            return _sharedContext.get();
        }
        return nullptr;
    }

    void ThreadContext::MakeDeferredContext()
    {
        auto* t = [[EAGLContext alloc] initWithAPI:_sharedContext.get().API sharegroup:_sharedContext.get().sharegroup];
        _deferredContext = moveptr(t);

        // All of the contexts within the same share group must have a matching "multiThreaded" flag
        // without this, we can get strange crashes & unexpected opengl errors
        _deferredContext.get().multiThreaded = _sharedContext.get().multiThreaded;

        assert(EAGLContext.currentContext != t);        // BoundContextVerification assumes that creating a context doesn't change the currentContext
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContextOpenGLES::ThreadContextOpenGLES(EAGLContext* sharedContext, const std::shared_ptr<Device>& device)
        : ThreadContext(sharedContext, device)
    {
        auto featureSet = AsGLESFeatureSet(sharedContext.API);
        _deviceContext = std::make_shared<Metal_OpenGLES::DeviceContext>(device, featureSet);
    }

    ThreadContextOpenGLES::~ThreadContextOpenGLES() {}

    void* ThreadContextOpenGLES::QueryInterface(size_t guid)
    {
        if (guid == typeid(IThreadContextOpenGLES).hash_code()) {
            return (IThreadContextOpenGLES*)this;
        }
        return ThreadContext::QueryInterface(guid);
    }

    bool ThreadContextOpenGLES::IsBoundToCurrentThread()
    {
        #if defined(_DEBUG)
            CheckContextIntegrity();
        #endif
        EAGLContext* currentContext = EAGLContext.currentContext;
        if (_activeFrameContext) {
            return currentContext == _activeFrameContext.get();
        }
        if (_deferredContext) {
            return currentContext == _deferredContext.get();
        }
        return currentContext == _sharedContext.get();
    }

    bool ThreadContextOpenGLES::BindToCurrentThread()
    {
        assert(!_activeFrameContext);
        if (_deferredContext) {
            // Double check that the "multiThreaded" flag on the deferred context matches the shared context flag
            // All contexts within the same share group must agree on the state of this flag. If one has
            // it enabled, but others do not, then strange things will happen.
            assert(_sharedContext.get().multiThreaded == _deferredContext.get().multiThreaded);
            EAGLContext.currentContext = _deferredContext.get();
            #if defined(_DEBUG)
                BoundContextVerification::GetInstance().BindToCurrentThread((size_t)_deferredContext.get());
            #endif
            return IsBoundToCurrentThread();
        } else if (_sharedContext) {
            EAGLContext.currentContext = _sharedContext.get();
            #if defined(_DEBUG)
                BoundContextVerification::GetInstance().BindToCurrentThread((size_t)_sharedContext.get());
            #endif
            return IsBoundToCurrentThread();
        } else {
            return false;
        }
    }

    void ThreadContextOpenGLES::UnbindFromCurrentThread()
    {
        assert(IsBoundToCurrentThread());
        glFlush();
        EAGLContext.currentContext = nil;
        #if defined(_DEBUG)
            if (_deferredContext) {
                BoundContextVerification::GetInstance().UnbindFromCurrentThread((size_t)_deferredContext.get());
            } else {
                BoundContextVerification::GetInstance().UnbindFromCurrentThread((size_t)_sharedContext.get());
            }
        #endif
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

