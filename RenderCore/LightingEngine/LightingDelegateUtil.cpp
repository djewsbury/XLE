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

	static ShadowProbes::Probe GetProbeDesc(ILightScene& lightScene, ILightScene::LightSourceId lightId)
	{
		ShadowProbes::Probe probe;
		probe._position = Zero<Float3>();
		probe._nearRadius = 1.f;
		probe._farRadius = 1024.f;
		auto* positional = lightScene.TryGetLightSourceInterface<IPositionalLightSource>(lightId);
		assert(positional);
		if (positional) {
			probe._position = ExtractTranslation(positional->GetLocalToWorld());
			probe._nearRadius = ExtractUniformScaleFast(AsFloat3x4(positional->GetLocalToWorld()));
		}
		auto* finite = lightScene.TryGetLightSourceInterface<IFiniteLightSource>(lightId);
		if (finite)
			probe._farRadius = finite->GetCutoffRange();
		return probe;
	}

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

				auto registration = LowerBound(_registeredLights, q);
				if (registration == _registeredLights.end() || registration->first != q)
					continue;	// deregistered at some point

				auto instanceProbeSlot = xl_ctz8(probeSlotsToUse);
				assert(instanceProbeSlot < 64);
				probeSlotsToUse &= ~(1ull << uint64_t(instanceProbeSlot));

				auto probeDesc = registration->second;
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

		auto registration = _registeredLights.begin();
		auto i = _allocatedProbes.begin();
		for (auto q:_probeSlotsPreparedInBackground) {
			registration = LowerBound2(MakeIteratorRange(registration, _registeredLights.end()), q.first);
			if (registration == _registeredLights.end() || registration->first != q.first) {
				// Light was deregistered while begin prepared. The probe slot should just become unassociated
				_unassociatedProbeSlots |= 1ull << uint64_t(q.second);
				continue;
			}

			i = LowerBound2(MakeIteratorRange(i, _allocatedProbes.end()), q.first);
			assert(i == _allocatedProbes.end() || i->first != q.first);		// attempting to assign a light that is already assigned to a slot

			AllocatedProbe p;
			p._attachedProbe = q.second;
			p._fading = 1;		// begins at minimum fade in
			i = _allocatedProbes.insert(i, {q.first, p});
			_unassociatedProbeSlots &= ~(1ull << uint64_t(q.second));
			if (auto* interf = _lightScene->TryGetLightSourceInterface<IAttachedShadowProbe>(q.first))
				interf->SetDatabaseEntry(q.second+1);
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
			for (auto l=_allocatedProbes.begin(); l!=_allocatedProbes.end();) {
				auto bit = 1ull << uint64_t(l->second._attachedProbe);
				if (_probeSlotsReservedInBackground & bit) {
					if (auto* interf = _lightScene->TryGetLightSourceInterface<IAttachedShadowProbe>(l->first))
						interf->SetDatabaseEntry(0);
					_unassociatedProbeSlots |= (1ull << uint64_t(l->second._attachedProbe));
					l=_allocatedProbes.erase(l);
				} else
					++l;
			}

			if (_readyToCommitBackgroundChanges) {
				CommitBackgroundChangesAlreadyLocked();
			} else {
				// just have to advance fading state
				for (auto& l:_allocatedProbes)
					if (l.second._attachedProbe != ~0u)
						l.second._fading = std::min(l.second._fading+1, fadeTransitionInFrames);
				return OnFrameBarrierResult::BackgroundOperationOngoing;
			}
		}

		// Given the current set of lights, calculate the optimal use of a finite number of shadow probe database entries
		// The easiest way to do this is to just the sort the list of lights we have by distance
		// but ideally this should really be tied into some visibility solution -- and perhaps avoid updating every frame
		std::vector<std::pair<ILightScene::LightSourceId, float>> lightsAndDistance;
		lightsAndDistance.reserve(_registeredLights.size());
		for (unsigned c=0; c<_registeredLights.size(); ++c)
			lightsAndDistance.emplace_back(_registeredLights[c].first, Magnitude(_registeredLights[c].second._position-newViewPosition) - _registeredLights[c].second._farRadius);

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

		auto currentStateIterator = _allocatedProbes.begin();
		auto newStateIterator = lightsAndDistance.begin();
		assert(_probeSlotsCount <= 64u);	// has to be small, because we're going to use a bitfield in a uint64_t
		uint64_t availableProbeSlots = _unassociatedProbeSlots;
		while (newStateIterator != lightsAndDistance.end()) {
			while (currentStateIterator != _allocatedProbes.end() && currentStateIterator->first < newStateIterator->first) {
				// This light fell out of the close lights list
				currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
				if (!currentStateIterator->second._fading) {
					availableProbeSlots |= 1ull << uint64_t(currentStateIterator->second._attachedProbe);
					currentStateIterator = _allocatedProbes.erase(currentStateIterator);
				} else
					++currentStateIterator;
			}
			while (newStateIterator != lightsAndDistance.end() && (currentStateIterator == _allocatedProbes.end() || newStateIterator->first < currentStateIterator->first)) {
				// This light is new to the close lights list. Note that newStateIterator->second is distance - cutoff range
				if (newStateIterator->second < drawDistance)
					potentialNewRenders[potentialRenderCount++] = *newStateIterator;
				++newStateIterator;
			}

			if (currentStateIterator != _allocatedProbes.end() && newStateIterator != lightsAndDistance.end() && currentStateIterator->first == newStateIterator->first) {
				currentStateIterator->second._fading = std::min(currentStateIterator->second._fading+1, fadeTransitionInFrames);
				++currentStateIterator;
				++newStateIterator;
			}
		}

		// all remaining lights fell off the close lights list
		while (currentStateIterator!=_allocatedProbes.end()) {
			currentStateIterator->second._fading = std::max(currentStateIterator->second._fading-1, 0);
			if (!currentStateIterator->second._fading) {
				availableProbeSlots |= 1ull << uint64_t(currentStateIterator->second._attachedProbe);
				currentStateIterator = _allocatedProbes.erase(currentStateIterator);
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

	void SemiStaticShadowProbeScheduler::AddLight(ILightScene::LightSourceId lightId)
	{
		ScopedLock(_lock);
		auto i = LowerBound(_registeredLights, lightId);
		assert(i == _registeredLights.end() || i->first != lightId);		// attempting to add the same light twice
		_registeredLights.emplace_back(lightId, GetProbeDesc(*_lightScene, lightId));
	}

	void SemiStaticShadowProbeScheduler::RemoveLight(ILightScene::LightSourceId lightId)
	{
		ScopedLock(_lock);
		auto i = LowerBound(_registeredLights, lightId);
		assert(i != _registeredLights.end() && i->first != lightId);		// attempt to remove a light that hasn't previously been added
		if (i != _registeredLights.end() && i->first != lightId) return;
		_registeredLights.erase(i);
	}

	SemiStaticShadowProbeScheduler::SemiStaticShadowProbeScheduler(std::shared_ptr<ShadowProbes> shadowProbes, ILightScene* lightScene) 
	: _shadowProbes(std::move(shadowProbes)), _lightScene(lightScene)
	{
		_probeSlotsCount = _shadowProbes->GetReservedProbeCount();
		assert(_probeSlotsCount <= 64);
		_unassociatedProbeSlots = (_probeSlotsCount == 64u) ? ~0ull : ((1ull << uint64_t(_probeSlotsCount)) - 1ull);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
		_allocatedProbes.reserve(_probeSlotsCount);
		_probeSlotsPreparedInBackground.reserve(_probeSlotsCount);
		_lastEvalBestRenders.reserve(_probeSlotsCount);
	}

}}}

