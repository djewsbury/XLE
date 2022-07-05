// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceUploadHelper.h"
#include "Metrics.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Metal/Metal.h"
#include "../RenderCore/Metal/Resource.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../OSServices/Log.h"
#include "../Utility/StringFormat.h"
#include <assert.h>

#if GFXAPI_TARGET == GFXAPI_DX11
    #include "../RenderCore/DX11/Metal/IncludeDX11.h"
#endif

namespace BufferUploads { namespace PlatformInterface
{
	using namespace RenderCore;

    void ResourceUploadHelper::UpdateFinalResourceFromStaging(
        const ResourceLocator& finalResource,
        RenderCore::IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize)
    {
        auto destinationDesc = finalResource.GetContainingResource()->GetDesc();
        auto& metalContext = *Metal::DeviceContext::Get(*_renderCoreContext);

        if (destinationDesc._type == ResourceDesc::Type::Texture) {
            assert(finalResource.IsWholeResource());
            auto destinationSize = ByteCount(destinationDesc);
            assert(destinationSize <= stagingSize);
            auto size = std::min(stagingSize, destinationSize);

            // During the transfer, the images must be in either TransferSrcOptimal, TransferDstOptimal or General.
            // assuming we don't have to CaptureForBind stagingResource, because it should be from a StagingPool, which
            // will always be ready for a transfer
            Metal::Internal::CaptureForBind cap{metalContext, *finalResource.GetContainingResource(), BindFlag::TransferDst};
            auto blitEncoder = metalContext.BeginBlitEncoder();
            blitEncoder.Copy(
                CopyPartial_Dest{*finalResource.GetContainingResource().get()},
                CopyPartial_Src{stagingResource, stagingOffset, stagingOffset+size});
        } else {
            assert(destinationDesc._type == ResourceDesc::Type::LinearBuffer);
            assert(stagingSize <= destinationDesc._linearBufferDesc._sizeInBytes);
            unsigned dstOffset = 0;
            
            if (!finalResource.IsWholeResource()) {
                auto range = finalResource.GetRangeInContainingResource();
                dstOffset = range.first;
                assert(stagingSize <= range.second-range.first);
            }

            Metal::Internal::CaptureForBind cap{metalContext, *finalResource.GetContainingResource(), BindFlag::TransferDst};
            auto blitEncoder = metalContext.BeginBlitEncoder();
            blitEncoder.Copy(
                CopyPartial_Dest{*finalResource.GetContainingResource().get(), dstOffset},
                CopyPartial_Src{stagingResource, stagingOffset, stagingOffset+stagingSize});
        }

        auto finalContainingGuid = finalResource.GetContainingResource()->GetGUID();
        metalContext.GetActiveCommandList().MakeResourcesVisible({&finalContainingGuid, &finalContainingGuid+1});
    }

    void ResourceUploadHelper::UpdateFinalResourceFromStaging(
        const ResourceLocator& finalResource,
        const RenderCore::Box2D& box, SubResourceId subRes,
        RenderCore::IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize)
    {
        // copy a partial subresource (but only a single subresource)
        assert(0);
    }

    unsigned ResourceUploadHelper::WriteViaMap(const ResourceLocator& resource, IteratorRange<const void*> data)
    {
        auto* metalResource = resource.GetContainingResource().get();
        size_t finalOffset = 0;
        size_t finalSize = data.size();
        if (!resource.IsWholeResource()) {
            auto range = resource.GetRangeInContainingResource();
            assert((range.second - range.first) >= finalSize);
            finalOffset += range.first;
        }

       return WriteViaMap(*metalResource, finalOffset, finalSize, data);
    }

    unsigned ResourceUploadHelper::WriteViaMap(
        IResource& resource, unsigned resourceOffset, unsigned resourceSize,
        IteratorRange<const void*> data)
    {
        assert(resource.GetDesc()._type == RenderCore::ResourceDesc::Type::LinearBuffer);
        Metal::ResourceMap map{*_renderCoreContext->GetDevice(), resource, Metal::ResourceMap::Mode::WriteDiscardPrevious, resourceOffset, resourceSize};
        auto copyAmount = std::min(map.GetData().size(), data.size());
        if (copyAmount > 0) {
            // attempt to use faster aligned copy, if available
            if (((size_t(map.GetData().begin())&0xf) | (size_t(data.begin())&0xf)) == 0) {
                XlCopyMemoryAlign16(map.GetData().begin(), data.begin(), copyAmount);
            } else
                XlCopyMemory(map.GetData().begin(), data.begin(), copyAmount);
        }
        map.FlushCache();
        return (unsigned)copyAmount;
    }

    unsigned ResourceUploadHelper::WriteViaMap(
        IResource& resource, unsigned resourceOffset, unsigned resourceSize,
        const RenderCore::TextureDesc& descForLayout,
        const RenderCore::IDevice::ResourceInitializer& multiSubresourceInitializer)
    {
        return Metal::Internal::CopyViaMemoryMap(
            *_renderCoreContext->GetDevice(), 
            resource, resourceOffset, resourceSize,
            descForLayout, multiSubresourceInitializer);
    }

    unsigned ResourceUploadHelper::WriteViaMap(
        IResource& resource,
        const RenderCore::IDevice::ResourceInitializer& multiSubresourceInitializer)
    {
        unsigned copyAmount = 0;
        Metal::ResourceMap map{*_renderCoreContext->GetDevice(), resource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
        auto desc = resource.GetDesc();
        if (desc._type == ResourceDesc::Type::Texture) {
            auto arrayLayerCount = ActualArrayLayerCount(desc._textureDesc);
            auto mipCount = desc._textureDesc._mipCount;
            for (unsigned a=0; a<arrayLayerCount; ++a)
                for (unsigned m=0; m<mipCount; ++m) {
                    auto src = multiSubresourceInitializer({m, a});
                    auto dst = map.GetData({m, a});
                    std::memcpy(dst.begin(), src._data.begin(), std::min(dst.size(), src._data.size()));
                    copyAmount += (unsigned)std::min(dst.size(), src._data.size());
                }
        } else {
            auto src = multiSubresourceInitializer({});
            auto dst = map.GetData();
            std::memcpy(dst.begin(), src._data.begin(), std::min(dst.size(), src._data.size()));
            copyAmount += (unsigned)std::min(dst.size(), src._data.size());

        }
        map.FlushCache();
        return copyAmount;
    }

#if 0
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
#endif

    bool ResourceUploadHelper::CanDirectlyMap(RenderCore::IResource& resource)
    {
        return Metal::ResourceMap::CanMap(*_renderCoreContext->GetDevice(), resource, Metal::ResourceMap::Mode::WriteDiscardPrevious);
    }

    unsigned ResourceUploadHelper::CalculateStagingBufferOffsetAlignment(const RenderCore::ResourceDesc& desc)
    {
        using namespace RenderCore;
		auto& objectFactory = Metal::GetObjectFactory();
		unsigned alignment = 1u;
		#if GFXAPI_TARGET == GFXAPI_VULKAN
			alignment = std::max(alignment, (unsigned)objectFactory.GetPhysicalDeviceProperties().limits.optimalBufferCopyOffsetAlignment);
		#endif
		if (desc._type == ResourceDesc::Type::Texture) {
			auto compressionParam = GetCompressionParameters(desc._textureDesc._format);
			if (compressionParam._blockWidth != 1) {
				alignment = std::max(alignment, compressionParam._blockBytes);
			} else {
				// non-blocked format -- alignment requirement is a multiple of the texel size
				alignment = std::max(alignment, BitsPerPixel(desc._textureDesc._format)/8u);
			}
		}
		return alignment;
    }

    void ResourceUploadHelper::DeviceBasedCopy(
        RenderCore::IResource& destination,
        RenderCore::IResource& source,
        IteratorRange<const Utility::RepositionStep*> steps)
    {
        // this interface only works with linear buffers (because RepositionStep is specialized for 1D)
        assert(destination.GetDesc()._type == RenderCore::ResourceDesc::Type::LinearBuffer);
        assert(source.GetDesc()._type == RenderCore::ResourceDesc::Type::LinearBuffer);

        auto& metalContext = *Metal::DeviceContext::Get(*_renderCoreContext);
        Metal::Internal::CaptureForBind cap0{metalContext, destination, BindFlag::TransferDst};
        Metal::Internal::CaptureForBind cap1{metalContext, source, BindFlag::TransferSrc};
        auto blitEncoder = metalContext.BeginBlitEncoder();
        // Vulkan allows for all of these copies to happen in a single cmd -- unfortunately our API doesn't support that, however
        for (auto& s:steps) {
            assert(s._sourceEnd > s._sourceStart);
            assert((s._destination + s._sourceEnd - s._sourceStart) <= destination.GetDesc()._linearBufferDesc._sizeInBytes);
            blitEncoder.Copy(
                CopyPartial_Dest{destination, s._destination},
                CopyPartial_Src{source, s._sourceStart, s._sourceEnd});
        }
    }

    void ResourceUploadHelper::DeviceBasedCopy(IResource& destination, IResource& source)
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
        result._bindFlags = BindFlag::TransferSrc;
        result._allocationRules = AllocationRules::HostVisibleSequentialWrite;
        XlCopyString(result._name, "[stage]");
        XlCatString(result._name, desc._name);
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

    auto StagingPage::Allocate(unsigned byteCount, unsigned alignment) -> Allocation
    {
        #if defined(_DEBUG)
            assert(_boundThread == std::this_thread::get_id());
        #endif
        assert(byteCount <= _stagingBufferHeap.HeapSize());
        auto stagingAllocation = _stagingBufferHeap.AllocateBack(byteCount, alignment);
        if (stagingAllocation == ~0u) {
            UpdateConsumerMarker();
            stagingAllocation = _stagingBufferHeap.AllocateBack(byteCount, alignment);
            if (stagingAllocation == ~0u) return {};
        }

        auto allocationId = _nextAllocationId++;
        _activeAllocations.push_back({allocationId, stagingAllocation+byteCount, true});
        return {*this, stagingAllocation, byteCount, allocationId};
    }

    void StagingPage::UpdateConsumerMarker()
    {
        #if defined(_DEBUG)
            assert(_boundThread == std::this_thread::get_id());
        #endif
        assert(_asyncTracker);

        // The normal deallocation scheme checks all cmd lists that were alive at the time of the deallocation. We only
        // care about a single cmd list, though, because we know that the staging page is only used with specific cmd
        // lists.
        const bool checkOnlyOurCmdList = true;
        if (checkOnlyOurCmdList) {
            while (!_allocationsWaitingOnDevice.empty()) {
                auto status = _asyncTracker->GetSpecificMarkerStatus(_allocationsWaitingOnDevice.front()._releaseMarker);
                if (status != Metal_Vulkan::IAsyncTracker::MarkerStatus::ConsumerCompleted)
                    break;

                assert(_allocationsWaitingOnDevice.front()._pendingNewFront != ~0u);
                _stagingBufferHeap.ResetFront(_allocationsWaitingOnDevice.front()._pendingNewFront);
                _allocationsWaitingOnDevice.erase(_allocationsWaitingOnDevice.begin());
            }
        } else {
            QueueMarker queueMarker = _asyncTracker->GetConsumerMarker();
            while (!_allocationsWaitingOnDevice.empty() && _allocationsWaitingOnDevice.front()._releaseMarker <= queueMarker) {
                assert(_allocationsWaitingOnDevice.front()._pendingNewFront != ~0u);
                _stagingBufferHeap.ResetFront(_allocationsWaitingOnDevice.front()._pendingNewFront);
                _allocationsWaitingOnDevice.erase(_allocationsWaitingOnDevice.begin());
            }
        }
    }

    void StagingPage::Release(unsigned allocationId, QueueMarker releaseMarker)
    {
        #if defined(_DEBUG)
            assert(_boundThread == std::this_thread::get_id());
        #endif
        assert(releaseMarker != 0);

        bool found = false;
        for (auto& a:_activeAllocations)
            if (a._allocationId == allocationId) {
                assert(a._unreleased);
                a._unreleased = false;
                a._releaseMarker = releaseMarker;
                found = true;
                break;
            }
        if (!found) {
            assert(0);
            return;
        }

        bool abandonCase = releaseMarker == 0;
        auto i = _activeAllocations.begin();
        while (i != _activeAllocations.end() && i->_unreleased == false) {
            assert(abandonCase || i->_releaseMarker <= releaseMarker); // a previously released allocation can't have a later releaseMarker
            releaseMarker = std::max(releaseMarker, i->_releaseMarker);
            ++i;
        }
        if (i != _activeAllocations.begin()) {
            // remove allocations from _activeAllocations and place into _allocationsWaitingOnDevice
            auto newFront = (i-1)->_pendingNewFront;
            _activeAllocations.erase(_activeAllocations.begin(), i);
            // We append to _allocationsWaitingOnDevice, even for abandoned allocations. This is because
            // we want to release abandoned allocations in order with non-abandoned allocations
            if (!_allocationsWaitingOnDevice.empty() && _allocationsWaitingOnDevice.back()._releaseMarker == releaseMarker) {
                _allocationsWaitingOnDevice.back()._pendingNewFront = newFront;
            } else {
                _allocationsWaitingOnDevice.push_back({releaseMarker, newFront});
                if (_allocationsWaitingOnDevice.size() > 16)        // try to avoid this getting too long, since we update it lazily
                    UpdateConsumerMarker();
            }
        }
    }

    void StagingPage::Abandon(unsigned allocationId) { Release(allocationId, 0); }

    auto StagingPage::GetQuickMetrics() const -> StagingPageMetrics
    {
        #if defined(_DEBUG)
            assert(_boundThread == std::this_thread::get_id());
        #endif

        auto heapMetrics = _stagingBufferHeap.GetQuickMetrics();
        StagingPageMetrics result;
        result._bytesAllocated = heapMetrics._bytesAllocated;
        result._maxNextBlockBytes = heapMetrics._maxNextBlockBytes;
        result._bytesAwaitingDevice = 0;
        if (!_allocationsWaitingOnDevice.empty()) {
            auto newFront = _allocationsWaitingOnDevice.back()._pendingNewFront;
            if (newFront > heapMetrics._front) {
                result._bytesAwaitingDevice = newFront - heapMetrics._front;
            } else {
                result._bytesAwaitingDevice = _stagingBufferHeap.HeapSize() - heapMetrics._front + newFront;
            }
        }
        result._bytesLockedDueToOrdering = 0;
        for (auto a=_activeAllocations.begin(); a!=_activeAllocations.end(); ++a) {
            if (a == _activeAllocations.begin()) {
                assert(a->_unreleased);
                continue;
            }

            if (!a->_unreleased) {  // ie -- if this is released, but still considered an "active allocation", not yet waiting on device
                auto prevFront = (a-1)->_pendingNewFront;
                auto newFront = a->_pendingNewFront;
                if (newFront > prevFront) {
                    result._bytesLockedDueToOrdering = newFront - prevFront;
                } else {
                    result._bytesLockedDueToOrdering = _stagingBufferHeap.HeapSize() - prevFront + newFront;
                }
            }
        }
        return result;
    }

    void StagingPage::BindThread()
    {
        #if defined(_DEBUG)
            _boundThread = std::this_thread::get_id();
        #endif
    }

    StagingPage::StagingPage(RenderCore::IDevice& device, unsigned size)
    {
		_stagingBufferHeap = CircularHeap(size);
		_stagingBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferSrc, AllocationRules::HostVisibleSequentialWrite | AllocationRules::PermanentlyMapped | AllocationRules::DisableAutoCacheCoherency | AllocationRules::DedicatedPage,
				LinearBufferDesc::Create(size),
				"staging-page"));

        auto* deviceVulkan = (RenderCore::IDeviceVulkan*)device.QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
        if (deviceVulkan)
            _asyncTracker = deviceVulkan->GetAsyncTracker();

        #if defined(_DEBUG)
            _boundThread = std::this_thread::get_id();
        #endif
    }

    StagingPage::~StagingPage()
    {
        // Ideally everything should be released before we get here
        // However, having some "_allocationsWaitingOnDevice" is ok, because it probably just means we haven't updated
        // the consumer marker
        assert(_activeAllocations.empty());
    }

    void StagingPage::Allocation::Release(QueueMarker queueMarker)
    {
        assert(queueMarker != 0);
        if (_page)
            _page->Release(_allocationId, queueMarker);
        _page = nullptr;
        _allocationId = ~0u;
        _resourceOffset = _allocationSize = 0;
    }

    StagingPage::Allocation::Allocation(StagingPage& page, unsigned resourceOffset, unsigned allocationSize, unsigned allocationId)
    : _page(&page), _resourceOffset(resourceOffset), _allocationSize(allocationSize), _allocationId(allocationId) {}

    StagingPage::Allocation::~Allocation()
    {
        if (_page) {
            assert(_allocationId != ~0u);
            _page->Abandon(_allocationId);
        }
    }
    StagingPage::Allocation::Allocation(Allocation&& moveFrom)
    {
        _resourceOffset = moveFrom._resourceOffset;
        _allocationSize = moveFrom._allocationSize;
        _allocationId = moveFrom._allocationId;
        _page = moveFrom._page;
        moveFrom._resourceOffset = moveFrom._allocationSize = 0;
        moveFrom._allocationId = ~0u;
        moveFrom._page = nullptr;
    }

    StagingPage::Allocation& StagingPage::Allocation::operator=(Allocation&& moveFrom)
    {
        if (_page) {
            assert(_allocationId != ~0u);
            _page->Abandon(_allocationId);
        }

        _resourceOffset = moveFrom._resourceOffset;
        _allocationSize = moveFrom._allocationSize;
        _allocationId = moveFrom._allocationId;
        _page = moveFrom._page;
        moveFrom._resourceOffset = moveFrom._allocationSize = 0;
        moveFrom._allocationId = ~0u;
        moveFrom._page = nullptr;
        return *this;
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

        void    Resource_Register(const IResource& resource, const char name[])
        {
        }

        void    Resource_Report(bool)
        {
        }

        void    Resource_SetName(const IResource& resource, const char name[])
        {
        }

        void    Resource_GetName(const IResource& resource, char buffer[], int bufferSize)
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

