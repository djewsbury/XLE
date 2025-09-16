// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelRendererConstruction.h"
#include "ModelMachine.h"
#include "AssetUtils.h"
#include "ModelScaffold.h"
#include "CompiledMaterialSet.h"
#include "ModelCompilationConfiguration.h"
#include "MaterialCompiler.h"
#include "RawMaterial.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/AssetMixins.h"
#include "../../Assets/ConfigFileContainer.h"

namespace RenderCore { namespace Assets
{
	class ModelRendererConstruction::Internal
	{
	public:
		using ElementId = unsigned;
		using ModelScaffoldMarker = std::shared_future<std::shared_ptr<Assets::ModelScaffold>>;
		using ModelScaffoldPtr = std::shared_ptr<Assets::ModelScaffold>;
		using MaterialScaffoldMarker = std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>>;
		using MaterialScaffoldPtr = std::shared_ptr<Assets::CompiledMaterialSet>;
		using MaterialScaffoldConstructionPtr = std::shared_ptr<Assets::MaterialSetConstruction>;
		using CompilationConfigurationMarker = std::shared_future<ResolvedMCC>;
		using CompilationConfigurationPtr = std::shared_ptr<Assets::ModelCompilationConfiguration>;

		std::vector<std::pair<ElementId, ModelScaffoldMarker>> _modelScaffoldMarkers;
		std::vector<std::pair<ElementId, ModelScaffoldPtr>> _modelScaffoldPtrs;
		std::vector<std::pair<ElementId, MaterialScaffoldMarker>> _materialScaffoldMarkers;
		std::vector<std::pair<ElementId, MaterialScaffoldPtr>> _materialScaffoldPtrs;
		std::vector<std::pair<ElementId, MaterialScaffoldConstructionPtr>> _materialScaffoldConstructionPtrs;
		std::vector<std::pair<ElementId, CompilationConfigurationMarker>> _compilationConfigurationMarkers;
		std::vector<std::pair<ElementId, CompilationConfigurationPtr>> _compilationConfigurationPtrs;
		std::vector<std::pair<ElementId, Float4x4>> _elementToObjects;
		std::vector<std::pair<ElementId, uint64_t>> _deformerBindPoints;
		std::vector<std::pair<ElementId, std::string>> _names;
		std::vector<std::pair<ElementId, std::string>> _modelScaffoldInitializers;
		std::vector<std::pair<ElementId, std::string>> _materialScaffoldInitializers;
		std::vector<std::pair<ElementId, std::string>> _compilationConfigurationInitializers;
		unsigned _elementCount = 0;

		std::shared_future<std::shared_ptr<Assets::SkeletonScaffold>> _skeletonScaffoldMarker;
		std::shared_ptr<Assets::SkeletonScaffold> _skeletonScaffoldPtr;
		std::shared_ptr<::Assets::OperationContext> _opContext;
		std::string _skeletonScaffoldInitializer;
		uint64_t _skeletonScaffoldHashValue = 0u;

		bool _sealed = false;

		mutable uint64_t _hash = 0ull;
		bool _disableHash = false;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_future<std::shared_ptr<Assets::ModelScaffold>> CreateModelScaffoldFuture(
		StringSection<> modelName,
		std::shared_future<ResolvedMCC> futureCompilationConfiguration)
	{
		std::promise<std::shared_ptr<Assets::ModelScaffold>> promise;
		auto future = promise.get_future();
		::Assets::WhenAll(futureCompilationConfiguration).ThenConstructToPromise(
			std::move(promise),
			[model=modelName.AsString()](auto&& promise, auto q) {
				auto chain = ::Assets::GetAssetFuture<std::shared_ptr<Assets::ModelScaffold>>(model, std::get<0>(q));
				::Assets::WhenAll(std::move(chain)).ThenConstructToPromise(std::move(promise));
			});
		return future;
	}

	static std::shared_future<std::shared_ptr<Assets::ModelScaffold>> CreateModelScaffoldFuture(
		StringSection<> modelName,
		std::shared_ptr<Assets::ModelCompilationConfiguration> compilationConfig)
	{
		return ::Assets::GetAssetFuture<std::shared_ptr<Assets::ModelScaffold>>(modelName, std::move(compilationConfig));
	}

	static std::shared_future<std::shared_ptr<Assets::ModelScaffold>> CreateModelScaffoldFuture(
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> modelName,
		std::shared_future<ResolvedMCC> futureCompilationConfiguration)
	{
		std::promise<std::shared_ptr<Assets::ModelScaffold>> promise;
		auto future = promise.get_future();
		::Assets::WhenAll(futureCompilationConfiguration).ThenConstructToPromise(
			std::move(promise),
			[model=modelName.AsString(), opContext=std::move(opContext)](auto&& promise, auto q) {
				auto chain = ::Assets::GetAssetFuture<std::shared_ptr<Assets::ModelScaffold>>(std::move(opContext), model, std::get<0>(q));
				::Assets::WhenAll(std::move(chain)).ThenConstructToPromise(std::move(promise));
			});
		return future;
	}

	static std::shared_future<std::shared_ptr<Assets::ModelScaffold>> CreateModelScaffoldFuture(
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> modelName,
		std::shared_ptr<Assets::ModelCompilationConfiguration> compilationConfig)
	{
		return ::Assets::GetAssetFuture<std::shared_ptr<Assets::ModelScaffold>>(std::move(opContext), modelName, std::move(compilationConfig));
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> CreateMaterialScaffoldFuture(
		StringSection<> materialName, StringSection<> modelName,
		std::shared_future<ResolvedMCC> futureCompilationConfiguration)
	{
		std::promise<std::shared_ptr<Assets::CompiledMaterialSet>> promise;
		auto future = promise.get_future();
		::Assets::WhenAll(futureCompilationConfiguration).ThenConstructToPromise(
			std::move(promise),
			[model=modelName.AsString(), material=materialName.AsString()](auto&& promise, auto q) {
				auto chain = ::Assets::GetAssetFuture<std::shared_ptr<Assets::CompiledMaterialSet>>(material, model, std::get<0>(q));
				::Assets::WhenAll(std::move(chain)).ThenConstructToPromise(std::move(promise));
			});
		return future;
	}

	static std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> CreateMaterialScaffoldFuture(
		StringSection<> materialName, StringSection<> modelName,
		std::shared_ptr<Assets::ModelCompilationConfiguration> compilationConfig)
	{
		return ::Assets::GetAssetFuture<std::shared_ptr<Assets::CompiledMaterialSet>>(materialName, modelName, std::move(compilationConfig));
	}

	static std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> CreateMaterialScaffoldFuture(
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> materialName, StringSection<> modelName,
		std::shared_future<ResolvedMCC> futureCompilationConfiguration)
	{
		std::promise<std::shared_ptr<Assets::CompiledMaterialSet>> promise;
		auto future = promise.get_future();
		::Assets::WhenAll(futureCompilationConfiguration).ThenConstructToPromise(
			std::move(promise),
			[model=modelName.AsString(), opContext=std::move(opContext), material=materialName.AsString()](auto&& promise, auto q) {
				auto chain = ::Assets::GetAssetFuture<std::shared_ptr<Assets::CompiledMaterialSet>>(std::move(opContext), material, model, std::get<0>(q));
				::Assets::WhenAll(std::move(chain)).ThenConstructToPromise(std::move(promise));
			});
		return future;
	}

	static std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> CreateMaterialScaffoldFuture(
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> materialName, StringSection<> modelName,
		std::shared_ptr<Assets::ModelCompilationConfiguration> compilationConfig)
	{
		return ::Assets::GetAssetFuture<std::shared_ptr<Assets::CompiledMaterialSet>>(std::move(opContext), materialName, modelName, std::move(compilationConfig));
	}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	auto ModelRendererConstruction::ElementConstructor::SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		assert(!model.IsEmpty());
		auto originalDisableHash = _internal->_disableHash;

		auto materialForAsset = material;
		if (materialForAsset.IsEmpty()) materialForAsset = model;

		auto i = LowerBound(_internal->_compilationConfigurationMarkers, _elementId);
		if (i != _internal->_compilationConfigurationMarkers.end() && i->first == _elementId) {
			if (_internal->_opContext) {
				SetModelScaffold(CreateModelScaffoldFuture(_internal->_opContext, model, i->second), model.AsString());
				SetMaterialScaffold(CreateMaterialScaffoldFuture(_internal->_opContext, materialForAsset, model, i->second), material.AsString());
			} else {
				SetModelScaffold(CreateModelScaffoldFuture(model, i->second), model.AsString());
				SetMaterialScaffold(CreateMaterialScaffoldFuture(materialForAsset, model, i->second), material.AsString());
			}
		} else {
			auto i2 = LowerBound(_internal->_compilationConfigurationPtrs, _elementId);
			if (i2 != _internal->_compilationConfigurationPtrs.end() && i2->first == _elementId) {
				if (_internal->_opContext) {
					SetModelScaffold(CreateModelScaffoldFuture(_internal->_opContext, model, i2->second), model.AsString());
					SetMaterialScaffold(CreateMaterialScaffoldFuture(_internal->_opContext, materialForAsset, model, i2->second), material.AsString());
				} else {
					SetModelScaffold(CreateModelScaffoldFuture(model, i2->second), model.AsString());
					SetMaterialScaffold(CreateMaterialScaffoldFuture(materialForAsset, model, i2->second), material.AsString());
				}
			} else {
				if (_internal->_opContext) {
					SetModelScaffold(::Assets::GetAssetFuture<Internal::ModelScaffoldPtr>(_internal->_opContext, model), model.AsString());
					SetMaterialScaffold(::Assets::GetAssetFuture<Internal::MaterialScaffoldPtr>(_internal->_opContext, materialForAsset, model), material.AsString());
				} else {
					SetModelScaffold(::Assets::GetAssetFuture<Internal::ModelScaffoldPtr>(model), model.AsString());
					SetMaterialScaffold(::Assets::GetAssetFuture<Internal::MaterialScaffoldPtr>(materialForAsset, model), material.AsString());
				}
			}
		}
		
		_internal->_disableHash = originalDisableHash;
		_internal->_hash = 0;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetModelAndMaterialScaffolds(StringSection<> model) -> ElementConstructor&
	{
		return SetModelAndMaterialScaffolds(model, StringSection<>{});
	}
	auto ModelRendererConstruction::ElementConstructor::SetModelScaffold(StringSection<> model) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		assert(!model.IsEmpty());
		auto originalDisableHash = _internal->_disableHash;

		auto modelStr = model.AsString();
		auto i = LowerBound(_internal->_compilationConfigurationMarkers, _elementId);
		if (i != _internal->_compilationConfigurationMarkers.end() && i->first == _elementId) {
			SetModelScaffold(CreateModelScaffoldFuture(model, i->second), modelStr);
		} else {
			auto i2 = LowerBound(_internal->_compilationConfigurationPtrs, _elementId);
			if (i2 != _internal->_compilationConfigurationPtrs.end() && i2->first == _elementId) {
				SetModelScaffold(CreateModelScaffoldFuture(model, i2->second), modelStr);
			} else {
				SetModelScaffold(::Assets::GetAssetFuture<Internal::ModelScaffoldPtr>(model), modelStr);
			}
		}

		_internal->_disableHash = originalDisableHash;
		_internal->_hash = 0;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetModelScaffold(std::shared_future<std::shared_ptr<Assets::ModelScaffold>> scaffoldMarker, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId) i->second = std::move(scaffoldMarker);
		else _internal->_modelScaffoldMarkers.insert(i, {_elementId, std::move(scaffoldMarker)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_modelScaffoldInitializers, _elementId);
			if (ii != _internal->_modelScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_modelScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetModelScaffold(std::shared_ptr<Assets::ModelScaffold> scaffoldPtr, std::string initializer) -> ElementConstructor& 
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) i->second = std::move(scaffoldPtr);
		else _internal->_modelScaffoldPtrs.insert(i, {_elementId, std::move(scaffoldPtr)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_modelScaffoldInitializers, _elementId);
			if (ii != _internal->_modelScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_modelScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this; 
	}
	auto ModelRendererConstruction::ElementConstructor::SetMaterialScaffold(std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> scaffoldMarker, std::string initializer) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldMarkers, _elementId);
		if (i != _internal->_materialScaffoldMarkers.end() && i->first == _elementId) i->second = std::move(scaffoldMarker);
		else _internal->_materialScaffoldMarkers.insert(i, {_elementId, std::move(scaffoldMarker)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_materialScaffoldInitializers, _elementId);
			if (ii != _internal->_materialScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_materialScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetMaterialScaffold(std::shared_ptr<Assets::CompiledMaterialSet> scaffoldPtr, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldPtrs, _elementId);
		if (i != _internal->_materialScaffoldPtrs.end() && i->first == _elementId) i->second = std::move(scaffoldPtr);
		else _internal->_materialScaffoldPtrs.insert(i, {_elementId, std::move(scaffoldPtr)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_materialScaffoldInitializers, _elementId);
			if (ii != _internal->_materialScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_materialScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetMaterialScaffold(std::shared_ptr<MaterialSetConstruction> scaffold, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		assert(scaffold);
		auto originalDisableHash = _internal->_disableHash;
		auto canBeHashed = scaffold->CanBeHashed();
		auto i4 = LowerBound(_internal->_materialScaffoldConstructionPtrs, _elementId);
		if (i4 != _internal->_materialScaffoldConstructionPtrs.end() && i4->first == _elementId) i4->second = scaffold;
		else i4 = _internal->_materialScaffoldConstructionPtrs.insert(i4, {_elementId, scaffold});

		// Create and set a material scaffold -- but this requires finding the model 
		std::promise<std::shared_ptr<CompiledMaterialSet>> promisedScaffold;
		auto futureScaffold = promisedScaffold.get_future();
		ConstructMaterialSet(std::move(promisedScaffold), i4->second);
		SetMaterialScaffold(std::move(futureScaffold));

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_materialScaffoldInitializers, _elementId);
			if (ii != _internal->_materialScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_materialScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = originalDisableHash || !canBeHashed;
		_internal->_hash = 0;
		return *this;
	}

	static std::shared_future<ResolvedMCC> GetFutureResolvedMCC(StringSection<> cfg)
	{
		return ::Assets::GetAssetFutureFn<
			::Assets::ResolveAssetToPromise<std::shared_ptr<ModelCompilationConfiguration>>
		>(cfg);
	}

	auto ModelRendererConstruction::ElementConstructor::SetCompilationConfiguration(StringSection<> cfg) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto originalDisableHash = _internal->_disableHash;
		SetCompilationConfiguration(GetFutureResolvedMCC(cfg), cfg.AsString());
		_internal->_disableHash = originalDisableHash;
		_internal->_hash = 0;
		return *this;
	}

	auto ModelRendererConstruction::ElementConstructor::SetCompilationConfiguration(std::shared_future<ResolvedMCC> futureCfg, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		{
			auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
			if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId)
				Throw(std::runtime_error("Compilation configuration must be set before the model scaffold in ModelRendererConstruction"));
			auto i2 = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
			if (i2 != _internal->_modelScaffoldPtrs.end() && i2->first == _elementId)
				Throw(std::runtime_error("Compilation configuration must be set before the model scaffold in ModelRendererConstruction"));
		}
		auto i = LowerBound(_internal->_compilationConfigurationMarkers, _elementId);
		if (i != _internal->_compilationConfigurationMarkers.end() && i->first == _elementId) i->second = std::move(futureCfg);
		else _internal->_compilationConfigurationMarkers.insert(i, {_elementId, std::move(futureCfg)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_compilationConfigurationInitializers, _elementId);
			if (ii != _internal->_compilationConfigurationInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_compilationConfigurationInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this;
	}

	auto ModelRendererConstruction::ElementConstructor::SetCompilationConfiguration(std::shared_ptr<ModelCompilationConfiguration> cfg, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_compilationConfigurationPtrs, _elementId);
		if (i != _internal->_compilationConfigurationPtrs.end() && i->first == _elementId) i->second = std::move(cfg);
		else _internal->_compilationConfigurationPtrs.insert(i, {_elementId, std::move(cfg)});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_compilationConfigurationInitializers, _elementId);
			if (ii != _internal->_compilationConfigurationInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_compilationConfigurationInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this;
	}

	auto ModelRendererConstruction::ElementConstructor::SetElementToObject(const Float4x4& modelToObject) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_elementToObjects, _elementId);
		if (i != _internal->_elementToObjects.end() && i->first == _elementId) i->second = modelToObject;
		else _internal->_elementToObjects.insert(i, {_elementId, modelToObject});
		return *this;
	}

	auto ModelRendererConstruction::ElementConstructor::SetDeformerBindPoint(uint64_t deformerBindPoint) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_deformerBindPoints, _elementId);
		if (i != _internal->_deformerBindPoints.end() && i->first == _elementId) i->second = deformerBindPoint;
		else _internal->_deformerBindPoints.insert(i, {_elementId, deformerBindPoint});
		return *this;
	}

	auto ModelRendererConstruction::ElementConstructor::SetName(const std::string& name) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_names, _elementId);
		if (i != _internal->_names.end() && i->first == _elementId) {
			i->second = name;
		} else
			_internal->_names.insert(i, {_elementId, name});
		return *this; 
	}

	std::shared_future<std::shared_ptr<Assets::ModelScaffold>> ModelRendererConstruction::ElementConstructor::GetFutureModelScaffold()
	{
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId)
			return i->second;
		return {};
	}

	std::shared_future<std::shared_ptr<Assets::CompiledMaterialSet>> ModelRendererConstruction::ElementConstructor::GetFutureMaterialScaffold()
	{
		auto i = LowerBound(_internal->_materialScaffoldMarkers, _elementId);
		if (i != _internal->_materialScaffoldMarkers.end() && i->first == _elementId)
			return i->second;
		return {};
	}

	void ModelRendererConstruction::SetSkeletonScaffold(StringSection<> skeleton)
	{
		_internal->_skeletonScaffoldHashValue = Hash64(skeleton);
		_internal->_skeletonScaffoldPtr = nullptr;
		if (_internal->_opContext) {
			_internal->_skeletonScaffoldMarker = ::Assets::GetAssetFuture<std::shared_ptr<Assets::SkeletonScaffold>>(_internal->_opContext, skeleton);
		} else
			_internal->_skeletonScaffoldMarker = ::Assets::GetAssetFuture<std::shared_ptr<Assets::SkeletonScaffold>>(skeleton);
		_internal->_skeletonScaffoldInitializer = skeleton.AsString();
	}
	void ModelRendererConstruction::SetSkeletonScaffold(std::shared_future<std::shared_ptr<Assets::SkeletonScaffold>> skeleton, std::string initializer)
	{
		_internal->_disableHash = true;
		_internal->_skeletonScaffoldPtr = nullptr;
		_internal->_skeletonScaffoldMarker = std::move(skeleton);
		_internal->_skeletonScaffoldInitializer = initializer;
	}
	void ModelRendererConstruction::SetSkeletonScaffold(const std::shared_ptr<Assets::SkeletonScaffold>& skeleton)
	{
		_internal->_disableHash = true;
		_internal->_skeletonScaffoldPtr = skeleton;
		_internal->_skeletonScaffoldMarker = {};
	}
	std::shared_ptr<Assets::SkeletonScaffold> ModelRendererConstruction::GetSkeletonScaffold() const
	{
		if (_internal->_skeletonScaffoldPtr)
			return _internal->_skeletonScaffoldPtr;
		if (_internal->_skeletonScaffoldMarker.valid())
			return _internal->_skeletonScaffoldMarker.get();
		return nullptr;
	}
	void ModelRendererConstruction::SetOperationContext(std::shared_ptr<::Assets::OperationContext> opContext)
	{
		_internal->_opContext = std::move(opContext);
	}

	template<typename Marker, typename Time>
		static bool MarkerTimesOut(Marker& marker, Time timeoutTime)
		{
			return marker.wait_until(timeoutTime) == std::future_status::timeout;
		}

	void ModelRendererConstruction::FulfillWhenNotPending(std::promise<std::shared_ptr<ModelRendererConstruction>>&& promise)
	{
		assert(_internal);
		_internal->_sealed = true;

		auto strongThis = shared_from_this();
		assert(strongThis);
		::Assets::PollToPromise(
			std::move(promise),
			[strongThis](auto timeout) {
				// wait until all pending scaffold markers are finished
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:strongThis->_internal->_modelScaffoldMarkers)
					if (MarkerTimesOut(f.second, timeoutTime))
						return ::Assets::PollStatus::Continue;

				for (auto& f:strongThis->_internal->_materialScaffoldMarkers)
					if (MarkerTimesOut(f.second, timeoutTime))
						return ::Assets::PollStatus::Continue;

				if (	strongThis->_internal->_skeletonScaffoldMarker.valid()
					&& 	MarkerTimesOut(strongThis->_internal->_skeletonScaffoldMarker, timeoutTime))
					return ::Assets::PollStatus::Continue;

				return ::Assets::PollStatus::Finish;
			},
			[strongThis]() mutable {
				assert(strongThis->GetAssetState() != ::Assets::AssetState::Pending);
				// query futures to propagate exceptions
				for (auto& f:strongThis->_internal->_modelScaffoldMarkers) {
					TRY {
						f.second.get();
					} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
						auto i = LowerBound(strongThis->_internal->_modelScaffoldInitializers, f.first);
						if (i != strongThis->_internal->_modelScaffoldInitializers.end() && i->first == f.first)
							Throw(::Assets::Exceptions::InvalidAsset(i->second, e.GetDependencyValidation(), ::Assets::AsBlob(i->second + ": " + e.what())));
					} CATCH (const std::exception& e) {
						auto i = LowerBound(strongThis->_internal->_modelScaffoldInitializers, f.first);
						if (i != strongThis->_internal->_modelScaffoldInitializers.end() && i->first == f.first)
							Throw(::Assets::Exceptions::InvalidAsset(i->second, {}, ::Assets::AsBlob(i->second + ": " + e.what())));
					} CATCH_END
				}
				for (auto& f:strongThis->_internal->_materialScaffoldMarkers) {
					TRY {
						f.second.get();
					} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
						auto i = LowerBound(strongThis->_internal->_materialScaffoldInitializers, f.first);
						if (i != strongThis->_internal->_materialScaffoldInitializers.end() && i->first == f.first)
							Throw(::Assets::Exceptions::InvalidAsset(i->second, e.GetDependencyValidation(), ::Assets::AsBlob(i->second + ": " + e.what())));
					} CATCH (const std::exception& e) {
						auto i = LowerBound(strongThis->_internal->_materialScaffoldInitializers, f.first);
						if (i != strongThis->_internal->_materialScaffoldInitializers.end() && i->first == f.first)
							Throw(::Assets::Exceptions::InvalidAsset(i->second, {}, ::Assets::AsBlob(i->second + ": " + e.what())));
					} CATCH_END
				}
				if (strongThis->_internal->_skeletonScaffoldMarker.valid()) strongThis->_internal->_skeletonScaffoldMarker.get();
				return std::move(strongThis);
			});
	}

	::Assets::AssetState ModelRendererConstruction::GetAssetState() const
	{
		assert(_internal);
		_internal->_sealed = true;
		
		bool hasPending = false;
		for (auto& f:_internal->_modelScaffoldMarkers) {
			auto state = f.second.wait_for(std::chrono::seconds(0));
			if (state == std::future_status::ready) {
				// only way to check for invalid assets, unfortunately. not super efficient!
				TRY { f.second.get();
				} CATCH(...) {
					return ::Assets::AssetState::Invalid;
				} CATCH_END
			}
			hasPending |= state == std::future_status::timeout;
		}
		if (_internal->_skeletonScaffoldMarker.valid()) {
			auto state = _internal->_skeletonScaffoldMarker.wait_for(std::chrono::seconds(0));
			if (state == std::future_status::ready) {
				// only way to check for invalid assets, unfortunately. not super efficient!
				TRY { _internal->_skeletonScaffoldMarker.get();
				} CATCH(...) {
					return ::Assets::AssetState::Invalid;
				} CATCH_END
			}
			hasPending |= state == std::future_status::timeout;
		}
		return hasPending ? ::Assets::AssetState::Pending : ::Assets::AssetState::Ready;
	}

	template<typename Future>
		static bool FutureInvalidated(Future& f)
	{
		TRY {
			return f.valid() && ::Assets::Internal::GetDependencyValidation(f.get()).GetValidationIndex() != 0;
		} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
			return e.GetDependencyValidation().GetValidationIndex() != 0;
		} CATCH(...) {
		} CATCH_END
		return false;
	}

	bool ModelRendererConstruction::AreScaffoldsInvalidated() const
	{
		// expecting to have already waited on this construction -- because we're querying all of the futures here
		// also, this is an expensive function; avoid calling it frequently (probably just during construction operations)
		for (const auto& m:_internal->_modelScaffoldMarkers)
			if (FutureInvalidated(m.second))
				return true;
		for (const auto& m:_internal->_modelScaffoldPtrs)
			if (m.second && m.second->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		for (const auto& m:_internal->_materialScaffoldMarkers)
			if (FutureInvalidated(m.second))
				return true;
		for (const auto& m:_internal->_materialScaffoldPtrs)
			if (m.second && m.second->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		for (const auto& m:_internal->_compilationConfigurationMarkers)
			if (FutureInvalidated(m.second))
				return true;
		// _internal->_compilationConfigurationPtrs don't have depvals
		if (FutureInvalidated(_internal->_skeletonScaffoldMarker))
			return true;
		if (_internal->_skeletonScaffoldPtr && _internal->_skeletonScaffoldPtr->GetDependencyValidation().GetValidationIndex() != 0)
			return true;
		return false;
	}

	::Assets::DependencyValidation ModelRendererConstruction::MakeScaffoldsDependencyValidation() const
	{
		// don't call before FulfillWhenNotPending (or before waiting on that promise), because otherwise this will stall
		std::vector<::Assets::DependencyValidationMarker> markers;
		markers.reserve(2 + _internal->_modelScaffoldMarkers.size() + _internal->_modelScaffoldPtrs.size() + _internal->_materialScaffoldMarkers.size() + _internal->_materialScaffoldPtrs.size());
		for (const auto& m:_internal->_modelScaffoldMarkers) {
			assert(m.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
			TRY {
				markers.push_back(m.second.get()->GetDependencyValidation());
			} CATCH (::Assets::Exceptions::ExceptionWithDepVal& e) {
				markers.push_back(e.GetDependencyValidation());
			} CATCH(...) {
			} CATCH_END
		}
		for (const auto& m:_internal->_modelScaffoldPtrs)
			markers.push_back(m.second->GetDependencyValidation());
		for (const auto& m:_internal->_materialScaffoldMarkers) {
			assert(m.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
			TRY {
				markers.push_back(m.second.get()->GetDependencyValidation());
			} CATCH (::Assets::Exceptions::ExceptionWithDepVal& e) {
				markers.push_back(e.GetDependencyValidation());
			} CATCH(...) {
			} CATCH_END
		}
		for (const auto& m:_internal->_materialScaffoldPtrs)
			markers.push_back(m.second->GetDependencyValidation());
		for (const auto& m:_internal->_compilationConfigurationMarkers) {
			assert(m.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
			TRY {
				markers.push_back(std::get<::Assets::DependencyValidation>(m.second.get()));
			} CATCH (::Assets::Exceptions::ExceptionWithDepVal& e) {
				markers.push_back(e.GetDependencyValidation());
			} CATCH(...) {
			} CATCH_END
		}
		// _internal->_compilationConfigurationPtrs don't have depvals
		// also can't get depVals from _internal->_materialScaffoldConstructionPtrs until they've been converted into _materialScaffoldMarkers

		if (_internal->_skeletonScaffoldMarker.valid()) {
			assert(_internal->_skeletonScaffoldMarker.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
			TRY {
				markers.push_back(_internal->_skeletonScaffoldMarker.get()->GetDependencyValidation());
			} CATCH (::Assets::Exceptions::ExceptionWithDepVal& e) {
				markers.push_back(e.GetDependencyValidation());
			} CATCH(...) {
			} CATCH_END
		}
		
		if (_internal->_skeletonScaffoldPtr)
			markers.push_back(_internal->_skeletonScaffoldPtr->GetDependencyValidation());
		
		return ::Assets::GetDepValSys().MakeOrReuse(markers);
	}

	std::shared_ptr<ModelRendererConstruction> ModelRendererConstruction::Reconstruct(const ModelRendererConstruction& src, std::shared_ptr<::Assets::OperationContext> opContext)
	{
		// rebuild the construction, querying all resources again, incase they need hot reloading
		auto result = std::make_shared<ModelRendererConstruction>();

		// skeleton
		result->_internal->_skeletonScaffoldPtr = src._internal->_skeletonScaffoldPtr;
		result->_internal->_skeletonScaffoldInitializer = src._internal->_skeletonScaffoldInitializer;
		result->_internal->_skeletonScaffoldHashValue = src._internal->_skeletonScaffoldHashValue;
		if (src._internal->_skeletonScaffoldMarker.valid() && !src._internal->_skeletonScaffoldInitializer.empty())
			result->_internal->_skeletonScaffoldMarker = ::Assets::GetAssetFuture<std::shared_ptr<Assets::SkeletonScaffold>>(std::move(opContext), result->_internal->_skeletonScaffoldInitializer);

		// model & material markers
		result->_internal->_modelScaffoldPtrs = src._internal->_modelScaffoldPtrs;
		result->_internal->_materialScaffoldPtrs = src._internal->_materialScaffoldPtrs;
		result->_internal->_materialScaffoldConstructionPtrs = src._internal->_materialScaffoldConstructionPtrs;
		result->_internal->_compilationConfigurationPtrs = src._internal->_compilationConfigurationPtrs;
		result->_internal->_modelScaffoldInitializers = src._internal->_modelScaffoldInitializers;
		result->_internal->_materialScaffoldInitializers = src._internal->_materialScaffoldInitializers;
		result->_internal->_compilationConfigurationInitializers = src._internal->_compilationConfigurationInitializers;

		for (unsigned eleIdx=0; eleIdx<src._internal->_elementCount; ++eleIdx) {
			auto modelName = LowerBound(src._internal->_modelScaffoldInitializers, eleIdx);
			auto modelMarker = LowerBound(src._internal->_modelScaffoldMarkers, eleIdx);
			auto materialName = LowerBound(src._internal->_materialScaffoldInitializers, eleIdx);
			auto materialMarker = LowerBound(src._internal->_materialScaffoldMarkers, eleIdx);
			auto materialConstruction = LowerBound(src._internal->_materialScaffoldConstructionPtrs, eleIdx);
			auto cfgName = LowerBound(src._internal->_compilationConfigurationInitializers, eleIdx);
			auto cfgMarker = LowerBound(src._internal->_compilationConfigurationMarkers, eleIdx);
			auto cfgPtr = LowerBound(src._internal->_compilationConfigurationPtrs, eleIdx);

			std::shared_future<ResolvedMCC> futureModelCompilationConfiguration;
			std::shared_ptr<Assets::ModelCompilationConfiguration> modelCompilationConfiguration;
			if (cfgMarker != src._internal->_compilationConfigurationMarkers.end() && cfgMarker->first == eleIdx && cfgMarker->second.valid()) {
				if (cfgName != src._internal->_compilationConfigurationInitializers.end() && cfgName->first == eleIdx) {
					futureModelCompilationConfiguration = GetFutureResolvedMCC(cfgName->second);
					result->_internal->_compilationConfigurationMarkers.emplace_back(eleIdx, futureModelCompilationConfiguration);
				} else {
					futureModelCompilationConfiguration = cfgMarker->second;
					result->_internal->_compilationConfigurationMarkers.emplace_back(*cfgMarker);
				}
			} else if (cfgPtr != src._internal->_compilationConfigurationPtrs.end() && cfgPtr->first == eleIdx)
				modelCompilationConfiguration = cfgPtr->second;

			if (modelMarker != src._internal->_modelScaffoldMarkers.end() && modelMarker->first == eleIdx && modelMarker->second.valid()) {
				if (modelName != src._internal->_modelScaffoldInitializers.end() && modelName->first == eleIdx) {
					if (futureModelCompilationConfiguration.valid()) {
						if (opContext) result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, CreateModelScaffoldFuture(opContext, modelName->second, futureModelCompilationConfiguration));
						else result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, CreateModelScaffoldFuture(modelName->second, futureModelCompilationConfiguration));
					} else if (modelCompilationConfiguration) {
						if (opContext) result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, CreateModelScaffoldFuture(opContext, modelName->second, modelCompilationConfiguration));
						else result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, CreateModelScaffoldFuture(modelName->second, modelCompilationConfiguration));
					} else {
						if (opContext) result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, ::Assets::GetAssetFuture<Internal::ModelScaffoldPtr>(opContext, modelName->second));
						else result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, ::Assets::GetAssetFuture<Internal::ModelScaffoldPtr>(modelName->second));
					}
				} else {
					result->_internal->_modelScaffoldMarkers.emplace_back(*modelMarker);
				}
			}

			bool foundMaterial = false;
			if (materialConstruction != src._internal->_materialScaffoldConstructionPtrs.end() && materialConstruction->first == eleIdx) {
				if (modelName != src._internal->_modelScaffoldInitializers.end() && modelName->first == eleIdx) {
					std::promise<std::shared_ptr<CompiledMaterialSet>> promisedScaffold;
					auto futureScaffold = promisedScaffold.get_future();

					auto modelStr = modelName->second;
					if (futureModelCompilationConfiguration.valid()) {
						ConstructMaterialSet(std::move(promisedScaffold), materialConstruction->second);
					} else if (modelCompilationConfiguration) {
						ConstructMaterialSet(std::move(promisedScaffold), materialConstruction->second);
					} else {
						ConstructMaterialSet(std::move(promisedScaffold), materialConstruction->second);
					}

					result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, std::move(futureScaffold));
					foundMaterial = true;
				}
			}
			
			if (!foundMaterial && materialName != src._internal->_materialScaffoldInitializers.end() && materialName->first == eleIdx) {
				if (modelName != src._internal->_modelScaffoldInitializers.end() && modelName->first == eleIdx) {
					auto finalMaterialName = materialName->second.empty() ? modelName->second : materialName->second;
					if (futureModelCompilationConfiguration.valid()) {
						if (opContext) result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, CreateMaterialScaffoldFuture(opContext, finalMaterialName, modelName->second, futureModelCompilationConfiguration));
						else result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, CreateMaterialScaffoldFuture(finalMaterialName, modelName->second, futureModelCompilationConfiguration));
					} else if (modelCompilationConfiguration) {
						if (opContext) result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, CreateMaterialScaffoldFuture(opContext, finalMaterialName, modelName->second, modelCompilationConfiguration));
						else result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, CreateMaterialScaffoldFuture(finalMaterialName, modelName->second, modelCompilationConfiguration));
					} else {
						if (opContext) result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::GetAssetFuture<Internal::MaterialScaffoldPtr>(opContext, finalMaterialName, modelName->second));
						else result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::GetAssetFuture<Internal::MaterialScaffoldPtr>(finalMaterialName, modelName->second));
					}
					foundMaterial = true;
				}
			}

			if (!foundMaterial && materialMarker != src._internal->_materialScaffoldMarkers.end() && materialMarker->first == eleIdx && materialMarker->second.valid())
				result->_internal->_materialScaffoldMarkers.emplace_back(*materialMarker);		// just have to reuse what we had before, because it can't be recreated
		}

		// etc
		result->_internal->_elementCount = src._internal->_elementCount;
		result->_internal->_hash = src._internal->_hash;
		result->_internal->_disableHash = src._internal->_disableHash;
		result->_internal->_names = src._internal->_names;
		result->_internal->_sealed = false;

		return result;
	}

	auto ModelRendererConstruction::AddElement() -> ElementConstructor
	{
		assert(_internal && !_internal->_sealed);
		auto result = ElementConstructor{_internal->_elementCount, *_internal.get()};
		++_internal->_elementCount;
		return result;
	}

	auto ModelRendererConstruction::begin() const -> ElementIterator
	{
		ElementIterator result;
		result._value._msmi = _internal->_modelScaffoldMarkers.begin();
		result._value._mspi = _internal->_modelScaffoldPtrs.begin();
		result._value._matsmi = _internal->_materialScaffoldMarkers.begin();
		result._value._matspi = _internal->_materialScaffoldPtrs.begin();
		result._value._matscpi = _internal->_materialScaffoldConstructionPtrs.begin();
		result._value._ccmi = _internal->_compilationConfigurationMarkers.begin();
		result._value._ccpi = _internal->_compilationConfigurationPtrs.begin();
		result._value._etoi = _internal->_elementToObjects.begin();
		result._value._dbpi = _internal->_deformerBindPoints.begin();
		result._value._ni = _internal->_names.begin();
		result._value._elementId = 0;
		result._value._internal = _internal.get();
		return result;
	}

	auto ModelRendererConstruction::end() const -> ElementIterator
	{
		ElementIterator result;
		result._value._msmi = _internal->_modelScaffoldMarkers.end();
		result._value._mspi = _internal->_modelScaffoldPtrs.end();
		result._value._matsmi = _internal->_materialScaffoldMarkers.end();
		result._value._matspi = _internal->_materialScaffoldPtrs.end();
		result._value._matscpi = _internal->_materialScaffoldConstructionPtrs.end();
		result._value._ccmi = _internal->_compilationConfigurationMarkers.end();
		result._value._ccpi = _internal->_compilationConfigurationPtrs.end();
		result._value._etoi = _internal->_elementToObjects.begin();
		result._value._dbpi = _internal->_deformerBindPoints.begin();
		result._value._ni = _internal->_names.end();
		result._value._elementId = _internal->_elementCount;
		result._value._internal = _internal.get();
		return result;
	}

	auto ModelRendererConstruction::GetElement(unsigned idx) const -> ElementIterator
	{
		assert(idx < _internal->_elementCount);
		auto result = begin();
		result._value._elementId = idx;
		if (idx != 0) result.UpdateElementIdx();		// advances iterators to find the right element idx
		return result;
	}

	unsigned ModelRendererConstruction::GetElementCount() const
	{
		return _internal->_elementCount;
	}

	uint64_t ModelRendererConstruction::GetHash() const
	{
		if (_internal->_disableHash)
			Throw(std::runtime_error("Attempting to generate a hash for a ModelRendererConstruction that cannot be hashed"));
		if (!_internal->_hash) {
			// collate the resource names, ensuring that we're careful about the sparse data structure
			auto ii0 = MakeIteratorRange(_internal->_modelScaffoldInitializers), ii1 = MakeIteratorRange(_internal->_materialScaffoldInitializers), ii2 = MakeIteratorRange(_internal->_compilationConfigurationInitializers);
			auto ii3 = MakeIteratorRange(_internal->_materialScaffoldConstructionPtrs);
			for (unsigned e=0; e<_internal->_elementCount; ++e) {
				while (!ii0.empty() && ii0.first->first < e) ++ii0.first;
				while (!ii1.empty() && ii1.first->first < e) ++ii1.first;
				while (!ii2.empty() && ii2.first->first < e) ++ii2.first;
				while (!ii3.empty() && ii3.first->first < e) ++ii3.first;
				if (!ii0.empty() && ii0.first->first == e) _internal->_hash = Hash64(ii0.first->second, _internal->_hash); else _internal->_hash = HashCombine(0, _internal->_hash);
				if (!ii1.empty() && ii1.first->first == e) _internal->_hash = Hash64(ii1.first->second, _internal->_hash); else _internal->_hash = HashCombine(0, _internal->_hash);
				if (!ii2.empty() && ii2.first->first == e) _internal->_hash = Hash64(ii2.first->second, _internal->_hash); else _internal->_hash = HashCombine(0, _internal->_hash);
				if (!ii3.empty() && ii3.first->first == e) _internal->_hash = HashCombine(ii3.first->second->GetHash(), _internal->_hash); else _internal->_hash = HashCombine(0, _internal->_hash);
			}
			_internal->_hash = Hash64(AsPointer(_internal->_elementToObjects.begin()), AsPointer(_internal->_elementToObjects.end()), _internal->_hash);
			_internal->_hash = Hash64(AsPointer(_internal->_deformerBindPoints.begin()), AsPointer(_internal->_deformerBindPoints.end()), _internal->_hash);
			_internal->_hash = _internal->_skeletonScaffoldHashValue ? HashCombine(_internal->_hash, _internal->_skeletonScaffoldHashValue) : _internal->_hash;
		}

		return _internal->_hash;
	}

	bool ModelRendererConstruction::CanBeHashed() const
	{
		return !_internal->_disableHash;
	}

	ModelRendererConstruction::ModelRendererConstruction()
	{
		_internal = std::make_unique<Internal>();
	}
	ModelRendererConstruction::~ModelRendererConstruction() {}

	ModelRendererConstruction::ElementIterator& ModelRendererConstruction::ElementIterator::operator++()
	{
		++_value._elementId;
		UpdateElementIdx();
		return *this;
	}

	void ModelRendererConstruction::ElementIterator::UpdateElementIdx()
	{
		assert(_value._internal);
		assert(_value._elementId <= _value._internal->_elementCount);
		auto e = _value._elementId;
		while (_value._msmi!=_value._internal->_modelScaffoldMarkers.end() && _value._msmi->first < e) ++_value._msmi;
		while (_value._mspi!=_value._internal->_modelScaffoldPtrs.end() && _value._mspi->first < e) ++_value._mspi;
		while (_value._matsmi!=_value._internal->_materialScaffoldMarkers.end() && _value._matsmi->first < e) ++_value._matsmi;
		while (_value._matspi!=_value._internal->_materialScaffoldPtrs.end() && _value._matspi->first < e) ++_value._matspi;
		while (_value._matscpi!=_value._internal->_materialScaffoldConstructionPtrs.end() && _value._matscpi->first < e) ++_value._matscpi;
		while (_value._ccmi!=_value._internal->_compilationConfigurationMarkers.end() && _value._ccmi->first < e) ++_value._ccmi;
		while (_value._ccpi!=_value._internal->_compilationConfigurationPtrs.end() && _value._ccpi->first < e) ++_value._ccpi;
		while (_value._etoi!=_value._internal->_elementToObjects.end() && _value._etoi->first < e) ++_value._etoi;
		while (_value._dbpi!=_value._internal->_deformerBindPoints.end() && _value._dbpi->first < e) ++_value._dbpi;
		while (_value._ni!=_value._internal->_names.end() && _value._ni->first < e) ++_value._ni;
	}

	ModelRendererConstruction::ElementIterator::ElementIterator() = default;

	std::shared_ptr<Assets::ModelScaffold> ModelRendererConstruction::ElementIterator::Value::GetModelScaffold() const
	{
		assert(_internal);
		if (_mspi!=_internal->_modelScaffoldPtrs.end() && _mspi->first == _elementId)
			return _mspi->second;
		if (_msmi!=_internal->_modelScaffoldMarkers.end() && _msmi->first == _elementId) {
			assert(_msmi->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);		// we should be ready, via ModelRendererConstruction::FulfillWhenNotPending before getting here
			return _msmi->second.get();
		}
		return nullptr;
	}

	std::shared_ptr<Assets::CompiledMaterialSet> ModelRendererConstruction::ElementIterator::Value::GetMaterialScaffold() const
	{
		assert(_internal);
		if (_matspi!=_internal->_materialScaffoldPtrs.end() && _matspi->first == _elementId)
			return _matspi->second;
		if (_matsmi!=_internal->_materialScaffoldMarkers.end() && _matsmi->first == _elementId) {
			assert(_matsmi->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);		// we should be ready, via ModelRendererConstruction::FulfillWhenNotPending before getting here
			return _matsmi->second.get();
		}
		return nullptr;
	}

	std::shared_ptr<Assets::MaterialSetConstruction> ModelRendererConstruction::ElementIterator::Value::GetMaterialScaffoldConstruction() const
	{
		assert(_internal);
		if (_matscpi!=_internal->_materialScaffoldConstructionPtrs.end() && _matscpi->first == _elementId)
			return _matscpi->second;
		return nullptr;
	}

	std::shared_ptr<Assets::ModelCompilationConfiguration> ModelRendererConstruction::ElementIterator::Value::GetCompilationConfiguration() const
	{
		assert(_internal);
		if (_ccpi!=_internal->_compilationConfigurationPtrs.end() && _ccpi->first == _elementId)
			return _ccpi->second;
		if (_ccmi!=_internal->_compilationConfigurationMarkers.end() && _ccmi->first == _elementId) {
			assert(_ccmi->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);		// we should be ready, via ModelRendererConstruction::FulfillWhenNotPending before getting here
			return std::get<0>(_ccmi->second.get());
		}
		return nullptr;
	}

	std::optional<Float4x4> ModelRendererConstruction::ElementIterator::Value::GetElementToObject() const
	{
		if (_etoi!=_internal->_elementToObjects.end() && _etoi->first == _elementId)
			return _etoi->second;
		return {};
	}

	std::optional<uint64_t> ModelRendererConstruction::ElementIterator::Value::GetDeformerBindPoint() const
	{
		if (_dbpi!=_internal->_deformerBindPoints.end() && _dbpi->first == _elementId)
			return _dbpi->second;
		return {};
	}

	std::string ModelRendererConstruction::ElementIterator::Value::GetModelScaffoldName() const
	{
		auto ii = LowerBound(_internal->_modelScaffoldInitializers, _elementId);
		if (ii != _internal->_modelScaffoldInitializers.end() && ii->first == _elementId) return ii->second;
		return {};
	}

	std::string ModelRendererConstruction::ElementIterator::Value::GetMaterialScaffoldName() const
	{
		auto ii = LowerBound(_internal->_materialScaffoldInitializers, _elementId);
		if (ii != _internal->_materialScaffoldInitializers.end() && ii->first == _elementId) return ii->second;
		return {};
	}

	std::string ModelRendererConstruction::ElementIterator::Value::GetCompilationConfigurationName() const
	{
		auto ii = LowerBound(_internal->_compilationConfigurationInitializers, _elementId);
		if (ii != _internal->_compilationConfigurationInitializers.end() && ii->first == _elementId) return ii->second;
		return {};
	}

	std::string ModelRendererConstruction::ElementIterator::Value::GetElementName() const
	{
		if (_ni != _internal->_names.end() && _ni->first == _elementId)
			return _ni->second;
		return {};
	}
	
	ModelRendererConstruction::ElementIterator::Value::Value() = default;

	std::future<std::shared_ptr<Assets::ModelRendererConstruction>> ToFuture(Assets::ModelRendererConstruction& construction)
	{
		std::promise<std::shared_ptr<Assets::ModelRendererConstruction>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

}}
