// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Resource.h"
#include "DeviceContext.h"
#include "DX11Utils.h"
#include "Format.h"
#include "ObjectFactory.h"
#include "TextureView.h"
#include "../IDeviceDX11.h"
#include "../../ResourceUtils.h"

#pragma warning(disable:4505) // 'RenderCore::Metal_DX11::GetSubResourceIndex': unreferenced local function has been removed

namespace RenderCore { namespace Metal_DX11
{
	constexpr auto s_resourceInterface = TypeHashCode<Resource>;

	void Copy(
		DeviceContext& context, 
		Resource& dst, const Resource& src,
		ImageLayout, ImageLayout)
	{
		context.GetUnderlying()->CopyResource(dst.GetUnderlying().get(), src.GetUnderlying().get());
	}

	void CopyPartial(
        DeviceContext& context, const BlitPass::CopyPartial_Dest& dst, const BlitPass::CopyPartial_Src& src,
        ImageLayout dstLayout, ImageLayout srcLayout)
	{
		BlitPass(context).Copy(dst, src);
	}

    static unsigned GetMipLayers(ID3D::Resource* resource)
    {
        // Lots of casting makes this inefficient!
        D3DTextureDesc<ID3D::Texture2D> t2DDesc(resource);
        if (t2DDesc.Width != 0) return t2DDesc.MipLevels;
        D3DTextureDesc<ID3D::Texture3D> t3DDesc(resource);
        if (t3DDesc.Width != 0) return t3DDesc.MipLevels;
        D3DTextureDesc<ID3D::Texture1D> t1DDesc(resource);
        if (t1DDesc.Width != 0) return t1DDesc.MipLevels;
        return 0;
    }

    static unsigned GetSubResourceIndex(ID3D::Resource* resource, SubResourceId id)
    {
        // Unfortunately we need to extract the mip count from the resource to get the
        // true number (if accessing an array layer other than the first)
        if (id._arrayLayer == 0)
            return id._mip;
        return D3D11CalcSubresource(id._mip, id._arrayLayer, GetMipLayers(resource));
    }

	static unsigned GetSubResourceIndex(Resource& resource, SubResourceId id)
    {
		if (id._arrayLayer == 0)
            return id._mip;
		assert(resource.GetDesc()._type == ResourceDesc::Type::Texture);
		auto mipLayers = resource.GetDesc()._textureDesc._mipCount;
		return D3D11CalcSubresource(id._mip, id._arrayLayer, mipLayers);
	}

    void BlitPass::Copy(const CopyPartial_Dest& dst, const CopyPartial_Src& src)
    {
        bool useSrcBox = false;
        D3D11_BOX srcBox;
        if (src._leftTopFront[0] != ~0u || src._rightBottomBack[0] != ~0u) {
            srcBox = D3D11_BOX {
                src._leftTopFront[0], src._leftTopFront[1], src._leftTopFront[2],
                src._rightBottomBack[0], src._rightBottomBack[1], src._rightBottomBack[2]
            };
            useSrcBox = true;
        }

		Resource* dstD3DResource = (Resource*)dst._resource->QueryInterface(s_resourceInterface);
		if (!dstD3DResource)
			Throw(std::runtime_error("BltPass::Copy failed because destination resource does not appear to be a compatible type"));

		Resource* srcD3DResource = (Resource*)src._resource->QueryInterface(s_resourceInterface);
		if (!srcD3DResource)
			Throw(std::runtime_error("BltPass::Copy failed because source resource does not appear to be a compatible type"));

        _boundContext->GetUnderlying()->CopySubresourceRegion(
            dstD3DResource->GetUnderlying().get(), GetSubResourceIndex(*dstD3DResource, dst._subResource),
            dst._leftTopFront[0], dst._leftTopFront[1], dst._leftTopFront[2],
            srcD3DResource->GetUnderlying().get(), GetSubResourceIndex(*srcD3DResource, src._subResource),
            useSrcBox ? &srcBox : nullptr);
    }

	void    BlitPass::Write(
        const CopyPartial_Dest& dst,
        const SubResourceInitData& srcData,
        Format srcDataFormat,
        VectorPattern<unsigned, 3> srcDataDimensions)
	{
		// unimplemented for this API
		assert(0);
	}

	BlitPass::BlitPass(IThreadContext& threadContext)
	{
		_boundContext = DeviceContext::Get(threadContext).get();
	}

	BlitPass::BlitPass(DeviceContext& devContext)
	{
		_boundContext = &devContext;
	}

	BlitPass::~BlitPass() {}

    intrusive_ptr<ID3D::Resource> Duplicate(DeviceContext& context, UnderlyingResourcePtr inputResource)
    {
        return DuplicateResource(context.GetUnderlying(), inputResource.get());
    }

	IResourcePtr Duplicate(DeviceContext& context, Resource& inputResource)
	{
		auto res = DuplicateResource(context.GetUnderlying(), inputResource.GetUnderlying().get());
		return AsResourcePtr(std::move(res));
	}

	void*       Resource::QueryInterface(size_t guid)
	{
		if (guid == TypeHashCode<Resource>)
			return (Resource*)this;
		return nullptr;
	}

	ResourceDesc	Resource::GetDesc() const
	{
		if (!_underlying) return {};
		return ExtractDesc(_underlying);
	}

	uint64_t        Resource::GetGUID() const
	{
		return _guid;
	}

	std::vector<uint8_t>    Resource::ReadBack(IThreadContext& context, SubResourceId subRes) const
	{
		auto desc = GetDesc();
		bool needsCPUMirror = !(desc._cpuAccess & CPUAccess::Read);
		if (!needsCPUMirror) {

			// map the resource directly, and read straight from there
			ResourceMap resMap(*DeviceContext::Get(context), *this, ResourceMap::Mode::Read, subRes);
			if (SUCCEEDED(resMap.GetMapResultCode())) {
				return std::vector<uint8_t> { (const uint8_t*)resMap.GetData().begin(), (const uint8_t*)resMap.GetData().end() };
			}

			// Sometimes we can get a map failure , even if the resource is marked for reading. In these
			// cases, we must fall back to the cpu mirror path
		}

		// This is a very inefficient path, but we create a temporary copy with CPU access enabled
		// and extract from there
		// We could do this more efficiently by duplicating only the specific subresource we're interested in
		// However, this function is mostly intended for unit tests and debugging purposes, so I'm
		// not going to overinvest in it
		auto mirrorDesc = desc;
		mirrorDesc._cpuAccess = CPUAccess::Read;
		mirrorDesc._gpuAccess = 0;
		mirrorDesc._bindFlags = BindFlag::TransferDst;
		mirrorDesc._allocationRules |= AllocationRules::Staging;
		auto mirrorResource = context.GetDevice()->CreateResource(mirrorDesc);
		auto* d3dMirrorResource = (Resource*)mirrorResource->QueryInterface(s_resourceInterface);
		Copy(*DeviceContext::Get(context), *d3dMirrorResource, *this);

		// It's tempting to just return mirrorResource->ReadBack(context, subRes); here,
		// but don't do it because it risks infinite recursion if the map fails on that resource

		ResourceMap resMap(*DeviceContext::Get(context), *d3dMirrorResource, ResourceMap::Mode::Read, subRes);
		if (SUCCEEDED(resMap.GetMapResultCode())) {
			return std::vector<uint8_t> { (const uint8_t*)resMap.GetData().begin(), (const uint8_t*)resMap.GetData().end() };
		} else {
			Throw(::Exceptions::BasicLabel("DirectX error while attempting to readback data from resource (%s)", desc._name));
		}
	}

	static uint64_t s_nextResourceGUID = 1;

	Resource::Resource() : _guid(s_nextResourceGUID++) {}
	Resource::Resource(const UnderlyingResourcePtr& underlying) : _underlying(underlying), _guid(s_nextResourceGUID++) {}
	Resource::Resource(UnderlyingResourcePtr&& underlying) : _underlying(std::move(underlying)), _guid(s_nextResourceGUID++) {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	ResourceMap::ResourceMap(
		DeviceContext& context, const Resource& resource,
		Mode mapMode,
        SubResourceId subResource)
	{
		assert(subResource._mip == 0 && subResource._arrayLayer == 0);

		_devContext = context.GetUnderlying();
		_underlyingResource = resource.GetUnderlying();
		_map = {};
		_mapSize = 0;

		auto underlyingMapType = (mapMode == Mode::Read) ? D3D11_MAP_READ : D3D11_MAP_WRITE_DISCARD;

		D3D11_MAPPED_SUBRESOURCE result { nullptr, 0, 0 };
        _mapResultCode = _devContext->Map(_underlyingResource.get(), 0, underlyingMapType, 0, &result);
        if (SUCCEEDED(_mapResultCode) && result.pData) {
			_map = result;
			_mapSize = result.DepthPitch;

			// D3D11 seems to align up the DepthPitch for buffers; so shrink
			// down to the size of the buffer if needed
			D3D11_RESOURCE_DIMENSION resDim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
			_underlyingResource->GetType(&resDim);
			if (resDim == D3D11_RESOURCE_DIMENSION_BUFFER) {
				_mapSize = std::min(result.DepthPitch, resource.GetDesc()._linearBufferDesc._sizeInBytes);
			}
		} else {
			_devContext.reset();
			_underlyingResource.reset();
		}
	}

	ResourceMap::ResourceMap()
	{
		_map = {};
	}

	ResourceMap::~ResourceMap()
	{
		TryUnmap();
	}

	ResourceMap::ResourceMap(ResourceMap&& moveFrom) never_throws
	: _underlyingResource(std::move(moveFrom._underlyingResource))
	, _devContext(std::move(moveFrom._devContext))
	{
		_map = moveFrom._map;
		moveFrom._map = {};
	}

	ResourceMap& ResourceMap::operator=(ResourceMap&& moveFrom) never_throws
	{
		TryUnmap();

		_underlyingResource = std::move(moveFrom._underlyingResource);
		_devContext = std::move(moveFrom._devContext);
		_map = moveFrom._map;
		moveFrom._map = {};

		return *this;
	}

	void ResourceMap::TryUnmap()
	{
		if (_underlyingResource && _devContext)
			_devContext->Unmap(_underlyingResource.get(), 0);
		_underlyingResource.reset();
		_devContext.reset();
		_map = {};
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static unsigned AsNativeCPUAccessFlag(CPUAccess::BitField bitField)
	{
		unsigned result = 0;
		if (bitField & CPUAccess::Read)
			result |= D3D11_CPU_ACCESS_READ;
		// if cpu access is Write; we want a "D3D11_USAGE_DEFAULT" buffer with 0 write access flags
		if ((bitField & CPUAccess::WriteDynamic) == CPUAccess::WriteDynamic)
			result |= D3D11_CPU_ACCESS_WRITE;
		return result;
	}

	static CPUAccess::BitField AsGenericCPUAccess(unsigned d3dFlags)
	{
		CPUAccess::BitField result = 0;
		if (d3dFlags & D3D11_CPU_ACCESS_READ)
			result |= CPUAccess::Read;
		if (d3dFlags & D3D11_CPU_ACCESS_WRITE)
			result |= CPUAccess::WriteDynamic;
		return result;
	}

	static D3D11_USAGE UsageForDesc(const ResourceDesc& desc, bool lateInitialisation)
	{
		if (desc._gpuAccess) {
			if ((desc._cpuAccess & CPUAccess::WriteDynamic) == CPUAccess::WriteDynamic) {   // Any resource with CPU write access must be marked as "Dynamic"! Don't set any CPU access flags for infrequent UpdateSubresource updates
				return D3D11_USAGE_DYNAMIC;
			} else if ((desc._cpuAccess & CPUAccess::Write) || lateInitialisation) {
				return D3D11_USAGE_DEFAULT;
			} else if (desc._gpuAccess & GPUAccess::Write) {
				return D3D11_USAGE_DEFAULT;
			} else {
				return D3D11_USAGE_IMMUTABLE;
			}
		} else {
			//  No GPU access means this can only be useful as a staging buffer...
			return D3D11_USAGE_STAGING;
		}
	}

	static unsigned AsNativeBindFlags(BindFlag::BitField flags)
	{
		unsigned result = 0;
		if (flags & BindFlag::VertexBuffer) { result |= D3D11_BIND_VERTEX_BUFFER; }
		if (flags & BindFlag::IndexBuffer) { result |= D3D11_BIND_INDEX_BUFFER; }
		if (flags & BindFlag::ShaderResource) { result |= D3D11_BIND_SHADER_RESOURCE; }
		if (flags & BindFlag::RenderTarget) { result |= D3D11_BIND_RENDER_TARGET; }
		if (flags & BindFlag::DepthStencil) { result |= D3D11_BIND_DEPTH_STENCIL; }
		if (flags & BindFlag::UnorderedAccess) { result |= D3D11_BIND_UNORDERED_ACCESS; }
		if (flags & BindFlag::ConstantBuffer) { result |= D3D11_BIND_CONSTANT_BUFFER; }
		if (flags & BindFlag::StreamOutput) { result |= D3D11_BIND_STREAM_OUTPUT; }
		return result;
	}

	static BindFlag::BitField AsGenericBindFlags(unsigned d3dBindFlags)
	{
		BindFlag::BitField result = 0;
		if (d3dBindFlags & D3D11_BIND_VERTEX_BUFFER) { result |= BindFlag::VertexBuffer; }
		if (d3dBindFlags & D3D11_BIND_INDEX_BUFFER) { result |= BindFlag::IndexBuffer; }
		if (d3dBindFlags & D3D11_BIND_SHADER_RESOURCE) { result |= BindFlag::ShaderResource; }
		if (d3dBindFlags & D3D11_BIND_RENDER_TARGET) { result |= BindFlag::RenderTarget; }
		if (d3dBindFlags & D3D11_BIND_DEPTH_STENCIL) { result |= BindFlag::DepthStencil; }
		if (d3dBindFlags & D3D11_BIND_UNORDERED_ACCESS) { result |= BindFlag::UnorderedAccess; }
		if (d3dBindFlags & D3D11_BIND_CONSTANT_BUFFER) { result |= BindFlag::ConstantBuffer; }
		if (d3dBindFlags & D3D11_BIND_STREAM_OUTPUT) { result |= BindFlag::StreamOutput; }
		return result;
	}

	intrusive_ptr<ID3D::Resource> CreateUnderlyingResource(
		const ObjectFactory& factory,
		const ResourceDesc& desc,
		const ResourceInitializer& init)
	{
		D3D11_SUBRESOURCE_DATA subResources[128];
		bool hasInitData = !!init; 
		
		switch (desc._type) {
		case ResourceDesc::Type::Texture:
			{
				if (hasInitData) {
					for (unsigned m = 0; m<std::max(1u, unsigned(desc._textureDesc._mipCount)); ++m) {
						for (unsigned a = 0; a<std::max(1u, unsigned(desc._textureDesc._arrayCount)); ++a) {
							uint32 subresourceIndex = D3D11CalcSubresource(m, a, desc._textureDesc._mipCount);
                            auto initData = init({m, a});
							subResources[subresourceIndex] = D3D11_SUBRESOURCE_DATA{initData._data.begin(), UINT(initData._pitches._rowPitch), UINT(initData._pitches._slicePitch)};

							// If the caller didn't fill in the "pitches" values (ie, just leaving them zero) -- we should
							// but in the defaults here. This is assuming fully packed data.
							if (!subResources[subresourceIndex].SysMemPitch || !subResources[subresourceIndex].SysMemSlicePitch) {
								auto defaultPitches = MakeTexturePitches(CalculateMipMapDesc(desc._textureDesc, m));
								if (!subResources[subresourceIndex].SysMemPitch)
									subResources[subresourceIndex].SysMemPitch = defaultPitches._rowPitch;
								if (!subResources[subresourceIndex].SysMemSlicePitch)
									subResources[subresourceIndex].SysMemSlicePitch = defaultPitches._slicePitch;
							}
						}
					}
				}

				if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D) {

					D3D11_TEXTURE1D_DESC textureDesc;
					XlZeroMemory(textureDesc);
					textureDesc.Width = desc._textureDesc._width;
					textureDesc.MipLevels = desc._textureDesc._mipCount;
					textureDesc.ArraySize = std::max(desc._textureDesc._arrayCount, uint16(1));
					textureDesc.Format = AsDXGIFormat(desc._textureDesc._format);
					const bool lateInitialisation = !hasInitData; /// it must be lateInitialisation, because we don't have any initialization data
					textureDesc.Usage = UsageForDesc(desc, lateInitialisation);
					textureDesc.BindFlags = AsNativeBindFlags(desc._bindFlags);
					textureDesc.CPUAccessFlags = AsNativeCPUAccessFlag(desc._cpuAccess);
					if (textureDesc.Usage == D3D11_USAGE_STAGING && !textureDesc.CPUAccessFlags)		// staging resources must have either D3D11_CPU_ACCESS_WRITE or D3D11_CPU_ACCESS_READ
						textureDesc.CPUAccessFlags = hasInitData ? D3D11_CPU_ACCESS_WRITE : D3D11_CPU_ACCESS_READ;
					textureDesc.MiscFlags = 0;
					return factory.CreateTexture1D(&textureDesc, hasInitData ? subResources : nullptr, desc._name);

				} else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D
					|| desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {

					D3D11_TEXTURE2D_DESC textureDesc;
					XlZeroMemory(textureDesc);
					textureDesc.Width = desc._textureDesc._width;
					textureDesc.Height = desc._textureDesc._height;
					textureDesc.MipLevels = desc._textureDesc._mipCount;
					textureDesc.ArraySize = std::max(desc._textureDesc._arrayCount, uint16(1));
					textureDesc.Format = AsDXGIFormat(desc._textureDesc._format);
					textureDesc.SampleDesc.Count = std::max(uint8(1), desc._textureDesc._samples._sampleCount);
					textureDesc.SampleDesc.Quality = desc._textureDesc._samples._samplingQuality;
					const bool lateInitialisation = !hasInitData; /// it must be lateInitialisation, because we don't have any initialization data
					textureDesc.Usage = UsageForDesc(desc, lateInitialisation);
					textureDesc.BindFlags = AsNativeBindFlags(desc._bindFlags);
					textureDesc.CPUAccessFlags = AsNativeCPUAccessFlag(desc._cpuAccess);
					if (textureDesc.Usage == D3D11_USAGE_STAGING && !textureDesc.CPUAccessFlags)		// staging resources must have either D3D11_CPU_ACCESS_WRITE or D3D11_CPU_ACCESS_READ
						textureDesc.CPUAccessFlags = hasInitData ? D3D11_CPU_ACCESS_WRITE : D3D11_CPU_ACCESS_READ;
					textureDesc.MiscFlags = 0;
					if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap)
						textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
					return factory.CreateTexture2D(&textureDesc, hasInitData ? subResources : nullptr, desc._name);

				} else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D) {

					D3D11_TEXTURE3D_DESC textureDesc;
					XlZeroMemory(textureDesc);
					textureDesc.Width = desc._textureDesc._width;
					textureDesc.Height = desc._textureDesc._height;
					textureDesc.Depth = desc._textureDesc._depth;
					textureDesc.MipLevels = desc._textureDesc._mipCount;
					textureDesc.Format = AsDXGIFormat(desc._textureDesc._format);
					const bool lateInitialisation = !hasInitData; /// it must be lateInitialisation, because we don't have any initialization data
					textureDesc.Usage = UsageForDesc(desc, lateInitialisation);
					textureDesc.BindFlags = AsNativeBindFlags(desc._bindFlags);
					textureDesc.CPUAccessFlags = AsNativeCPUAccessFlag(desc._cpuAccess);
					if (textureDesc.Usage == D3D11_USAGE_STAGING && !textureDesc.CPUAccessFlags)		// staging resources must have either D3D11_CPU_ACCESS_WRITE or D3D11_CPU_ACCESS_READ
						textureDesc.CPUAccessFlags = hasInitData ? D3D11_CPU_ACCESS_WRITE : D3D11_CPU_ACCESS_READ;
					textureDesc.MiscFlags = 0;
					return factory.CreateTexture3D(&textureDesc, hasInitData ? subResources : nullptr, desc._name);

				} else {
					Throw(::Exceptions::BasicLabel("Unknown texture dimensionality creating resource"));
				}
			}
			break;

		case ResourceDesc::Type::LinearBuffer:
			{
				if (hasInitData) {
                    auto top = init({0,0});
					subResources[0] = D3D11_SUBRESOURCE_DATA{top._data.begin(), UINT(top._data.size()), UINT(top._data.size())};
					hasInitData = !top._data.empty();
				}

				D3D11_BUFFER_DESC d3dDesc;
				XlZeroMemory(d3dDesc);
				d3dDesc.ByteWidth = desc._linearBufferDesc._sizeInBytes;
				const bool lateInitialisation = !hasInitData;
				d3dDesc.Usage = UsageForDesc(desc, lateInitialisation);
				d3dDesc.BindFlags = AsNativeBindFlags(desc._bindFlags);
				d3dDesc.CPUAccessFlags = AsNativeCPUAccessFlag(desc._cpuAccess);
				d3dDesc.MiscFlags = 0;
				d3dDesc.StructureByteStride = 0;
				if (desc._bindFlags & BindFlag::UnorderedAccess) {
					d3dDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
					d3dDesc.StructureByteStride = desc._linearBufferDesc._structureByteSize;
				}
				if (desc._bindFlags & BindFlag::DrawIndirectArgs)
					d3dDesc.MiscFlags |= D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
				if (desc._bindFlags & BindFlag::RawViews)
					d3dDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
				if (d3dDesc.Usage == D3D11_USAGE_STAGING && !d3dDesc.CPUAccessFlags)		// staging resources must have either D3D11_CPU_ACCESS_WRITE or D3D11_CPU_ACCESS_READ
					d3dDesc.CPUAccessFlags = hasInitData ? D3D11_CPU_ACCESS_WRITE : D3D11_CPU_ACCESS_READ;
				assert(desc._bindFlags != BindFlag::IndexBuffer || d3dDesc.BindFlags == D3D11_USAGE_DYNAMIC);

				return factory.CreateBuffer(&d3dDesc, hasInitData ? subResources : nullptr, desc._name);
			}

		default:
			Throw(::Exceptions::BasicLabel("Unknown resource type while creating resource"));
		}
	}

	std::shared_ptr<IResource> CreateResource(
		const ObjectFactory& factory,
		const ResourceDesc& desc,
		const ResourceInitializer& init)
	{
		return AsResourcePtr(CreateUnderlyingResource(factory, desc, init));
	}

	ResourceDesc AsGenericDesc(const D3D11_BUFFER_DESC& d3dDesc)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::LinearBuffer;
        desc._cpuAccess = AsGenericCPUAccess(d3dDesc.CPUAccessFlags);
        if (d3dDesc.Usage == D3D11_USAGE_DEFAULT) {
            desc._cpuAccess |= CPUAccess::Write;
        } else if (d3dDesc.Usage == D3D11_USAGE_DYNAMIC) {
            desc._cpuAccess |= CPUAccess::WriteDynamic;
        }
        desc._gpuAccess = GPUAccess::Read;
        desc._bindFlags = AsGenericBindFlags(d3dDesc.BindFlags);
        desc._linearBufferDesc._sizeInBytes = d3dDesc.ByteWidth;
        desc._name[0] = '\0';
        return desc;
    }

	ResourceDesc AsGenericDesc(const D3D11_TEXTURE2D_DESC& d3dDesc)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::Texture;
        desc._cpuAccess = AsGenericCPUAccess(d3dDesc.CPUAccessFlags);
        desc._gpuAccess = GPUAccess::Read;
        desc._bindFlags = AsGenericBindFlags(d3dDesc.BindFlags);
        desc._textureDesc._dimensionality = TextureDesc::Dimensionality::T2D;
        desc._textureDesc._width = d3dDesc.Width;
        desc._textureDesc._height = d3dDesc.Height;
        desc._textureDesc._mipCount = uint8(d3dDesc.MipLevels);
        desc._textureDesc._arrayCount = uint16(d3dDesc.ArraySize);
        desc._textureDesc._format = AsFormat(d3dDesc.Format);
        desc._textureDesc._samples = TextureSamples::Create();
        desc._name[0] = '\0';
        return desc;
    }

	ResourceDesc AsGenericDesc(const D3D11_TEXTURE1D_DESC& d3dDesc)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::Texture;
        desc._cpuAccess = AsGenericCPUAccess(d3dDesc.CPUAccessFlags);
        desc._gpuAccess = GPUAccess::Read;
        desc._bindFlags = AsGenericBindFlags(d3dDesc.BindFlags);
        desc._textureDesc._dimensionality = TextureDesc::Dimensionality::T1D;
        desc._textureDesc._width = d3dDesc.Width;
        desc._textureDesc._height = 1;
        desc._textureDesc._mipCount = uint8(d3dDesc.MipLevels);
        desc._textureDesc._arrayCount = uint16(d3dDesc.ArraySize);
        desc._textureDesc._format = AsFormat(d3dDesc.Format);
        desc._textureDesc._samples = TextureSamples::Create();
        desc._name[0] = '\0';
        return desc;
    }

	ResourceDesc AsGenericDesc(const D3D11_TEXTURE3D_DESC& d3dDesc)
    {
		ResourceDesc desc;
        desc._type = ResourceDesc::Type::Texture;
        desc._cpuAccess = AsGenericCPUAccess(d3dDesc.CPUAccessFlags);
        desc._gpuAccess = GPUAccess::Read;
        desc._bindFlags = AsGenericBindFlags(d3dDesc.BindFlags);
        desc._textureDesc._dimensionality = TextureDesc::Dimensionality::T3D;
        desc._textureDesc._width = d3dDesc.Width;
        desc._textureDesc._height = d3dDesc.Height;
        desc._textureDesc._mipCount = uint8(d3dDesc.MipLevels);
        desc._textureDesc._arrayCount = 1;
        desc._textureDesc._format = AsFormat(d3dDesc.Format);
        desc._textureDesc._samples = TextureSamples::Create();
        desc._name[0] = '\0';
        return desc;
    }

	ResourceDesc ExtractDesc(const UnderlyingResourcePtr& res)
    {
        if (intrusive_ptr<ID3D::Texture2D> texture2D = QueryInterfaceCast<ID3D::Texture2D>(res.get())) {
            D3D11_TEXTURE2D_DESC d3dDesc;
            texture2D->GetDesc(&d3dDesc);
            return AsGenericDesc(d3dDesc);
        } else if (intrusive_ptr<ID3D::Buffer> buffer = QueryInterfaceCast<ID3D::Buffer>(res.get())) {
            D3D11_BUFFER_DESC d3dDesc;
            buffer->GetDesc(&d3dDesc);
            return AsGenericDesc(d3dDesc);
        } else if (intrusive_ptr<ID3D::Texture1D> texture1D = QueryInterfaceCast<ID3D::Texture1D>(res.get())) {
            D3D11_TEXTURE1D_DESC d3dDesc;
            texture1D->GetDesc(&d3dDesc);
            return AsGenericDesc(d3dDesc);
        } else if (intrusive_ptr<ID3D::Texture3D> texture3D = QueryInterfaceCast<ID3D::Texture3D>(res.get())) {
            D3D11_TEXTURE3D_DESC d3dDesc;
            texture3D->GetDesc(&d3dDesc);
            return AsGenericDesc(d3dDesc);
        }
		ResourceDesc desc = {};
        desc._type = ResourceDesc::Type::Unknown;
        return desc;
    }

    ResourceDesc ExtractDesc(const ShaderResourceView& res)
    {
        return ExtractDesc(ExtractResource<ID3D::Resource>(res.GetUnderlying()).get());
    }

	ResourceDesc ExtractDesc(const RenderTargetView& res)
    {
		return ExtractDesc(ExtractResource<ID3D::Resource>(res.GetUnderlying()).get());
    }

    ResourceDesc ExtractDesc(const DepthStencilView& res)
    {
		return ExtractDesc(ExtractResource<ID3D::Resource>(res.GetUnderlying()).get());
    }

    ResourceDesc ExtractDesc(const UnorderedAccessView& res)
    {
		return ExtractDesc(ExtractResource<ID3D::Resource>(res.GetUnderlying()).get());
    }

	ID3D::Resource* AsID3DResource(UnderlyingResourcePtr res)
	{
		return res.get();
	}

	ID3D::Resource* AsID3DResource(IResource& res)
	{
		auto*d3dRes = (Resource*)res.QueryInterface(s_resourceInterface);
		if (!d3dRes) return nullptr;
		return d3dRes->_underlying.get();
	}

	IResourcePtr AsResourcePtr(ID3D::Resource* res)
	{
		return std::make_shared<Resource>(res);
	}

	IResourcePtr AsResourcePtr(intrusive_ptr<ID3D::Resource>&& ptr)
	{
		return std::make_shared<Resource>(std::move(ptr));
	}

	Resource& AsResource(IResource& res)
	{
		auto* r = (Resource*)res.QueryInterface(s_resourceInterface);
		assert(r);
		return *r;
	}
}}
