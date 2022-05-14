// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScaffoldCmdStream.h"
#include "ModelMachine.h"
#include "AssetUtils.h"
#include "ModelScaffold.h"
#include "MaterialScaffold.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"

namespace RenderCore { namespace Assets
{
	auto RendererConstruction::ElementConstructor::SetModelAndMaterialScaffolds(StringSection<> model, StringSection<> material) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto originalDisableHash = _internal->_disableHash;
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(model));
		SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(material, model));
		_internal->_disableHash = originalDisableHash;
		if (_internal->_elementHashValues.size() < _internal->_elementCount)
			_internal->_elementHashValues.resize(_internal->_elementCount, 0);
		_internal->_elementHashValues[_elementId] = Hash64(model, Hash64(material));
		_internal->_hash = 0;
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetModelScaffold(const ::Assets::PtrToMarkerPtr<ModelScaffold>& scaffoldMarker) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_modelScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		_internal->_disableHash = true;
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<MaterialScaffold>& scaffoldMarker) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldMarkers, _elementId);
		if (i != _internal->_materialScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_materialScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		_internal->_disableHash = true;
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetModelScaffold(const std::shared_ptr<ModelScaffold>& scaffoldPtr) -> ElementConstructor& 
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_modelScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		_internal->_disableHash = true;
		return *this; 
	}
	auto RendererConstruction::ElementConstructor::SetMaterialScaffold(const std::shared_ptr<MaterialScaffold>& scaffoldPtr) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldPtrs, _elementId);
		if (i != _internal->_materialScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_materialScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		_internal->_disableHash = true;
		return *this; 
	}

	auto RendererConstruction::ElementConstructor::SetRootTransform(const Float4x4&) -> ElementConstructor&
	{ 
		assert(0);
		return *this;
	}

	auto RendererConstruction::ElementConstructor::SetName(const std::string& name) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_names, _elementId);
		if (i != _internal->_names.end() && i->first == _elementId) {
			i->second = name;
		} else
			_internal->_names.insert(i, {_elementId, name});
		return *this; 
	}

	void RendererConstruction::SetSkeletonScaffold(StringSection<> skeleton)
	{
		_internal->_skeletonScaffoldHashValue = Hash64(skeleton);
		_internal->_skeletonScaffoldPtr = nullptr;
		_internal->_skeletonScaffoldMarker = ::Assets::MakeAsset<std::shared_ptr<SkeletonScaffold>>(skeleton);
	}
	void RendererConstruction::SetSkeletonScaffold(const ::Assets::PtrToMarkerPtr<SkeletonScaffold>& skeleton)
	{
		_internal->_disableHash = true;
		_internal->_skeletonScaffoldPtr = nullptr;
		_internal->_skeletonScaffoldMarker = skeleton;
	}
	void RendererConstruction::SetSkeletonScaffold(const std::shared_ptr<SkeletonScaffold>& skeleton)
	{
		_internal->_disableHash = true;
		_internal->_skeletonScaffoldPtr = skeleton;
		_internal->_skeletonScaffoldMarker = nullptr;
	}
	std::shared_ptr<SkeletonScaffold> RendererConstruction::GetSkeletonScaffold() const
	{
		if (_internal->_skeletonScaffoldPtr)
			return _internal->_skeletonScaffoldPtr;
		if (_internal->_skeletonScaffoldMarker)
			return _internal->_skeletonScaffoldMarker->ActualizeBkgrnd();
		return nullptr;
	}

	template<typename Marker, typename Time>
		static bool MarkerTimesOut(Marker& marker, Time timeoutTime)
		{
			auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
			if (remainingTime.count() <= 0) return true;
			auto t = marker.StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
			return t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending;
		}

	void RendererConstruction::FulfillWhenNotPending(std::promise<std::shared_ptr<RendererConstruction>>&& promise)
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
					if (MarkerTimesOut(*f.second, timeoutTime))
						return ::Assets::PollStatus::Continue;

				for (auto& f:strongThis->_internal->_materialScaffoldMarkers)
					if (MarkerTimesOut(*f.second, timeoutTime))
						return ::Assets::PollStatus::Continue;

				if (	strongThis->_internal->_skeletonScaffoldMarker
					&& 	MarkerTimesOut(*strongThis->_internal->_skeletonScaffoldMarker, timeoutTime))
					return ::Assets::PollStatus::Continue;

				return ::Assets::PollStatus::Finish;
			},
			[strongThis]() mutable {
				assert(strongThis->GetAssetState() != ::Assets::AssetState::Pending);
				return std::move(strongThis);
			});
	}

	::Assets::AssetState RendererConstruction::GetAssetState() const
	{
		assert(_internal);
		_internal->_sealed = true;
		
		bool hasPending = false;
		for (auto& f:_internal->_modelScaffoldMarkers) {
			auto state = f.second->GetAssetState();
			if (state == ::Assets::AssetState::Invalid)
				return ::Assets::AssetState::Invalid;
			hasPending |= state == ::Assets::AssetState::Pending;
		}
		return hasPending ? ::Assets::AssetState::Pending : ::Assets::AssetState::Ready;
	}

	auto RendererConstruction::AddElement() -> ElementConstructor
	{
		assert(_internal && !_internal->_sealed);
		auto result = ElementConstructor{_internal->_elementCount, *_internal.get()};
		++_internal->_elementCount;
		return result;
	}

	auto RendererConstruction::begin() const -> ElementIterator
	{
		ElementIterator result;
		result._value._msmi = _internal->_modelScaffoldMarkers.begin();
		result._value._mspi = _internal->_modelScaffoldPtrs.begin();
		result._value._matsmi = _internal->_materialScaffoldMarkers.begin();
		result._value._matspi = _internal->_materialScaffoldPtrs.begin();
		result._value._elementId = 0;
		result._value._internal = _internal.get();
		return result;
	}

	auto RendererConstruction::end() const -> ElementIterator
	{
		ElementIterator result;
		result._value._msmi = _internal->_modelScaffoldMarkers.end();
		result._value._mspi = _internal->_modelScaffoldPtrs.end();
		result._value._matsmi = _internal->_materialScaffoldMarkers.end();
		result._value._matspi = _internal->_materialScaffoldPtrs.end();
		result._value._elementId = _internal->_elementCount;
		result._value._internal = _internal.get();
		return result;
	}

	auto RendererConstruction::GetElement(unsigned idx) const -> ElementIterator
	{
		assert(idx < _internal->_elementCount);
		ElementIterator result;
		result._value._msmi = _internal->_modelScaffoldMarkers.begin() + idx;
		result._value._mspi = _internal->_modelScaffoldPtrs.begin() + idx;
		result._value._matsmi = _internal->_materialScaffoldMarkers.begin() + idx;
		result._value._matspi = _internal->_materialScaffoldPtrs.begin() + idx;
		result._value._elementId = idx;
		result._value._internal = _internal.get();
		return result;
	}

	unsigned RendererConstruction::GetElementCount() const
	{
		return _internal->_elementCount;
	}

	uint64_t RendererConstruction::GetHash() const
	{
		if (_internal->_disableHash)
			Throw("Attempting to generate a hash for a RendererConstruction that cannot be hashed");
		if (!_internal->_hash) {
			_internal->_hash = Hash64(AsPointer(_internal->_elementHashValues.begin()), AsPointer(_internal->_elementHashValues.begin()));
			_internal->_hash = _internal->_skeletonScaffoldHashValue ? HashCombine(_internal->_hash, _internal->_skeletonScaffoldHashValue) : _internal->_hash;
		}

		return _internal->_hash;
	}

	RendererConstruction::RendererConstruction()
	{
		_internal = std::make_unique<Internal>();
	}
	RendererConstruction::~RendererConstruction() {}

	RendererConstruction::ElementIterator& RendererConstruction::ElementIterator::operator++()
	{
		assert(_value._internal);
		++_value._elementId;
		assert(_value._elementId <= _value._internal->_elementCount);
		auto e = _value._elementId;
		while (_value._msmi!=_value._internal->_modelScaffoldMarkers.end() && _value._msmi->first < e) ++_value._msmi;
		while (_value._mspi!=_value._internal->_modelScaffoldPtrs.end() && _value._mspi->first < e) ++_value._mspi;
		while (_value._matsmi!=_value._internal->_materialScaffoldMarkers.end() && _value._matsmi->first < e) ++_value._matsmi;
		while (_value._matspi!=_value._internal->_materialScaffoldPtrs.end() && _value._matspi->first < e) ++_value._matspi;
		return *this;
	}

	RendererConstruction::ElementIterator::ElementIterator() = default;

	std::shared_ptr<ModelScaffold> RendererConstruction::ElementIterator::Value::GetModelScaffold() const
	{
		assert(_internal);
		if (_mspi!=_internal->_modelScaffoldPtrs.end() && _mspi->first == _elementId)
			return _mspi->second;
		if (_msmi!=_internal->_modelScaffoldMarkers.end() && _msmi->first == _elementId) {
			assert(!_msmi->second->IsBkgrndPending());		// we should be ready, via RendererConstruction::FulfillWhenNotPending before getting here
			return _msmi->second->ActualizeBkgrnd();
		}
		return nullptr;
	}

	std::shared_ptr<MaterialScaffold> RendererConstruction::ElementIterator::Value::GetMaterialScaffold() const
	{
		assert(_internal);
		if (_matspi!=_internal->_materialScaffoldPtrs.end() && _matspi->first == _elementId)
			return _matspi->second;
		if (_matsmi!=_internal->_materialScaffoldMarkers.end() && _matsmi->first == _elementId) {
			assert(!_msmi->second->IsBkgrndPending());		// we should be ready, via RendererConstruction::FulfillWhenNotPending before getting here
			return _matsmi->second->ActualizeBkgrnd();
		}
		return nullptr;
	}

	std::string RendererConstruction::ElementIterator::Value::GetModelScaffoldName() const
	{
		return {};
	}

	std::string RendererConstruction::ElementIterator::Value::GetMaterialScaffoldName() const
	{
		return {};
	}
	
	RendererConstruction::ElementIterator::Value::Value() = default;

}}
