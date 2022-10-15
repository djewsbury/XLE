// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include "../IDevice.h"
#include "../Metal/Forward.h"        // for GFXAPI_TARGET
#include "../../Utility/IntrusivePtr.h"
#include "../../Utility/HeapUtils.h"
#include <utility>

namespace Utility { struct RepositionStep; }
namespace Utility { template<typename Type, int Count> class LockFreeFixedSizeQueue; }
namespace RenderCore { namespace Metal_Vulkan { class IAsyncTracker; } }

namespace RenderCore { namespace BufferUploads { struct StagingPageMetrics; } }
namespace RenderCore { namespace BufferUploads { namespace PlatformInterface
{
    class ResourceUploadHelper
    {
    public:
            ////////   P U S H   T O   R E S O U R C E   ////////
        unsigned WriteViaMap(
            IResource& resource, unsigned resourceOffset, unsigned resourceSize,
            IteratorRange<const void*> data);

        unsigned WriteViaMap(
            const ResourceLocator& locator,
            IteratorRange<const void*> data);

        // Write to a buffer using the correct arrangement of subresources required for copying from here to a texture
        // This is used in the staging texture case (ie, there will be a subsequent staging linear buffer to device local texture copy)
        // resourceOffset & resourceSize describe the part of resource that will be written to
        unsigned WriteViaMap(
            IResource& resource, unsigned resourceOffset, unsigned resourceSize,
            const TextureDesc& descForLayout,
            const IDevice::ResourceInitializer& multiSubresourceInitializer);

        // Write directly to a resource that may have subresources with the given initializer
        // This can be used with either linear buffers or textures, but must write to the entire
        // destination resource
        unsigned WriteViaMap(
            IResource& resource,
            const IDevice::ResourceInitializer& multiSubresourceInitializer);

        void UpdateFinalResourceFromStaging(
            const ResourceLocator& finalResource,
            IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize);

        void UpdateFinalResourceFromStaging(
            const ResourceLocator& finalResource,
            const Box2D& box, SubResourceId subRes,
            IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize);

        void UpdateFinalResourceViaCmdListAttachedStaging(
            IThreadContext& context,
            const ResourceLocator& finalResource,
            IDataPacket& initialisationData);

        bool CanDirectlyMap(IResource& resource);

        std::vector<IAsyncDataSource::SubResource> CalculateUploadList(
            Metal::ResourceMap& map,
            const ResourceDesc& desc);

        unsigned CalculateStagingBufferOffsetAlignment(const ResourceDesc& desc);

            ////////   R E S O U R C E   C O P Y   ////////
        void DeviceBasedCopy(
            IResource& destination,
            IResource& source,
            IteratorRange<const Utility::RepositionStep*> steps);
        void DeviceBasedCopy(IResource& destination, IResource& source);

            ////////   C O N S T R U C T I O N   ////////
        ResourceUploadHelper(IThreadContext& renderCoreContext);
        ~ResourceUploadHelper();

        IThreadContext& GetUnderlying() { return *_renderCoreContext; }

        #if GFXAPI_TARGET == GFXAPI_DX11
            private: 
                bool _useUpdateSubresourceWorkaround;
        #endif

    private:
        IThreadContext*         _renderCoreContext;
    };

    using QueueMarker = unsigned;

    class StagingPage
    {
    public:
        struct Allocation
        {
            void Release(QueueMarker queueMarker);
            unsigned GetResourceOffset() const { return _resourceOffset; }
            unsigned GetAllocationSize() const { return _allocationSize; }
            operator bool() const { return Valid(); }
            bool Valid() const { return _allocationSize != 0; }

            Allocation() = default;
            ~Allocation();
            Allocation(Allocation&&);
            Allocation& operator=(Allocation&&);
        private:
            unsigned _resourceOffset = 0;
            unsigned _allocationSize = 0;
            unsigned _allocationId = ~0u;
            StagingPage* _page = nullptr;
            Allocation(StagingPage& page, unsigned resourceOffset, unsigned allocationSize, unsigned allocationId);
            friend class StagingPage;
        };

        Allocation Allocate(unsigned byteCount, unsigned alignment);
        IResource& GetStagingResource() { return *_stagingBuffer; }

        StagingPageMetrics GetQuickMetrics() const;
        void BindThread();
        void UpdateConsumerMarker();
        size_t MaxSize() const { return _stagingBufferHeap.HeapSize(); }

        StagingPage(IDevice& device, unsigned size);
        ~StagingPage();
        StagingPage(StagingPage&&) = default;
        StagingPage& operator=(StagingPage&&) = default;

    private:
        CircularHeap _stagingBufferHeap;
		std::shared_ptr<IResource> _stagingBuffer;
        std::shared_ptr<Metal_Vulkan::IAsyncTracker> _asyncTracker;

        struct ActiveAllocation
        {
            unsigned _allocationId, _pendingNewFront;
            bool _unreleased;
            QueueMarker _releaseMarker;
        };
        std::vector<ActiveAllocation> _activeAllocations;
        unsigned _nextAllocationId = 1;

        struct AllocationWaitingOnDevice
        {
            QueueMarker _releaseMarker;
            unsigned _pendingNewFront = ~0u;
        };
        std::vector<AllocationWaitingOnDevice> _allocationsWaitingOnDevice;

        void Release(unsigned allocationId, QueueMarker releaseMarker);
        void Abandon(unsigned allocationId);

        #if defined(_DEBUG)
            std::thread::id _boundThread;
        #endif
    };

    IDevice::ResourceInitializer AsResourceInitializer(IDataPacket& pkt);

        //////   T H R E A D   C O N T E X T   //////

    class UploadsThreadContext
    {
    public:
        void                    ResolveCommandList(CommandListID id);
        void                    CommitToImmediate(IThreadContext& commitTo, unsigned frameId);

        CommandListMetrics      PopMetrics();

        CommandListID           CommandList_GetCommittedToImmediate() const;

        CommandListMetrics&     GetMetricsUnderConstruction();

        class DeferredOperations;
        DeferredOperations&     GetDeferredOperationsUnderConstruction();

        unsigned                CommitCount_Current();

        PlatformInterface::StagingPage&     GetStagingPage();
        PlatformInterface::QueueMarker      GetProducerCmdListSpecificMarker();

        PlatformInterface::ResourceUploadHelper&    GetResourceUploadHelper() { return _resourceUploadHelper; }
        IThreadContext&                 GetRenderCoreThreadContext() { return *_underlyingContext; }
        IDevice&                        GetRenderCoreDevice() { return *_underlyingContext->GetDevice(); }

        UploadsThreadContext(std::shared_ptr<IThreadContext> underlyingContext);
        ~UploadsThreadContext();
    private:
        std::shared_ptr<IThreadContext> _underlyingContext;
        PlatformInterface::ResourceUploadHelper _resourceUploadHelper;

        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class UploadsThreadContext::DeferredOperations
    {
    public:
        struct DeferredCopy
        {
            ResourceLocator _destination;
            ResourceDesc _resourceDesc;
            std::vector<uint8_t> _temporaryBuffer;
        };

        struct DeferredDefragCopy
        {
            std::shared_ptr<IResource> _destination;
            std::shared_ptr<IResource> _source;
            std::vector<RepositionStep> _steps;
            DeferredDefragCopy(std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<RepositionStep>& steps);
            ~DeferredDefragCopy();
        };

        void Add(DeferredCopy&& copy);
        void Add(DeferredDefragCopy&& copy);
        void AddDelayedDelete(ResourceLocator&& locator);
        void CommitToImmediate_PreCommandList(IThreadContext& immediateContext);
        void CommitToImmediate_PostCommandList(IThreadContext& immediateContext);
        bool IsEmpty() const;

        void swap(DeferredOperations& other);

        DeferredOperations();
        DeferredOperations(DeferredOperations&& moveFrom) = default;
        DeferredOperations& operator=(DeferredOperations&& moveFrom) = default;
        ~DeferredOperations();
    private:
        std::vector<DeferredCopy>       _deferredCopies;
        std::vector<DeferredDefragCopy> _deferredDefragCopies;
        std::vector<ResourceLocator>    _delayedDeletes;
    };

        /////////////////////////////////////////////////////////////////////

    struct BufferMetrics : public ResourceDesc
    {
    public:
        unsigned        _systemMemorySize;
        unsigned        _videoMemorySize;
        const char*     _pixelFormatName;
    };

    void            Resource_Register(IResource& resource, const char name[]);
    void            Resource_Report(bool justVolatiles);
    void            Resource_SetName(IResource& resource, const char name[]);
    void            Resource_GetName(IResource& resource, char buffer[], int bufferSize);
    size_t          Resource_GetAll(BufferMetrics** bufferDescs);

    size_t          Resource_GetVideoMemoryHeadroom();
    void            Resource_RecalculateVideoMemoryHeadroom();
    void            Resource_ScheduleVideoMemoryHeadroomCalculation();

        ///////////////////////////////////////////////////////////////////

            ////////   F U N C T I O N A L I T Y   F L A G S   ////////

#if 1

        //          Use these to customise behaviour for platforms
        //          without lots of #if defined(...) type code
    #if GFXAPI_TARGET == GFXAPI_DX11
		static const bool SupportsResourceInitialisation_Texture = true;
		static const bool SupportsResourceInitialisation_Buffer = true;
        static const bool RequiresStagingTextureUpload = false;
        static const bool RequiresStagingResourceReadBack = true;
        static const bool CanDoNooverwriteMapInBackground = false;
        static const bool UseMapBasedDefrag = false;
        static const bool ContextBasedMultithreading = true;
        static const bool CanDoPartialMaps = false;
    #elif GFXAPI_TARGET == GFXAPI_DX9
		static const bool SupportsResourceInitialisation_Texture = false;
		static const bool SupportsResourceInitialisation_Buffer = false;
        static const bool RequiresStagingTextureUpload = true;
        static const bool RequiresStagingResourceReadBack = false;
        static const bool CanDoNooverwriteMapInBackground = true;
        static const bool UseMapBasedDefrag = true;
        static const bool ContextBasedMultithreading = false;
        static const bool CanDoPartialMaps = true;
    #elif GFXAPI_TARGET == GFXAPI_OPENGLES
        static const bool SupportsResourceInitialisation_Texture = true;
		static const bool SupportsResourceInitialisation_Buffer = true;
        static const bool RequiresStagingTextureUpload = false;
        static const bool RequiresStagingResourceReadBack = true;
        static const bool CanDoNooverwriteMapInBackground = false;
        static const bool UseMapBasedDefrag = false;
        static const bool ContextBasedMultithreading = true;
        static const bool CanDoPartialMaps = false;
	#elif GFXAPI_TARGET == GFXAPI_VULKAN
		// Vulkan capabilities haven't been tested!
		static const bool SupportsResourceInitialisation_Texture = false;
		static const bool SupportsResourceInitialisation_Buffer = false;
		static const bool RequiresStagingTextureUpload = true;
		static const bool RequiresStagingResourceReadBack = true;
		static const bool CanDoNooverwriteMapInBackground = true;
		static const bool UseMapBasedDefrag = false;
		static const bool ContextBasedMultithreading = true;
		static const bool CanDoPartialMaps = true;
	#else
        #error Unsupported platform!
    #endif
#endif

        /////////////////////////////////////////////////////////////////////

}}}
