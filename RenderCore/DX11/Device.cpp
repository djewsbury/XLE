// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Device.h"
#include "../IAnnotator.h"
#include "../Format.h"
#include "../RenderUtils.h"		// (for Exceptions::GenericFailure)
#include "../Init.h"
#include "Metal/DeviceContext.h"
#include "Metal/State.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Resource.h"
#include "Metal/Format.h"
#include "Metal/TextureView.h"
#include "Metal/ObjectFactory.h"
#include "Metal/Shader.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../OSServices/Log.h"
#include "../../OSServices/LogUtil.h"
#include "../../OSServices/WinAPI/WinAPIWrapper.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/FunctionUtils.h"
#include "../../Core/Exceptions.h"
#include <type_traits>
#include <assert.h>

#include "Metal/IncludeDX11.h"
#include "Metal/DX11Utils.h"        // for QueryInterfaceCast
#include <dxgi.h>
#include <dxgidebug.h>

using namespace Utility::Literals;

namespace RenderCore { namespace ImplDX11
{
    static void DumpAllDXGIObjects();

	std::unique_ptr<IAnnotator> CreateAnnotator(IDevice& device);

    //////////////////////////////////////////////////////////////////////////////////////////////////

    intrusive_ptr<IDXGI::Adapter> SelectAdapter() { return intrusive_ptr<IDXGI::Adapter>(); };

    static HRESULT D3D11CreateDevice_Wrapper(
        IDXGI::Adapter* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVersion,
        ID3D::Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel,
        ID3D::DeviceContext** ppImmediateContext )
    {
        // This is a wrapped for the D3DCreateDevice() call from the D3D DLL. It allows is to
        // manually load the DLL. If we use the D3D library version directly, the OS will attempt
        // to load the D3D dll as the executable is loading. We don't have any way to catch
        // errors in the case. So if there are any problems (eg, DirectX isn't installed, or it's
        // the wrong version), the user will just we the OS error dialog box. 
        //
        // But, if we bind the dll in this way, we have some control over what happens. We can
        // pop up a more informative error box for the user.
        //
        // We could also do some verification here to check for cases where someone has injected
        // a custom dll in place of the real dll here.
        // Note that if the module is opened successfully, then we never close it.

        static HMODULE module = (HMODULE)INVALID_HANDLE_VALUE;
        if (module == INVALID_HANDLE_VALUE) {
            module = (*OSServices::Windows::Fn_LoadLibrary)("d3d11.dll");
        }
        if (!module || module == INVALID_HANDLE_VALUE) {
            Throw(::Exceptions::BasicLabel("Could not load D3D11 library"));
        }

        auto fn = (PFN_D3D11_CREATE_DEVICE)(*OSServices::Windows::Fn_GetProcAddress)(module, "D3D11CreateDevice");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(module);
            module = (HMODULE)INVALID_HANDLE_VALUE;
            Throw(::Exceptions::BasicLabel("D3D11 library appears corrupt"));
        }

        return (*fn)(
            pAdapter, DriverType, Software, Flags, 
            pFeatureLevels, FeatureLevels, 
            SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    }

    Device::Device() : _featureLevel(D3D_FEATURE_LEVEL(~unsigned(0x0)))
    {
            //
            //      Create an underlying D3D device with all of the sensible (typical) defaults
            //
        auto adapter = SelectAdapter();

        #if defined(_DEBUG)
            const auto nsightMode = ConsoleRig::CrossModule::GetInstance()._services.CallDefault("nsight"_h, false);
            unsigned deviceCreationFlags = nsightMode?0:D3D11_CREATE_DEVICE_DEBUG;
        #else
            unsigned deviceCreationFlags = 0;
        #endif

        // deviceCreationFlags |= D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;

        const D3D_FEATURE_LEVEL featureLevelsToTarget[] = {
            #if DX_VERSION >= DX_VERSION_11_1             // Note -- this is causing problems when running on a machine that doesn't have the Win8 SDK redist installed. We're getting back an "INVALID_ARG" hresult from DirectX
                D3D_FEATURE_LEVEL_11_1,
            #endif
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 
        };

        intrusive_ptr<ID3D::Device> underlying;
        intrusive_ptr<ID3D::DeviceContext> immediateContext;
        
        auto hresult = S_OK;
        {
            ID3D::Device* deviceTemp = 0;
            ID3D::DeviceContext* contextTemp = 0;

            hresult = D3D11CreateDevice_Wrapper(
                adapter.get(), D3D_DRIVER_TYPE_HARDWARE, NULL,
                deviceCreationFlags, featureLevelsToTarget, dimof(featureLevelsToTarget),
                D3D11_SDK_VERSION, &deviceTemp, &_featureLevel, &contextTemp);

                //
                //   Note --  std::move here avoids adding extra references.
                //            intrusive_ptr::operator= should never throw 
                //              (it invokes release, but destructors should suppress exceptions)
                //            So we should be safe to assign these 2 pointers.
                //
            underlying = moveptr(deviceTemp);
            immediateContext = moveptr(contextTemp);
        }

        if (!SUCCEEDED(hresult)) {
            Throw(::Exceptions::BasicLabel("Failure in D3D11 device construction. Aborting."));
        }

        // Metal_DX11::ObjectFactory::PrepareDevice(*underlying);
        _mainFactory = std::make_shared<Metal_DX11::ObjectFactory>(std::ref(*underlying));

            //  Once we know there can be no more exceptions thrown, we can commit
            //  locals to the members.
        _underlying = std::move(underlying);
        _immediateContext = std::move(immediateContext);
    }

    Device::~Device()
    {
        _mainFactory = nullptr;
        Metal_DX11::ObjectFactory::ReleaseDevice(*_underlying);

        _immediateThreadContext.reset();
        _immediateContext.reset();
        _underlying.reset();

            //  After we've released our references, dump a list of all 
            //  active objects. Ideally we want to have no active objects
            //  after the device is released (if there are any active
            //  child objects, it will prevent the device from being destroyed).
        DumpAllDXGIObjects();
    }

    static void DumpAllDXGIObjects()
    {
            //  We can use the "IDXGIDebug" interface to dump a list of all live DXGI objects.
            //  the libraries for this don't seem to be always available in every winsdk; so let's
            //  call this in a way that doesn't require any extra .lib files.
        #if defined(_DEBUG)
            auto debugModule = LoadLibrary(TEXT("dxgidebug.dll"));
			if (debugModule) {
				auto rawProc = GetProcAddress(debugModule, "DXGIGetDebugInterface");
				typedef HRESULT WINAPI getDebugInterfaceType(REFIID, void **);
				auto proc = (getDebugInterfaceType*)rawProc;
				if (proc) {
					IDXGIDebug* debugInterface = nullptr;
					auto hresult = (*proc)(__uuidof(IDXGIDebug), (void**)&debugInterface);
					if (SUCCEEDED(hresult) && debugInterface) {
						const GUID debugAll = { 0xe48ae283, 0xda80, 0x490b, 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8 };
						debugInterface->ReportLiveObjects(debugAll, DXGI_DEBUG_RLO_ALL);
						debugInterface->Release();
					}
				}

				FreeLibrary(debugModule);
			}
        #endif
    }

    template<typename DestinationType, typename SourceType>
        intrusive_ptr<DestinationType> GetParent(intrusive_ptr<SourceType> child)
        {
            DestinationType* tempPtr = 0;
            auto hresult = child->GetParent(__uuidof(DestinationType), (void**)&tempPtr);
            if (!SUCCEEDED(hresult)) {
                if (tempPtr) { tempPtr->Release(); }
                return intrusive_ptr<DestinationType>();
            }
            return moveptr(tempPtr);
        }

    intrusive_ptr<IDXGI::Factory>      Device::GetDXGIFactory()
    {
        if (auto dxgiDevice = Metal_DX11::QueryInterfaceCast<IDXGI::Device>(_underlying)) {
            if (auto dxgiAdapter = GetParent<IDXGI::Adapter>(dxgiDevice)) {
                return GetParent<IDXGI::Factory>(dxgiAdapter);
            }
        }
        return intrusive_ptr<IDXGI::Factory>();
    }

    std::unique_ptr<IPresentationChain>   Device::CreatePresentationChain(const void* platformValue, const PresentationChainDesc& desc)
    {
        intrusive_ptr<IDXGI::Factory> factory = GetDXGIFactory();
        if (!factory) {
            return std::unique_ptr<IPresentationChain>();
        }

        DXGI_MODE_DESC modeDesc = {
            desc._width, desc._height, {0,0}, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 
            DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, DXGI_MODE_SCALING_UNSPECIFIED };
        DXGI_SAMPLE_DESC sampleDesc  = { 1, 0 };     // (no multi-sampling)
        const auto backBufferCount   = 2u;
        const auto windowed          = true;
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {
            modeDesc, sampleDesc,
            DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT,       // (select shader input, etc, in order to provide greater access to the back buffer)
            backBufferCount, HWND(platformValue),
            windowed, DXGI_SWAP_EFFECT_DISCARD, 
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
        };

        intrusive_ptr<IDXGI::SwapChain> result;
        auto hresult = S_OK;
        {
            IDXGI::SwapChain* swapChainTemp = 0;
            hresult = factory->CreateSwapChain(_underlying.get(), &swapChainDesc, &swapChainTemp);
            result = moveptr(swapChainTemp);
        }
        if (!SUCCEEDED(hresult)) {
            Throw(::Exceptions::BasicLabel("Failure while constructing a swap chain. Aborting."));
        }

            //
            //      Build a window association here.
            //
        factory->MakeWindowAssociation(HWND(platformValue), 0);

        return std::make_unique<PresentationChain>(std::move(result), platformValue);
    }

    std::shared_ptr<IThreadContext> Device::GetImmediateContext()
    {
        if (!_immediateThreadContext) {
            _immediateThreadContext = std::make_shared<ThreadContextDX11>(_immediateContext, shared_from_this());
        }
        return _immediateThreadContext;
    }

    std::unique_ptr<IThreadContext> Device::CreateDeferredContext()
    {
            // create a new deferred context, and return a wrapper object
        D3D11_FEATURE_DATA_THREADING threadingSupport;
        XlZeroMemory(threadingSupport);
        HRESULT hresult = _underlying->CheckFeatureSupport(
            D3D11_FEATURE_THREADING, &threadingSupport, sizeof(threadingSupport) );
        uint32 driverCreateFlags = _underlying->GetCreationFlags();
        if (SUCCEEDED(hresult)) {
            LogLine(
				Verbose,
                "D3D Multithreading support: concurrent creates: {}, command lists: {}, driver single threaded: {}", 
                 threadingSupport.DriverConcurrentCreates, threadingSupport.DriverCommandLists,
                 (driverCreateFlags&D3D11_CREATE_DEVICE_SINGLETHREADED)?1:0);
        }
        
        const bool multithreadingOk = 
                !(driverCreateFlags&D3D11_CREATE_DEVICE_SINGLETHREADED)
			&& (SUCCEEDED(hresult) && threadingSupport.DriverConcurrentCreates)
            ;
        
        if (multithreadingOk) {
            ID3D::DeviceContext* defContextTemp = nullptr;
            hresult = _underlying->CreateDeferredContext(0, &defContextTemp);
            if (SUCCEEDED(hresult) && defContextTemp) {
                intrusive_ptr<ID3D::DeviceContext> defContext(moveptr(defContextTemp));
                return std::make_unique<ThreadContextDX11>(std::move(defContext), shared_from_this());
            }
        }

        return nullptr;
    }

	static const char* AsString(TextureDesc::Dimensionality dimensionality)
	{
		switch (dimensionality) {
		case TextureDesc::Dimensionality::CubeMap:  return "Cube";
		case TextureDesc::Dimensionality::T1D:      return "T1D";
		case TextureDesc::Dimensionality::T2D:      return "T2D";
		case TextureDesc::Dimensionality::T3D:      return "T3D";
		default:                                    return "<<unknown>>";
		}
	}

	IResourcePtr Device::CreateResource(
		const ResourceDesc& desc,
		const std::function<SubResourceInitData(SubResourceId)>& init)
	{
		try {
			return Metal_DX11::CreateResource(*_mainFactory, desc, init);
		} catch (const Exceptions::GenericFailure& e) {
			// try to make "GenericFailure" exceptions a little more expressive
			if (desc._type == ResourceDesc::Type::LinearBuffer) {
				Throw(::Exceptions::BasicLabel(
					"Generic failure in Device::CreateResource while creating linear buffer resource with size (%i) and structure size (%i). Resource Flags (0x%x, 0x%x, 0x%x, 0x%x). Name: (%s). Failure message (%s)",
					desc._linearBufferDesc._sizeInBytes, desc._linearBufferDesc._structureByteSize,
					desc._bindFlags, desc._cpuAccess, desc._gpuAccess, desc._allocationRules,
					desc._name,
					e.what()));
			} else if (desc._type == ResourceDesc::Type::Texture) {
				const auto& tDesc = desc._textureDesc;
				Throw(::Exceptions::BasicLabel(
					"Generic failure in Device::CreateResource while creating texture resource [Tex(%4s) (%4ix%4i) mips:(%2i) format:(%s)]. Resource Flags (0x%x, 0x%x, 0x%x, 0x%x). Name: (%s). Failure message (%s)",
					AsString(tDesc._dimensionality), tDesc._width, tDesc._height, tDesc._mipCount, AsString(tDesc._format),
					desc._bindFlags, desc._cpuAccess, desc._gpuAccess, desc._allocationRules,
					desc._name,
					e.what()));
			} else {
				throw;
			}
		}
	}

	FormatCapability    Device::QueryFormatCapability(Format format, BindFlag::BitField bindingType)
	{
		return FormatCapability::Supported;
	}

    static const char* s_underlyingApi = "DX11";
        
    DeviceDesc Device::GetDesc()
    {
		auto libVersion = ConsoleRig::GetLibVersionDesc();
        return DeviceDesc{s_underlyingApi, libVersion._versionString, libVersion._buildDateString};
    }

	void Device::Stall()
	{
		// There's no straight forward way of doing this on DX11. We have to create a query and wait
		// for it; or use some kind of fence trickery
		_immediateContext->Flush();
		assert(0);	// not fully implemented & tested
	}

	std::shared_ptr<ILowLevelCompiler>		Device::CreateShaderCompiler()
	{
		return Metal_DX11::CreateLowLevelShaderCompiler(*this, _featureLevel);
	}

    //////////////////////////////////////////////////////////////////////////////////////////////////

    void*                   DeviceDX11::QueryInterface(size_t guid)
    {
        switch (guid) {
        case TypeHashCode<IDeviceDX11>: return (IDeviceDX11*)this;
        default: return nullptr;
        }
    }

    ID3D::Device*           DeviceDX11::GetUnderlyingDevice()
    {
        return _underlying.get();
    }

    ID3D::DeviceContext*    DeviceDX11::GetImmediateDeviceContext()
    {
        return _immediateContext.get();
    }

    DeviceDX11::DeviceDX11()
    {
    }

    DeviceDX11::~DeviceDX11()
    {
        _immediateContext->Flush();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

    static UInt2 GetBufferSize(IDXGI::SwapChain& swapChain)
    {
        DXGI_SWAP_CHAIN_DESC dxgiDesc;
        auto hresult = swapChain.GetDesc(&dxgiDesc);
		if (SUCCEEDED(hresult)) {
			return UInt2(dxgiDesc.BufferDesc.Width, dxgiDesc.BufferDesc.Height);
		} else {
			return UInt2(0, 0);
		}
    }

    PresentationChain::PresentationChain(intrusive_ptr<IDXGI::SwapChain> underlying, const void* attachedWindow)
    : _underlying(std::move(underlying))
    , _attachedWindow(attachedWindow)
    {
        _desc = std::make_shared<PresentationChainDesc>();
        auto dims = GetBufferSize(*_underlying);
        _desc->_width = dims[0];
        _desc->_height = dims[1];
        _desc->_format = Format::R8G8B8A8_UNORM_SRGB;
        _desc->_samples = TextureSamples::Create();
    }

    PresentationChain::~PresentationChain()
    {
    }

    void            PresentationChain::Resize(unsigned newWidth, unsigned newHeight)
    {
        if (newWidth == 0 || newHeight == 0) {
            if (_attachedWindow != INVALID_HANDLE_VALUE) {
                RECT clientRect; 
                GetClientRect(HWND(_attachedWindow), &clientRect);
                newWidth = clientRect.right - clientRect.left;
                newHeight = clientRect.bottom - clientRect.top;
            } else {
                Throw(::Exceptions::BasicLabel("Cannot resize because this presentation chain isn't attached to a window."));
            }
        }

		_lastBackBuffer.reset();
		_lastBackBufferResource.reset();

        const auto backBufferCount = 2u;
        _underlying->ResizeBuffers(
            backBufferCount, newWidth, newHeight, 
            Metal_DX11::AsDXGIFormat(_desc->_format), DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

        auto dims = GetBufferSize(*_underlying);
        _desc->_width = dims[0];
        _desc->_height = dims[1];
        _defaultDepthTarget.reset(0);
    }

    const std::shared_ptr<PresentationChainDesc>& PresentationChain::GetDesc() const
    {
        return _desc;
    }

    void PresentationChain::AttachToContext(Metal_DX11::DeviceContext& context, Metal_DX11::ObjectFactory& factory)
    {
            //
            //      DavidJ -- temporary implementation -- replace with better management of 
            //                  render target types in the future
            //
            
        IDXGI::SwapChain* swapChain = _underlying.get();
        ID3D::Texture2D* backBuffer0Temp = nullptr;
        HRESULT hresult = swapChain->GetBuffer(0, __uuidof(ID3D::Texture2D), (void**)&backBuffer0Temp);
        intrusive_ptr<ID3D::Texture2D> backBuffer0 = moveptr(backBuffer0Temp);
        if (SUCCEEDED(hresult)) {
                //  By default, when writing to this texture, we should
                //  write in "SRGB" mode. This will automatically convert
                //  from linear colour outputs into SRGB space (and do the
                //  blending correctly in linear space).
                //      ... however, there are some cases were we want to
                //      write to this buffer in non-SRGB mode (ie, we will
                //      manually apply SRGB convertion)
            TextureViewDesc viewWindow;
            Metal_DX11::RenderTargetView rtv(Metal_DX11::AsResourcePtr(backBuffer0), viewWindow);
            Metal_DX11::ViewportDesc vp(0.f, 0.f, (float)_desc->_width, (float)_desc->_height);
			context.SetViewportAndScissorRects(MakeIteratorRange(&vp, &vp+1), {});
        }
    }

	IResourcePtr PresentationChain::GetPresentationResource()
	{
		ID3D::Texture2D* backBuffer0Temp = nullptr;
        HRESULT hresult = _underlying->GetBuffer(0, __uuidof(ID3D::Texture2D), (void**)&backBuffer0Temp);
        intrusive_ptr<ID3D::Texture2D> backBuffer0 = moveptr(backBuffer0Temp);
        if (SUCCEEDED(hresult)) {
			if (_lastBackBuffer != backBuffer0) {
				_lastBackBuffer = std::move(backBuffer0);
				_lastBackBufferResource = Metal_DX11::AsResourcePtr((ID3D::Resource*)_lastBackBuffer.get());
			}
            return _lastBackBufferResource;
		}
        
        return nullptr;
	}

    //////////////////////////////////////////////////////////////////////////////////////////////////

    render_dll_export std::shared_ptr<IDevice>    CreateDevice()
    {
        return std::make_shared<DeviceDX11>();
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////

    IResourcePtr    ThreadContext::BeginFrame(IPresentationChain& presentationChain)
    {
        PresentationChain* swapChain = checked_cast<PresentationChain*>(&presentationChain);
        auto d = _device.lock();
        swapChain->AttachToContext(*_underlying, Metal_DX11::GetObjectFactory(*d->GetUnderlyingDevice()));
        IncrFrameId();

		return swapChain->GetPresentationResource();
    }

    void            ThreadContext::Present(IPresentationChain& presentationChain)
    {
        // end the current render pass, if one has been begun -- 
        // and release the reference on the presentation target in the device context
        // _activeRPI.reset();
        // _underlying->GetNamedResources().UnbindAll();

        PresentationChain* swapChain = checked_cast<PresentationChain*>(&presentationChain);
        swapChain->GetUnderlying()->Present(0, 0);
    }

	void	ThreadContext::CommitCommands(CommitCommandsFlags::BitField flags)
	{
        assert(flags==0);
		// We essentially just want to trigger the execution of the command list we're building
		// on the GPU.
		_underlying->GetUnderlying()->Flush();
	}

    bool    ThreadContext::IsImmediate() const
    {
        return _underlying->IsImmediate();
    }

    auto ThreadContext::GetStateDesc() const -> ThreadContextStateDesc
    {
		auto viewport = _underlying->GetBoundViewport();

        ThreadContextStateDesc result;
        result._viewportDimensions = {unsigned(viewport.Width), unsigned(viewport.Height)};
        result._frameId = _frameId;
        return result;
    }

	void ThreadContext::InvalidateCachedState() const
	{
		_underlying->InvalidateCachedState();
	}

	IAnnotator& ThreadContext::GetAnnotator()
	{
		if (!_annotator) {
			auto d = _device.lock();
			assert(d);
			_annotator = CreateAnnotator(*d);
		}
		return *_annotator;
	}

    ThreadContext::ThreadContext(intrusive_ptr<ID3D::DeviceContext> devContext, std::shared_ptr<Device> device)
    : _device(std::move(device))
    {
        _underlying = std::make_shared<Metal_DX11::DeviceContext>(std::move(devContext));
        _frameId = 0;
    }

    ThreadContext::~ThreadContext() 
    {
    }

    std::shared_ptr<IDevice> ThreadContext::GetDevice() const
    {
        // Find a pointer back to the IDevice object associated with this 
        // thread context...
        // There are two ways to do this:
        //  1) get the D3D::IDevice from the DeviceContext
        //  2) there is a pointer back to the RenderCore::IDevice() hidden within
        //      the D3D::IDevice
        // Or, we could just store a std::shared_ptr back to the device within
        // this object.
        return _device.lock();
    }

    void ThreadContext::IncrFrameId()
    {
        ++_frameId;
    }

    void*   ThreadContextDX11::QueryInterface(size_t guid)
    {
        switch (guid) {
        case TypeHashCode<IThreadContextDX11>: return (IThreadContextDX11*)this;
        default: return nullptr;
        }
    }

    std::shared_ptr<Metal_DX11::DeviceContext>&  ThreadContextDX11::GetUnderlying()
    {
        return _underlying;
    }

    ID3D::Device* ThreadContextDX11::GetUnderlyingDevice()
    {
        auto dev = _device.lock();
        return dev ? dev->GetUnderlyingDevice() : nullptr;
    }

    ThreadContextDX11::ThreadContextDX11(intrusive_ptr<ID3D::DeviceContext> devContext, std::shared_ptr<Device> device)
    : ThreadContext(devContext, std::move(device))
    {}

    ThreadContextDX11::~ThreadContextDX11() {}

}}

namespace RenderCore
{
	IDeviceDX11::~IDeviceDX11() {}
	IThreadContextDX11::~IThreadContextDX11() {}
}
