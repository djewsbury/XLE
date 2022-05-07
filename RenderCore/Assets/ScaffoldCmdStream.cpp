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
	auto RendererConstruction::Element::SetModelScaffold(StringSection<> initializer) -> Element&
	{
		assert(_internal && !_internal->_sealed);
		SetModelScaffold(::Assets::MakeAsset<Internal::ModelScaffoldPtr>(initializer));
		return *this;
	}
	auto RendererConstruction::Element::SetMaterialScaffold(StringSection<> initializer) -> Element&
	{ 
		assert(_internal && !_internal->_sealed);
		SetMaterialScaffold(::Assets::MakeAsset<Internal::MaterialScaffoldPtr>(initializer));
		return *this; 
	}
	auto RendererConstruction::Element::SetModelScaffold(const ::Assets::PtrToMarkerPtr<ModelScaffoldCmdStreamForm>& scaffoldMarker) -> Element&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldMarkers, _elementId);
		if (i != _internal->_modelScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_modelScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		return *this;
	}
	auto RendererConstruction::Element::SetMaterialScaffold(const ::Assets::PtrToMarkerPtr<MaterialScaffoldCmdStreamForm>& scaffoldMarker) -> Element&
	{ 
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldMarkers, _elementId);
		if (i != _internal->_materialScaffoldMarkers.end() && i->first == _elementId) {
			i->second = scaffoldMarker;
		} else
			_internal->_materialScaffoldMarkers.insert(i, {_elementId, scaffoldMarker});
		return *this;
	}
	auto RendererConstruction::Element::SetModelScaffold(const std::shared_ptr<ModelScaffoldCmdStreamForm>& scaffoldPtr) -> Element& 
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_modelScaffoldPtrs, _elementId);
		if (i != _internal->_modelScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_modelScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		return *this; 
	}
	auto RendererConstruction::Element::SetMaterialScaffold(const std::shared_ptr<MaterialScaffoldCmdStreamForm>& scaffoldPtr) -> Element&
	{
		assert(_internal && !_internal->_sealed);
		auto i = LowerBound(_internal->_materialScaffoldPtrs, _elementId);
		if (i != _internal->_materialScaffoldPtrs.end() && i->first == _elementId) {
			i->second = scaffoldPtr;
		} else
			_internal->_materialScaffoldPtrs.insert(i, {_elementId, scaffoldPtr});
		return *this; 
	}

	auto RendererConstruction::Element::AddMorphTarget(uint64_t targetName, StringSection<> srcFile) -> Element& { return *this; }

	auto RendererConstruction::Element::SetRootTransform(const Float4x4&) -> Element& { return *this; }

	auto RendererConstruction::Element::SetName(const std::string& name) -> Element&
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

	auto RendererConstruction::AddElement() -> Element
	{
		assert(_internal && !_internal->_sealed);
		auto result = Element{_internal->_elementCount, *_internal.get()};
		++_internal->_elementCount;
		return result;
	}

	RendererConstruction::RendererConstruction()
	{
		_internal = std::make_unique<Internal>();
	}
	RendererConstruction::~RendererConstruction() {}

}}
