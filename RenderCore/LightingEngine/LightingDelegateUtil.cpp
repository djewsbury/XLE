// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingDelegateUtil.h"
#include "LightingEngineIterator.h"
#include "ShadowUniforms.h"
#include "ShadowPreparer.h"
#include "ShadowProbes.h"
#include "LightingEngineInitialization.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/ParsingContext.h"
#include "../../Utility/PtrUtils.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"
#include <utility>

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	static TechniqueSequenceParseId SetupShadowParse(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId)
	{
		auto& standardProj = *checked_cast<Internal::StandardShadowProjection*>(&proj);
		std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> volumeTester;

		// Call the driver if one exists
		if (standardProj._driver) {
			// Note the TryGetLightSourceInterface is expensive particular, and scales poorly with the number of
			// lights in the scene
			auto* positionalLight = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(associatedLightId);
			if (positionalLight)
				volumeTester = ((Internal::IShadowProjectionDriver*)standardProj._driver->QueryInterface(typeid(Internal::IShadowProjectionDriver).hash_code()))->UpdateProjections(
					*iterator._parsingContext, *positionalLight, standardProj);
		}

		// todo - cull out any offscreen projections
		if (standardProj._multiViewInstancingPath) {
			std::vector<Techniques::ProjectionDesc> projDescs;
			projDescs.resize(standardProj._projections.Count());
			CalculateProjections(MakeIteratorRange(projDescs), standardProj._projections);
			return sequence.CreateMultiViewParseScene(Techniques::BatchFlags::Opaque, std::move(projDescs), std::move(volumeTester));
		} else {
			if (volumeTester) {
				return sequence.CreateParseScene(Techniques::BatchFlags::Opaque, std::move(volumeTester));
			} else
				return sequence.CreateParseScene(Techniques::BatchFlags::Opaque);
		}
	}

	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		Internal::ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		PipelineType descSetPipelineType,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto parseId = SetupShadowParse(iterator, sequence, proj, lightScene, associatedLightId);

		auto& standardProj = *checked_cast<Internal::StandardShadowProjection*>(&proj);
		auto& preparer = *standardProj._preparer;
		auto res = preparer.CreatePreparedShadowResult();
		sequence.CreateStep_CallFunction(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool, parseId, res, descSetPipelineType](LightingTechniqueIterator& iterator) {
				auto rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					proj,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool);
				iterator.ExecuteDrawables(parseId, *preparer.GetSequencerConfig().first, preparer.GetSequencerConfig().second);
				rpi.End();
				preparer.End(*iterator._threadContext, *iterator._parsingContext, rpi, descSetPipelineType, *res);
			});
		return res;
	}

	class BuildGBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
        virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			assert(bindingFlags == 1<<0);
			dst[0] = _normalsFitting.get();
			context.RequireCommandList(_completionCmdList);
		}

		BuildGBufferResourceDelegate(Techniques::DeferredShaderResource& normalsFittingResource)
		{
			BindResourceView(0, Utility::Hash64("NormalsFittingTexture"));
			_normalsFitting = normalsFittingResource.GetShaderResource();
			_completionCmdList = normalsFittingResource.GetCompletionCommandList();
		}
		std::shared_ptr<IResourceView> _normalsFitting;
		BufferUploads::CommandListID _completionCmdList;
	};

	::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate()
	{
		auto normalsFittingTexture = ::Assets::MakeAssetPtr<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
		::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> result("gbuffer-srdelegate");
		::Assets::WhenAll(normalsFittingTexture).ThenConstructToPromise(
			result.AdoptPromise(),
			[](auto nft) { return std::make_shared<BuildGBufferResourceDelegate>(*nft); });
		return result;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	static ShadowProbes::Probe GetProbeDesc(ILightBase& light)
	{
		ShadowProbes::Probe probe;
		probe._position = Zero<Float3>();
		probe._nearRadius = 1.f;
		probe._farRadius = 1024.f;
		auto* positional = (IPositionalLightSource*)light.QueryInterface(typeid(IPositionalLightSource).hash_code());
		assert(positional);
		if (positional) {
			probe._position = ExtractTranslation(positional->GetLocalToWorld());
			probe._nearRadius = ExtractUniformScaleFast(AsFloat3x4(positional->GetLocalToWorld()));
		}
		auto* finite = (IFiniteLightSource*)light.QueryInterface(typeid(IFiniteLightSource).hash_code());
		if (finite)
			probe._farRadius = finite->GetCutoffRange();
		return probe;
	}

	class SemiStaticShadowProbeScheduler::SceneSet
	{
	public:
		// we just maintain a parallel list of the light probes we're interested in
		struct ProbeEntry
		{
			ShadowProbes::Probe _probeDesc;
			unsigned _attachedDatabaseIndex = ~0u;
			int _fading = 0;
		};
		std::vector<ProbeEntry> _probes;
		BitHeap _activeProbes;

		void RegisterLight(unsigned index, ILightBase& light)
		{
			if (_probes.size() <= index)
				_probes.resize(index+1);
			_probes[index] = { GetProbeDesc(light), ~0u, 0 };
			_activeProbes.Allocate(index);
		}
		void DeregisterLight(unsigned index)
		{
			_activeProbes.Deallocate(index);
		}

		SceneSet() = default;
		SceneSet(SceneSet&&) = default;
		SceneSet& operator=(SceneSet&&) = default;
	};

	static uint32_t GetSetIndex(uint64_t lightIndex) { return lightIndex >> 32; }
	static uint32_t GetLightIndex(uint64_t lightIndex) { return uint32_t(lightIndex); }

	std::shared_ptr<IProbeRenderingInstance> SemiStaticShadowProbeScheduler::BeginPrepare(IThreadContext& threadContext, unsigned maxProbeCount)
	{
		// Can be called in a background thread -- begins prepare for the most important queued probes, as 
		// calculated in the last OnFrameBarrier

		std::vector<std::pair<unsigned, ShadowProbes::Probe>> probesToPrepare;
		{
			ScopedLock(_lock);
			if (_lastEvalBestRenders.empty()) return nullptr;

			probesToPrepare.reserve(_lastEvalBestRenders.size());
			uint64_t probeSlotsToUse = _lastEvalAvailableProbeSlots;
			assert(_probeSlotsReservedInBackground == 0);
			assert(_probeSlotsPreparedInBackground.empty());
			_probeSlotsReservedInBackground = 0;
			_probeSlotsPreparedInBackground.clear();
			_readyToCommitBackgroundChanges = false;

			for (auto q:_lastEvalBestRenders) {
				if (probesToPrepare.size() >= maxProbeCount) break;

				const auto& comp = _sceneSets[GetSetIndex(q)];
				if (!comp._activeProbes.IsAllocated(GetLightIndex(q)))
					continue;	// deregistered at some point

				auto instanceProbeSlot = xl_ctz8(probeSlotsToUse);
				assert(instanceProbeSlot < 64);
				probeSlotsToUse &= ~(1ull << uint64_t(instanceProbeSlot));

				auto probeDesc = comp._probes[GetLightIndex(q)]._probeDesc;
				probeDesc._nearRadius = std::max(probeDesc._nearRadius, _defaultNearRadius);
				probesToPrepare.emplace_back(instanceProbeSlot, probeDesc);

				_probeSlotsReservedInBackground |= 1ull << uint64_t(instanceProbeSlot);
				_probeSlotsPreparedInBackground.emplace_back(q, instanceProbeSlot);
			}

			// note -- eviction based on _probeSlotsReservedInBackground will be performed in the foreground on the next 
			// OnFrameBarrier
		}

		if (probesToPrepare.empty()) return nullptr;
		return _shadowProbes->PrepareStaticProbes(threadContext, MakeIteratorRange(probesToPrepare));
	}

	void SemiStaticShadowProbeScheduler::EndPrepare(IThreadContext& threadContext)
	{
		ScopedLock(_lock);
		_readyToCommitBackgroundChanges = true;
	}

	void SemiStaticShadowProbeScheduler::CommitBackgroundChangesAlreadyLocked()
	{
		assert(!_probeSlotsPreparedInBackground.empty());
		assert(_probeSlotsReservedInBackground != 0);
		assert(_readyToCommitBackgroundChanges);

		// Assign the probes we just completed into the main list
		// note -- scheduling is complicated here, since we've just completed and queued the GPU commands

		auto i = _allocatedDatabaseEntries.begin();
		for (auto q:_probeSlotsPreparedInBackground) {
			auto& comp = _sceneSets[GetSetIndex(q.first)];
			if (!comp._activeProbes.IsAllocated(GetLightIndex(q.first))) {
				// Light was deregistered while begin prepared. The probe slot should just become unassociated
				_unassociatedProbeSlots |= 1ull << uint64_t(q.second);
				continue;
			}

			i = LowerBound2(MakeIteratorRange(i, _allocatedDatabaseEntries.end()), q.first);
			assert(i == _allocatedDatabaseEntries.end() || i->first != q.first);		// attempting to assign a light that is already assigned to a slot

			AllocatedDatabaseEntry p;
			p._databaseIndex = q.second;
			p._fading = 1;		// begins at minimum fade in
			i = _allocatedDatabaseEntries.insert(i, {q.first, p});
			_unassociatedProbeSlots &= ~(1ull << uint64_t(q.second));

			comp._probes[GetLightIndex(q.first)]._attachedDatabaseIndex = ~0u;
			comp._probes[GetLightIndex(q.first)]._fading = 0;
		}

		_probeSlotsReservedInBackground = 0;
		_probeSlotsPreparedInBackground.clear();
		_readyToCommitBackgroundChanges = false;
	}

	void SemiStaticShadowProbeScheduler::SetNearRadius(float nearRadius) { _defaultNearRadius = nearRadius; }
	float SemiStaticShadowProbeScheduler::GetNearRadius(float) { return _defaultNearRadius; }

	auto SemiStaticShadowProbeScheduler::OnFrameBarrier(const Float3& newViewPosition, float drawDistance) -> OnFrameBarrierResult
	{
		ScopedLock(_lock);

		const int fadeTransitionInFrames = 16;

		if (_probeSlotsReservedInBackground) {

			// Ensure that none of the current lights are using any of the probes we're going to rewrite now
			// Scheduling here is a little complicated, since we're going to rewrite this probe instance pretty very
			// soon, we don't want it to be read from
			// this is actually the "evict" step
			for (auto l=_allocatedDatabaseEntries.begin(); l!=_allocatedDatabaseEntries.end();) {
				auto bit = 1ull << uint64_t(l->second._databaseIndex);
				if (_probeSlotsReservedInBackground & bit) {
					auto& inComponent = _sceneSets[GetSetIndex(l->first)]._probes[GetLightIndex(l->first)];
					inComponent._attachedDatabaseIndex = ~0u;
					inComponent._fading = 0;

					_unassociatedProbeSlots |= (1ull << uint64_t(l->second._databaseIndex));
					l=_allocatedDatabaseEntries.erase(l);
				} else
					++l;
			}

			if (_readyToCommitBackgroundChanges) {
				CommitBackgroundChangesAlreadyLocked();
			} else {
				// just have to advance fading state
				for (auto& l:_allocatedDatabaseEntries) {
					l.second._fading = std::min(l.second._fading+1, fadeTransitionInFrames);
					_sceneSets[GetSetIndex(l.first)]._probes[GetLightIndex(l.first)]._fading = l.second._fading;
				}
				return OnFrameBarrierResult::BackgroundOperationOngoing;
			}
		}

		// Given the current set of lights, calculate the optimal use of a finite number of shadow probe database entries
		// The easiest way to do this is to just the sort the list of lights we have by distance
		// but ideally this should really be tied into some visibility solution -- and perhaps avoid updating every frame
		std::vector<std::pair<ILightScene::LightSourceId, float>> lightsAndDistance;
		lightsAndDistance.reserve(256);
		for (unsigned compIdx=0; compIdx<_sceneSets.size(); ++compIdx) {
			auto& comp = _sceneSets[compIdx];
			unsigned offset = 0;
			for (auto q:comp._activeProbes.InternalArray()) {
				q = ~q;		// bit heap inverts allocations
				while (q) {
					auto idx = xl_ctz8(q);
					q ^= 1ull << uint64_t(idx);
					idx += offset;
					auto& probe = comp._probes[idx]._probeDesc;
					lightsAndDistance.emplace_back((uint64_t(compIdx) << 32ull) | idx, Magnitude(probe._position-newViewPosition) - probe._farRadius);
				}
				offset += 64;
			}
		}

		if (lightsAndDistance.size() > _probeSlotsCount) {
			// find the smallest N items and then restore sort order
			std::nth_element(lightsAndDistance.begin(), lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end(), CompareSecond2{});
			lightsAndDistance.erase(lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end());
			std::sort(lightsAndDistance.begin(), lightsAndDistance.end(), CompareFirst2{});
		}
		
		// compare to the list lights currently in the database and figure out
		// evictions and new renderings
		std::pair<ILightScene::LightSourceId, float> potentialNewRenders[lightsAndDistance.size()];
		unsigned potentialRenderCount = 0;

		auto currentStateIterator = _allocatedDatabaseEntries.begin();
		auto newStateIterator = lightsAndDistance.begin();
		assert(_probeSlotsCount <= 64u);	// has to be small, because we're going to use a bitfield in a uint64_t
		uint64_t availableProbeSlots = _unassociatedProbeSlots;
		while (newStateIterator != lightsAndDistance.end()) {
			while (currentStateIterator != _allocatedDatabaseEntries.end() && currentStateIterator->first < newStateIterator->first) {
				// This light fell out of the close lights list
				currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
				if (!currentStateIterator->second._fading) {
					availableProbeSlots |= 1ull << uint64_t(currentStateIterator->second._databaseIndex);
					auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]._probes[GetLightIndex(currentStateIterator->first)];
					inComponent._attachedDatabaseIndex = ~0u;
					inComponent._fading = 0;
					currentStateIterator = _allocatedDatabaseEntries.erase(currentStateIterator);
				} else
					++currentStateIterator;
			}
			while (newStateIterator != lightsAndDistance.end() && (currentStateIterator == _allocatedDatabaseEntries.end() || newStateIterator->first < currentStateIterator->first)) {
				// This light is new to the close lights list. Note that newStateIterator->second is distance - cutoff range
				if (newStateIterator->second < drawDistance)
					potentialNewRenders[potentialRenderCount++] = *newStateIterator;
				++newStateIterator;
			}

			if (currentStateIterator != _allocatedDatabaseEntries.end() && newStateIterator != lightsAndDistance.end() && currentStateIterator->first == newStateIterator->first) {
				currentStateIterator->second._fading = std::min(currentStateIterator->second._fading+1, fadeTransitionInFrames);
				auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]._probes[GetLightIndex(currentStateIterator->first)];
				inComponent._fading = currentStateIterator->second._fading;
				++currentStateIterator;
				++newStateIterator;
			}
		}

		// all remaining lights fell off the close lights list
		while (currentStateIterator!=_allocatedDatabaseEntries.end()) {
			currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
			if (!currentStateIterator->second._fading) {
				availableProbeSlots |= 1ull << uint64_t(currentStateIterator->second._databaseIndex);
				auto& inComponent = _sceneSets[GetSetIndex(currentStateIterator->first)]._probes[GetLightIndex(currentStateIterator->first)];
				inComponent._attachedDatabaseIndex = ~0u;
				inComponent._fading = 0;
				currentStateIterator = _allocatedDatabaseEntries.erase(currentStateIterator);
			} else
				++currentStateIterator;
		}

		// avoid stealing something begin written to in the background right now
		availableProbeSlots &= ~_probeSlotsReservedInBackground;

		// If we have some lights to render, we need to prioritize them and record
		auto freeSlotCount =  countbits(availableProbeSlots);
		if (potentialRenderCount && freeSlotCount) {
			if (freeSlotCount < potentialRenderCount) {
				std::partial_sort(potentialNewRenders, potentialNewRenders+freeSlotCount, potentialNewRenders+potentialRenderCount, CompareSecond2{});
				potentialRenderCount = freeSlotCount;
			} else {
				std::sort(potentialNewRenders, potentialNewRenders+potentialRenderCount, CompareSecond2{});
			}
			_lastEvalBestRenders.clear();
			_lastEvalBestRenders.reserve(potentialRenderCount);
			for (unsigned c=0; c<potentialRenderCount; ++c) _lastEvalBestRenders.push_back(potentialNewRenders[c].first);
		} else {
			_lastEvalBestRenders.clear();
		}
		_lastEvalAvailableProbeSlots = availableProbeSlots;

		return _lastEvalBestRenders.empty() ? OnFrameBarrierResult::NoChange : OnFrameBarrierResult::QueuedRenders;
	}

	void SemiStaticShadowProbeScheduler::RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light)
	{
		_sceneSets[setIdx].RegisterLight(lightIdx, light);
	}

	void SemiStaticShadowProbeScheduler::DeregisterLight(unsigned setIdx, unsigned lightIdx)
	{
		_sceneSets[setIdx].DeregisterLight(lightIdx);
	}

	bool SemiStaticShadowProbeScheduler::BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId shadowOperator, unsigned setIdx)
	{
		if (shadowOperator != _operatorId) return false;
		if (_sceneSets.size() <= setIdx)
			_sceneSets.resize(setIdx+1);
		return true;
	}

	void* SemiStaticShadowProbeScheduler::QueryInterface(unsigned lightIdx, uint64_t interfaceTypeCode)
	{
		return nullptr;
	}

	auto SemiStaticShadowProbeScheduler::GetAllocatedDatabaseEntry(unsigned setIdx, unsigned lightIdx) -> AllocatedDatabaseEntry
	{
		assert(setIdx < _sceneSets.size());
		assert(_sceneSets[setIdx]._activeProbes.IsAllocated(lightIdx));
		auto& p = _sceneSets[setIdx]._probes[lightIdx];
		return { p._attachedDatabaseIndex, p._fading };
	}

	SemiStaticShadowProbeScheduler::SemiStaticShadowProbeScheduler(std::shared_ptr<ShadowProbes> shadowProbes, ILightScene::ShadowOperatorId operatorId) 
	: _shadowProbes(std::move(shadowProbes)), _operatorId(operatorId)
	{
		_probeSlotsCount = _shadowProbes->GetReservedProbeCount();
		assert(_probeSlotsCount <= 64);
		_unassociatedProbeSlots = (_probeSlotsCount == 64u) ? ~0ull : ((1ull << uint64_t(_probeSlotsCount)) - 1ull);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
		_allocatedDatabaseEntries.reserve(_probeSlotsCount);
		_probeSlotsPreparedInBackground.reserve(_probeSlotsCount);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
	}

	SemiStaticShadowProbeScheduler::~SemiStaticShadowProbeScheduler() {}

}}}

