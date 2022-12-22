// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "Resource.h"
#include "../IDeviceDX11.h"
#include "../../RenderUtils.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include "../../../Utility/Threading/Mutex.h"

#include "IncludeDX11.h"

namespace RenderCore { namespace Metal_DX11
{
	static intrusive_ptr<ID3D::Device> ExtractUnderlyingDevice(RenderCore::IDevice& device)
	{
		auto* dx11Device = (RenderCore::IDeviceDX11*)device.QueryInterface(TypeHashCode<RenderCore::IDeviceDX11>);
		if (dx11Device) {
			return dx11Device->GetUnderlyingDevice();
		}
		return nullptr;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

    class AttachedData : public IUnknown, RefCountedObject
    {
    public:
        static const GUID Guid;

        virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject)
        {
            if (riid == IID_IUnknown) {
                *reinterpret_cast<IUnknown**>(ppvObject) = this;
                return S_OK;
            }

            *ppvObject = NULL;
            return E_NOINTERFACE;
        }

        virtual ULONG STDMETHODCALLTYPE AddRef() { return RefCountedObject::AddRef(); }
        virtual ULONG STDMETHODCALLTYPE Release() { return RefCountedObject::Release(); }

        AttachedData() {}
        virtual ~AttachedData();

		std::unique_ptr<ObjectFactory> _factory;
    };

    AttachedData::~AttachedData() {}

        // {D2192F4A-E4D8-4A33-891A-FDBCEBB83E5D}
    const GUID AttachedData::Guid = { 0xd2192f4a, 0xe4d8, 0x4a33, { 0x89, 0x1a, 0xfd, 0xbc, 0xeb, 0xb8, 0x3e, 0x5d } };

    template<typename ResultType>
        static intrusive_ptr<ResultType> D3DDevice_FinalizeCreate(ResultType* tempPtr, HRESULT hresult, const char name[])
    {
        if (!SUCCEEDED(hresult) || !tempPtr) {
            Throw(Exceptions::GenericFailure("Failure during construction of D3D object"));
        }

        intrusive_ptr<ResultType> result(tempPtr, false);
        // if (name && name[0]) { Resource_Register(*result.get(), name); }
        return result;
    }

    template <typename ResultType, typename IntermediateType, typename Desc>
        static intrusive_ptr<ResultType> D3DDeviceCreate(
            ID3D::Device* device,
            HRESULT (__stdcall ID3D11Device::*CreationFunction)(const Desc*, const D3D11_SUBRESOURCE_DATA*, IntermediateType**),
            const Desc* desc, const D3D11_SUBRESOURCE_DATA* subResourceData = NULL, const char name[] = nullptr)
    {
        IntermediateType* tempPtr = 0;
        auto hresult = (device->*CreationFunction)(desc, subResourceData, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, name);
    }

    template <typename ResultType, typename IntermediateType, typename Desc>
        static intrusive_ptr<ResultType> D3DDeviceCreate(
            ID3D::Device* device,
            HRESULT (__stdcall ID3D11Device::*CreationFunction)(const Desc*, IntermediateType**),
            const Desc* desc, const char name[] = nullptr)
    {
        IntermediateType* tempPtr = 0;
        auto hresult = (device->*CreationFunction)(desc, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, name);
    }

    template <typename ResultType, typename IntermediateType, typename Desc>
        static intrusive_ptr<ResultType> D3DDeviceCreate(
            ID3D::Device* device,
            HRESULT (__stdcall ID3D11Device::*CreationFunction)(ID3D::Resource*, const Desc*, IntermediateType**),
            ID3D::Resource* resource, const Desc* desc, const char name[] = nullptr)
    {
        IntermediateType* tempPtr = 0;
        auto hresult = (device->*CreationFunction)(resource, desc, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, name);
    }

    template <typename ResultType, typename IntermediateType>
        static intrusive_ptr<ResultType> D3DDeviceCreate(
            ID3D::Device* device,
            HRESULT (__stdcall ID3D11Device::*CreationFunction)(const void*, SIZE_T, ID3D::ClassLinkage*, IntermediateType**),
            const void* byteCode, SIZE_T size, ID3D::ClassLinkage* linkage, const char name[] = nullptr)
    {
        IntermediateType* tempPtr = 0;
        auto hresult = (device->*CreationFunction)(byteCode, size, linkage, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, name);
    }

	class ObjectFactory::Pimpl
	{
	public:
		Threading::Mutex _creationLock;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Basic states
    intrusive_ptr<ID3D::BlendState> ObjectFactory::CreateBlendState(const D3D11_BLEND_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::BlendState>(_device, &ID3D::Device::CreateBlendState, desc);
    }

    intrusive_ptr<ID3D::DepthStencilState> ObjectFactory::CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::DepthStencilState>(_device, &ID3D::Device::CreateDepthStencilState, desc);
    }

    intrusive_ptr<ID3D::RasterizerState> ObjectFactory::CreateRasterizerState(const D3D11_RASTERIZER_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::RasterizerState>(_device, &ID3D::Device::CreateRasterizerState, desc);
    }

    intrusive_ptr<ID3D::SamplerState> ObjectFactory::CreateSamplerState(const D3D11_SAMPLER_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::SamplerState>(_device, &ID3D::Device::CreateSamplerState, desc);
    }
        /// @}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Resources
    intrusive_ptr<ID3D::Buffer> ObjectFactory::CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* subResData, const char[]) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::Buffer>(_device, &ID3D::Device::CreateBuffer, desc, subResData);
    }

    intrusive_ptr<ID3D::Texture1D> ObjectFactory::CreateTexture1D(const D3D11_TEXTURE1D_DESC* desc, const D3D11_SUBRESOURCE_DATA* subResData, const char[]) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::Texture1D>(_device, &ID3D::Device::CreateTexture1D, desc, subResData);
    }

    intrusive_ptr<ID3D::Texture2D> ObjectFactory::CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* subResData, const char[]) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::Texture2D>(_device, &ID3D::Device::CreateTexture2D, desc, subResData);
    }

    intrusive_ptr<ID3D::Texture3D> ObjectFactory::CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* subResData, const char[]) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::Texture3D>(_device, &ID3D::Device::CreateTexture3D, desc, subResData);
    }
        /// @}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Resource views
    intrusive_ptr<ID3D::RenderTargetView> ObjectFactory::CreateRenderTargetView(ID3D::Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::RenderTargetView>(_device, &ID3D::Device::CreateRenderTargetView, resource, desc);
    }

    intrusive_ptr<ID3D::ShaderResourceView> ObjectFactory::CreateShaderResourceView(ID3D::Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::ShaderResourceView>(_device, &ID3D::Device::CreateShaderResourceView, resource, desc);
    }

    intrusive_ptr<ID3D::UnorderedAccessView> ObjectFactory::CreateUnorderedAccessView(ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::UnorderedAccessView>(_device, &ID3D::Device::CreateUnorderedAccessView, resource, desc);
    }

    intrusive_ptr<ID3D::DepthStencilView> ObjectFactory::CreateDepthStencilView(ID3D::Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::DepthStencilView>(_device, &ID3D::Device::CreateDepthStencilView, resource, desc);
    }
        /// @}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Shaders
    intrusive_ptr<ID3D::VertexShader> ObjectFactory::CreateVertexShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::VertexShader>(_device, &ID3D::Device::CreateVertexShader, data, size, linkage);
    }

    intrusive_ptr<ID3D::PixelShader> ObjectFactory::CreatePixelShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::PixelShader>(_device, &ID3D::Device::CreatePixelShader, data, size, linkage);
    }

    intrusive_ptr<ID3D::ComputeShader> ObjectFactory::CreateComputeShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::ComputeShader>(_device, &ID3D::Device::CreateComputeShader, data, size, linkage);
    }

    intrusive_ptr<ID3D::GeometryShader> ObjectFactory::CreateGeometryShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::GeometryShader>(_device, &ID3D::Device::CreateGeometryShader, data, size, linkage);
    }

    intrusive_ptr<ID3D::GeometryShader> ObjectFactory::CreateGeometryShaderWithStreamOutput(
        const void* data, size_t size,
        const D3D11_SO_DECLARATION_ENTRY* declEntries,
        unsigned declEntryCount, const unsigned bufferStrides[], unsigned stridesCount,
        unsigned rasterizedStreamIndex, ID3D::ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        ID3D::GeometryShader* tempPtr = nullptr;
        auto hresult = _device->CreateGeometryShaderWithStreamOutput(
            data, size, declEntries, declEntryCount, bufferStrides, stridesCount,
            rasterizedStreamIndex, linkage, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, nullptr);
    }

    intrusive_ptr<ID3D::DomainShader> ObjectFactory::CreateDomainShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::DomainShader>(_device, &ID3D::Device::CreateDomainShader, data, size, linkage);
    }

    intrusive_ptr<ID3D::HullShader> ObjectFactory::CreateHullShader(const void* data, size_t size, ID3D11ClassLinkage* linkage) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::HullShader>(_device, &ID3D::Device::CreateHullShader, data, size, linkage);
    }
        /// @}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Shader dynamic linking
    intrusive_ptr<ID3D::ClassLinkage> ObjectFactory::CreateClassLinkage() const
    {
        ScopedLock(_pimpl->_creationLock);
        ID3D::ClassLinkage* tempPtr = 0;
        auto hresult = _device->CreateClassLinkage(&tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, nullptr);
    }
        /// @}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        /// @{
        /// Misc
    intrusive_ptr<ID3D::DeviceContext> ObjectFactory::CreateDeferredContext() const
    {
        ScopedLock(_pimpl->_creationLock);
        ID3D::DeviceContext* tempPtr = nullptr;
        auto hresult = _device->CreateDeferredContext(0, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, nullptr);
    }

    intrusive_ptr<ID3D::InputLayout> ObjectFactory::CreateInputLayout(
        const D3D11_INPUT_ELEMENT_DESC inputElements[], unsigned inputElementsCount,
        const void * byteCode, size_t byteCodeSize) const
    {
        ScopedLock(_pimpl->_creationLock);
        ID3D::InputLayout* tempPtr = nullptr;
        auto hresult = _device->CreateInputLayout(inputElements, inputElementsCount, byteCode, byteCodeSize, &tempPtr);
        return D3DDevice_FinalizeCreate(tempPtr, hresult, nullptr);
    }

    intrusive_ptr<ID3D::Query> ObjectFactory::CreateQuery(const D3D11_QUERY_DESC* desc) const
    {
        ScopedLock(_pimpl->_creationLock);
        return D3DDeviceCreate<ID3D::Query>(_device, &ID3D::Device::CreateQuery, desc);
    }
        /// @}

	ObjectFactory::ObjectFactory(ID3D::Device& dev) : _device(&dev) 
	{
		_pimpl = std::make_unique<Pimpl>();
	}

    static ID3D::Device* s_defaultDevice = nullptr;

    ObjectFactory::~ObjectFactory() {}

    void ObjectFactory::AttachCurrentModule()
    {
        assert(s_defaultDevice == nullptr);
        s_defaultDevice = _device;
    }

    void ObjectFactory::DetachCurrentModule()
    {
        assert(s_defaultDevice == _device);
        s_defaultDevice = nullptr;
    }

	static auto InitAttachedData(ID3D::Device* device) -> intrusive_ptr<AttachedData>
	{
		intrusive_ptr<AttachedData> result;
		if (device) {
			AttachedData* tempPtr = nullptr;
			unsigned size = sizeof(tempPtr);
			auto hresult = device->GetPrivateData(AttachedData::Guid, &size, (void*)&tempPtr);
			if (SUCCEEDED(hresult) && tempPtr && size == sizeof(tempPtr)) {
				result = moveptr(tempPtr);    // we must inherit the reference given when we call GetPrivateData
			}

			if (!result) {
				result = make_intrusive<AttachedData>();
				result->_factory = std::make_unique<ObjectFactory>(*device);
				device->SetPrivateDataInterface(AttachedData::Guid, result.get());
			}
		}
		return std::move(result);
	}

	void ObjectFactory::PrepareDevice(ID3D::Device& device)
	{
		InitAttachedData(&device);
	}

	void ObjectFactory::ReleaseDevice(ID3D::Device& device)
	{
		device.SetPrivateDataInterface(AttachedData::Guid, nullptr);
	}


///////////////////////////////////////////////////////////////////////////////////////////////////

	ObjectFactory& GetObjectFactory(IDevice& device)
	{
		auto devDX = ExtractUnderlyingDevice(device);
		if (!devDX) Throw(::Exceptions::BasicLabel("Could not get object factory associated with device"));
		return GetObjectFactory(*devDX);
	}

	ObjectFactory& GetObjectFactory(ID3D::Device& device)
	{
		auto result = InitAttachedData(&device);
		if (!result) Throw(::Exceptions::BasicLabel("Could not successfully initialize attached data for D3DDevice"));
		return *result->_factory.get();
	}

	ObjectFactory& GetObjectFactory(ID3D::Resource& resource)
	{
		ID3D::Device* devRaw = nullptr;
		resource.GetDevice(&devRaw);
        intrusive_ptr<ID3D::Device> dev = moveptr(devRaw);
		if (!dev) Throw(::Exceptions::BasicLabel("Could not get object factory associated with device"));
		return GetObjectFactory(*dev);
	}

	ObjectFactory& GetObjectFactory(IResource& res)
	{
		auto*d3dRes = (Resource*)res.QueryInterface(TypeHashCode<Resource>);
		if (!d3dRes) Throw(::Exceptions::BasicLabel("Could not get object factory associated with device"));
		return GetObjectFactory(*d3dRes->_underlying);
	}

	ObjectFactory& GetObjectFactory(DeviceContext& context)
	{
		return context.GetFactory();
	}

	ObjectFactory& GetObjectFactory()
	{
		return GetObjectFactory(*s_defaultDevice);
	}
	
}}
