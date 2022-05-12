// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformOperationFactory.h"
#include "Services.h"
#include "../../Assets/Marker.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
	void DeformerConstruction::Add(
		::Assets::PtrToMarkerPtr<IGeoDeformer> deformer,
		DeformOperationInstantiation&& instantiation,
		unsigned elementIdx,
		unsigned geoIdx)
	{
		assert(!_sealed);
		// If we can immediately actualize, just treat it as an actualized deformer
		auto* actualized = deformer->TryActualize();
		if (actualized) {
			Add(*actualized, std::move(instantiation), elementIdx, geoIdx);
			return;
		}

		StoredEntry newEntry;
		auto i = std::find(_deformerMarkers.begin(), _deformerMarkers.end(), deformer);
		if (i != _deformerMarkers.end()) {
			newEntry._deformerIdx = (unsigned)std::distance(_deformerMarkers.begin(), i);
		} else {
			newEntry._deformerIdx = (unsigned)_deformerMarkers.size();
			_deformerMarkers.push_back(std::move(deformer));
			_deformers.push_back(nullptr);
		}
		newEntry._elementIdx = elementIdx;
		newEntry._geoIdx = geoIdx;
		newEntry._instantiation = std::move(instantiation);
		_storedEntries.emplace_back(std::move(newEntry));
	}

	void DeformerConstruction::Add(
		std::shared_ptr<IGeoDeformer> deformer,
		DeformOperationInstantiation&& instantiation,
		unsigned elementIdx,
		unsigned geoIdx)
	{
		assert(!_sealed);
		StoredEntry newEntry;
		auto i = std::find(_deformers.begin(), _deformers.end(), deformer);
		if (i != _deformers.end()) {
			newEntry._deformerIdx = (unsigned)std::distance(_deformers.begin(), i);
		} else {
			newEntry._deformerIdx = (unsigned)_deformers.size();
			_deformers.push_back(std::move(deformer));
			_deformerMarkers.push_back(nullptr);
		}
		newEntry._elementIdx = elementIdx;
		newEntry._geoIdx = geoIdx;
		newEntry._instantiation = std::move(instantiation);
		_storedEntries.emplace_back(std::move(newEntry));
	}

	auto DeformerConstruction::GetEntries() const -> std::vector<Entry>
	{
		std::vector<Entry> result;
		result.reserve(_storedEntries.size());
		for (const auto& e:_storedEntries) {
			result.emplace_back(
				Entry{
					_deformers[e._deformerIdx],
					&e._instantiation,
					e._elementIdx, e._geoIdx});
		}
		return result;
	}

	bool DeformerConstruction::IsEmpty() const
	{
		return _storedEntries.empty();
	}

	void DeformerConstruction::FulfillWhenNotPending(std::promise<std::shared_ptr<DeformerConstruction>>&& promise)
	{
		_sealed = true;

		auto strongThis = shared_from_this();
		assert(strongThis);
		::Assets::PollToPromise(
			std::move(promise),
			[strongThis](auto timeout) {
				// wait until all pending scaffold markers are finished
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:strongThis->_deformerMarkers) {
					if (!f) continue;
					auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
					if (remainingTime.count() <= 0) return ::Assets::PollStatus::Continue;
					auto t = f->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
					if (t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
						return ::Assets::PollStatus::Continue;
				}
				return ::Assets::PollStatus::Finish;
			},
			[strongThis]() mutable {
				std::vector<std::shared_ptr<IGeoDeformer>> newFinishedDeformers;
				newFinishedDeformers.reserve(strongThis->_deformerMarkers.size());
				for (auto& f:strongThis->_deformerMarkers) {
					if (f) newFinishedDeformers.push_back(f->ActualizeBkgrnd());
					else newFinishedDeformers.push_back(nullptr);
				}
				// After every one has been actualized correctly, move the pointer to the _deformers vector
				for (unsigned c=0; c<newFinishedDeformers.size(); ++c) {
					if (!newFinishedDeformers[c]) continue;
					assert(!strongThis->_deformers[c]);
					strongThis->_deformers[c] = std::move(newFinishedDeformers[c]);
					strongThis->_deformerMarkers[c] = nullptr;
				}
				return std::move(strongThis);
			});
	}

	DeformerConstruction::DeformerConstruction() = default;
	DeformerConstruction::~DeformerConstruction() = default;

#if 0
	auto DeformOperationFactorySet::CreateDeformOperators(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName) -> std::vector<Deformer>
	{
		std::vector<Deformer> result;

		auto* i = initializer.begin();
		while (i != initializer.end()) {
			auto* start = i;
			
			while (i!=initializer.end() && *i != ';') ++i;
			auto* end = i;
			if (i!=initializer.end()) ++i;

			auto* colon = XlFindChar(MakeStringSection(start, end), ':');
			auto* afterColon = end;
			if (colon) {
				afterColon = colon+1;
			} else {
				colon = end;
			}

			auto hash = Hash64(MakeStringSection(start, colon));
			auto i = LowerBound(_instantiationFunctions, hash);
			if (i==_instantiationFunctions.end() || i->first != hash)
				continue;

			Deformer deformer;
			deformer._factory = i->second._factory;
			deformer._operator = i->second._factory->Configure(
				deformer._instantiations, 
				MakeStringSection(afterColon, end), modelScaffold, modelScaffoldName);
			if (deformer._operator)
				result.push_back(std::move(deformer));
		}
		return result;
	}

	std::shared_ptr<IGeoDeformerFactory> DeformOperationFactorySet::GetFactory(uint64_t name) const
	{
		auto i = LowerBound(_instantiationFunctions, name);
		if (i!=_instantiationFunctions.end() && i->first == name)
			return i->second._factory;
		return nullptr;
	}

	auto DeformOperationFactorySet::Register(StringSection<> name, std::shared_ptr<IGeoDeformerFactory> factory) -> RegisteredDeformId
	{
		RegisteredDeformId result = _nextDeformId++;
		auto hash = Hash64(name.begin(), name.end());
		auto i = LowerBound(_instantiationFunctions, hash);
		if (i!=_instantiationFunctions.end() && i->first == hash) {
			i->second = {std::move(factory), result};
		} else {
			_instantiationFunctions.insert(i, std::make_pair(hash, RegisteredDeformOp{std::move(factory), result}));
		}
		return result;
	}

	void DeformOperationFactorySet::Deregister(RegisteredDeformId deformId)
	{
		auto i = std::remove_if(
			_instantiationFunctions.begin(),
			_instantiationFunctions.end(),
			[deformId](const std::pair<uint64_t, RegisteredDeformOp>& i) {
				return i.second._deformId == deformId;
			});
		_instantiationFunctions.erase(i, _instantiationFunctions.end());
	}

	DeformOperationFactorySet::DeformOperationFactorySet()
	{
		_nextDeformId = 1;
	}

	DeformOperationFactorySet::~DeformOperationFactorySet()
	{
	}

	DeformOperationFactorySet& DeformOperationFactorySet::GetInstance()
	{
		return Services::GetDeformOperationFactorySet();
	}

	IGeoDeformerFactory::~IGeoDeformerFactory() {}
#endif

	std::shared_ptr<IGeoDeformerFactory> DeformOperationFactorySet::GetFactory(uint64_t name) const { return nullptr; }

	auto DeformOperationFactorySet::Register(StringSection<> name, std::shared_ptr<IGeoDeformerFactory>) -> RegisteredDeformId
	{
		return 0;
	}
	void DeformOperationFactorySet::Deregister(RegisteredDeformId) {}
	
	DeformOperationFactorySet::DeformOperationFactorySet() {}
	DeformOperationFactorySet::~DeformOperationFactorySet() {}

	DeformOperationFactorySet& DeformOperationFactorySet::GetInstance()
	{
		return Services::GetDeformOperationFactorySet();
	}

	IGeoDeformerFactory::~IGeoDeformerFactory() {}
}}
