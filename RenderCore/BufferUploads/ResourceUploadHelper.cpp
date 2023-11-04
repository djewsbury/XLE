// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceUploadHelper.h"
#include "Metrics.h"
#include "../Format.h"
#include "../Metal/Metal.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../IDevice.h"
#include "../IAnnotator.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../../OSServices/Log.h"
#include "../../OSServices/TimeUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/HeapUtils.h"
#include "../../Utility/Threading/LockFree.h"
#include <assert.h>

#if !defined(NDEBUG)
    #define RECORD_BU_THREAD_CONTEXT_METRICS
#endif

namespace RenderCore { namespace BufferUploads { namespace PlatformInterface
{
	using namespace RenderCore;

    void ResourceUploadHelper::EnsureOpenCmdListWriter()
    {
        if (!_cmdListWriter) {
            _cmdListWriter = Metal::DeviceContext::Get(*_threadContext).get();
            #if defined(_DEBUG)
                if (auto* vulkanContext = query_interface_cast<IThreadContextVulkan*>(_threadContext))
                    vulkanContext->AttachNameToCommandList("BufferUploads");
            #endif
        }
    }

    void ResourceUploadHelper::UpdateFinalResourceFromStaging(
        const ResourceLocator& finalResource,
        IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize)
    {
        auto destinationDesc = finalResource.GetContainingResource()->GetDesc();
        assert((stagingOffset % CalculateStagingBufferOffsetAlignment(destinationDesc)) == 0);      // copy src must be aligned by the pixel size
        EnsureOpenCmdListWriter();
        Metal::BarrierHelper{*_cmdListWriter}.Add(*finalResource.GetContainingResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::TransferDst);

        if (destinationDesc._type == ResourceDesc::Type::Texture) {
            assert(finalResource.IsWholeResource());
            auto destinationSize = ByteCount(destinationDesc);
            assert(destinationSize <= stagingSize);
            auto size = std::min(stagingSize, destinationSize);

            // During the transfer, the images must be in either TransferSrcOptimal, TransferDstOptimal or General.
            // assuming we don't have to CaptureForBind stagingResource, because it should be from a StagingPool, which
            // will always be ready for a transfer
            auto blitEncoder = _cmdListWriter->BeginBlitEncoder();
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

            auto blitEncoder = _cmdListWriter->BeginBlitEncoder();
            blitEncoder.Copy(
                CopyPartial_Dest{*finalResource.GetContainingResource().get(), dstOffset},
                CopyPartial_Src{stagingResource, stagingOffset, stagingOffset+stagingSize});
        }
    }

    void ResourceUploadHelper::UpdateFinalResourceFromStaging(
        const ResourceLocator& finalResource,
        const Box2D& box, SubResourceId subRes,
        IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize)
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
        assert(resource.GetDesc()._type == ResourceDesc::Type::LinearBuffer);
        Metal::ResourceMap map{*_device, resource, Metal::ResourceMap::Mode::WriteDiscardPrevious, resourceOffset, resourceSize};
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
        const TextureDesc& descForLayout,
        const IDevice::ResourceInitializer& multiSubresourceInitializer)
    {
        assert(_device);
        return Metal::Internal::CopyViaMemoryMap(
            *_device, 
            resource, resourceOffset, resourceSize,
            descForLayout, multiSubresourceInitializer);
    }

    unsigned ResourceUploadHelper::WriteViaMap(
        IResource& resource,
        const IDevice::ResourceInitializer& multiSubresourceInitializer)
    {
        assert(_device);
        unsigned copyAmount = 0;
        Metal::ResourceMap map{*_device, resource, Metal::ResourceMap::Mode::WriteDiscardPrevious};
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

    void ResourceUploadHelper::UpdateFinalResourceViaCmdListAttachedStaging(
        const ResourceLocator& finalResource,
        IDataPacket& initialisationData)
    {
        auto desc = finalResource.GetContainingResource()->GetDesc();
        auto byteCount = ByteCount(desc);
        if (!finalResource.IsWholeResource()) {
            assert(desc._type == ResourceDesc::Type::LinearBuffer);
            byteCount = finalResource.GetRangeInContainingResource().second - finalResource.GetRangeInContainingResource().first;
            desc._linearBufferDesc._sizeInBytes = byteCount;
        }
        auto alignment = CalculateStagingBufferOffsetAlignment(desc);       // alignment varies based on pixel format
        
        EnsureOpenCmdListWriter();
        auto stagingSpace = _cmdListWriter->MapTemporaryStorage(byteCount, BindFlag::TransferSrc, alignment);
        auto uploadList = CalculateUploadList(stagingSpace, desc);
        for (const auto& upload:uploadList) {
            SubResourceInitData srcSubResource = {};
            srcSubResource._data = initialisationData.GetData(upload._id);
            assert(!srcSubResource._data.empty());
            srcSubResource._pitches = initialisationData.GetPitches(upload._id);

            if (desc._type == ResourceDesc::Type::Texture) {
                // probably just a straight memcpy, anyway
                CopyMipLevel(upload._destination.begin(), upload._destination.size(), upload._pitches, CalculateMipMapDesc(desc._textureDesc, upload._id._mip), srcSubResource);
            } else {
                assert(upload._destination.size() == srcSubResource._data.size());
                std::memcpy(upload._destination.begin(), srcSubResource._data.begin(), upload._destination.size());
            }
        }

        auto beginAndEndInResource = stagingSpace.GetBeginAndEndInResource();
        UpdateFinalResourceFromStaging(
            finalResource, 
            *stagingSpace.GetResource(), beginAndEndInResource.first, beginAndEndInResource.second-beginAndEndInResource.first);
    }

    void ResourceUploadHelper::TransferQueueRelease(IteratorRange<const QueueTransfer*> transfers)
    {
        #if defined(_DEBUG)
            for (unsigned c=0; c<transfers.size(); ++c)
                for (unsigned c2=c+1; c2<transfers.size(); ++c2)
                    assert(transfers[c]._resource != transfers[c2]._resource);
        #endif
        EnsureOpenCmdListWriter();
        Metal::BarrierHelper barrierHelper{*_cmdListWriter};
        for (const auto& c:transfers) {
            assert(c._srcLayout.has_value());
            barrierHelper.Add(
                *c._resource->GetContainingResource(),
                Metal::BarrierResourceUsage{*c._srcLayout, Metal::BarrierResourceUsage::Queue::DedicatedTransfer},
                Metal::BarrierResourceUsage{c._dstLayout, Metal::BarrierResourceUsage::Queue::Graphics});
        }
    }

    void ResourceUploadHelper::GraphicsQueueAcquire(IteratorRange<const QueueTransfer*> transfers)
    {
        #if defined(_DEBUG)
            for (unsigned c=0; c<transfers.size(); ++c)
                for (unsigned c2=c+1; c2<transfers.size(); ++c2)
                    assert(transfers[c]._resource != transfers[c2]._resource);
        #endif
        EnsureOpenCmdListWriter();
        Metal::BarrierHelper barrierHelper{*_cmdListWriter};
        for (const auto& c:transfers) {
            if (c._srcLayout.has_value()) {
                barrierHelper.Add(
                    *c._resource->GetContainingResource(),
                    Metal::BarrierResourceUsage{*c._srcLayout, Metal::BarrierResourceUsage::Queue::DedicatedTransfer},
                    Metal::BarrierResourceUsage{c._dstLayout, Metal::BarrierResourceUsage::Queue::Graphics});
            } else {
                // this case should be used when there are no actual context operations on the transfer queue - we just switch into the
                // requested layout
                barrierHelper.Add(
                    *c._resource->GetContainingResource(),
                    Metal::BarrierResourceUsage::Preinitialized(),
                    c._dstLayout);
            }
        }
    }

    void ResourceUploadHelper::PipelineBarrier(IteratorRange<const QueueTransfer*> pipelineBarriers)
    {
        EnsureOpenCmdListWriter();
        Metal::BarrierHelper barrierHelper{*_cmdListWriter};
        for (const auto& c:pipelineBarriers) {
            if (c._srcLayout.has_value()) {
                barrierHelper.Add(*c._resource->GetContainingResource(), *c._srcLayout, c._dstLayout);
            } else {
                barrierHelper.Add(*c._resource->GetContainingResource(), Metal::BarrierResourceUsage::Preinitialized(), c._dstLayout);
            }
        }
    }

    void ResourceUploadHelper::MakeResourcesVisibleForGraphics(IteratorRange<const uint64_t*> resources)
    {
        assert(_cmdListWriter);
        _cmdListWriter->GetActiveCommandList().MakeResourcesVisible(resources);
    }

    std::vector<IAsyncDataSource::SubResource> ResourceUploadHelper::CalculateUploadList(
        Metal::ResourceMap& map,
        const ResourceDesc& desc)
    {
        std::vector<IAsyncDataSource::SubResource> uploadList;
        if (desc._type == ResourceDesc::Type::Texture) {

            // arrange the upload locations as per required for a staging texture
            auto arrayCount = ActualArrayLayerCount(desc._textureDesc);
            auto mipCount = desc._textureDesc._mipCount;
            assert(mipCount >= 1);
            assert(arrayCount >= 1);

            uploadList.resize(mipCount*arrayCount);
            for (unsigned a=0; a<arrayCount; ++a) {
                for (unsigned mip=0; mip<mipCount; ++mip) {
                    SubResourceId subRes { mip, a };
                    auto& upload = uploadList[subRes._arrayLayer*mipCount+subRes._mip];
                    upload._id = subRes;
                    auto offset = GetSubResourceOffset(desc._textureDesc, mip, a);
                    upload._destination = { PtrAdd(map.GetData().begin(), offset._offset), PtrAdd(map.GetData().begin(), offset._offset+offset._size) };
                    upload._pitches = offset._pitches;
                }
            }

        } else {
            uploadList.resize(1);
            auto& upload = uploadList[0];
            upload._id = {};
            upload._destination = map.GetData(upload._id);
            upload._pitches = map.GetPitches(upload._id);
        }
        return uploadList;
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

    bool ResourceUploadHelper::CanDirectlyMap(IResource& resource)
    {
        assert(_device);
        return Metal::ResourceMap::CanMap(*_device, resource, Metal::ResourceMap::Mode::WriteDiscardPrevious);
    }

    unsigned ResourceUploadHelper::CalculateStagingBufferOffsetAlignment(const ResourceDesc& desc)
    {
        using namespace RenderCore;
		unsigned alignment = _copyBufferOffsetAlignment;
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
        IResource& destination,
        IResource& source,
        IteratorRange<const Utility::RepositionStep*> steps)
    {
        // this interface only works with linear buffers (because RepositionStep is specialized for 1D)
        assert(destination.GetDesc()._type == ResourceDesc::Type::LinearBuffer);
        assert(source.GetDesc()._type == ResourceDesc::Type::LinearBuffer);

        EnsureOpenCmdListWriter();
        Metal::BarrierHelper{*_cmdListWriter}
            .Add(source, Metal::BarrierResourceUsage::AllCommandsWrite(), BindFlag::TransferSrc)
            .Add(destination, Metal::BarrierResourceUsage::NoState(), BindFlag::TransferDst);

        auto blitEncoder = _cmdListWriter->BeginBlitEncoder();
        // Vulkan allows for all of these copies to happen in a single cmd -- unfortunately our API doesn't support that, however
        for (auto& s:steps) {
            assert(s._sourceEnd > s._sourceStart);
            assert((s._destination + s._sourceEnd - s._sourceStart) <= destination.GetDesc()._linearBufferDesc._sizeInBytes);
            assert((s._sourceStart % _copyBufferOffsetAlignment) == 0);
            assert((s._destination % _copyBufferOffsetAlignment) == 0);
            blitEncoder.Copy(
                CopyPartial_Dest{destination, s._destination},
                CopyPartial_Src{source, s._sourceStart, s._sourceEnd});
        }

        Metal::BarrierHelper{*_cmdListWriter}
            .Add(destination, BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsReadAndWrite());
    }

    void ResourceUploadHelper::DeviceBasedCopy(IResource& destination, IResource& source)
    {
        assert(0);
    }

    IDevice::ResourceInitializer AsResourceInitializer(IDataPacket& pkt)
    {
        return [&pkt](SubResourceId sr) -> SubResourceInitData
            {
                SubResourceInitData result;
				result._data = pkt.GetData(sr);
                result._pitches = pkt.GetPitches(sr);
                return result;
            };
    }

    ResourceUploadHelper::ResourceUploadHelper(IThreadContext& renderCoreContext)
	{
        _device = renderCoreContext.GetDevice().get();
        _threadContext = &renderCoreContext;
		_copyBufferOffsetAlignment = _device->GetDeviceLimits()._copyBufferOffsetAlignment;
        _cmdListWriter = nullptr;
	}
    ResourceUploadHelper::ResourceUploadHelper(IDevice& device, Metal::DeviceContext& metalContext)
	{
        _device = &device;
        _threadContext = nullptr;
		_copyBufferOffsetAlignment = _device->GetDeviceLimits()._copyBufferOffsetAlignment;
        _cmdListWriter = &metalContext;
	}
    ResourceUploadHelper::ResourceUploadHelper()
    {
        _cmdListWriter = nullptr; _threadContext = nullptr; _device = nullptr;
        _copyBufferOffsetAlignment = 1;
    }
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
        char buffer[2048];
        if (desc._type == ResourceDesc::Type::Texture) {
            const TextureDesc& tDesc = desc._textureDesc;
            xl_snprintf(
                buffer, dimof(buffer), "Tex(%4s) (%4ix%4i) mips:(%2i)", 
                AsString(tDesc._dimensionality),
                tDesc._width, tDesc._height, tDesc._mipCount);
        } else if (desc._type == ResourceDesc::Type::LinearBuffer) {
            if (desc._bindFlags & BindFlag::VertexBuffer) {
                xl_snprintf(buffer, dimof(buffer), "VB (%6.1fkb)", 
                    desc._linearBufferDesc._sizeInBytes/1024.f);
            } else if (desc._bindFlags & BindFlag::IndexBuffer) {
                xl_snprintf(buffer, dimof(buffer), "IB (%6.1fkb)", 
                    desc._linearBufferDesc._sizeInBytes/1024.f);
            } else {
                xl_snprintf(buffer, dimof(buffer), "Buffer (%6.1fkb)", 
                    desc._linearBufferDesc._sizeInBytes/1024.f);
            }
        } else {
            xl_snprintf(buffer, dimof(buffer), "Unknown");
        }
        return std::string(buffer);
    }

    static ResourceDesc ApplyLODOffset(const ResourceDesc& desc, unsigned lodOffset)
    {
            //  Remove the top few LODs from the desc...
        ResourceDesc result = desc;
        if (result._type == ResourceDesc::Type::Texture) {
            result._textureDesc = CalculateMipMapDesc(desc._textureDesc, lodOffset);
        }
        return result;
    }
    
    static bool IsFull2DPlane(const ResourceDesc& resDesc, const Box2D& box)
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
                if (status != Metal_Vulkan::IAsyncTracker::MarkerStatus::ConsumerCompleted && status != Metal_Vulkan::IAsyncTracker::MarkerStatus::Abandoned)
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

    void StagingPage::Release(unsigned allocationId, bool abandon)
    {
        #if defined(_DEBUG)
            assert(_boundThread == std::this_thread::get_id());
        #endif

		QueueMarker releaseMarker = abandon ? 0 : _asyncTracker->GetProducerMarker();

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

    void StagingPage::Abandon(unsigned allocationId) { Release(allocationId, true); }

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

    StagingPage::StagingPage(IThreadContext& threadContext, unsigned size)
    {
		_stagingBufferHeap = CircularHeap(size);
		_stagingBuffer = threadContext.GetDevice()->CreateResource(
			CreateDesc(
				BindFlag::TransferSrc, AllocationRules::HostVisibleSequentialWrite | AllocationRules::PermanentlyMapped | AllocationRules::DisableAutoCacheCoherency | AllocationRules::DedicatedPage,
				LinearBufferDesc::Create(size)),
           "staging-page");

        constexpr auto threadContextVulkanInterface = TypeHashCode<IThreadContextVulkan>;
        if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&threadContext))
            _asyncTracker = threadContextVulkan->GetQueueTracker();

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

    void StagingPage::Allocation::Release()
    {
        if (_page)
            _page->Release(_allocationId);
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

        //////////////////////////////////////////////////////////////////////////////////////////////

    struct UploadsThreadContext::Pimpl
    {
        CommandListMetrics _commandListUnderConstruction;
        DeferredOperations _deferredOperationsUnderConstruction;
        struct QueuedCommandList
        {
            mutable CommandListMetrics _metrics;
            DeferredOperations _deferredOperations;
            CommandListID _id;
            std::shared_ptr<Metal::CommandList> _graphicsQueueAdditionalCmdList;
            unsigned _advanceGraphicsQueueCountAtQueueTime = 0;

            QueuedCommandList() = default;
            QueuedCommandList(QueuedCommandList&&) = default;
            QueuedCommandList& operator=(QueuedCommandList&&) = default;
        };
        std::queue<QueuedCommandList> _queuedForAdvanceGraphicsQueue;   // protected by _queuedForAdvanceGraphicsQueueLock
        unsigned _advanceGraphicsQueueCount = 0;                        // protected by _queuedForAdvanceGraphicsQueueLock
        Threading::Mutex _queuedForAdvanceGraphicsQueueLock;
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            LockFreeFixedSizeQueue<CommandListMetrics, 256> _recentRetirements;
        #endif

        CommandListID _commandListIDReadyForGraphicsQueue;

        Metal_Vulkan::VulkanSharedPtr<VkSemaphore> _transferQueueTimeline;
        Metal_Vulkan::VulkanSharedPtr<VkSemaphore> _graphicsQueueTimeline;

        std::unique_ptr<StagingPage> _stagingPage;

        unsigned _frameId = 0;
        bool _backgroundContext = false;
        bool _isDedicatedTransferContext = false;

        std::shared_ptr<Metal::DeviceContext> _fallbackGraphicsQueueCmdList;

        void RetireToGraphicsQueue(IThreadContext& commitTo, QueuedCommandList&& commandList);
        void RetireToGraphicsQueue_IrregularThreadContext(IThreadContext& commitTo, QueuedCommandList&& commandList);
    };

    void UploadsThreadContext::QueueToHardware(std::optional<CommandListID> completeCmdList)
    {
        // Queue what we've got, and start the GPU on processing them
        //
        // Unfortunately the underlying graphics API cmd list tracker markers are not the same as the
        // buffer uploads CommandListID; this makes everything much more complicated
        // 
        // But this is necessary because sometimes we queue only part of a buffer uploads cmd list
        // So when we want to create a dependency between a client cmd list and a buffer uploads cmd list,
        // we need to ensure that the dependency exists to all underlying hardware cmd lists that might be
        // relevant.
        //
        // The easiest way to do this is to ensure that all cmd lists on our queue have dependencies between
        // them, so that they will be completed in order. This way we only have to track the hardware cmd list.
        //
        // However, this is only optimal if there's no particular benefit in running transfers in parallel for
        // the given hardware.

        int64_t currentTime = OSServices::GetPerformanceCounter();
        Pimpl::QueuedCommandList newCommandList;
        newCommandList._metrics = std::move(_pimpl->_commandListUnderConstruction);
        newCommandList._metrics._resolveTime = currentTime;
        newCommandList._metrics._processingEnd = currentTime;
        newCommandList._deferredOperations.swap(_pimpl->_deferredOperationsUnderConstruction);
        newCommandList._id = completeCmdList.value_or(~0u);

        #if GFXAPI_TARGET == GFXAPI_VULKAN
            if (completeCmdList) {
                // note -- we can't just open an empty command list here. On nvidia, the signal does not appear to be triggered if
                // it's attached to an empty command list
                assert(_helper._cmdListWriter);
                auto& metalContext = *_helper._cmdListWriter;
                assert(metalContext.HasActiveCommandList());        // we need a command list, even if it does nothing but advance the semaphore value
                metalContext.GetActiveCommandList().AddSignalOnCompletion(_pimpl->_transferQueueTimeline, *completeCmdList);
            }
        #else
            #error GFXAPI specific implementation required in UploadsThreadContext::QueueToHardware()
        #endif

        if (_pimpl->_backgroundContext) {
            TRY {
                _mainContext->CommitCommands();
                if (_pimpl->_fallbackGraphicsQueueCmdList) {
                    newCommandList._graphicsQueueAdditionalCmdList = _pimpl->_fallbackGraphicsQueueCmdList->ResolveCommandList();
                    _pimpl->_fallbackGraphicsQueueCmdList.reset();
                }
            } CATCH(const std::exception& e) {
                if (!newCommandList._metrics._exceptionMsg.empty()) newCommandList._metrics._exceptionMsg += ", ";
                newCommandList._metrics._exceptionMsg += e.what();
                // If we get a failure during CommitCommands(), cancel all deferred operations
                newCommandList._deferredOperations = {};
            } CATCH_END

            ScopedLock(_pimpl->_queuedForAdvanceGraphicsQueueLock);
            newCommandList._advanceGraphicsQueueCountAtQueueTime = _pimpl->_advanceGraphicsQueueCount;
            _pimpl->_queuedForAdvanceGraphicsQueue.push(std::move(newCommandList));
        } else {
            _pimpl->RetireToGraphicsQueue(*_mainContext, std::move(newCommandList));
        }

        _helper = ResourceUploadHelper { *_mainContext };        // resetResourceUploadHelper() to clear _cmdListWriter ptr, now that we've queued that command list
        _fallbackGraphicsHelper = {};

        _pimpl->_commandListUnderConstruction = CommandListMetrics();
        _pimpl->_commandListUnderConstruction._processingStart = currentTime;
        DeferredOperations().swap(_pimpl->_deferredOperationsUnderConstruction);
    }

    bool UploadsThreadContext::AdvanceGraphicsQueue(IThreadContext& commitTo, CommandListID cmdListRequired, MarkCommandListDependencyFlags::BitField flags)
    {
        if (!_pimpl->_backgroundContext) {
            assert(&commitTo == _mainContext.get());
            return _pimpl->_commandListIDReadyForGraphicsQueue >= cmdListRequired;
        }

        bool annotatorStart = false;
        auto advanceGraphicsQueueCount = ~0u;

        // maxIdlyQueuedCount is the max number of calls to AdvanceGraphicsQueue() a queued cmdlist can be ignored because cmdListRequired doesn't require it
        // we don't want this to be too high, because we don't want cmd lists to end up sitting on the queue unprocessed -- that would clog up the async trackers
        const unsigned maxIdlyQueuedCount = 4;

        bool wroteSomeStub = false;
        for (;;) {
            Pimpl::QueuedCommandList cmdListsToProcess[16];
            unsigned cmdListToProcessCount = 0;
            bool needAnotherBatch = false;

            {
                // move out the command lists we're going to tackle -- reduce the time we're in the "_queuedForAdvanceGraphicsQueueLock" lock
                ScopedLock(_pimpl->_queuedForAdvanceGraphicsQueueLock);
                if (advanceGraphicsQueueCount == ~0u)
                    advanceGraphicsQueueCount = _pimpl->_advanceGraphicsQueueCount++;
                unsigned cmdlistIterator = _pimpl->_commandListIDReadyForGraphicsQueue;
                while (!_pimpl->_queuedForAdvanceGraphicsQueue.empty()) {

                    if (cmdlistIterator < cmdListRequired && (advanceGraphicsQueueCount - _pimpl->_queuedForAdvanceGraphicsQueue.front()._advanceGraphicsQueueCountAtQueueTime) < maxIdlyQueuedCount)
                        break;      // don't need to process following cmdlists immediately, they can sit queued for the moment

                    if (cmdListToProcessCount >= dimof(cmdListsToProcess)) {
                        needAnotherBatch = true;
                        break;
                    }

                    if (_pimpl->_queuedForAdvanceGraphicsQueue.front()._id != ~0u) {
                        assert(_pimpl->_queuedForAdvanceGraphicsQueue.front()._id > cmdlistIterator);
                        cmdlistIterator = _pimpl->_queuedForAdvanceGraphicsQueue.front()._id;
                    }
                    cmdListsToProcess[cmdListToProcessCount++] = std::move(_pimpl->_queuedForAdvanceGraphicsQueue.front());
                    _pimpl->_queuedForAdvanceGraphicsQueue.pop();
                }
            }

            for (auto& commandList:MakeIteratorRange(cmdListsToProcess, &cmdListsToProcess[cmdListToProcessCount])) {
                if (!annotatorStart) {
                    commitTo.GetAnnotator().Event("BufferUploads", IAnnotator::EventTypes::MarkerBegin);
                    annotatorStart = true;
                }

                // Unfortunately, we have to wait on the graphics queue timeline, not the transfer queue timeline here
                // (because of _graphicsQueueAdditionalCmdList & _deferredOperations)
                // this means that a cmd list from one graphics thread context may become delayed for another graphics
                // thread context, if both threads are calling IBufferUploads::StallAndMarkCommandListDependency
                //
                // To avoid this problem, when given the flag "IrregularThreadContext", we will commit to the graphics
                // queue immediately, even if it means committing a tiny command list.
                if (!(flags & MarkCommandListDependencyFlags::IrregularThreadContext)) {
                    _pimpl->RetireToGraphicsQueue(commitTo, std::move(commandList));
                    wroteSomeStub = true;
                } else {
                    _pimpl->RetireToGraphicsQueue_IrregularThreadContext(commitTo, std::move(commandList));
                }
            }

            if (!needAnotherBatch) break;
        }

        if (annotatorStart)
            commitTo.GetAnnotator().Event("BufferUploads", IAnnotator::EventTypes::MarkerEnd);

        if (!wroteSomeStub && (_pimpl->_commandListIDReadyForGraphicsQueue >= cmdListRequired)) {
            auto& metalContext = *Metal::DeviceContext::Get(commitTo);
            assert(metalContext.HasActiveCommandList());
            metalContext.GetActiveCommandList().AddWaitBeforeBegin(_pimpl->_graphicsQueueTimeline, cmdListRequired);
        }

        return _pimpl->_commandListIDReadyForGraphicsQueue >= cmdListRequired;
    }

    void UploadsThreadContext::Pimpl::RetireToGraphicsQueue(IThreadContext& commitTo, QueuedCommandList&& commandList)
    {
        // See also RetireToGraphicsQueue_IrregularThreadContext
        TRY {
            if (!commandList._deferredOperations.IsEmpty()) {
                auto metalContext = Metal::DeviceContext::BeginPrimaryCommandList(commitTo);
                ResourceUploadHelper helper{*commitTo.GetDevice(), *metalContext};
                commandList._deferredOperations.CommitToImmediate_PreCommandList(helper);
                commandList._deferredOperations.CommitToImmediate_ResourceTransfers(helper);
                commandList._deferredOperations.CommitToImmediate_PostCommandList(helper);

                auto metalCmdList = metalContext->ResolveCommandList();
                metalContext.reset();

                if (commandList._id != ~0u) {
                    metalCmdList->AddWaitBeforeBegin(_transferQueueTimeline, commandList._id);
                    metalCmdList->AddSignalOnCompletion(_graphicsQueueTimeline, commandList._id);
                }

                if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&commitTo)) {
                    if (commandList._graphicsQueueAdditionalCmdList)
                        threadContextVulkan->AddPreFrameCommandList(std::move(*commandList._graphicsQueueAdditionalCmdList));
                        
                    // the command list generated will become a "pre-frame" command list; meaning it will go into
                    // the queue earlier than the main frame rendering command list
                    threadContextVulkan->AddPreFrameCommandList(std::move(*metalCmdList));
                } else {
                    UNREACHABLE();      // missing gfx api specific implementation
                }
            } else if (commandList._id != ~0u) {
                auto& metalContext = *Metal::DeviceContext::Get(commitTo);
                assert(metalContext.HasActiveCommandList());
                metalContext.GetActiveCommandList().AddWaitBeforeBegin(_transferQueueTimeline, commandList._id);
                metalContext.GetActiveCommandList().AddSignalOnCompletion(_graphicsQueueTimeline, commandList._id);

                if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&commitTo)) {
                    if (commandList._graphicsQueueAdditionalCmdList)
                        threadContextVulkan->AddPreFrameCommandList(std::move(*commandList._graphicsQueueAdditionalCmdList));
                } else {
                    UNREACHABLE();      // missing gfx api specific implementation
                }
            } else if (commandList._graphicsQueueAdditionalCmdList) {
                if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&commitTo)) {
                    threadContextVulkan->AddPreFrameCommandList(std::move(*commandList._graphicsQueueAdditionalCmdList));
                } else {
                    UNREACHABLE();      // missing gfx api specific implementation
                }
            }
        } CATCH (const std::exception& e) {
            // we have to catch any exception to ensure (at the very least) that we don't attempt to resubmit this same cmd list again
            if (!commandList._metrics._exceptionMsg.empty()) commandList._metrics._exceptionMsg += ", ";
            commandList._metrics._exceptionMsg += e.what();
        } CATCH_END

        if (commandList._id != ~0u)
            _commandListIDReadyForGraphicsQueue = std::max(_commandListIDReadyForGraphicsQueue, commandList._id);
    
        commandList._metrics._frameId                  = _frameId;
        commandList._metrics._commitTime               = OSServices::GetPerformanceCounter();
        
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            while (!_recentRetirements.push(std::move(commandList._metrics)))
                _recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
        #endif
    }

    void UploadsThreadContext::Pimpl::RetireToGraphicsQueue_IrregularThreadContext(IThreadContext& commitTo, QueuedCommandList&& commandList)
    {
        TRY {

            // Unfortunately this cmd list will often end up begin trivial. But if the thread context is considered "irregular",
            // we can't necessarily afford to piggyback on other command lists

            auto metalContext = Metal::DeviceContext::BeginPrimaryCommandList(commitTo);
            auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(&commitTo);
            assert(threadContextVulkan);        // missing gfx api specific implementation

            if (commandList._graphicsQueueAdditionalCmdList)
                threadContextVulkan->QueuePrimaryCommandList(std::move(*commandList._graphicsQueueAdditionalCmdList));

            if (!commandList._deferredOperations.IsEmpty()) {                
                ResourceUploadHelper helper{*commitTo.GetDevice(), *metalContext};
                commandList._deferredOperations.CommitToImmediate_PreCommandList(helper);
                commandList._deferredOperations.CommitToImmediate_ResourceTransfers(helper);
                commandList._deferredOperations.CommitToImmediate_PostCommandList(helper);
            }

            auto metalCmdList = metalContext->ResolveCommandList();
            metalContext.reset();

            if (commandList._id != ~0u) {
                metalCmdList->AddWaitBeforeBegin(_transferQueueTimeline, commandList._id);
                metalCmdList->AddSignalOnCompletion(_graphicsQueueTimeline, commandList._id);
            }

            threadContextVulkan->QueuePrimaryCommandList(std::move(*metalCmdList));

        } CATCH (const std::exception& e) {
            // we have to catch any exception to ensure (at the very least) that we don't attempt to resubmit this same cmd list again
            if (!commandList._metrics._exceptionMsg.empty()) commandList._metrics._exceptionMsg += ", ";
            commandList._metrics._exceptionMsg += e.what();
        } CATCH_END

        if (commandList._id != ~0u)
            _commandListIDReadyForGraphicsQueue = std::max(_commandListIDReadyForGraphicsQueue, commandList._id);
    
        commandList._metrics._frameId                  = _frameId;
        commandList._metrics._commitTime               = OSServices::GetPerformanceCounter();
        
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            while (!_recentRetirements.push(std::move(commandList._metrics)))
                _recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
        #endif
    }

    CommandListMetrics UploadsThreadContext::PopMetrics()
    {
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            CommandListMetrics* ptr;
            if (_pimpl->_recentRetirements.try_front(ptr)) {
                CommandListMetrics result = std::move(*ptr);
                _pimpl->_recentRetirements.pop();
                return result;
            }
        #endif
        return CommandListMetrics();
    }

    CommandListID           UploadsThreadContext::CommandList_GetReadyForGraphicsQueue() const { return _pimpl->_commandListIDReadyForGraphicsQueue; }
    CommandListMetrics&     UploadsThreadContext::GetMetricsUnderConstruction() { return _pimpl->_commandListUnderConstruction; }
    auto                    UploadsThreadContext::GetDeferredOperationsUnderConstruction() -> DeferredOperations& { return _pimpl->_deferredOperationsUnderConstruction; }

    std::optional<CommandListID>    UploadsThreadContext::CommandList_LatestPendingProcessing() const
    {
        // What's the furthest AdvanceGraphicsQueue can go without causing a stall on background CPU processing?
        ScopedLock(_pimpl->_queuedForAdvanceGraphicsQueueLock);
        if (!_pimpl->_queuedForAdvanceGraphicsQueue.empty())
            return _pimpl->_queuedForAdvanceGraphicsQueue.back()._id;
        return {};
    }

    unsigned                UploadsThreadContext::FrameId() const { return _pimpl->_frameId; }
    void                    UploadsThreadContext::AdvanceFrameId() { ++_pimpl->_frameId; }
    bool                    UploadsThreadContext::IsDedicatedTransferContext() const { return _pimpl->_isDedicatedTransferContext; }

    ResourceUploadHelper&    UploadsThreadContext::GetFallbackGraphicsQueueResourceUploadHelper()
    {
        if (!IsDedicatedTransferContext() || _fallbackGraphicsQueueContext == _mainContext) return GetResourceUploadHelper();

        if (!_pimpl->_fallbackGraphicsQueueCmdList) {
            _pimpl->_fallbackGraphicsQueueCmdList = Metal::DeviceContext::BeginPrimaryCommandList(*_fallbackGraphicsQueueContext);
            _fallbackGraphicsHelper = ResourceUploadHelper{*_fallbackGraphicsQueueContext->GetDevice(), *_pimpl->_fallbackGraphicsQueueCmdList};
        }

        return _fallbackGraphicsHelper;
    }

    StagingPage&     UploadsThreadContext::GetStagingPage()
    {
        assert(_pimpl->_stagingPage);
        return *_pimpl->_stagingPage;
    }

    void UploadsThreadContext::UpdateGPUTracking()
    {
        if (auto* vulkanThreadContext = query_interface_cast<IThreadContextVulkan*>(_mainContext.get()))
            return vulkanThreadContext->UpdateGPUTracking();
    }

    bool UploadsThreadContext::HasOpenMainContextCommandList() const
    {
        return (_helper._cmdListWriter != nullptr) && _helper._cmdListWriter->HasActiveCommandList();
    }
    
    bool UploadsThreadContext::HasOpenCommandList() const
    {
        return HasOpenMainContextCommandList() || _pimpl->_fallbackGraphicsQueueCmdList;
    }

    UploadsThreadContext::UploadsThreadContext(
        std::shared_ptr<IThreadContext> graphicsQueueContext,
        std::shared_ptr<IThreadContext> transferQueueContext,
        bool reserveStagingSpace, bool backgroundContext)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_frameId = 0;
        _pimpl->_commandListIDReadyForGraphicsQueue = 0;
        _pimpl->_backgroundContext = backgroundContext;
        _pimpl->_isDedicatedTransferContext = false;

        if (transferQueueContext) {
            if (auto* vulkanThreadContext = query_interface_cast<IThreadContextVulkan*>(transferQueueContext.get()))
                assert(vulkanThreadContext->IsDedicatedTransferContext());

            _mainContext = std::move(transferQueueContext);
            _fallbackGraphicsQueueContext = std::move(graphicsQueueContext);
            _pimpl->_isDedicatedTransferContext = true;
        } else {
            _mainContext = _fallbackGraphicsQueueContext = std::move(graphicsQueueContext);
        }

        _helper = ResourceUploadHelper { *_mainContext };

        if (reserveStagingSpace) {
            const unsigned stagingPageSize = 64*1024*1024;
            _pimpl->_stagingPage = std::make_unique<StagingPage>(*_mainContext, stagingPageSize);

            auto& objectFactory = RenderCore::Metal::GetObjectFactory(*_mainContext->GetDevice());
            _pimpl->_transferQueueTimeline = objectFactory.CreateTimelineSemaphore();
            _pimpl->_graphicsQueueTimeline = objectFactory.CreateTimelineSemaphore();
        }
    }

    UploadsThreadContext::~UploadsThreadContext() {}

        //////////////////////////////////////////////////////////////////////////////////////////////

    UploadsThreadContext::DeferredOperations::DeferredDefragCopy::DeferredDefragCopy(
		std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<RepositionStep>& steps)
    : _destination(std::move(destination)), _source(std::move(source)), _steps(steps)
    {}

    UploadsThreadContext::DeferredOperations::DeferredDefragCopy::~DeferredDefragCopy()
    {}

    void UploadsThreadContext::DeferredOperations::Add(DeferredOperations::DeferredCopy&& copy)
    {
        _deferredCopies.push_back(std::forward<DeferredOperations::DeferredCopy>(copy));
    }

    void UploadsThreadContext::DeferredOperations::Add(DeferredOperations::DeferredDefragCopy&& copy)
    {
        _deferredDefragCopies.push_back(std::forward<DeferredOperations::DeferredDefragCopy>(copy));
    }

    void UploadsThreadContext::DeferredOperations::AddDelayedDelete(ResourceLocator&& locator)
    {
        _delayedDeletes.push_back(std::move(locator));
    }

    void UploadsThreadContext::DeferredOperations::Add(ResourceTransfer&& transfer)
    {
        _transfers.emplace_back(std::move(transfer));
    }

    void UploadsThreadContext::DeferredOperations::CommitToImmediate_PreCommandList(ResourceUploadHelper& helper)
    {
        // D3D11 has some issues with mapping and writing to linear buffers from a background thread
        // we get around this by deferring some write operations to the main thread, at the point
        // where we commit the command list to the device
        if (!_deferredCopies.empty()) {
            for (const auto&copy:_deferredCopies)
                helper.WriteViaMap(copy._destination, MakeIteratorRange(copy._temporaryBuffer));
            _deferredCopies.clear();
        }
    }

    void UploadsThreadContext::DeferredOperations::CommitToImmediate_PostCommandList(ResourceUploadHelper& helper)
    {
        if (_deferredDefragCopies.empty() && _transfers.empty())
            return;

        for (auto i=_deferredDefragCopies.begin(); i!=_deferredDefragCopies.end(); ++i)
            helper.DeviceBasedCopy(*i->_destination, *i->_source, i->_steps);
        _deferredDefragCopies.clear();
    }

    void UploadsThreadContext::DeferredOperations::CommitToImmediate_ResourceTransfers(ResourceUploadHelper& helper)
    {
        if (_transfers.empty()) return;

        // queue ownership transfer
        std::vector<ResourceUploadHelper::QueueTransfer> transfers;
        VLA(uint64_t, makeVisibleResources, _transfers.size());
        unsigned makeVisibleCount = 0;
        transfers.reserve(_transfers.size());
        for (auto& i:_transfers) {
            transfers.push_back(ResourceUploadHelper::QueueTransfer{&i._resource, i._transferQueueLayout, i._graphicsQueueLayout});
            makeVisibleResources[makeVisibleCount++] = i._resource.GetContainingResource()->GetGUID();
        }
        helper.GraphicsQueueAcquire(transfers);
        helper.MakeResourcesVisibleForGraphics(MakeIteratorRange(makeVisibleResources, &makeVisibleResources[makeVisibleCount]));
        _transfers.clear();
    }

    bool UploadsThreadContext::DeferredOperations::IsEmpty() const 
    {
        return _deferredCopies.empty() && _deferredDefragCopies.empty() && _delayedDeletes.empty() && _transfers.empty();
    }

    void UploadsThreadContext::DeferredOperations::swap(DeferredOperations& other)
    {
        _deferredCopies.swap(other._deferredCopies);
        _deferredDefragCopies.swap(other._deferredDefragCopies);
        _delayedDeletes.swap(other._delayedDeletes);
        _transfers.swap(other._transfers);
    }

    UploadsThreadContext::DeferredOperations::DeferredOperations()
    {
    }

    UploadsThreadContext::DeferredOperations::~DeferredOperations()
    {
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

}}}

