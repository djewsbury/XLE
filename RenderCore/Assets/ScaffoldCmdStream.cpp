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
	auto RendererConstruction::ElementConstructor::SetModelScaffold(StringSection<> initializer) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(initializer));
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetMaterialScaffold(StringSection<> initializer) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(initializer));
		return *this; 
	}
	auto RendererConstruction::ElementConstructor::SetModelScaffold(const ::Assets::PtrToMarkerPtr<ModelScaffoldCmdStreamForm>& scaffoldMarker) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_modelScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<MaterialScaffoldCmdStreamForm>& scaffoldMarker) -> ElementConstructor&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldMarkers, _elementId);
		if (i != _internal->_materialScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_materialScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		return *this;
	}
	auto RendererConstruction::ElementConstructor::SetModelScaffold(const std::shared_ptr<ModelScaffoldCmdStreamForm>& scaffoldPtr) -> ElementConstructor& 
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_modelScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		return *this; 
	}
	auto RendererConstruction::ElementConstructor::SetMaterialScaffold(const std::shared_ptr<MaterialScaffoldCmdStreamForm>& scaffoldPtr) -> ElementConstructor&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldPtrs, _elementId);
		if (i != _internal->_materialScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_materialScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		return *this; 
	}

	auto RendererConstruction::ElementConstructor::AddMorphTarget(uint64_t targetName, StringSection<> srcFile) -> ElementConstructor& { return *this; }

	auto RendererConstruction::ElementConstructor::SetRootTransform(const Float4x4&) -> ElementConstructor& { return *this; }

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
				for (auto& f:strongThis->_internal->_modelScaffoldMarkers) {
					auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
					if (remainingTime.count() <= 0) return ::Assets::PollStatus::Continue;
					auto t = f.second->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
					if (t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
						return ::Assets::PollStatus::Continue;
				}
				return ::Assets::PollStatus::Finish;
			},
			[strongThis]() {
				assert(strongThis->GetAssetState() != ::Assets::AssetState::Pending);
				return strongThis;
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

	std::shared_ptr<ModelScaffoldCmdStreamForm> RendererConstruction::ElementIterator::Value::GetModelScaffold() const
	{
		assert(_internal);
		if (_mspi!=_internal->_modelScaffoldPtrs.end() && _mspi->first == _elementId)
			return _mspi->second;
		if (_msmi!=_internal->_modelScaffoldMarkers.end() && _msmi->first == _elementId)
			return _msmi->second->Actualize();
		return nullptr;
	}

	std::shared_ptr<MaterialScaffoldCmdStreamForm> RendererConstruction::ElementIterator::Value::GetMaterialScaffold() const
	{
		assert(_internal);
		if (_matspi!=_internal->_materialScaffoldPtrs.end() && _matspi->first == _elementId)
			return _matspi->second;
		if (_matsmi!=_internal->_materialScaffoldMarkers.end() && _matsmi->first == _elementId)
			return _matsmi->second->Actualize();
		return nullptr;
	}
	
	RendererConstruction::ElementIterator::Value::Value() = default;

}}
