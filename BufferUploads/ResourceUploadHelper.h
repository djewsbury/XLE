// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include "../RenderCore/IDevice.h"
#include "../Utility/IntrusivePtr.h"
#include "../Utility/HeapUtils.h"
#include "../RenderCore/Metal/Forward.h"        // for GFXAPI_TARGET
#include <utility>

namespace Utility { class DefragStep; }

namespace BufferUploads { namespace PlatformInterface
{
        /////////////////////////////////////////////////////////////////////

    struct BufferMetrics : public RenderCore::ResourceDesc
    {
    public:
        unsigned        _systemMemorySize;
        unsigned        _videoMemorySize;
        const char*     _pixelFormatName;
    };

    void            Resource_Register(RenderCore::IResource& resource, const char name[]);
    void            Resource_Report(bool justVolatiles);
    void            Resource_SetName(RenderCore::IResource& resource, const char name[]);
    void            Resource_GetName(RenderCore::IResource& resource, char buffer[], int bufferSize);
    size_t          Resource_GetAll(BufferMetrics** bufferDescs);

    size_t          Resource_GetVideoMemoryHeadroom();
    void            Resource_RecalculateVideoMemoryHeadroom();
    void            Resource_ScheduleVideoMemoryHeadroomCalculation();

        /////////////////////////////////////////////////////////////////////

    RenderCore::IDevice::ResourceInitializer AsResourceInitializer(IDataPacket& pkt);
    
    struct StagingToFinalMapping
    {
        RenderCore::Box2D _dstBox;
        unsigned _dstLodLevelMin=0, _dstLodLevelMax=~unsigned(0x0);
        unsigned _dstArrayLayerMin=0, _dstArrayLayerMax=~unsigned(0x0);
        
        unsigned _stagingLODOffset = 0;
        unsigned _stagingArrayOffset = 0;
        VectorPattern<unsigned, 2> _stagingXYOffset = {0,0};
    };

    std::pair<ResourceDesc, StagingToFinalMapping> CalculatePartialStagingDesc(const ResourceDesc& dstDesc, const PartialResource& part);
    using QueueMarker = unsigned;

    class ResourceUploadHelper
    {
    public:
            ////////   P U S H   T O   R E S O U R C E   ////////
        unsigned WriteToBufferViaMap(
            const IResource& resource, unsigned resourceOffset, unsigned resourceSize,
            IteratorRange<const void*> data);

        unsigned WriteToBufferViaMap(
            const ResourceLocator& locator,
            IteratorRange<const void*> data);

        unsigned WriteToBufferViaMap(
            const IResource& resource, unsigned resourceOffset, unsigned resourceSize,
            const RenderCore::ResourceDesc& dstDesc,
            IDataPacket& dataPacket);

        unsigned WriteToTextureViaMap(
            const ResourceLocator& resource, const ResourceDesc& desc,
            const RenderCore::Box2D& box, 
            const RenderCore::IDevice::ResourceInitializer& data);

        void UpdateFinalResourceFromStaging(
            const ResourceLocator& finalResource,
            RenderCore::IResource& stagingResource, unsigned stagingOffset, unsigned stagingSize);

        bool CanDirectlyMap(RenderCore::IResource& resource);

        unsigned CalculateStagingBufferOffsetAlignment(const RenderCore::ResourceDesc& desc);

            ////////   R E S O U R C E   C O P Y   ////////
        void ResourceCopy_DefragSteps(const std::shared_ptr<RenderCore::IResource>& destination, const std::shared_ptr<RenderCore::IResource>& source, const std::vector<Utility::DefragStep>& steps);
        void ResourceCopy(RenderCore::IResource& destination, RenderCore::IResource& source);

            ////////   C O N S T R U C T I O N   ////////
        ResourceUploadHelper(RenderCore::IThreadContext& renderCoreContext);
        ~ResourceUploadHelper();

        RenderCore::IThreadContext& GetUnderlying() { return *_renderCoreContext; }

        #if GFXAPI_TARGET == GFXAPI_DX11
            private: 
                bool _useUpdateSubresourceWorkaround;
        #endif

    private:
        RenderCore::IThreadContext*         _renderCoreContext;
    };


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
        RenderCore::IResource& GetStagingResource() { return *_stagingBuffer; }
        void UpdateConsumerMarker(QueueMarker);

        StagingPage(RenderCore::IDevice& device, unsigned size);
        ~StagingPage();
        StagingPage(StagingPage&&) = default;
        StagingPage& operator=(StagingPage&&) = default;

    private:
        CircularHeap _stagingBufferHeap;
		std::shared_ptr<RenderCore::IResource> _stagingBuffer;

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
    };

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

}}
