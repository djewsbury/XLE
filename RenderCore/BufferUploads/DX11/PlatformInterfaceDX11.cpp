// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Core/Prefix.h"
#include "../../RenderCore/Metal/Metal.h"

#if GFXAPI_TARGET == GFXAPI_DX11

    #include "../PlatformInterface.h"
    #include "../DataPacket.h"
    #include "../../RenderCore/DX11/IDeviceDX11.h"
    #include "../../RenderCore/DX11/Metal/DX11Utils.h"
    #include "../../RenderCore/DX11/Metal/IncludeDX11.h"
	#include "../../RenderCore/DX11/Metal/ObjectFactory.h"
	#include "../../RenderCore/DX11/Metal/Format.h"
	#include "../../RenderCore/DX11/Metal/Resource.h"
    #include "../../RenderCore/RenderUtils.h"
	#include "../../RenderCore/Format.h"
    #include "../../RenderCore/ResourceUtils.h"
    #include "../../Utility/HeapUtils.h"
    #include <functional>

    namespace BufferUploads { namespace PlatformInterface
    {
		using namespace RenderCore;

        static bool IsDXTCompressed(Format format) { return GetCompressionType(format) == FormatCompressionType::BlockCompression; }
        static ID3D::Resource*        ResPtr(UnderlyingResource& resource) { return Metal::AsID3DResource(resource); }

        unsigned ResourceUploadHelper::WriteToTextureViaUpdateSubresource(
            UnderlyingResource& resource, const ResourceDesc& desc,
            const Box2D& box, 
            const ResourceInitializer& data)
        {
            assert(desc._type == ResourceDesc::Type::Texture);

			auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
            const bool isFullUpdate = box == Box2D();

            #if defined(_DEBUG)
                if (isFullUpdate) {
                    intrusive_ptr<ID3D::Texture2D> texture = Metal::QueryInterfaceCast<ID3D::Texture2D>(ResPtr(resource));
                    if (texture) {
                        D3D11_TEXTURE2D_DESC tdesc;
                        texture->GetDesc(&tdesc);
                        assert(tdesc.Usage == D3D11_USAGE_DEFAULT);
                    }
                }
            #endif

            unsigned copiedBytes = 0;
            for (unsigned mip=0; mip<std::max(1u, unsigned(desc._textureDesc._mipCount)); ++mip) {
                for (unsigned arrayLayer=0; arrayLayer<std::max(1u, unsigned(desc._textureDesc._arrayCount)); ++arrayLayer) {
                    auto srd = data({mip, arrayLayer});
                    if (!srd._data.size()) continue;

                    uint32_t subResource = D3D11CalcSubresource(mip, arrayLayer, desc._textureDesc._mipCount);
                    if (isFullUpdate) {

				        metalContext->GetUnderlying()->UpdateSubresource(
                            ResPtr(resource), subResource, NULL, srd._data.begin(), 
                            srd._pitches._rowPitch, srd._pitches._slicePitch);

                    } else {

                        D3D11_BOX d3dBox = {(UINT)box._left, (UINT)box._top, 0u, (UINT)box._right, (UINT)box._bottom, 1u};
                        const void* pAdjustedSrcData = srd._data.begin();

                        #if DX_VERSION >= DX_VERSION_11_1
                                //  Attempt to use "ID3D11DeviceContext1", if we can get it. This version solves
                                //  a bug in the earlier version of D3D11
                            ID3D11DeviceContext1 * devContext1Temp = nullptr;
                            auto hresult = metalContext->GetUnderlying()->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&devContext1Temp);
                            intrusive_ptr<ID3D11DeviceContext1> devContext1(devContext1Temp, false);
                            if (SUCCEEDED(hresult) && devContext1) {

                                assert(!IsBadReadPtr(srd._data.begin(), srd._pitches._slicePitch));
                                unsigned copyFlags = 0; // (can be no_overwrite / discard on Win8)
                                devContext1->UpdateSubresource1(
                                    ResPtr(resource), subResource, &d3dBox, srd._data.begin(), 
                                    srd._pitches._rowPitch, srd._pitches._slicePitch, copyFlags);
                            
                            } else 
                        #endif
                        {

                                //
                                //   note --    See documentation in ID3D11DeviceContext::UpdateSubresource for a bug when 
                                //              calling this in a deferred context with non-NULL D3D11_BOX
                                //              Here is the work around...
                                //                  (note we can also use ID3D11DeviceContext1::UpdateSubresource1 which
                                //                  has a fix for this)
                                //
                            if (_useUpdateSubresourceWorkaround) {

                                    D3D11_BOX alignedBox = d3dBox;
                                    unsigned srcBitsPerElement = BitsPerPixel(desc._textureDesc._format);

                                        // convert from pixels to blocks
                                    if (IsDXTCompressed(desc._textureDesc._format)) {
                                        alignedBox.left     /= 4;
                                        alignedBox.right    /= 4;
                                        alignedBox.top      /= 4;
                                        alignedBox.bottom   /= 4;
                                        srcBitsPerElement   *= 16;
                                    }
                             
                                    pAdjustedSrcData = ((const BYTE*)srd._data.begin()) - (alignedBox.front * srd._pitches._slicePitch) - (alignedBox.top * srd._pitches._rowPitch) - (alignedBox.left * (srcBitsPerElement/8));
                            }

                            Metal::TextureDesc2D destinationDesc(ResPtr(resource));

                            // {
                            //     char buffer[4196];
                            //     _snprintf_s(buffer, _TRUNCATE, "Do UpdateSubresource: {%i,%i,%i,%i,%i,%i} 0x%x08 %0x08x (%i,%i)\n",
                            //         d3dBox.left, d3dBox.top, d3dBox.front, d3dBox.right, d3dBox.bottom, d3dBox.back,
                            //         pAdjustedSrcData, data, rowAndSlicePitch.first, rowAndSlicePitch.second);
                            //     OutputDebugString(buffer);
                            // }

                            assert(pAdjustedSrcData != nullptr);
                            assert(!IsBadReadPtr(srd._data.begin(), srd._pitches._slicePitch));
					        metalContext->GetUnderlying()->UpdateSubresource(
                                ResPtr(resource), subResource, &d3dBox, pAdjustedSrcData, 
                                srd._pitches._rowPitch, srd._pitches._slicePitch);

                        }
                    }

                    copiedBytes += (unsigned)srd._data.size();
                }
            }

            return copiedBytes;
        }

        unsigned ResourceUploadHelper::WriteToBufferViaMap(
            UnderlyingResource& resource, const ResourceDesc& desc, unsigned offset,
            const void* data, size_t dataSize)
        {
            assert(desc._type == ResourceDesc::Type::LinearBuffer);
            auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);

            unsigned copiedBytes = 0;
                //
                //      When CPU access is 0, we can only use UpdateSubresource.
                //      But when we can't use UpdateSubresource when usage is D3D11_USAGE_DYNAMIC.
                //  
            if ((desc._cpuAccess&CPUAccess::WriteDynamic)!=CPUAccess::WriteDynamic) {
                D3D11_BOX box;
                box.top = box.front = 0;
                box.bottom = box.back = 1;
                box.left = offset;
                box.right = (UINT)(offset+dataSize);
                if (_useUpdateSubresourceWorkaround) {  // see documentation for ID3D11DeviceContext::UpdateSubresource for the description for this workaround
                    data = (const void*)(size_t(data)-offset);
                }
                #if defined(_DEBUG)
                    {
                        intrusive_ptr<ID3D::Buffer> buffer = Metal::QueryInterfaceCast<ID3D::Buffer>(ResPtr(resource));
                        if (buffer) {
                            D3D11_BUFFER_DESC tdesc;
                            buffer->GetDesc(&tdesc);
                            assert(tdesc.Usage == D3D11_USAGE_DEFAULT);
                        }
                    }
                #endif
				metalContext->GetUnderlying()->UpdateSubresource(ResPtr(resource), 0, &box, data, 0, 0);
                copiedBytes += (unsigned)dataSize;
            } else {
                D3D11_MAPPED_SUBRESOURCE mappedSubresource;
                const bool canDoNoOverwrite = _renderCoreContext->IsImmediate();
                assert(canDoNoOverwrite || offset==0);     //  this code could be dangerous when the resource offset value != 0. Map() will map the entire
                                                                        //  resource. But when using a batched index buffer, we only want to map and write to a small part of the buffer.
                HRESULT hresult = metalContext->GetUnderlying()->Map(ResPtr(resource), 0, canDoNoOverwrite?D3D11_MAP_WRITE_NO_OVERWRITE:D3D11_MAP_WRITE_DISCARD, 0/*D3D11_MAP_FLAG_DO_NOT_WAIT*/, &mappedSubresource);
                assert(SUCCEEDED(hresult));
                if (SUCCEEDED(hresult)) {
                    assert(mappedSubresource.RowPitch >= dataSize);
                    XlCopyMemoryAlign16(PtrAdd(mappedSubresource.pData, offset), data, dataSize);
				    metalContext->GetUnderlying()->Unmap(ResPtr(resource), 0);
                    copiedBytes += (unsigned)dataSize;
                }
            }

            return copiedBytes;
        }

        unsigned ResourceUploadHelper::WriteToTextureViaMap(
            UnderlyingResource& resource, const ResourceDesc&desc,
            const Box2D& box,
            const ResourceInitializer& data)
        {
            // In D3D, we must map each subresource separately
            unsigned copiedBytes = 0;
            auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
            for (unsigned m=0; m<desc._textureDesc._mipCount; ++m)
                for (unsigned a=0; a<desc._textureDesc._arrayCount; ++a) {
                    auto subResData = data({m, a});
                    if (!subResData._data.size()) continue;

                    uint32_t subResource = D3D11CalcSubresource(m, a, desc._textureDesc._mipCount);
                    D3D11_MAPPED_SUBRESOURCE mappedSubresource;
                    XlZeroMemory(mappedSubresource);
                    HRESULT hresult = metalContext->GetUnderlying()->Map(
                        ResPtr(resource), subResource, 
                        D3D11_MAP_WRITE, 0, &mappedSubresource);
                    assert(SUCCEEDED(hresult));

                    if (SUCCEEDED(hresult) && mappedSubresource.pData) {
                        auto dstSize = subResData._data.size();        // D3D11_MAPPED_SUBRESOURCE doesn't contain a size!
                        copiedBytes += CopyMipLevel(
                            mappedSubresource.pData, dstSize, TexturePitches{mappedSubresource.DepthPitch, mappedSubresource. DepthPitch},
                            CalculateMipMapDesc(desc._textureDesc, m),
                            subResData);
                    }
                }
            return copiedBytes;
        }

        void ResourceUploadHelper::UpdateFinalResourceFromStaging(
            UnderlyingResource& finalResource, UnderlyingResource& staging, 
            const ResourceDesc& destinationDesc, 
            unsigned lodLevelMin, unsigned lodLevelMax, unsigned stagingLODOffset,
            VectorPattern<unsigned, 2> stagingXYOffset,
            const RenderCore::Box2D& srcBox)
        {
            auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
            auto allLods = 
                (lodLevelMin == ~unsigned(0x0) || lodLevelMin == 0u)
                && (lodLevelMax == ~unsigned(0x0) || lodLevelMax == (std::max(1u, (unsigned)destinationDesc._textureDesc._mipCount)-1));

            if (allLods && destinationDesc._type == ResourceDesc::Type::Texture && !stagingLODOffset && !stagingXYOffset[0] && !stagingXYOffset[1]) {
                Metal::Copy(
                    *metalContext, 
					Metal::AsResource(finalResource), Metal::AsResource(staging),
                    Metal::ImageLayout::TransferDstOptimal, Metal::ImageLayout::TransferSrcOptimal);
            } else {
                for (unsigned a=0; a<std::max(1u, (unsigned)destinationDesc._textureDesc._arrayCount); ++a) {
                    for (unsigned c=lodLevelMin; c<=lodLevelMax; ++c) {
                        Metal::CopyPartial(
                            *metalContext,
                            Metal::CopyPartial_Dest{
                                &finalResource, 
                                SubResourceId{c, a}, 
								{stagingXYOffset[0], stagingXYOffset[1], 0}},
                            Metal::CopyPartial_Src{
								&staging,
                                SubResourceId{c-stagingLODOffset, a},
                                {(unsigned)srcBox._left, (unsigned)srcBox._top, 0u},
								{(unsigned)srcBox._right, (unsigned)srcBox._bottom, 1u}},
                                Metal::ImageLayout::Undefined, Metal::ImageLayout::Undefined);
                    }
                }
            }
        }

        struct MapType { enum Enum { Discard, NoOverwrite, ReadOnly, Write }; };
        class MappedBuffer
        {
        public:
            void*           GetData()               { return _data; }
            const void*     GetData() const         { return _data; }
            TexturePitches  GetPitches() const      { return _pitches; }
        
            MappedBuffer(Metal::DeviceContext&, const UnderlyingResourcePtr&, unsigned, void*, TexturePitches pitches);
            MappedBuffer();
            MappedBuffer(MappedBuffer&& moveFrom) never_throws;
            const MappedBuffer& operator=(MappedBuffer&& moveFrom) never_throws;
            ~MappedBuffer();
        private:
            Metal::DeviceContext* _sourceContext;
			UnderlyingResourcePtr _resource;
            unsigned _subResourceIndex;
            void* _data;
            TexturePitches _pitches;
        };

        static MappedBuffer Map(Metal::DeviceContext& context, const UnderlyingResourcePtr& resource, MapType::Enum mapType, unsigned subResource=0)
        {
            // uint32_t subResource = 0;
            // intrusive_ptr<ID3D::Texture2D> tex2D = QueryInterfaceCast<ID3D::Texture2D>(ResPtr(resource));
            // if (tex2D && arrayIndex > 0) {
            //     D3D11_TEXTURE2D_DESC desc;
            //     tex2D->GetDesc(&desc);
            //     subResource = D3D11CalcSubresource(lodLevel, arrayIndex, desc.MipLevels);
            // } else {
            //     subResource = D3D11CalcSubresource(lodLevel, 0, 0);
            // }
            D3D11_MAPPED_SUBRESOURCE result;
            D3D11_MAP platformMap;
            switch (mapType) {
            case MapType::NoOverwrite:  platformMap = D3D11_MAP_WRITE_NO_OVERWRITE; break;
            case MapType::Write:        platformMap = D3D11_MAP_WRITE;              break;
            case MapType::Discard:      platformMap = D3D11_MAP_WRITE_DISCARD;      break;
            case MapType::ReadOnly:     platformMap = D3D11_MAP_READ;               break;
            default:                    platformMap = D3D11_MAP_WRITE;              break;
            }
            HRESULT hresult = context.GetUnderlying()->Map(ResPtr(*resource), subResource, platformMap, 0/*D3D11_MAP_FLAG_DO_NOT_WAIT*/, &result);
            if (SUCCEEDED(hresult)) {
                return MappedBuffer(context, resource, subResource, result.pData, TexturePitches{result.RowPitch, result.DepthPitch});
            }
            return MappedBuffer();
        }

        static MappedBuffer MapPartial(Metal::DeviceContext& context, UnderlyingResource& resource, MapType::Enum mapType, unsigned offset, unsigned size, unsigned subResource)
        {
            assert(0);  // can't do partial maps in D3D11
            return MappedBuffer();
        }

        static void Unmap(Metal::DeviceContext& context, UnderlyingResource& resource, unsigned subResourceIndex)
        {
			context.GetUnderlying()->Unmap(ResPtr(resource), subResourceIndex);
        }

        MappedBuffer::~MappedBuffer()
        {
            if (_sourceContext && _data) {
                Unmap(*_sourceContext, *_resource, _subResourceIndex);
            }
        }

        MappedBuffer::MappedBuffer()
        {
            _sourceContext = 0;
            _subResourceIndex = 0;
            _data = 0;
        }

        MappedBuffer::MappedBuffer(MappedBuffer&& moveFrom) never_throws
        {
            _sourceContext = std::move(moveFrom._sourceContext);
            _resource = std::move(moveFrom._resource);
            _subResourceIndex = std::move(moveFrom._subResourceIndex);
            _data = std::move(moveFrom._data);
            _pitches = moveFrom._pitches;
            moveFrom._data = nullptr;
            moveFrom._subResourceIndex = 0;
            moveFrom._sourceContext = nullptr;
            moveFrom._pitches = TexturePitches();
        }

        const MappedBuffer& MappedBuffer::operator=(MappedBuffer&& moveFrom) never_throws
        {
            if (_sourceContext && _data) {
                Unmap(*_sourceContext, *_resource, _subResourceIndex);
            }

            _sourceContext = std::move(moveFrom._sourceContext);
            _resource = std::move(moveFrom._resource);
            _subResourceIndex = std::move(moveFrom._subResourceIndex);
            _data = std::move(moveFrom._data);
            _pitches = moveFrom._pitches;
            moveFrom._data = nullptr;
            moveFrom._subResourceIndex = 0;
            moveFrom._sourceContext = nullptr;
            moveFrom._pitches = TexturePitches();
            return *this;
        }

        MappedBuffer::MappedBuffer(
            Metal::DeviceContext& context, const UnderlyingResourcePtr& resource, 
            unsigned subResourceIndex, void* data,
            TexturePitches pitches)
	    : _resource(resource)
        {
            _sourceContext = &context;
            _subResourceIndex = subResourceIndex;
            _data = data;
            _pitches = pitches;
        }

        void ResourceUploadHelper::ResourceCopy_DefragSteps(const UnderlyingResourcePtr& destination, const UnderlyingResourcePtr& source, const std::vector<DefragStep>& steps)
        {
            auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext); 
            if (!constant_expression<UseMapBasedDefrag>::result()) {
                    //
                    //      For each adjustment, we perform one CopySubresourceRegion...
                    //
				for (std::vector<DefragStep>::const_iterator i=steps.begin(); i!=steps.end(); ++i) {
                    assert(i->_sourceEnd > i->_sourceStart);
                    using namespace RenderCore;
                    Metal::CopyPartial(
                        *metalContext,
						Metal::CopyPartial_Dest{destination.get(), SubResourceId{}, {0, i->_destination}},
						Metal::CopyPartial_Src{source.get(), SubResourceId{}, {0, i->_sourceStart}, {i->_sourceEnd, 1, 1}});
                }
            } else {
                MappedBuffer sourceBuffer       = Map(*metalContext, source, MapType::ReadOnly);
                MappedBuffer destinationBuffer  = Map(*metalContext, destination, MapType::Discard);
                if (sourceBuffer.GetData() && destinationBuffer.GetData()) {
                    for (std::vector<DefragStep>::const_iterator i=steps.begin(); i!=steps.end(); ++i) {
                        const void* sourcePtr   = PtrAdd(sourceBuffer.GetData(), i->_sourceStart);
                        void* destinationPtr    = PtrAdd(destinationBuffer.GetData(), i->_destination);
                        XlCopyMemoryAlign16(destinationPtr, sourcePtr, int(i->_sourceEnd-i->_sourceStart));
                    }
                }
            }
        }

        void ResourceUploadHelper::ResourceCopy(UnderlyingResource& destination, UnderlyingResource& source)
        {
			auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
            RenderCore::Metal::Copy(*metalContext, Metal::AsResource(destination), Metal::AsResource(source));
        }

        intrusive_ptr<RenderCore::Metal::CommandList> ResourceUploadHelper::ResolveCommandList()
        {
			auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
            return metalContext->ResolveCommandList();
        }

        void                        ResourceUploadHelper::BeginCommandList()
        {
			auto metalContext = Metal::DeviceContext::Get(*_renderCoreContext);
			metalContext->BeginCommandList();
        }

		std::shared_ptr<RenderCore::IDevice> ResourceUploadHelper::GetObjectFactory()
		{
			return _renderCoreContext->GetDevice();
		}

        ResourceUploadHelper::ResourceUploadHelper(RenderCore::IThreadContext& renderCoreContext) 
        : _renderCoreContext(&renderCoreContext)
        {
            _useUpdateSubresourceWorkaround = false;

            if (!renderCoreContext.IsImmediate()) {
				auto dev = renderCoreContext.GetDevice();
				IDeviceDX11* devDX11 = (IDeviceDX11*)dev->QueryInterface(typeid(IDeviceDX11).hash_code());
				if (devDX11) {
						//
						//  See D3D documentation for "ID3D11DeviceContext::UpdateSubresource'
						//  There's a bug in D3D related to using a deferred context when the driver
						//  doesn't support driver command lists. Let's check if we need to use
						//  the workaround for this bug...
						//
					D3D11_FEATURE_DATA_THREADING threadingCaps = { FALSE, FALSE };
					HRESULT hr = devDX11->GetUnderlyingDevice()->CheckFeatureSupport(
						D3D11_FEATURE_THREADING, &threadingCaps, sizeof(threadingCaps));
					_useUpdateSubresourceWorkaround = SUCCEEDED(hr) && !threadingCaps.DriverCommandLists;
				}
            }
        }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class RawDataPacket_ReadBack : public DataPacket
    {
    public:
        void*           GetData(SubResourceId subRes);
        size_t          GetDataSize(SubResourceId subRes) const;
        TexturePitches  GetPitches(SubResourceId subRes) const;

        std::shared_ptr<Marker> BeginBackgroundLoad() { return nullptr; }

        RawDataPacket_ReadBack(
            const ResourceLocator& locator, 
            PlatformInterface::ResourceUploadHelper& context);
        ~RawDataPacket_ReadBack();

    protected:
        unsigned _dataOffset;
        std::vector<MappedBuffer> _mappedBuffer;
        unsigned _mipCount;
        unsigned _arrayCount;
    };

    void*     RawDataPacket_ReadBack::GetData(SubResourceId subRes)
    {
        auto arrayIndex = subRes._arrayLayer, mip = subRes._mip;
        unsigned subResIndex = mip + arrayIndex * _mipCount;
        assert(subResIndex < _mappedBuffer.size());
        return PtrAdd(_mappedBuffer[subResIndex].GetData(), _dataOffset);
    }

    size_t          RawDataPacket_ReadBack::GetDataSize(SubResourceId subRes) const
    {
        auto arrayIndex = subRes._arrayLayer, mip = subRes._mip;
        unsigned subResIndex = mip + arrayIndex * _mipCount;
        assert(subResIndex < _mappedBuffer.size());
        return _mappedBuffer[subResIndex].GetPitches()._slicePitch - _dataOffset;
    }

    TexturePitches RawDataPacket_ReadBack::GetPitches(SubResourceId subRes) const
    {
        auto arrayIndex = subRes._arrayLayer, mip = subRes._mip;
        unsigned subResIndex = mip + arrayIndex * _mipCount;
        assert(subResIndex < _mappedBuffer.size());
        return _mappedBuffer[subResIndex].GetPitches();
    }

    static ResourceDesc AsStagingDesc(const ResourceDesc& desc)
    {
        ResourceDesc result = desc;
        result._cpuAccess = CPUAccess::Write|CPUAccess::Read;
        result._gpuAccess = 0;
        result._bindFlags = BindFlag::TransferSrc;
        result._allocationRules |= AllocationRules::Staging;
        return result;
    }

    RawDataPacket_ReadBack::RawDataPacket_ReadBack(
		const ResourceLocator& locator, 
		PlatformInterface::ResourceUploadHelper& context)
    : _dataOffset(0)
    {
        assert(!locator.IsEmpty());
        auto resource = locator.GetUnderlying();
        UnderlyingResourcePtr stagingResource;

        auto desc = PlatformInterface::ExtractDesc(*resource);
        auto subResCount = 1u;
        _mipCount = _arrayCount = 1u;
        if (desc._type == ResourceDesc::Type::Texture) {
            subResCount = 
                  std::max(1u, unsigned(desc._textureDesc._mipCount))
                * std::max(1u, unsigned(desc._textureDesc._arrayCount));
            _mipCount = desc._textureDesc._mipCount;
            _arrayCount = desc._textureDesc._arrayCount;
        }

            //
            //      If we have to read back through a staging resource, then let's create
            //      a temporary resource and initialise it...
            //
        using namespace PlatformInterface;
        if (RequiresStagingResourceReadBack && !(desc._cpuAccess & CPUAccess::Read)) {
            ResourceDesc stagingDesc = AsStagingDesc(desc);
            stagingResource = CreateResource(*context.GetObjectFactory(), stagingDesc);
            if (stagingResource.get()) {
                context.ResourceCopy(*stagingResource.get(), *resource);
                resource = stagingResource;
            }
        }

        _mappedBuffer.reserve(subResCount);

        auto metalContext = Metal::DeviceContext::Get(context.GetUnderlying());
        if (constant_expression<CanDoPartialMaps>::result()) {
            for (unsigned c=0; c<subResCount; ++c)
                _mappedBuffer.push_back(MapPartial(
                    *metalContext,
                    *resource, MapType::ReadOnly, 
                    locator.Offset(), locator.Size(), c));
        } else {
            for (unsigned c=0; c<subResCount; ++c)
                _mappedBuffer.push_back(Map(*metalContext, resource, MapType::ReadOnly, c));
            _dataOffset = (locator.Offset() != ~unsigned(0x0))?locator.Offset():0;
        }
    }

    RawDataPacket_ReadBack::~RawDataPacket_ReadBack()
    {
    }

    intrusive_ptr<DataPacket> ResourceUploadHelper::Readback(const ResourceLocator& locator)
    {
        return make_intrusive<RawDataPacket_ReadBack>(std::ref(locator), std::ref(*this));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

        void    Query_End(ID3D::DeviceContext* context, ID3D::Query* query)
        {
            context->End(query);
        }

        bool    Query_IsEventTriggered(ID3D::DeviceContext* context, ID3D::Query* query)
        {
            uint32_t queryValue = 0;
            HRESULT result = context->GetData(
                query, &queryValue, sizeof(queryValue), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            return SUCCEEDED(result) && queryValue;
        }

        intrusive_ptr<ID3D::Query> Query_CreateEvent(Metal::ObjectFactory& objFactory)
        {
            D3D11_QUERY_DESC queryDesc;
            queryDesc.Query      = D3D11_QUERY_EVENT;
            queryDesc.MiscFlags  = 0;
            return objFactory.CreateQuery(&queryDesc);
        }

        IResourcePtr CreateResource(RenderCore::IDevice& device, const ResourceDesc& desc, DataPacket* initialisationData)
        {
			if (initialisationData) {
				return device.CreateResource(desc,
					[initialisationData](SubResourceId sr) -> RenderCore::SubResourceInitData
					{
						RenderCore::SubResourceInitData result;
						const void* data = initialisationData->GetData(sr);
						auto size = initialisationData->GetDataSize(sr);
						result._data = MakeIteratorRange(data, PtrAdd(data, size));
						result._pitches = initialisationData->GetPitches(sr);
						return result;
					});
			} else {
				return device.CreateResource(desc);
			}
        }

		ResourceDesc ExtractDesc(UnderlyingResource& resource)
		{
			return resource.GetDesc();
		}

        void AttachObject(ID3D::Resource* resource, const GUID& guid, IUnknown* attachableObject)
        {
            HRESULT hresult = resource->SetPrivateDataInterface(guid, attachableObject);
            assert(SUCCEEDED(hresult)); (void)hresult;
        }


#if 0
    #if GFXAPI_TARGET == GFXAPI_DX11
        intrusive_ptr<ID3D::Query> Query_CreateEvent(Metal::ObjectFactory& factory);
        bool    Query_IsEventTriggered(ID3D::DeviceContext* context, ID3D::Query* query);
        void    Query_End(ID3D::DeviceContext* context, ID3D::Query* query);
    #endif

    static const GPUEventStack::EventID EventID_Temporary    = ~GPUEventStack::EventID(0x1);
    static const GPUEventStack::EventID EventID_Unallocated  = ~GPUEventStack::EventID(0x0);

    void  GPUEventStack::TriggerEvent(RenderCore::Metal::DeviceContext* context, EventID event)
    {
#if GFXAPI_TARGET == GFXAPI_DX11
            //
            //      Look for a query in the query stack that isn't being used...
            //      this will become our.
            //      Must be done in the render thread! (event queries only work on immediate context)
            //
        for (std::vector<Query>::iterator i=_queries.begin(); i!=_queries.end(); ++i) {
            const EventID previousValue = Interlocked::CompareExchange(&i->_assignedID, EventID_Temporary, EventID_Unallocated);
            if (previousValue == EventID_Unallocated) {
                QueryID thisQueryID = Interlocked::Increment(&_nextQueryID);

                    //
                    //      Trigger the event... But make sure we do it in the correct order... use the 
                    //      _nextIDToSchedule variable to make sure it happens correctly, even when multiple 
                    //      threads are scheduling events at the same time!
                    //
                    //      But -- note that this ordering only really works correctly if 
                    //
                QueryID nextToSchedule = thisQueryID+1;
                for (;;) {
                    const QueryID scheduleResult = Interlocked::CompareExchange(&_nextQueryIDToSchedule, nextToSchedule, thisQueryID);
                    if (scheduleResult == thisQueryID) {
                        if (!i->_query) { i->_query = Query_CreateEvent(*_objFactory); }
                        if (i->_query) { Query_End(context->GetUnderlying(), i->_query.get()); }
                        break;
                    }
                    Threading::Pause();
                }
                i->_eventID = event;
                i->_assignedID = thisQueryID;
                return;
            }
        }
        LogWarning << "Ran out of free query objects in GPUEventStack";
        _lastCompletedID = std::max(_lastCompletedID, event);       // consider it immediately completed
#endif
    }

    void        GPUEventStack::Update(RenderCore::Metal::DeviceContext* context)
    {
#if GFXAPI_TARGET == GFXAPI_DX11
            //
            //      Look for completed queries, and update our current ID as they complete (also return the 
            //      query to the pool)
            //      Must be done in the render thread! (event queries only work on immediate context)
            //
        for (std::vector<Query>::iterator i=_queries.begin(); i!=_queries.end(); ++i) {
            if (i->_assignedID >= 0) {
                if (!i->_query || Query_IsEventTriggered(context->GetUnderlying(), i->_query.get())) {
                    _lastCompletedID = std::max(_lastCompletedID, i->_eventID);
                    Interlocked::Exchange(&i->_assignedID, EventID_Unallocated);
                }
            }
        }
#endif
    }

    void        GPUEventStack::OnLostDevice()
    {
#if GFXAPI_TARGET != GFXAPI_OPENGLES
            // On device lost, we must consider all queries triggered, and then un-allocate them
        for (std::vector<Query>::iterator i=_queries.begin(); i!=_queries.end(); ++i) {
            i->_query.reset();
            if (i->_assignedID >= 0) {
                _lastCompletedID = std::max(_lastCompletedID, i->_eventID);
                Interlocked::Exchange(&i->_assignedID, EventID_Unallocated);
            }
        }
#endif
    }

    void        GPUEventStack::OnDeviceReset()
    {
            // On device lost, we must consider all queries triggered, and then un-allocate them
        for (std::vector<Query>::iterator i=_queries.begin(); i!=_queries.end(); ++i) {
            *i = Query();
        }
    }

    GPUEventStack::GPUEventStack(RenderCore::IDevice& device) : _objFactory(&Metal::GetObjectFactory(device))
    {
            //
            //      What we really want is an "event" query that holds an integer value (like the events on the 
            //      PS3 hardware). This would allow us to track the GPU progress without needing multiple event
            //      objects.
            //
            //      But D3D only has the boolean events; so we need to use a separate event object for each marker
            //      we want to record...
            //
        const unsigned queryStackDepth = 32;
        _queries.resize(queryStackDepth);
        _nextQueryIDToSchedule = _nextQueryID = 1;
        _lastCompletedID = 0;
    }

    GPUEventStack::~GPUEventStack()
    {
    }

    GPUEventStack::Query::Query()
    {
#if GFXAPI_TARGET != GFXAPI_OPENGLES
        _query = nullptr;
#endif
        _assignedID = EventID_Unallocated;
    }

    GPUEventStack::Query::~Query()
    {}      // just a place for so the intrusive_ptr destructor code doesn't get compiled into multiple source files

#endif

    }}

#endif
