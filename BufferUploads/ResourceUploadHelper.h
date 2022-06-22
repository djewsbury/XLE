// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include "../RenderCore/IDevice.h"
#include "../Utility/IntrusivePtr.h"
#include "../RenderCore/Metal/Forward.h"
#include <utility>

namespace Utility { class DefragStep; }

namespace BufferUploads { namespace PlatformInterface
{
        /////////////////////////////////////////////////////////////////////

	using UnderlyingResource = RenderCore::IResource;
	using UnderlyingResourcePtr = RenderCore::IResourcePtr;

    struct BufferMetrics : public RenderCore::ResourceDesc
    {
    public:
        unsigned        _systemMemorySize;
        unsigned        _videoMemorySize;
        const char*     _pixelFormatName;
    };

    void            Resource_Register(UnderlyingResource& resource, const char name[]);
    void            Resource_Report(bool justVolatiles);
    void            Resource_SetName(UnderlyingResource& resource, const char name[]);
    void            Resource_GetName(UnderlyingResource& resource, char buffer[], int bufferSize);
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

    class ResourceUploadHelper
    {
    public:
            ////////   P U S H   T O   R E S O U R C E   ////////
        unsigned WriteToBufferViaMap(
            const ResourceLocator& resource, const ResourceDesc& desc, unsigned offset,
            IteratorRange<const void*> data);
        
        unsigned WriteToTextureViaMap(
            const ResourceLocator& resource, const ResourceDesc& desc,
            const RenderCore::Box2D& box, 
            const RenderCore::IDevice::ResourceInitializer& data);

        void UpdateFinalResourceFromStaging(
            const ResourceLocator& finalResource, const ResourceLocator& staging,
            const ResourceDesc& destinationDesc, 
            const StagingToFinalMapping& stagingToFinalMapping);

            ////////   R E S O U R C E   C O P Y   ////////
        void ResourceCopy_DefragSteps(const UnderlyingResourcePtr& destination, const UnderlyingResourcePtr& source, const std::vector<Utility::DefragStep>& steps);
        void ResourceCopy(UnderlyingResource& destination, UnderlyingResource& source);

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

        ///////////////////////////////////////////////////////////////////

            ////////   F U N C T I O N A L I T Y   F L A G S   ////////

#if 0

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
