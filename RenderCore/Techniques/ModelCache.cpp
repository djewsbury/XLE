// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCache.h"
#include "SimpleModelRenderer.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/MaterialScaffold.h"
#include "../../Assets/AssetHeapLRU.h"
#include "../../Utility/HeapUtils.h"
#include <unordered_map>

namespace RenderCore { namespace Techniques
{
    using BoundingBox = std::pair<Float3, Float3>;
    class ModelCache::Pimpl
    {
    public:
        std::vector<std::pair<uint64_t, BoundingBox>> _boundingBoxes;

        ::Assets::AssetHeapLRU<std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm>>		_modelScaffolds;
        ::Assets::AssetHeapLRU<std::shared_ptr<RenderCore::Assets::MaterialScaffoldCmdStreamForm>>	_materialScaffolds;
        FrameByFrameLRUHeap<std::shared_ptr<::Assets::MarkerPtr<SimpleModelRenderer>>> _modelRenderers;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
        std::shared_ptr<IDeformAcceleratorPool> _deformAcceleratorPool;

        uint32_t _reloadId;

        Pimpl(const ModelCache::Config& cfg);
        ~Pimpl();
    };
        
    ModelCache::Pimpl::Pimpl(const ModelCache::Config& cfg)
    : _modelScaffolds(cfg._modelScaffoldCount)
    , _materialScaffolds(cfg._materialScaffoldCount)
    , _modelRenderers(cfg._rendererCount)
    , _reloadId(0)
    {}

    ModelCache::Pimpl::~Pimpl() {}

    uint32_t ModelCache::GetReloadId() const { return _pimpl->_reloadId; }

    auto ModelCache::GetModelRenderer(
        StringSection<ResChar> modelFilename,
		StringSection<ResChar> materialFilename) -> ::Assets::PtrToMarkerPtr<SimpleModelRenderer>
    {
		auto hash = HashCombine(Hash64(modelFilename), Hash64(materialFilename));

		::Assets::PtrToMarkerPtr<SimpleModelRenderer> newFuture;
		{
			auto query = _pimpl->_modelRenderers.Query(hash);
			if (query.GetType() == LRUCacheInsertType::Update) {
				if (!::Assets::IsInvalidated(*query.GetExisting()))
					return query.GetExisting();
				++_pimpl->_reloadId;
			} else if (query.GetType() == LRUCacheInsertType::Fail) {
                return nullptr; // cache blown during this frame
            }

			auto stringInitializer = ::Assets::Internal::AsString(modelFilename, materialFilename);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<::Assets::MarkerPtr<SimpleModelRenderer>>(stringInitializer);
			query.Set(decltype(newFuture){newFuture});
		}

		auto modelScaffold = _pimpl->_modelScaffolds.Get(modelFilename);
		auto materialScaffold = _pimpl->_materialScaffolds.Get(materialFilename, modelFilename);
        auto construction = std::make_shared<Assets::RendererConstruction>();
        construction->AddElement().SetModelScaffold(modelScaffold).SetMaterialScaffold(materialScaffold);

		::Assets::AutoConstructToPromise(newFuture->AdoptPromise(), _pimpl->_pipelineAcceleratorPool, construction);
		return newFuture;
    }

	auto ModelCache::GetModelScaffold(StringSection<ResChar> name) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffoldCmdStreamForm>
	{
		return _pimpl->_modelScaffolds.Get(name);
	}

	auto ModelCache::GetMaterialScaffold(StringSection<ResChar> materialName, StringSection<ResChar> modelName) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffoldCmdStreamForm>
	{
		return _pimpl->_materialScaffolds.Get(materialName, modelName);
	}

    void ModelCache::OnFrameBarrier()
    {
        _pimpl->_modelRenderers.OnFrameBarrier();
    }

    ModelCache::Records ModelCache::LogRecords() const
    {
        ModelCache::Records result;
        result._modelScaffolds = _pimpl->_modelScaffolds.LogRecords();
        result._materialScaffolds = _pimpl->_materialScaffolds.LogRecords();

#if 0
        auto rendererRecords = _pimpl->_modelRenderers.LogRecords();
        result._modelRenderers.reserve(rendererRecords.size());
        for (const auto& r:rendererRecords) {
            auto actual = r._value->TryActualize();
            if (actual)
                result._modelRenderers.push_back({(*actual)->GetModelScaffoldName(), (*actual)->GetMaterialScaffoldName(), r._decayFrames});
        }
#endif
        assert(0);
        return result;
    }

    ModelCache::ModelCache(
        std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool,
        std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool,
        const Config& cfg)
    {
        _pimpl = std::make_unique<Pimpl>(cfg);
		_pimpl->_pipelineAcceleratorPool = std::move(pipelineAcceleratorPool);
        _pimpl->_deformAcceleratorPool = std::move(deformAcceleratorPool);
    }

    ModelCache::~ModelCache()
    {}


}}
