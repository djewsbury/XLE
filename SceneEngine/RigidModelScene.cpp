// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RigidModelScene.h"
#include "IScene.h"
#include "../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../RenderCore/Techniques/ModelRendererConstruction.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"		// just so we can use GetDevice()
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/ResourceConstructionContext.h"
#include "../RenderCore/Techniques/DeformAccelerator.h"
#include "../RenderCore/Techniques/DeformGeometryInfrastructure.h"
#include "../RenderCore/Techniques/DeformerConstruction.h"
#include "../RenderCore/Techniques/DrawableConstructor.h"
#include "../RenderCore/Techniques/LightWeightBuildDrawables.h"
#include "../RenderCore/BufferUploads/BatchedResources.h"
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../Math/ProjectionMath.h"
#include "../Assets/AssetHeapLRU.h"
#include "../Utility/HeapUtils.h"
#include <unordered_map>

namespace SceneEngine
{
	using BoundingBox = std::pair<Float3, Float3>;
	namespace RigidModelSceneInternal
	{
		struct ModelEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>> _completedConstruction;
			std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction> _referenceHolder;
		};

		struct DeformerEntry
		{
			std::shared_future<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> _completedConstruction;
			std::shared_ptr<RenderCore::Techniques::DeformerConstruction> _referenceHolder;
		};

		struct Renderer
		{
			std::shared_ptr<RenderCore::Techniques::DrawableConstructor> _drawableConstructor;
			std::shared_ptr<RenderCore::Techniques::DeformAccelerator> _deformAccelerator;
			std::shared_ptr<RenderCore::Assets::SkeletonScaffold> _skeletonScaffold;
			std::shared_ptr<RenderCore::Assets::ModelScaffold> _firstModelScaffold;
			RenderCore::BufferUploads::CommandListID _completionCmdList;
			std::pair<Float3, Float3> _aabb;

			const RenderCore::Assets::SkeletonMachine& GetSkeletonMachine() const
			{
				if (_skeletonScaffold) {
					return _skeletonScaffold->GetSkeletonMachine();
				} else {
					assert(_firstModelScaffold->EmbeddedSkeleton());
					return *_firstModelScaffold->EmbeddedSkeleton();
				}
			}
		};

		struct RendererEntry
		{
			std::shared_ptr<ModelEntry> _model;
			std::shared_ptr<DeformerEntry> _deformer;
			Renderer _renderer;
			std::shared_future<RigidModelSceneInternal::Renderer> _pendingRenderer;
			::Assets::DependencyValidation _depVal;
		};

		struct PendingUpdate
		{
			std::weak_ptr<RendererEntry> _dst;
			Renderer _renderer;
		};

		struct PendingExceptionUpdate
		{
			std::weak_ptr<RendererEntry> _dst;
			::Assets::Blob _log;
			::Assets::DependencyValidation _depVal;
		};
	}

	static std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>> ToFuture(RenderCore::Techniques::DrawableConstructor& construction)
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	template<typename T>
		std::future<void> AsOpaqueFuture(T inputFuture)
		{
			std::promise<void> promise;
			auto result = promise.get_future();
			::Assets::WhenAll(std::move(inputFuture)).Then(
				[promise=std::move(promise)](auto&& future) mutable {
					TRY {
						future.get();
						promise.set_value();
					} CATCH(...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
			return result;
		}

#if 0
	static void CollateAssetRecords(
		std::vector<std::pair<uint64_t, ::Assets::AssetHeapRecord>>& records,
		const RenderCore::Techniques::ModelRendererConstruction& rendererConstruction)
	{
		for (unsigned e=0; e<rendererConstruction.GetElementCount(); ++e) {
			auto ele = rendererConstruction.GetElement(e);
			::Assets::AssetHeapRecord materialRecord;
			materialRecord._initializer = ele->GetMaterialScaffoldName();
			if (materialRecord._initializer.empty())
				materialRecord._initializer = ele->GetModelScaffoldName();
			materialRecord._state = ::Assets::AssetState::Ready;		// we can't check the status without querying the future -- which would be a little too inefficient
			materialRecord._typeCode = RenderCore::Assets::MaterialScaffold::CompileProcessType;
			records.emplace_back(Hash64(materialRecord._initializer), std::move(materialRecord));

			::Assets::AssetHeapRecord modelRecord;
			modelRecord._initializer = ele->GetModelScaffoldName();
			modelRecord._state = ::Assets::AssetState::Ready;
			modelRecord._typeCode = RenderCore::Assets::ModelScaffold::CompileProcessType;
			records.emplace_back(Hash64(modelRecord._initializer), std::move(modelRecord));
		}
	}
#endif

	static std::string GetShortDescription(const RenderCore::Techniques::ModelRendererConstruction& construction)
	{
		std::stringstream result;
		if (construction.GetElementCount() != 1)
			result << "(Multi-element)";
		result << construction.GetElement(0)->GetModelScaffoldName();
		auto matName = construction.GetElement(0)->GetMaterialScaffoldName();
		if (!matName.empty())
			result << "[" << matName << "]";
		return result.str();
	}

	class RigidModelScene : public IRigidModelScene, public std::enable_shared_from_this<RigidModelScene>
	{
	public:
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
		std::shared_ptr<RenderCore::Techniques::ResourceConstructionContext> _constructionContext;
		std::shared_ptr<::Assets::OperationContext> _loadingContext;
		Config _cfg;

		Threading::Mutex _poolLock;
		
		std::vector<std::pair<uint64_t, std::weak_ptr<RigidModelSceneInternal::ModelEntry>>> _modelEntries;
		std::vector<std::weak_ptr<RigidModelSceneInternal::DeformerEntry>> _deformerEntries;
		std::vector<std::weak_ptr<RigidModelSceneInternal::RendererEntry>> _renderers;
		std::vector<RigidModelSceneInternal::PendingUpdate> _pendingUpdates;
		std::vector<RigidModelSceneInternal::PendingExceptionUpdate> _pendingExceptionUpdates;
		Signal<IteratorRange<const std::pair<uint64_t, ::Assets::AssetHeapRecord>*>> _updateSignal;

		OpaquePtr CreateModel(std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction> construction) override
		{
			auto hash = construction->GetHash();
			ScopedLock(_poolLock);
			auto i = LowerBound(_modelEntries, hash);
			if (i != _modelEntries.end() && i->first == hash) {
				auto l = i->second.lock();
				if (l) return std::move(l);
			}

			auto newEntry = std::make_shared<RigidModelSceneInternal::ModelEntry>();
			std::promise<std::shared_ptr<RenderCore::Techniques::ModelRendererConstruction>> promise;
			newEntry->_completedConstruction = promise.get_future();
			construction->FulfillWhenNotPending(std::move(promise));
			newEntry->_referenceHolder = std::move(construction);

			if (i != _modelEntries.end() && i->first == hash) {
				i->second = newEntry;		// rebuilding after previously expiring
			} else {
				_modelEntries.insert(i, {hash, newEntry});
			}

			return std::move(newEntry);
		}

		std::shared_ptr<void> CreateDeformers(std::shared_ptr<RenderCore::Techniques::DeformerConstruction> construction) override
		{
			// we can't hash this, so we always allocate a new one

			auto newEntry = std::make_shared<RigidModelSceneInternal::DeformerEntry>();
			std::promise<std::shared_ptr<RenderCore::Techniques::DeformerConstruction>> promise;
			newEntry->_completedConstruction = promise.get_future();
			construction->FulfillWhenNotPending(std::move(promise));
			newEntry->_referenceHolder = std::move(construction);
			
			ScopedLock(_poolLock);
			_deformerEntries.emplace_back(newEntry);
			return std::move(newEntry);
		}

		std::shared_ptr<void> CreateRenderer(std::shared_ptr<void> model, OpaquePtr deformers) override
		{
			ScopedLock(_poolLock);
			for (const auto& renderer:_renderers) {
				auto l = renderer.lock();
				if (!l) continue;
				if (l->_model == model && l->_deformer == deformers)
					return l;
			}

			auto newEntry = std::make_shared<RigidModelSceneInternal::RendererEntry>();
			newEntry->_model = std::static_pointer_cast<RigidModelSceneInternal::ModelEntry>(model);

			std::promise<RigidModelSceneInternal::Renderer> rendererPromise;
			newEntry->_pendingRenderer = rendererPromise.get_future();
			if (deformers) {
				newEntry->_deformer = std::static_pointer_cast<RigidModelSceneInternal::DeformerEntry>(deformers);

				::Assets::WhenAll(newEntry->_model->_completedConstruction, newEntry->_deformer->_completedConstruction).ThenConstructToPromise(
					std::move(rendererPromise),
					[drawablesPool=_drawablesPool, pipelineAcceleratorPool=_pipelineAcceleratorPool, constructionContext=_constructionContext, deformAcceleratorPool=_deformAcceleratorPool](
						auto&& promise, 
						auto completedConstruction, auto completedDeformerConstruction) mutable {

						std::shared_ptr<RenderCore::Techniques::DeformAccelerator> deformAccelerator;
						std::shared_ptr<RenderCore::Techniques::IDeformGeoAttachment> geoDeformer;
						if (completedDeformerConstruction && !completedDeformerConstruction->IsEmpty()) {
							geoDeformer = RenderCore::Techniques::CreateDeformGeoAttachment(
								*pipelineAcceleratorPool->GetDevice(), *completedConstruction, *completedDeformerConstruction);
							deformAccelerator = deformAcceleratorPool->CreateDeformAccelerator();
							deformAcceleratorPool->Attach(*deformAccelerator, geoDeformer);
						}

						auto drawableConstructor = std::make_shared<RenderCore::Techniques::DrawableConstructor>(
							drawablesPool, std::move(pipelineAcceleratorPool), std::move(constructionContext),
							*completedConstruction, deformAcceleratorPool, deformAccelerator);

						if (geoDeformer) {
							::Assets::WhenAll(ToFuture(*drawableConstructor), geoDeformer->GetInitializationFuture()).ThenConstructToPromiseWithFutures(
								std::move(promise),
								[geoDeformer, deformAccelerator, completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture, std::shared_future<void>&& deformerInitFuture) mutable {
									deformerInitFuture.get();	// propagate exceptions

									RigidModelSceneInternal::Renderer renderer;
									renderer._drawableConstructor = drawableConstructorFuture.get();
									renderer._completionCmdList = std::max(renderer._drawableConstructor->_completionCommandList, geoDeformer->GetCompletionCommandList());
									renderer._deformAccelerator = deformAccelerator;
									renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
									if (completedConstruction->GetElementCount() != 0) {
										renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModelScaffold();
										renderer._aabb = renderer._firstModelScaffold->GetStaticBoundingBox();
									} else {
										renderer._aabb = {Zero<Float3>(), Zero<Float3>()};
									}
									return renderer;
								});
						} else {
							::Assets::WhenAll(ToFuture(*drawableConstructor)).ThenConstructToPromiseWithFutures(
								std::move(promise),
								[completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture) mutable {
									RigidModelSceneInternal::Renderer renderer;
									renderer._drawableConstructor = drawableConstructorFuture.get();
									renderer._completionCmdList = renderer._drawableConstructor->_completionCommandList;
									renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
									if (completedConstruction->GetElementCount() != 0)
										renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModelScaffold();
									return renderer;
								});
						}
					});
			} else {
				// When no deformers explicitly specified, we don't apply the defaults -- just use the no-deformers case
				::Assets::WhenAll(newEntry->_model->_completedConstruction).ThenConstructToPromise(
					std::move(rendererPromise),
					[drawablesPool=_drawablesPool, pipelineAcceleratorPool=_pipelineAcceleratorPool, constructionContext=_constructionContext](
						auto&& promise, 
						auto completedConstruction) mutable {

						auto drawableConstructor = std::make_shared<RenderCore::Techniques::DrawableConstructor>(
							drawablesPool, std::move(pipelineAcceleratorPool), std::move(constructionContext),
							*completedConstruction);

						::Assets::WhenAll(ToFuture(*drawableConstructor)).ThenConstructToPromiseWithFutures(
							std::move(promise),
							[completedConstruction](std::future<std::shared_ptr<RenderCore::Techniques::DrawableConstructor>>&& drawableConstructorFuture) mutable {
								RigidModelSceneInternal::Renderer renderer;
								renderer._drawableConstructor = drawableConstructorFuture.get();
								renderer._completionCmdList = renderer._drawableConstructor->_completionCommandList;
								renderer._skeletonScaffold = completedConstruction->GetSkeletonScaffold();
								if (completedConstruction->GetElementCount() != 0)
									renderer._firstModelScaffold = completedConstruction->GetElement(0)->GetModelScaffold();
								return renderer;
							});
					});
			}

			::Assets::WhenAll(newEntry->_pendingRenderer).Then(
				[dstEntryWeak=std::weak_ptr<RigidModelSceneInternal::RendererEntry>(newEntry), sceneWeak=weak_from_this()](auto rendererFuture) {
					auto scene = sceneWeak.lock();
					if (scene) {
						ScopedLock(scene->_poolLock);
						TRY {
							auto renderer = rendererFuture.get();
							scene->_pendingUpdates.emplace_back(RigidModelSceneInternal::PendingUpdate { dstEntryWeak, std::move(renderer) });
						} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
							scene->_pendingExceptionUpdates.emplace_back(RigidModelSceneInternal::PendingExceptionUpdate { dstEntryWeak, e.GetActualizationLog(), e.GetDependencyValidation() });
						} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
							scene->_pendingExceptionUpdates.emplace_back(RigidModelSceneInternal::PendingExceptionUpdate { dstEntryWeak, e.GetActualizationLog(), e.GetDependencyValidation() });
						} CATCH(const std::exception& e) {
							scene->_pendingExceptionUpdates.emplace_back(RigidModelSceneInternal::PendingExceptionUpdate { dstEntryWeak, ::Assets::AsBlob(e.what()) });
						} CATCH_END
					}
				});

			// asset tracking
			{
				std::pair<uint64_t, ::Assets::AssetHeapRecord> rendererRecord;
				rendererRecord.first = newEntry->_model->_referenceHolder->GetHash();
				rendererRecord.second._initializer = GetShortDescription(*newEntry->_model->_referenceHolder);
				rendererRecord.second._state = ::Assets::AssetState::Pending;
				rendererRecord.second._typeCode = 0;
				_updateSignal.Invoke(MakeIteratorRange(&rendererRecord, &rendererRecord+1));
			}

			_renderers.emplace_back(newEntry);
			return newEntry;
		}

		void OnFrameBarrier() override
		{
			// flush out any pending updates
			ScopedLock(_poolLock);
			std::vector<std::pair<uint64_t, ::Assets::AssetHeapRecord>> updateRecords;
			updateRecords.reserve(_pendingUpdates.size() + _pendingExceptionUpdates.size());
			for (auto&u:_pendingUpdates) {
				auto l = u._dst.lock();
				if (!l) continue;
				l->_renderer = std::move(u._renderer);
				l->_pendingRenderer = {};
				// todo -- set dep val

				::Assets::AssetHeapRecord rendererRecord;
				rendererRecord._initializer = GetShortDescription(*l->_model->_referenceHolder);;
				rendererRecord._state = ::Assets::AssetState::Ready;
				rendererRecord._typeCode = 0;
				updateRecords.emplace_back(l->_model->_referenceHolder->GetHash(), std::move(rendererRecord));
			}
			_pendingUpdates.clear();
			for (auto&u:_pendingExceptionUpdates) {
				auto l = u._dst.lock();
				if (!l) continue;
				l->_depVal = std::move(u._depVal);
				l->_pendingRenderer = {};
				// todo -- record exception msg

				::Assets::AssetHeapRecord rendererRecord;
				rendererRecord._initializer = GetShortDescription(*l->_model->_referenceHolder);;
				rendererRecord._state = ::Assets::AssetState::Invalid;
				rendererRecord._typeCode = 0;
				rendererRecord._actualizationLog = u._log;
				updateRecords.emplace_back(l->_model->_referenceHolder->GetHash(), std::move(rendererRecord));
			}
			_pendingExceptionUpdates.clear();
			// todo -- check invalidations

			if (!updateRecords.empty())
				_updateSignal.Invoke(updateRecords);
		}

		SignalId BindUpdateSignal(std::function<UpdateSignalSig>&& fn) override
		{
			ScopedLock(_poolLock);
			auto recordsOnBind = LogRecordsAlreadyLocked();
			if (!recordsOnBind.empty())
				fn(MakeIteratorRange(recordsOnBind));
			return _updateSignal.Bind(std::move(fn));
		}

		void UnbindUpdateSignal(SignalId signal) override
		{
			_updateSignal.Unbind(signal);
		}

		std::vector<std::pair<uint64_t, ::Assets::AssetHeapRecord>> LogRecordsAlreadyLocked()
		{
			std::vector<std::pair<uint64_t, ::Assets::AssetHeapRecord>> result;
			for (const auto&rendererEntry:_renderers) {
				auto l = rendererEntry.lock();
				if (!l) continue;
				if (!l->_renderer._drawableConstructor) continue;

				::Assets::AssetHeapRecord rendererRecord;
				rendererRecord._initializer = GetShortDescription(*l->_model->_referenceHolder);;
				rendererRecord._state = ::Assets::AssetState::Ready;
				rendererRecord._typeCode = 0;
				result.emplace_back(l->_model->_referenceHolder->GetHash(), std::move(rendererRecord));
			}

			std::sort(result.begin(), result.end(), CompareFirst2());
			return result;
		}

		std::future<void> FutureForRenderer(void* renderer) override
		{
			ScopedLock(_poolLock);
			auto& rendererEntry = *(const RigidModelSceneInternal::RendererEntry*)renderer;
			return AsOpaqueFuture(rendererEntry._pendingRenderer);
		}

		RenderCore::BufferUploads::CommandListID GetCompletionCommandList(void* renderer) override
		{
			return ((const RigidModelSceneInternal::RendererEntry*)renderer)->_renderer._completionCmdList;
		}

		std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetVBResources() override
		{
			if (_constructionContext)
				return _constructionContext->GetRepositionableGeometryConduit()->GetVBResourcePool();
			return nullptr;
		}

		std::shared_ptr<RenderCore::BufferUploads::IBatchedResources> GetIBResources() override
		{
			if (_constructionContext)
				return _constructionContext->GetRepositionableGeometryConduit()->GetIBResourcePool();
			return nullptr;
		}

		std::shared_ptr<Assets::OperationContext> GetLoadingContext() override
		{
			return _loadingContext;
		}

		void CancelConstructions() override
		{
			if (_constructionContext)
				_constructionContext->Cancel();
		}

		RigidModelScene(
			std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool, 
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
			std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
			std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
			std::shared_ptr<::Assets::OperationContext> loadingContext,
			const Config& cfg)
		: _cfg(cfg)
		{
			_pipelineAcceleratorPool = std::move(pipelineAcceleratorPool);
			_deformAcceleratorPool = std::move(deformAcceleratorPool);
			_drawablesPool = std::move(drawablesPool);
			_loadingContext = std::move(loadingContext);
			if (bufferUploads) {
				auto repositionableGeometry = std::make_shared<RenderCore::Techniques::RepositionableGeometryConduit>(
					RenderCore::BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, RenderCore::BindFlag::VertexBuffer, 1024*1024),
					RenderCore::BufferUploads::CreateBatchedResources(*_pipelineAcceleratorPool->GetDevice(), bufferUploads, RenderCore::BindFlag::IndexBuffer, 1024*1024));
				_constructionContext = std::make_shared<RenderCore::Techniques::ResourceConstructionContext>(bufferUploads, std::move(repositionableGeometry));
			}
		}

		~RigidModelScene()
		{}
	};

	void IRigidModelScene::BuildDrawablesHelper::BuildDrawables(
		unsigned instanceIdx,
		const Float3x4& localToWorld, uint32_t viewMask, uint64_t cmdStream)
	{
		assert(cmdStream == 0);
		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			localToWorld, instanceIdx, viewMask);
		EnableInstanceDeform(*_activeRenderer->_deformAccelerator, instanceIdx);
	}

	void IRigidModelScene::BuildDrawablesHelper::BuildDrawablesInstancedFixedSkeleton(
		IteratorRange<const Float3x4*> objectToWorlds,
		IteratorRange<const unsigned*> viewMasks,
		uint64_t cmdStream)
	{
		assert(cmdStream == 0);
		RenderCore::Techniques::LightWeightBuildDrawables::InstancedFixedSkeleton(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			objectToWorlds, viewMasks);
	}

	void IRigidModelScene::BuildDrawablesHelper::BuildDrawablesInstancedFixedSkeleton(
		IteratorRange<const Float3x4*> objectToWorlds,
		uint64_t cmdStream)
	{
		assert(cmdStream == 0);
		RenderCore::Techniques::LightWeightBuildDrawables::InstancedFixedSkeleton(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			objectToWorlds);
	}

	void IRigidModelScene::BuildDrawablesHelper::CullAndBuildDrawables(
		unsigned instanceIdx, const Float3x4& localToWorld)
	{
		if (_complexCullingVolume && _complexCullingVolume->TestAABB(localToWorld, _activeRenderer->_aabb.first, _activeRenderer->_aabb.second) == CullTestResult::Culled)
			return;
		uint32_t viewMask = 0;
		for (unsigned v=0; v<_views.size(); ++v) {
			auto localToClip = Combine(localToWorld, _views[v]._worldToProjection);
			viewMask |= (!CullAABB(localToClip, _activeRenderer->_aabb.first, _activeRenderer->_aabb.second, RenderCore::Techniques::GetDefaultClipSpaceType())) << v;
		}
		if (!viewMask) return;

		RenderCore::Techniques::LightWeightBuildDrawables::SingleInstance(
			*_activeRenderer->_drawableConstructor,
			_pkts,
			localToWorld, instanceIdx, viewMask);
		EnableInstanceDeform(*_activeRenderer->_deformAccelerator, instanceIdx);
	}

	bool IRigidModelScene::BuildDrawablesHelper::SetRenderer(void* renderer)
	{
		auto* rendererEntry = (RigidModelSceneInternal::RendererEntry*)renderer;
		_activeRenderer = &rendererEntry->_renderer;
		return _activeRenderer->_drawableConstructor != nullptr;
	}

	IRigidModelScene::BuildDrawablesHelper::BuildDrawablesHelper(
		IRigidModelScene& scene,
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
		IteratorRange<const RenderCore::Techniques::ProjectionDesc*> views,
		const XLEMath::ArbitraryConvexVolumeTester* complexCullingVolume)
	: _pkts(pkts)
	, _activeRenderer(nullptr)
	, _views(views), _complexCullingVolume(complexCullingVolume)
	{}

	IRigidModelScene::BuildDrawablesHelper::BuildDrawablesHelper(
		IRigidModelScene& scene,
		SceneEngine::ExecuteSceneContext& executeContext)
	: _pkts(executeContext._destinationPkts)
	, _activeRenderer(nullptr)
	, _views(executeContext._views), _complexCullingVolume(executeContext._complexCullingVolume)
	{	
	}

	IRigidModelScene::~IRigidModelScene() = default;

	std::shared_ptr<IRigidModelScene> CreateRigidModelScene(
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool, 
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool, 
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
		std::shared_ptr<::Assets::OperationContext> loadingContext,
		const IRigidModelScene::Config& cfg)
	{
		return std::make_shared<RigidModelScene>(
			std::move(drawablesPool), std::move(pipelineAcceleratorPool), std::move(deformAcceleratorPool),
			std::move(bufferUploads), std::move(loadingContext),
			cfg);
	}

}
