// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include "../Core/Types.h"
#include <utility>

namespace RenderCore { namespace Assets
{
    class ModelScaffold;
    class MaterialScaffold;
}}

namespace Assets { class AssetHeapRecord; class OperationContext; }
namespace RenderCore { namespace BufferUploads { class IManager; class IBatchedResources; }}

namespace RenderCore { namespace Techniques
{
    class SimpleModelRenderer;
    class DrawableConstructor;
	class IPipelineAcceleratorPool;
    class IDeformAcceleratorPool;
    class IDrawablesPool;
}}

namespace SceneEngine
{
    class ModelCache
    {
    public:
        class Config
        {
        public:
            unsigned _modelScaffoldCount;
            unsigned _materialScaffoldCount;
            unsigned _rendererCount;

            Config()
            : _modelScaffoldCount(2000), _materialScaffoldCount(2000)
            , _rendererCount(200) {}
        };

        using SupplementGUID = uint64_t;
        using SupplementRange = IteratorRange<const SupplementGUID*>;

		auto GetRendererMarker(
            StringSection<> modelFilename, 
            StringSection<> materialFilename) -> ::Assets::PtrToMarkerPtr<RenderCore::Techniques::SimpleModelRenderer>;
        auto TryGetRendererActual(
            uint64_t modelFilenameHash, StringSection<> modelFilename, 
            uint64_t materialFilenameHash, StringSection<> materialFilename) -> const RenderCore::Techniques::SimpleModelRenderer*;

        auto GetModelScaffold(StringSection<>) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>;
		auto GetMaterialScaffold(StringSection<>, StringSection<>) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>;

        uint32_t GetReloadId() const;
        void OnFrameBarrier();

        struct Records;
        Records LogRecords() const;

        std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetVBResources();
        std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetIBResources();
        void CancelConstructions();

        ModelCache(
			std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool, 
            std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool, 
            std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
            std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
            std::shared_ptr<::Assets::OperationContext> loadingContext,
			const Config& cfg = Config());
        ~ModelCache();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    struct ModelCache::Records
    {
        std::vector<::Assets::AssetHeapRecord> _modelScaffolds;
        std::vector<::Assets::AssetHeapRecord> _materialScaffolds;

        struct Renderer
        {
            std::string _model, _material;
            unsigned _decayFrames = 0;
        };
        std::vector<Renderer> _modelRenderers;
    };

}

