// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RigidModelScene.h"
#include "../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../RenderCore/Techniques/ModelRendererConstruction.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"		// just so we can use GetDevice()
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/ResourceConstructionContext.h"
#include "../RenderCore/BufferUploads/BatchedResources.h"
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../Assets/AssetHeapLRU.h"
#include "../Utility/HeapUtils.h"
#include <unordered_map>

namespace SceneEngine
{
	using BoundingBox = std::pair<Float3, Float3>;
	class ModelCache::Pimpl
	{
	public:
		std::vector<std::pair<uint64_t, BoundingBox>> _boundingBoxes;

		::Assets::AssetHeapLRU<std::shared_ptr<RenderCore::Assets::ModelScaffold>> _modelScaffolds;
		::Assets::AssetHeapLRU<std::shared_ptr<RenderCore::Assets::MaterialScaffold>> _materialScaffolds;
		struct Renderer
		{
			::Assets::PtrToMarkerPtr<RenderCore::Techniques::SimpleModelRenderer> _rendererMarker;
			std::string _modelScaffoldName, _materialScaffoldName;
		};
		FrameByFrameLRUHeap<Renderer> _modelRenderers;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
		std::shared_ptr<RenderCore::Techniques::ResourceConstructionContext> _constructionContext;
		std::shared_ptr<::Assets::OperationContext> _loadingContext;

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

	auto ModelCache::GetRendererMarker(
		StringSection<> modelFilename,
		StringSection<> materialFilename) -> ::Assets::PtrToMarkerPtr<RenderCore::Techniques::SimpleModelRenderer>
	{
		auto hash = HashCombine(Hash64(modelFilename), Hash64(materialFilename));

		::Assets::PtrToMarkerPtr<RenderCore::Techniques::SimpleModelRenderer> newFuture;
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
			newFuture = std::make_shared<::Assets::MarkerPtr<RenderCore::Techniques::SimpleModelRenderer>>(stringInitializer);
			Pimpl::Renderer r;
			r._rendererMarker = newFuture;
			r._modelScaffoldName = modelFilename.AsString();
			r._materialScaffoldName = materialFilename.AsString();
			query.Set(std::move(r));
		}

		auto modelScaffold = _pimpl->_modelScaffolds.Get(_pimpl->_loadingContext, modelFilename);
		auto materialScaffold = _pimpl->_materialScaffolds.Get(_pimpl->_loadingContext, materialFilename, modelFilename);
		auto construction = std::make_shared<RenderCore::Techniques::ModelRendererConstruction>();
		construction->AddElement().SetModelScaffold(modelScaffold->ShareFuture(), modelFilename.AsString()).SetMaterialScaffold(materialScaffold->ShareFuture(), materialFilename.AsString());

		::Assets::AutoConstructToPromise(newFuture->AdoptPromise(), _pimpl->_drawablesPool, _pimpl->_pipelineAcceleratorPool, _pimpl->_constructionContext, construction);
		return newFuture;
	}

	auto ModelCache::TryGetRendererActual(
		uint64_t modelFilenameHash, StringSection<> modelFilename,
		uint64_t materialFilenameHash, StringSection<> materialFilename) -> const RenderCore::Techniques::SimpleModelRenderer*
	{
		auto hash = HashCombine(modelFilenameHash, materialFilenameHash);

		::Assets::PtrToMarkerPtr<RenderCore::Techniques::SimpleModelRenderer> newFuture;
		{
			auto query = _pimpl->_modelRenderers.Query(hash);
			if (query.GetType() == LRUCacheInsertType::Update) {
				auto attempt = query.GetExisting()._rendererMarker->TryActualize();
				return attempt ? attempt->get() : nullptr;
			} else if (query.GetType() == LRUCacheInsertType::Fail) {
				return nullptr; // cache blown during this frame
			}

			auto stringInitializer = ::Assets::Internal::AsString(modelFilename, materialFilename);	// (used for tracking/debugging purposes)
			newFuture = std::make_shared<::Assets::MarkerPtr<RenderCore::Techniques::SimpleModelRenderer>>(stringInitializer);
			Pimpl::Renderer r;
			r._rendererMarker = newFuture;
			r._modelScaffoldName = modelFilename.AsString();
			r._materialScaffoldName = materialFilename.AsString();
			query.Set(std::move(r));
		}

		auto modelScaffold = _pimpl->_modelScaffolds.Get(_pimpl->_loadingContext, modelFilename);
		auto materialScaffold = _pimpl->_materialScaffolds.Get(_pimpl->_loadingContext, materialFilename, modelFilename);
		auto construction = std::make_shared<RenderCore::Techniques::ModelRendererConstruction>();
		construction->AddElement().SetModelScaffold(modelScaffold->ShareFuture(), modelFilename.AsString()).SetMaterialScaffold(materialScaffold->ShareFuture(), materialFilename.AsString());

		::Assets::AutoConstructToPromise(newFuture->AdoptPromise(), _pimpl->_drawablesPool, _pimpl->_pipelineAcceleratorPool, _pimpl->_constructionContext, construction);
		return nullptr;	// implicitly not available immediately
	}

	auto ModelCache::GetModelScaffold(StringSection<> name) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>
	{
		return _pimpl->_modelScaffolds.Get(_pimpl->_loadingContext, name);
	}

	auto ModelCache::GetMaterialScaffold(StringSection<> materialName, StringSection<> modelName) -> ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>
	{
		return _pimpl->_materialScaffolds.Get(_pimpl->_loadingContext, materialName, modelName);
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

	std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> ModelCache::GetVBResources()
	{
		if (_pimpl->_constructionContext)
			return _pimpl->_constructionContext->GetRepositionableGeometryConduit()->GetVBResourcePool();
		return nullptr;
	}

	std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> ModelCache::GetIBResources()
	{
		if (_pimpl->_constructionContext)
			return _pimpl->_constructionContext->GetRepositionableGeometryConduit()->GetIBResourcePool();
		return nullptr;
	}

	void ModelCache::CancelConstructions()
	{
		if (_pimpl->_constructionContext)
			_pimpl->_constructionContext->Cancel();
	}

	ModelCache::ModelCache(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool, 
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<::Assets::OperationContext> loadingContext,
		const Config& cfg)
	{
		_pimpl = std::make_unique<Pimpl>(cfg);
		_pimpl->_pipelineAcceleratorPool = std::move(pipelineAcceleratorPool);
		_pimpl->_deformAcceleratorPool = std::move(deformAcceleratorPool);
		_pimpl->_drawablesPool = std::move(drawablesPool);
		_pimpl->_loadingContext = std::move(loadingContext);
		if (bufferUploads) {
			auto repositionableGeometry = std::make_shared<RenderCore::Techniques::RepositionableGeometryConduit>(
				RenderCore::BufferUploads::CreateBatchedResources(*_pimpl->_pipelineAcceleratorPool->GetDevice(), bufferUploads, RenderCore::BindFlag::VertexBuffer, 1024*1024),
				RenderCore::BufferUploads::CreateBatchedResources(*_pimpl->_pipelineAcceleratorPool->GetDevice(), bufferUploads, RenderCore::BindFlag::IndexBuffer, 1024*1024));
			_pimpl->_constructionContext = std::make_shared<RenderCore::Techniques::ResourceConstructionContext>(bufferUploads, std::move(repositionableGeometry));
		}
	}

	ModelCache::~ModelCache()
	{}


}
