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

			for (auto q:_lastEvalBestRenders) {
				if (probesToPrepare.size() >= maxProbeCount) break;

				auto i = LowerBound(_associatedLights, q);
				assert(i!=_associatedLights.end() && i->first == q);

				auto instanceProbeSlot = xl_clz8(probeSlotsToUse);
				assert(instanceProbeSlot < 64);

				auto probeDesc = i->second._probeDesc;
				probeDesc._nearRadius = std::max(probeDesc._nearRadius, _defaultNearRadius);
				probesToPrepare.emplace_back(instanceProbeSlot, probeDesc);

				_probeSlotsReservedInBackground |= 1ull << uint64_t(instanceProbeSlot);
				_probeSlotsPreparedInBackground.emplace_back(i->first, instanceProbeSlot);
			}

			// Ensure that none of the current lights are using any of the probes we're going to rewrite now
			// Scheduling here is a little complicated, since we're going to rewrite this probe instance pretty very
			// soon, we don't want it to be read from
			for (auto& l:_associatedLights) {
				if (l.second._attachedProbe == ~0u) continue;
				if (std::find_if(probesToPrepare.begin(), probesToPrepare.end(), [p=l.second._attachedProbe](const auto& q) { return q.first == p; }) != probesToPrepare.end())
					l.second._attachedProbe = ~0u;
			}

			assert(!probesToPrepare.empty());
		}

		return _shadowProbes->PrepareStaticProbes(threadContext, MakeIteratorRange(probesToPrepare));
	}

	void SemiStaticShadowProbeScheduler::EndPrepare(IThreadContext& threadContext)
	{
		ScopedLock(_lock);
		assert(!_probeSlotsPreparedInBackground.empty());
		assert(_probeSlotsReservedInBackground != 0);

		// Assign the probes we just completed into the main list
		// note -- scheduling is complicated here, since we've just completed and queued the GPU commands

		for (auto q:_probeSlotsPreparedInBackground) {
			auto i = LowerBound(_associatedLights, q.first);
			if (i->first == q.first) {
				assert(i->second._attachedProbe == ~0u);
				i->second._attachedProbe = q.second;
				i->second._fading = 1;		// begins at minimum fade in
				_unassociatedProbeSlots &= ~(1ull << uint64_t(q.second));
			} else {
				// Light was removed while begin prepared. The probe slot should just become unassociated
				_unassociatedProbeSlots |= 1ull << uint64_t(q.second);
			}
		}

		_probeSlotsReservedInBackground = 0;
		_probeSlotsPreparedInBackground.clear();
	}

	void SemiStaticShadowProbeScheduler::SetNearRadius(float nearRadius) { _defaultNearRadius = nearRadius; }
	float SemiStaticShadowProbeScheduler::GetNearRadius(float) { return _defaultNearRadius; }

	auto SemiStaticShadowProbeScheduler::OnFrameBarrier(const Float3& newViewPosition, float drawDistance) -> OnFrameBarrierResult
	{
		ScopedLock(_lock);

		// Given the current set of lights, calculate the optimal use of a finite number of shadow probe database entries
		// The easiest way to do this is to just the sort the list of lights we have by distance
		// but ideally this should really be tied into some visibility solution -- and perhaps avoid updating every frame
		std::vector<std::pair<ILightScene::LightSourceId, float>> lightsAndDistance;
		lightsAndDistance.reserve(_associatedLights.size());
		for (unsigned c=0; c<_associatedLights.size(); ++c)
			lightsAndDistance.emplace_back(_associatedLights[c].first, Magnitude(_associatedLights[c].second._probeDesc._position-newViewPosition) - _associatedLights[c].second._probeDesc._farRadius);

		if (lightsAndDistance.size() > _probeSlotsCount) {
			// find the smallest N items and then restore sort order
			std::nth_element(lightsAndDistance.begin(), lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end(), CompareSecond2{});
			lightsAndDistance.erase(lightsAndDistance.begin()+_probeSlotsCount, lightsAndDistance.end());
			std::sort(lightsAndDistance.begin(), lightsAndDistance.end(), CompareFirst2{});
		}
		
		const int fadeTransitionInFrames = 16;

		// compare to the list lights currently in the database and figure out
		// evictions and new renderings
		std::pair<ILightScene::LightSourceId, float> potentialNewRenders[lightsAndDistance.size()];
		unsigned potentialRenderCount = 0;

		auto li = _associatedLights.begin();
		auto newStateIterator = lightsAndDistance.begin();
		assert(_probeSlotsCount <= 64u);	// has to be small, because we're 
		uint64_t availableProbeSlots = _unassociatedProbeSlots;
		while (newStateIterator != lightsAndDistance.end()) {
			while (li != _associatedLights.end() && li->first < newStateIterator->first) {
				// This light fell out of the close lights list
				li->second._fading = std::max(li->second._fading-1, 0);
				if (!li->second._fading && li->second._attachedProbe != ~0u)
					availableProbeSlots |= 1ull << uint64_t(li->second._attachedProbe);
				++li;
			}
			assert(li != _associatedLights.end());		// shouldn't happen so long as all lights in lightsAndDistance are in _associatedLights
			assert(li->first == newStateIterator->first);
			if (li->second._attachedProbe != ~0u) {
				li->second._fading = std::min(li->second._fading+1, fadeTransitionInFrames);
			} else {
				// This light is new to the close lights list. Note that newStateIterator->second is distance - cutoff range
				if (newStateIterator->second < drawDistance)
					potentialNewRenders[potentialRenderCount++] = *newStateIterator;
			}
			++li;
			++newStateIterator;
		}

		// all remaining lights fell off the close lights list
		for (; li!=_associatedLights.end(); ++li) {
			li->second._fading = std::max(li->second._fading-1, 0);
			if (!li->second._fading && li->second._attachedProbe != ~0u)
				availableProbeSlots |= 1ull << uint64_t(li->second._attachedProbe);
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
		auto i = LowerBound(_associatedLights, lightId);
		assert(i == _associatedLights.end() || i->first != lightId);		// attempting to add the same light twice

		AssociatedLight light;
		light._probeDesc = GetProbeDesc(*_lightScene, lightId);
		_associatedLights.emplace_back(lightId, light);
	}

	void SemiStaticShadowProbeScheduler::RemoveLight(ILightScene::LightSourceId lightId)
	{
		ScopedLock(_lock);
		auto i = LowerBound(_associatedLights, lightId);
		assert(i != _associatedLights.end() && i->first != lightId);		// attempt to remove a light that hasn't previously been added
		if (i != _associatedLights.end() && i->first != lightId) return;
		_associatedLights.erase(i);
	}

	SemiStaticShadowProbeScheduler::SemiStaticShadowProbeScheduler(std::shared_ptr<ShadowProbes> shadowProbes, ILightScene* lightScene) 
	: _shadowProbes(std::move(shadowProbes)), _lightScene(lightScene)
	{
		_probeSlotsCount = _shadowProbes->GetReservedProbeCount();
		assert(_probeSlotsCount <= 64);
		_unassociatedProbeSlots = (_probeSlotsCount == 64u) ? ~0ull : (1ull << uint64_t(_probeSlotsCount));
		_lastEvalBestRenders.reserve(_probeSlotsCount);
	}

}}}

