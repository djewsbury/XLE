// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCache.h"
#include "SimpleModelRenderer.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/MaterialScaffold.h"
#include "../../Assets/AssetLRUHeap.h"
#include "../../Utility/HeapUtils.h"
#include <unordered_map>

namespace RenderCore { namespace Techniques
{
    using BoundingBox = std::pair<Float3, Float3>;
    class ModelCache::Pimpl
    {
    public:
        std::vector<std::pair<uint64_t, BoundingBox>> _boundingBoxes;

        ::Assets::AssetLRUHeap<RenderCore::Assets::ModelScaffold>		_modelScaffolds;
        ::Assets::AssetLRUHeap<RenderCore::Assets::MaterialScaffold>	_materialScaffolds;

		Threading::Mutex _modelRenderersLock;
        LRUCache<::Assets::FuturePtr<SimpleModelRenderer>>			_modelRenderers;
		std::shared_ptr<IPipelineAcceleratorPool>  _pipelineAcceleratorPool;

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
		StringSection<ResChar> materialFilename) -> ::Assets::PtrToFuturePtr<SimpleModelRenderer>
    {
		auto hash = HashCombine(Hash64(modelFilename), Hash64(materialFilename));

		::Assets::PtrToFuturePtr<SimpleModelRenderer> newFuture;
		{
			ScopedLock(_pimpl->_modelRenderersLock);
			auto& existing = _pimpl->_modelRenderers.Get(hash);
			if (existing) {
				if (!::Assets::IsInvalidated(*existing))
					return existing;
				++_pimpl->_reloadId;
			}

			auto stringInitializer = ::Assets::Internal::AsString(modelFilename, materialFilename);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<::Assets::FuturePtr<SimpleModelRenderer>>(stringInitializer);
			_pimpl->_modelRenderers.Insert(hash, newFuture);
		}

		auto modelScaffold = _pimpl->_modelScaffolds.Get(modelFilename);
		auto materialScaffold = _pimpl->_materialScaffolds.Get(materialFilename, modelFilename);

		::Assets::AutoConstructToFuture<SimpleModelRenderer>(*newFuture, _pimpl->_pipelineAcceleratorPool, modelScaffold, materialScaffold);
		return newFuture;
    }

	auto ModelCache::GetModelScaffold(StringSection<ResChar> name) -> ::Assets::PtrToFuturePtr<RenderCore::Assets::ModelScaffold>
	{
		return _pimpl->_modelScaffolds.Get(name);
	}

	auto ModelCache::GetMaterialScaffold(StringSection<ResChar> materialName, StringSection<ResChar> modelName) -> ::Assets::PtrToFuturePtr<RenderCore::Assets::MaterialScaffold>
	{
		return _pimpl->_materialScaffolds.Get(materialName, modelName);
	}

    ModelCache::ModelCache(const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool, const Config& cfg)
    {
        _pimpl = std::make_unique<Pimpl>(cfg);
		_pimpl->_pipelineAcceleratorPool = pipelineAcceleratorPool;
    }

    ModelCache::~ModelCache()
    {}


}}
