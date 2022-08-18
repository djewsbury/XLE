// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelRendererConstruction.h"
#include "ModelMachine.h"
#include "AssetUtils.h"
#include "ModelScaffold.h"
#include "MaterialScaffold.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"

namespace RenderCore { namespace Assets
{
	class ModelRendererConstruction::Internal
	{
	public:
		using ElementId = unsigned;
		using ModelScaffoldMarker = std::shared_future<std::shared_ptr<Assets::ModelScaffold>>;
		using ModelScaffoldPtr = std::shared_ptr<Assets::ModelScaffold>;
		using MaterialScaffoldMarker = std::shared_future<std::shared_ptr<Assets::MaterialScaffold>>;
		using MaterialScaffoldPtr = std::shared_ptr<Assets::MaterialScaffold>;

		std::vector<std::pair<ElementId, ModelScaffoldMarker>> _modelScaffoldMarkers;
		std::vector<std::pair<ElementId, ModelScaffoldPtr>> _modelScaffoldPtrs;
		std::vector<std::pair<ElementId, MaterialScaffoldMarker>> _materialScaffoldMarkers;
		std::vector<std::pair<ElementId, MaterialScaffoldPtr>> _materialScaffoldPtrs;
		std::vector<std::pair<ElementId, Float4x4>> _elementToObjects;
		std::vector<std::pair<ElementId, uint64_t>> _deformerBindPoints;
		std::vector<std::pair<ElementId, std::string>> _names;
		std::vector<std::pair<ElementId, std::string>> _modelScaffoldInitializers;
		std::vector<std::pair<ElementId, std::string>> _materialScaffoldInitializers;
		unsigned _elementCount = 0;

		std::shared_future<std::shared_ptr<Assets::SkeletonScaffold>> _skeletonScaffoldMarker;
		std::shared_ptr<Assets::SkeletonScaffold> _skeletonScaffoldPtr;
		std::string _skeletonScaffoldInitializer;
		uint64_t _skeletonScaffoldHashValue = 0u;

		bool _sealed = false;

		std::vector<uint64_t> _elementHashValues;
		mutable uint64_t _hash = 0ull;
		bool _disableHash = false;
	};

	auto ModelRendererConstruction::ElementConstructor::SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto originalDisableHash = _internal->_disableHash;
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(model), model.AsString());
		if (!material.IsEmpty()) {
			SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(material, model), material.AsString());
		} else
			SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(model, model), {});
		_internal->_disableHash = originalDisableHash;
		if (_internal->_elementHashValues.size() < _internal->_elementCount)
			_internal->_elementHashValues.resize(_internal->_elementCount, 0);
		_internal->_elementHashValues[_elementId] = Hash64(model, Hash64(material));
		_internal->_hash = 0;
		return *this;
	}
	auto ModelRendererConstruction::ElementConstructor::SetModelAndMaterialScaffolds(std::shared_ptr<::Assets::OperationContext> opContext, StringSection<> model, StringSection<> material) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto originalDisableHash = _internal->_disableHash;
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(opContext, model), model.AsString());
		if (!material.IsEmpty()) {
			SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(std::move(opContext), material, model), material.AsString());
		} else
			SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(std::move(opContext), model, model), {});
		_internal->_disableHash = originalDisableHash;
		if (_internal->_elementHashValues.size() < _internal->_elementCount)
			_internal->_elementHashValues.resize(_internal->_elementCount, 0);
		_internal->_elementHashValues[_elementId] = Hash64(model, Hash64(material));
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
	auto ModelRendererConstruction::ElementConstructor::SetMaterialScaffold(std::shared_future<std::shared_ptr<Assets::MaterialScaffold>> scaffoldMarker, std::string initializer) -> ElementConstructor&
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
	auto ModelRendererConstruction::ElementConstructor::SetModelScaffold(const std::shared_ptr<Assets::ModelScaffold>& scaffoldPtr, std::string initializer) -> ElementConstructor& 
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) i->second = scaffoldPtr;
		else _internal->_modelScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_modelScaffoldInitializers, _elementId);
			if (ii != _internal->_modelScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_modelScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
		}

		_internal->_disableHash = true;
		return *this; 
	}
	auto ModelRendererConstruction::ElementConstructor::SetMaterialScaffold(const std::shared_ptr<Assets::MaterialScaffold>& scaffoldPtr, std::string initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldPtrs, _elementId);
		if (i != _internal->_materialScaffoldPtrs.end() && i->first == _elementId) i->second = scaffoldPtr;
		else _internal->_materialScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});

		if (!initializer.empty()) {
			auto ii = LowerBound(_internal->_materialScaffoldInitializers, _elementId);
			if (ii != _internal->_materialScaffoldInitializers.end() && ii->first == _elementId) ii->second = std::move(initializer);
			else _internal->_materialScaffoldInitializers.insert(ii, {_elementId, std::move(initializer)});
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

	void ModelRendererConstruction::SetSkeletonScaffold(StringSection<> skeleton)
	{
		_internal->_skeletonScaffoldHashValue = Hash64(skeleton);
		_internal->_skeletonScaffoldPtr = nullptr;
		_internal->_skeletonScaffoldMarker = ::Assets::MakeAsset<std::shared_ptr<Assets::SkeletonScaffold>>(skeleton);
	}
	void ModelRendererConstruction::SetSkeletonScaffold(std::shared_ptr<::Assets::OperationContext> opContext, StringSection<> skeleton)
	{
		_internal->_skeletonScaffoldHashValue = Hash64(skeleton);
		_internal->_skeletonScaffoldPtr = nullptr;
		_internal->_skeletonScaffoldMarker = ::Assets::MakeAsset<std::shared_ptr<Assets::SkeletonScaffold>>(std::move(opContext), skeleton);
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
				for (auto& f:strongThis->_internal->_modelScaffoldMarkers) f.second.get();
				for (auto& f:strongThis->_internal->_materialScaffoldMarkers) f.second.get();
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

	bool ModelRendererConstruction::IsInvalidated() const
	{
		// expecting to have already waited on this construction -- because we're querying all of the futures here
		for (const auto& m:_internal->_modelScaffoldMarkers)
			if (m.second.valid() && m.second.get()->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		for (const auto& m:_internal->_modelScaffoldPtrs)
			if (m.second && m.second->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		for (const auto& m:_internal->_materialScaffoldMarkers)
			if (m.second.valid() && m.second.get()->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		for (const auto& m:_internal->_materialScaffoldPtrs)
			if (m.second && m.second->GetDependencyValidation().GetValidationIndex() != 0)
				return true;
		if (_internal->_skeletonScaffoldMarker.valid() && _internal->_skeletonScaffoldMarker.get()->GetDependencyValidation().GetValidationIndex() != 0)
			return true;
		if (_internal->_skeletonScaffoldPtr && _internal->_skeletonScaffoldPtr->GetDependencyValidation().GetValidationIndex() != 0)
			return true;
		return false;
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
			result->_internal->_skeletonScaffoldMarker = ::Assets::MakeAsset<std::shared_ptr<Assets::SkeletonScaffold>>(std::move(opContext), result->_internal->_skeletonScaffoldInitializer);

		// model & material markers
		result->_internal->_modelScaffoldPtrs = src._internal->_modelScaffoldPtrs;
		result->_internal->_materialScaffoldPtrs = src._internal->_materialScaffoldPtrs;
		result->_internal->_modelScaffoldInitializers = src._internal->_modelScaffoldInitializers;
		result->_internal->_materialScaffoldInitializers = src._internal->_materialScaffoldInitializers;

		for (unsigned eleIdx=0; eleIdx<src._internal->_elementCount; ++eleIdx) {
			auto modelName = LowerBound(src._internal->_modelScaffoldInitializers, eleIdx);
			auto modelMarker = LowerBound(src._internal->_modelScaffoldMarkers, eleIdx);
			auto materialName = LowerBound(src._internal->_materialScaffoldInitializers, eleIdx);
			auto materialMarker = LowerBound(src._internal->_materialScaffoldMarkers, eleIdx);

			if (modelMarker != src._internal->_modelScaffoldMarkers.end() && modelMarker->first == eleIdx && modelMarker->second.valid()) {
				if (modelName != src._internal->_modelScaffoldInitializers.end() && modelName->first == eleIdx) {
					if (opContext) result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::ModelScaffoldPtr>(opContext, modelName->second));
					else result->_internal->_modelScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::ModelScaffoldPtr>(modelName->second));
				} else {
					result->_internal->_modelScaffoldMarkers.emplace_back(*modelMarker);
				}
			}

			if (materialMarker != src._internal->_materialScaffoldMarkers.end() && materialMarker->first == eleIdx && materialMarker->second.valid()) {
				if (modelName != src._internal->_modelScaffoldInitializers.end() && modelName->first == eleIdx) {
					if (materialName != src._internal->_materialScaffoldInitializers.end() && materialName->first == eleIdx) {
						if (opContext) result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(opContext, materialName->second, modelName->second));
						else result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(materialName->second, modelName->second));
					} else {
						if (opContext) result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(opContext, modelName->second, modelName->second));
						else result->_internal->_materialScaffoldMarkers.emplace_back(eleIdx, ::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(modelName->second, modelName->second));
					}
				} else {
					result->_internal->_materialScaffoldMarkers.emplace_back(*materialMarker);
				}
			}
		}

		// etc
		result->_internal->_elementCount = src._internal->_elementCount;
		result->_internal->_elementHashValues = src._internal->_elementHashValues;
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
			_internal->_hash = Hash64(AsPointer(_internal->_elementHashValues.begin()), AsPointer(_internal->_elementHashValues.end()), _internal->_hash);
			_internal->_hash = Hash64(AsPointer(_internal->_elementToObjects.begin()), AsPointer(_internal->_elementToObjects.end()), _internal->_hash);
			_internal->_hash = Hash64(AsPointer(_internal->_deformerBindPoints.begin()), AsPointer(_internal->_deformerBindPoints.end()), _internal->_hash);
			_internal->_hash = _internal->_skeletonScaffoldHashValue ? HashCombine(_internal->_hash, _internal->_skeletonScaffoldHashValue) : _internal->_hash;
		}

		return _internal->_hash;
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

	std::shared_ptr<Assets::MaterialScaffold> ModelRendererConstruction::ElementIterator::Value::GetMaterialScaffold() const
	{
		assert(_internal);
		if (_matspi!=_internal->_materialScaffoldPtrs.end() && _matspi->first == _elementId)
			return _matspi->second;
		if (_matsmi!=_internal->_materialScaffoldMarkers.end() && _matsmi->first == _elementId) {
			assert(_msmi->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready);		// we should be ready, via ModelRendererConstruction::FulfillWhenNotPending before getting here
			return _matsmi->second.get();
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
