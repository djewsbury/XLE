// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCache.h"
#include "SimpleModelRenderer.h"
#include "ModelRendererConstruction.h"
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

		::Assets::AssetHeapLRU<std::shared_ptr<Assets::ModelScaffold>> _modelScaffolds;
		::Assets::AssetHeapLRU<std::shared_ptr<Assets::MaterialScaffold>> _materialScaffolds;
		struct Renderer
		{
			::Assets::PtrToMarkerPtr<SimpleModelRenderer> _rendererMarker;
			std::string _modelScaffoldName, _materialScaffoldName;
		};
		FrameByFrameLRUHeap<Renderer> _modelRenderers;
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
				if (!::Assets::IsInvalidated(*query.GetExisting()._rendererMarker))
					return query.GetExisting()._rendererMarker;
				++_pimpl->_reloadId;
			} else if (query.GetType() == LRUCacheInsertType::Fail) {
				return nullptr; // cache blown during this frame
			}

			auto stringInitializer = ::Assets::Internal::AsString(modelFilename, materialFilename);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<::Assets::MarkerPtr<SimpleModelRenderer>>(stringInitializer);
			Pimpl::Renderer r;
			r._rendererMarker = newFuture;
			r._modelScaffoldName = modelFilename.AsString();
			r._materialScaffoldName = materialFilename.AsString();
			query.Set(std::move(r));
		}

		auto modelScaffold = _pimpl->_modelScaffolds.Get(modelFilename);
		auto materialScaffold = _pimpl->_materialScaffolds.Get(materialFilename, modelFilename);
		auto construction = std::make_shared<ModelRendererConstruction>();
		construction->AddElement().SetModelScaffold(modelScaffold).SetMaterialScaffold(materialScaffold);

		::Assets::AutoConstructToPromise(newFuture->AdoptPromise(), _pimpl->_pipelineAcceleratorPool, construction);
		return newFuture;
	}

	auto ModelCache::GetModelScaffold(StringSection<ResChar> name) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>
	{
		return _pimpl->_modelScaffolds.Get(name);
	}

	auto ModelCache::GetMaterialScaffold(StringSection<ResChar> materialName, StringSection<ResChar> modelName) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>
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

		auto rendererRecords = _pimpl->_modelRenderers.LogRecords();
		result._modelRenderers.reserve(rendererRecords.size());
		for (const auto& r:rendererRecords)
			if (r._value._rendererMarker->TryActualize())
				result._modelRenderers.push_back({r._value._modelScaffoldName, r._value._materialScaffoldName, r._decayFrames});
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
