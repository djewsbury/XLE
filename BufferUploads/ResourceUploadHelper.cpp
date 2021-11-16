// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceUploadHelper.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Metal/Metal.h"
#include "../RenderCore/Metal/Resource.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/IDevice.h"
#include "../OSServices/Log.h"
#include "../Utility/StringFormat.h"
#include <assert.h>

#if GFXAPI_TARGET == GFXAPI_DX11
    #include "../RenderCore/DX11/Metal/IncludeDX11.h"
#endif

namespace BufferUploads { namespace PlatformInterface
{
	using namespace RenderCore;

    unsigned ResourceUploadHelper::WriteToTextureViaMap(
        const ResourceLocator& resource, const ResourceDesc& desc,
        const Box2D& box, 
        const IDevice::ResourceInitializer& data)
    {
        assert(resource.IsWholeResource());
        auto* metalResource = resource.GetContainingResource().get();

        // In Vulkan, the only way we have to send data to a resource is by using
        // a memory map and CPU assisted copy. 
        assert(desc._type == ResourceDesc::Type::Texture);
        if (box == Box2D()) {
            auto* vulkanResource = (Metal::Resource*)metalResource->QueryInterface(typeid(Metal::Resource).hash_code());
            assert(vulkanResource);
            return Metal::Internal::CopyViaMemoryMap(*_renderCoreContext->GetDevice(), *vulkanResource, data);
        }

        auto& metalContext = *Metal::DeviceContext::Get(*_renderCoreContext);

        // When we have a box, we support writing to only a single subresource
        // We will iterate through the subresources an mip a single one
        auto dev = _renderCoreContext->GetDevice();
        auto copiedBytes = 0u;
        for (unsigned mip=0; mip<desc._textureDesc._mipCount; ++mip)
            for (unsigned arrayLayer=0; arrayLayer<ActualArrayLayerCount(desc._textureDesc); ++arrayLayer) {
                auto srd = data({mip, arrayLayer});
                if (!srd._data.size()) continue;

                SubResourceId sub{mip, arrayLayer};
                Metal::ResourceMap map(metalContext, *metalResource, Metal::ResourceMap::Mode::WriteDiscardPrevious, sub);
                copiedBytes += CopyMipLevel(
                    map.GetData(sub).begin(), map.GetData(sub).size(), map.GetPitches(sub), 
                    desc._textureDesc,
                    box, srd);
            }

        return copiedBytes;
    }

    void ResourceUploadHelper::UpdateFinalResourceFromStaging(
        const ResourceLocator& finalResource, const ResourceLocator& stagingResource, 
        const ResourceDesc& destinationDesc, 
        const StagingToFinalMapping& stagingToFinalMapping)
    {
        assert(finalResource.IsWholeResource());
        assert(stagingResource.IsWholeResource());

        assert(destinationDesc._type == ResourceDesc::Type::Texture);
        auto dstLodLevelMax = std::min(stagingToFinalMapping._dstLodLevelMax, (unsigned)destinationDesc._textureDesc._mipCount-1);
        auto dstArrayLayerMax = std::min(stagingToFinalMapping._dstArrayLayerMax, (unsigned)destinationDesc._textureDesc._arrayCount-1);
        auto allLods = stagingToFinalMapping._dstLodLevelMin == 0 && dstLodLevelMax == ((unsigned)destinationDesc._textureDesc._mipCount-1);
        auto allArrayLayers = stagingToFinalMapping._dstArrayLayerMin == 0 && dstArrayLayerMax == ((unsigned)destinationDesc._textureDesc._arrayCount-1);
        if (destinationDesc._textureDesc._arrayCount == 0) {
            dstArrayLayerMax = 0;
            allArrayLayers = true;
        }
        auto entire2DPlane = stagingToFinalMapping._stagingXYOffset[0] == 0 && stagingToFinalMapping._stagingXYOffset[1] == 0;

        // During the transfer, the images must be in either TransferSrcOptimal, TransferDstOptimal or General.
        auto& metalContext = *Metal::DeviceContext::Get(*_renderCoreContext);
        Metal::Internal::CaptureForBind(metalContext, *finalResource.GetContainingResource(), BindFlag::TransferDst);
        Metal::Internal::CaptureForBind(metalContext, *stagingResource.GetContainingResource(), BindFlag::TransferSrc);
        auto blitEncoder = metalContext.BeginBlitEncoder();

        if (allLods && allArrayLayers && entire2DPlane) {
            blitEncoder.Copy(*finalResource.GetContainingResource().get(), *stagingResource.GetContainingResource().get());
        } else {
            auto& dstBox = stagingToFinalMapping._dstBox;
            for (unsigned a=stagingToFinalMapping._dstArrayLayerMin; a<=dstArrayLayerMax; ++a) {
                for (unsigned mip=stagingToFinalMapping._dstLodLevelMin; mip<=dstLodLevelMax; ++mip) {
                    blitEncoder.Copy(
                        CopyPartial_Dest{
                            *finalResource.GetContainingResource(), 
                            SubResourceId{mip, a}, {(unsigned)dstBox._left, (unsigned)dstBox._top, 0}},
                        CopyPartial_Src{
                            *stagingResource.GetContainingResource(), 
                            SubResourceId{mip-stagingToFinalMapping._stagingLODOffset, a-stagingToFinalMapping._stagingArrayOffset},
                            {(unsigned)dstBox._left - stagingToFinalMapping._stagingXYOffset[0], (unsigned)dstBox._top - stagingToFinalMapping._stagingXYOffset[1], 0u},
                            {(unsigned)dstBox._right - stagingToFinalMapping._stagingXYOffset[0], (unsigned)dstBox._bottom - stagingToFinalMapping._stagingXYOffset[1], 1u}});
                }
            }
        }

        auto finalContainingGuid = finalResource.GetContainingResource()->GetGUID();
        metalContext.GetActiveCommandList().MakeResourcesVisible({&finalContainingGuid, &finalContainingGuid+1});
    }

    unsigned ResourceUploadHelper::WriteToBufferViaMap(
        const ResourceLocator& resource, const ResourceDesc& desc, unsigned offset,
        IteratorRange<const void*> data)
    {
        auto* metalResource = resource.GetContainingResource().get();
        size_t finalOffset = offset;
        size_t finalSize = data.size();
        if (!resource.IsWholeResource()) {
            auto range = resource.GetRangeInContainingResource();
            assert((range.second - range.first) >= finalSize);
            finalOffset += range.first;
        }

        // note -- this is a direct, immediate map... There must be no contention while we map.
        assert(desc._type == ResourceDesc::Type::LinearBuffer);
        auto& metalContext = *Metal::DeviceContext::Get(*_renderCoreContext);
        Metal::ResourceMap map(metalContext, *metalResource, Metal::ResourceMap::Mode::WriteDiscardPrevious, finalOffset, finalSize);
        auto copyAmount = std::min(map.GetData().size(), data.size());
        if (copyAmount > 0)
            XlCopyMemory(map.GetData().begin(), data.begin(), copyAmount);
        return (unsigned)copyAmount;
    }

    void ResourceUploadHelper::ResourceCopy_DefragSteps(
        const UnderlyingResourcePtr& destination, const UnderlyingResourcePtr& source, 
        const std::vector<DefragStep>& steps)
    {
        assert(0);
    }

    void ResourceUploadHelper::ResourceCopy(UnderlyingResource& destination, UnderlyingResource& source)
    {
        assert(0);
    }

    RenderCore::IDevice::ResourceInitializer AsResourceInitializer(IDataPacket& pkt)
    {
        return [&pkt](SubResourceId sr) -> RenderCore::SubResourceInitData
            {
                RenderCore::SubResourceInitData result;
				result._data = pkt.GetData(sr);
                result._pitches = pkt.GetPitches(sr);
                return result;
            };
    }

    ResourceUploadHelper::ResourceUploadHelper(IThreadContext& renderCoreContext) : _renderCoreContext(&renderCoreContext) {}
    ResourceUploadHelper::~ResourceUploadHelper() {}

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

    static std::string BuildDescription(const ResourceDesc& desc)
    {
        using namespace BufferUploads;
        char buffer[2048];
        if (desc._type == ResourceDesc::Type::Texture) {
            const TextureDesc& tDesc = desc._textureDesc;
            xl_snprintf(buffer, dimof(buffer), "[%s] Tex(%4s) (%4ix%4i) mips:(%2i)", 
                desc._name, AsString(tDesc._dimensionality),
                tDesc._width, tDesc._height, tDesc._mipCount);
        } else if (desc._type == ResourceDesc::Type::LinearBuffer) {
            if (desc._bindFlags & BindFlag::VertexBuffer) {
                xl_snprintf(buffer, dimof(buffer), "[%s] VB (%6.1fkb)", 
                    desc._name, desc._linearBufferDesc._sizeInBytes/1024.f);
            } else if (desc._bindFlags & BindFlag::IndexBuffer) {
                xl_snprintf(buffer, dimof(buffer), "[%s] IB (%6.1fkb)", 
                    desc._name, desc._linearBufferDesc._sizeInBytes/1024.f);
            }
        } else {
            xl_snprintf(buffer, dimof(buffer), "Unknown");
        }
        return std::string(buffer);
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

    static ResourceDesc ApplyLODOffset(const ResourceDesc& desc, unsigned lodOffset)
    {
            //  Remove the top few LODs from the desc...
        ResourceDesc result = desc;
        if (result._type == ResourceDesc::Type::Texture) {
            result._textureDesc = RenderCore::CalculateMipMapDesc(desc._textureDesc, lodOffset);
        }
        return result;
    }
    
    static bool IsFull2DPlane(const ResourceDesc& resDesc, const RenderCore::Box2D& box)
    {
        assert(resDesc._type == ResourceDesc::Type::Texture);
        if (box == Box2D{}) return true;
        return 
            box._left == 0 && box._top == 0
            && box._right == resDesc._textureDesc._width
            && box._left == resDesc._textureDesc._height;
    }

    static bool IsAllLodLevels(const ResourceDesc& resDesc, unsigned lodLevelMin, unsigned lodLevelMax)
    {
        assert(resDesc._type == ResourceDesc::Type::Texture);
        assert(lodLevelMin != lodLevelMax);
        auto max = std::min(lodLevelMax, (unsigned)resDesc._textureDesc._mipCount-1);
        return (lodLevelMin == 0 && max == resDesc._textureDesc._mipCount-1);
    }

    static bool IsAllArrayLayers(const ResourceDesc& resDesc, unsigned arrayLayerMin, unsigned arrayLayerMax)
    {
        assert(resDesc._type == ResourceDesc::Type::Texture);
        assert(arrayLayerMin != arrayLayerMax);
        if (resDesc._textureDesc._arrayCount == 0) return true;

        auto max = std::min(arrayLayerMax, (unsigned)resDesc._textureDesc._arrayCount-1);
        return (arrayLayerMin == 0 && max == resDesc._textureDesc._arrayCount-1);
    }

    std::pair<ResourceDesc, PlatformInterface::StagingToFinalMapping> CalculatePartialStagingDesc(
        const ResourceDesc& dstDesc,
        const PartialResource& part)
    {
        assert(dstDesc._type == ResourceDesc::Type::Texture);
        ResourceDesc stagingDesc = AsStagingDesc(dstDesc);
        PlatformInterface::StagingToFinalMapping mapping;
        mapping._dstBox = part._box;
        if (IsFull2DPlane(dstDesc, mapping._dstBox)) {
            // When writing to the full 2d plane, we can selectively update only some lod levels
            if (!IsAllLodLevels(dstDesc, part._lodLevelMin, part._lodLevelMax)) {
                mapping._stagingLODOffset = part._lodLevelMin;
                mapping._dstLodLevelMin = part._lodLevelMin;
                mapping._dstLodLevelMax = std::min(part._lodLevelMax, (unsigned)dstDesc._textureDesc._mipCount-1);
                stagingDesc = ApplyLODOffset(stagingDesc, mapping._stagingLODOffset);
            }
        } else {
            // We need this restriction because otherwise (assuming the mip chain goes to 1x1) we
            // would have to recalculate all mips
            if (!IsAllLodLevels(dstDesc, part._lodLevelMin, part._lodLevelMax))
                Throw(std::runtime_error("When updating texture data for only part of the 2d plane, you must update all lod levels"));

            // Shrink the size of the staging texture to just the parts we want
            assert(mapping._dstBox._right > mapping._dstBox._left);
            assert(mapping._dstBox._bottom > mapping._dstBox._top);
            mapping._stagingXYOffset = { (unsigned)mapping._dstBox._left, (unsigned)mapping._dstBox._top };
            stagingDesc._textureDesc._width = mapping._dstBox._right - mapping._dstBox._left;
            stagingDesc._textureDesc._height = mapping._dstBox._bottom - mapping._dstBox._top;
        }

        if (!IsAllArrayLayers(dstDesc, part._arrayIndexMin, part._arrayIndexMax)) {
            assert(part._arrayIndexMax > part._arrayIndexMin);
            mapping._stagingArrayOffset = part._arrayIndexMin;
            mapping._dstArrayLayerMin = part._arrayIndexMin;
            mapping._dstArrayLayerMax = std::min(part._arrayIndexMax, (unsigned)dstDesc._textureDesc._arrayCount-1);
            stagingDesc._textureDesc._arrayCount = mapping._dstArrayLayerMax + 1 - mapping._dstArrayLayerMin;
            if (stagingDesc._textureDesc._arrayCount == 1)
                stagingDesc._textureDesc._arrayCount = 0;
        }

        return std::make_pair(stagingDesc, mapping);
    }

    #if defined(INTRUSIVE_D3D_PROFILING)
        class ResourceTracker : public IUnknown
        {
        public:
            ResourceTracker(ID3D::Resource* resource, const char name[]);
            virtual ~ResourceTracker();

            ID3D::Resource* GetResource() const { return _resource; }
            const std::string & GetName() const      { return _name; }
            const ResourceDesc& GetDesc() const   { return _desc; }

            virtual HRESULT STDMETHODCALLTYPE   QueryInterface(REFIID riid, __RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject);
            virtual ULONG STDMETHODCALLTYPE     AddRef();
            virtual ULONG STDMETHODCALLTYPE     Release();
        private:
            Interlocked::Value  _referenceCount;
            ID3D::Resource* _resource;
            std::string _name;
            Interlocked::Value _allocatedMemory[ResourceDesc::Type_Max];
            ResourceDesc _desc;
        };

        // {7D2F715A-5C04-450A-8C2C-8931136581F9}
        EXTERN_C const GUID DECLSPEC_SELECTANY GUID_ResourceTracker = { 0x7d2f715a, 0x5c04, 0x450a, { 0x8c, 0x2c, 0x89, 0x31, 0x13, 0x65, 0x81, 0xf9 } };

        std::vector<ResourceTracker*>   g_Resources;
        CryCriticalSection              g_Resources_Lock;
        Interlocked::Value              g_AllocatedMemory[ResourceDesc::Type_Max]          = { 0, 0, 0 };

        struct CompareResource
        {
            bool operator()( const ResourceTracker* lhs,    const ID3D::Resource* rhs  ) const  { return lhs->GetResource() < rhs; }
            bool operator()( const ID3D::Resource*  lhs,    const ResourceTracker* rhs ) const  { return lhs < rhs->GetResource(); }
            bool operator()( const ResourceTracker* lhs,    const ResourceTracker* rhs ) const  { return lhs < rhs; }
        };

        static unsigned CalculateVideoMemory(const ResourceDesc& desc)
        {
            if (desc._allocationRules != AllocationRules::Staging && desc._gpuAccess) {
                return ByteCount(desc);
            }
            return 0;
        }

        ResourceTracker::ResourceTracker(ID3D::Resource* resource, const char name[]) : _name(name), _resource(resource), _referenceCount(0)
        {
            XlZeroMemory(_allocatedMemory);
            _desc = ExtractDesc(resource);
            _allocatedMemory[_desc._type] = CalculateVideoMemory(_desc);
            Interlocked::Add(&g_AllocatedMemory[_desc._type], _allocatedMemory[_desc._type]);
        }

        ResourceTracker::~ResourceTracker()
        {
            for (unsigned c=0; c<ResourceDesc::Type_Max; ++c) {
                if (_allocatedMemory[c]) {
                    Interlocked::Add(&g_AllocatedMemory[c], -_allocatedMemory[c]);
                }
            }
            ScopedLock(g_Resources_Lock);
            std::vector<ResourceTracker*>::iterator i=std::lower_bound(g_Resources.begin(), g_Resources.end(), _resource, CompareResource());
            if (i!=g_Resources.end() && (*i) == this) {
                g_Resources.erase(i);
            }
        }

        HRESULT STDMETHODCALLTYPE ResourceTracker::QueryInterface(REFIID riid, __RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject)
        {
            ppvObject = NULL;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE ResourceTracker::AddRef()
        {
            return Interlocked::Increment(&_referenceCount) + 1;
        }

        ULONG STDMETHODCALLTYPE ResourceTracker::Release()
        {
            Interlocked::Value newRefCount = Interlocked::Decrement(&_referenceCount) - 1;
            if (!newRefCount) {
                delete this;
            }
            return newRefCount;
        }

        #if defined(DIRECT3D9)
            void    Resource_Register(IDirect3DResource9* resource, const char name[])
        #else
            void    Resource_Register(ID3D11Resource* resource, const char name[])
        #endif
        {
            ResourceTracker* tracker = new ResourceTracker(resource, name);
            {
                ScopedLock(g_Resources_Lock);
                std::vector<ResourceTracker*>::iterator i=std::lower_bound(g_Resources.begin(), g_Resources.end(), resource, CompareResource());
                if (i!=g_Resources.end() && (*i)->GetResource()==resource) {
                    delete tracker;
                    return;
                }
                g_Resources.insert(i, tracker);
            }
            AttachObject(resource, GUID_ResourceTracker, tracker);
            Resource_SetName(resource, name);
        }

        static void LogString(const char value[])
        {
                //      After a device reset we can't see the log in the console, and the log text file might 
                //      not be updated yet... We need to use the debugger logging connection
            #if defined(WIN32)
                OutputDebugString(value);
            #endif
        }
        
        void    Resource_Report()
        {
            LogString("D3D allocated resources report:\n");
            LogString(XlDynFormatString("Total for texture objects: %8.6fMB\n", g_AllocatedMemory         [ResourceDesc::Type::Texture     ] / (1024.f*1024.f)).c_str());
            LogString(XlDynFormatString("Total for buffer objects : %8.6fMB\n", g_AllocatedMemory         [ResourceDesc::Type::LinearBuffer] / (1024.f*1024.f)).c_str());

            ScopedLock(g_Resources_Lock);
            for (std::vector<ResourceTracker*>::iterator i=g_Resources.begin(); i!=g_Resources.end(); ++i) {
                std::string name = (*i)->GetName();
                intrusive_ptr<ID3D::Resource> resource = QueryInterfaceCast<ID3D::Resource>((*i)->GetResource());

                const ResourceDesc& desc = (*i)->GetDesc();
                char buffer[2048];
                strcpy(buffer, BuildDescription(desc).c_str());
                char nameBuffer[256];
                Resource_GetName(resource, nameBuffer, dimof(nameBuffer));
                if (nameBuffer[0]) {
                    strcat(buffer, "  Device name: ");
                    strcat(buffer, nameBuffer);
                }
                resource->AddRef();
                DWORD refCount = resource->Release();
                sprintf(&buffer[strlen(buffer)], "  Ref count: %i\n", refCount);
                LogString(buffer);
            }
        }

        static void CalculateExtraFields(BufferMetrics& metrics)
        {
            if (metrics._allocationRules != AllocationRules::Staging && metrics._gpuAccess) {
                metrics._videoMemorySize = ByteCount(metrics);
                metrics._systemMemorySize = 0;
            } else {
                metrics._videoMemorySize = 0;
                metrics._systemMemorySize = ByteCount(metrics);
            }

            if (metrics._type == ResourceDesc::Type::Texture) {
                ETEX_Format format = CTexture::TexFormatFromDeviceFormat((NativeFormat::Enum)metrics._textureDesc._nativePixelFormat);
                metrics._pixelFormatName = CTexture::NameForTextureFormat(format);
            } else {
                metrics._pixelFormatName = 0;
            }
        }

        static BufferMetrics ExtractMetrics(const ResourceDesc& desc, const std::string& name)
        {
            BufferMetrics result;
            static_cast<ResourceDesc&>(result) = desc;
            strncpy(result._name, name.c_str(), dimof(result._name)-1);
            result._name[dimof(result._name)-1] = '\0';
            CalculateExtraFields(result);
            return result;
        }
        
        static BufferMetrics ExtractMetrics(ID3D::Resource* resource)
        {
            BufferMetrics result;
            static_cast<ResourceDesc&>(result) = ExtractDesc(resource);
            Resource_GetName(resource, result._name, dimof(result._name));
            CalculateExtraFields(result);
            return result;
        }

        size_t  Resource_GetAll(BufferUploads::BufferMetrics** bufferDescs)
        {
                // DavidJ --    Any D3D9 call with "g_Resources_Lock" locked can cause a deadlock... but I'm hoping AddRef() is fine!
                //              because we AddRef when constructing the smart pointer, we should be ok....
            ScopedLock(g_Resources_Lock);
            size_t count = g_Resources.size();
            BufferUploads::BufferMetrics* result = new BufferUploads::BufferMetrics[count];
            size_t c=0;
            for (std::vector<ResourceTracker*>::const_iterator i=g_Resources.begin(); i!=g_Resources.end(); ++i, ++c) {
                result[c] = ExtractMetrics((*i)->GetDesc(), (*i)->GetName());
            }
            
            (*bufferDescs) = result;
            return count;
        }

        void    Resource_SetName(ID3D::Resource* resource, const char name[])
        {
            if (name && name[0]) {
                #if defined(DIRECT3D9)
                    resource->SetPrivateData(WKPDID_D3DDebugObjectName, name, strlen(name), 0);
                #else
                    resource->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(name)-1, name);
                #endif
            }
        }

        void    Resource_GetName(ID3D::Resource* resource, char nameBuffer[], int nameBufferSize)
        {
            DWORD finalSize = nameBufferSize;
            #if defined(DIRECT3D9)
                HRESULT hresult = resource->GetPrivateData(WKPDID_D3DDebugObjectName, nameBuffer, &finalSize);
            #else
                HRESULT hresult = resource->GetPrivateData(WKPDID_D3DDebugObjectName, (uint32_t*)&finalSize, nameBuffer);
            #endif
            if (SUCCEEDED(hresult) && finalSize) {
                nameBuffer[std::min(size_t(finalSize),size_t(nameBufferSize-1))] = '\0';
            } else {
                nameBuffer[0] = '\0';
            }
        }

        static size_t   g_lastVideoMemoryHeadroom = 0;
        static bool     g_pendingVideoMemoryHeadroomCalculation = false;

        size_t  Resource_GetVideoMemoryHeadroom()
        {
            return g_lastVideoMemoryHeadroom;
        }

        void        Resource_ScheduleVideoMemoryHeadroomCalculation()
        {
            g_pendingVideoMemoryHeadroomCalculation = true;
        }

        void    Resource_RecalculateVideoMemoryHeadroom()
        {
            if (g_pendingVideoMemoryHeadroomCalculation) {
                    //
                    //      Calculate how much video memory we can allocate by making many
                    //      allocations until they fail.
                    //
                ResourceDesc desc;
                desc._type = ResourceDesc::Type::Texture;
                desc._bindFlags = BindFlag::ShaderResource;
                desc._cpuAccess = 0;
                desc._gpuAccess = GPUAccess::Read;
                desc._allocationRules = 0;
                desc._textureDesc._width = 1024;
                desc._textureDesc._height = 1024;
                desc._textureDesc._dimensionality = TextureDesc::Dimensionality::T2D;
                desc._textureDesc._nativePixelFormat = CTexture::DeviceFormatFromTexFormat(eTF_A8R8G8B8);
                desc._textureDesc._mipCount = 1;
                desc._textureDesc._arrayCount = 1;
                desc._textureDesc._samples = TextureSamples::Create();

                std::vector<intrusive_ptr<ID3D::Resource> > resources;
                for (;;) {
                    intrusive_ptr<ID3D::Resource> t = CreateResource(desc, NULL);
                    if (!t) {
                        break;
                    }
                    resources.push_back(t);
                }
                g_lastVideoMemoryHeadroom = ByteCount(desc) * resources.size();
                g_pendingVideoMemoryHeadroomCalculation = false;
            }
        }

    #else

        void    Resource_Register(const UnderlyingResource& resource, const char name[])
        {
        }

        void    Resource_Report(bool)
        {
        }

        void    Resource_SetName(const UnderlyingResource& resource, const char name[])
        {
        }

        void    Resource_GetName(const UnderlyingResource& resource, char buffer[], int bufferSize)
        {
        }

        size_t      Resource_GetAll(BufferMetrics** bufferDescs)
        {
            *bufferDescs = NULL;
            return 0;
        }

        size_t  Resource_GetVideoMemoryHeadroom()
        {
            return 0;
        }

        void    Resource_RecalculateVideoMemoryHeadroom()
        {
        }

        void    Resource_ScheduleVideoMemoryHeadroomCalculation()
        {
        }

    #endif

}}

