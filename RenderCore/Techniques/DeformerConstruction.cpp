// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformerConstruction.h"
#include "DeformGeometryInfrastructure.h"
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

		StoredGeoEntry newEntry;
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
		_storedGeoEntries.emplace_back(std::move(newEntry));
	}

	void DeformerConstruction::Add(
		std::shared_ptr<IGeoDeformer> deformer,
		DeformOperationInstantiation&& instantiation,
		unsigned elementIdx,
		unsigned geoIdx)
	{
		assert(!_sealed);
		StoredGeoEntry newEntry;
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
		_storedGeoEntries.emplace_back(std::move(newEntry));
	}

	auto DeformerConstruction::GetGeoEntries() const -> std::vector<GeoEntry>
	{
		std::vector<GeoEntry> result;
		result.reserve(_storedGeoEntries.size());
		for (const auto& e:_storedGeoEntries) {
			result.emplace_back(
				GeoEntry{
					_deformers[e._deformerIdx],
					&e._instantiation,
					e._elementIdx, e._geoIdx});
		}
		return result;
	}

	void DeformerConstruction::Add(std::shared_ptr<IDeformUniformsAttachment> deformer)
	{
		assert(!_sealed);
		assert(!_storedUniformsEntry._deformer);
		_storedUniformsEntry._deformer = std::move(deformer);
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

				strongThis->_completedGeoAttachment = CreateDeformGeoAttachment(
					*strongThis->_device, *strongThis->_rendererConstruction, *strongThis);

				return std::move(strongThis);
			});
	}

	uint64_t DeformerConstruction::GetHash() const
	{
		// This is used in the SimpleModelRenderer asset interface, so we need a GetHash() function
		// ... however we can't easily create a good hash for it
		return 0;
	}

	DeformerConstruction::DeformerConstruction(std::shared_ptr<IDevice> device, std::shared_ptr<Assets::ModelRendererConstruction> rendererConstruction)
	: _device(device), _rendererConstruction(rendererConstruction) {}
	DeformerConstruction::DeformerConstruction() = default;
	DeformerConstruction::~DeformerConstruction() = default;

	InputStreamFormatter<char>& IDeformConfigure::EmptyFormatter()
	{
		static InputStreamFormatter<char> dummy;
		return dummy;
	}

	IDeformConfigure::~IDeformConfigure() = default;
}}
