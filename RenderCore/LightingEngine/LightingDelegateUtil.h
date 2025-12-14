// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "StandardLightScene.h"		// for ILightSceneComponent
#include "ShadowProbes.h"
#include "ShadowPreparer.h"
#include "Sequence.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Types.h"
#include "../ResourceUtils.h"		// for ViewPool
#include "../Techniques/PipelineCollection.h"		// for FrameBufferTarget
#include "../../Assets/Marker.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
	class IFrameBufferPool;
	class IAttachmentPool;
	class IShaderResourceDelegate;
	struct PreregisteredAttachment;
}}

namespace RenderCore { namespace LightingEngine
{
	class IPreparedShadowResult;
	class ShadowProbes;
	class SequenceIterator;
	class Sequence;
	class IProbeRenderingInstance;
}}
namespace RenderCore { class IThreadContext; class IDevice; }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase;
	class PriorityShadowSchedulerUtil;

	std::future<std::shared_ptr<Techniques::IShaderResourceDelegate>> CreateDefaultSequencerResourceDelegate();
	UInt2 ExtractOutputResolution(IteratorRange<const Techniques::PreregisteredAttachment*>);
	UInt2 ExtractOutputResolution(IteratorRange<const Techniques::PreregisteredAttachment*>, uint64_t outputSemantic);

	struct SharedProbeSceneSet;

	// SemiStaticShadowProbeScheduler assumes that there is no animated content in the probes, and lights are not moving
	// In other words, the probes are only recalculated after eviction
	// There is a finite number of active probes, and the scheduler will attempt to make active only the most relevant lights
	// Probes can be updated asynchronously
	class SemiStaticShadowProbeScheduler : public ISemiStaticShadowProbeScheduler, public ILightSceneComponent
	{
	public:
		OnFrameBarrierResult OnFrameBarrier(const Float3& newViewPosition, float drawDistance) override;
		void SetNearRadius(float nearRadius) override;
		float GetNearRadius(float) override;
		void SetFadeTransition(unsigned) override;		// frame count

		std::shared_ptr<IProbeRenderingInstance> BeginPrepare(IThreadContext& threadContext, unsigned maxProbeCount) override;
		void EndPrepare(IThreadContext& threadContext) override;

		struct AllocatedDatabaseEntry
		{
			unsigned _databaseIndex = ~0u;
			int _fading = 0;
		};
		AllocatedDatabaseEntry GetAllocatedDatabaseEntry(unsigned setIdx, unsigned lightIdx);

		bool DoneInitialBackgroundPrepare() const { return _doneInitialBackgroundPrepare; }		// when this is false, the shadow probes image is probably still in an undefined layout

		SemiStaticShadowProbeScheduler(
			std::shared_ptr<ShadowProbes> shadowProbes,
			ILightScene::LightOperatorId operatorId);
		~SemiStaticShadowProbeScheduler();
	private:
		Threading::Mutex _lock;

		using LightIndex = uint64_t;		// encoded set index and light index within that set
		std::vector<LightIndex> _lastEvalBestRenders;
		uint64_t _lastEvalAvailableProbeSlots = 0;
		uint64_t _unassociatedProbeSlots = 0ull;
		unsigned _probeSlotsCount = 0;

		uint64_t _probeSlotsReservedInBackground = 0ull;
		std::vector<std::pair<LightIndex, unsigned>> _probeSlotsPreparedInBackground;
		bool _readyToCommitBackgroundChanges = false;
		bool _doneInitialBackgroundPrepare = false;
		void CommitBackgroundChangesAlreadyLocked();

		std::vector<std::pair<LightIndex, AllocatedDatabaseEntry>> _allocatedDatabaseEntries;

		std::shared_ptr<ShadowProbes> _shadowProbes;

		std::vector<SharedProbeSceneSet> _sceneSets;
		ILightScene::LightOperatorId _operatorId;
		float _defaultNearRadius = 1.f;
		unsigned _fadeTransitionInFrames = 16;

		// ILightSceneComponent
		void RegisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx, ILightBase& light) override;
		void DeregisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, unsigned setIdx) override;
		void* QueryInterface(LightSetId setIdx, ILightScene::LightSourceId lightIdx, uint64_t interfaceTypeCode) override;
	};

	// DynamicShadowProbeScheduler is like SemiStaticShadowProbeScheduler, but probes are updated every frame
	// Probe update is handled synchronously with the main scene render. Lights can be shadowed by both
	// SemiStaticShadowProbeScheduler and DynamicShadowProbeScheduler, allowing animated and static shadowing
	// geometry to be handled separately
	class DynamicShadowProbeScheduler : public ILightSceneComponent
	{
	public:
		void SetNearRadius(float nearRadius);
		float GetNearRadius(float);
		void SetFadeTransition(unsigned newValue);

		void DoShadowPrepare(
			SequenceIterator& iterator,
			Sequence& sequence);
		void ClearPreparedShadows();

		struct AllocatedDatabaseEntry
		{
			unsigned _databaseIndex = ~0u;
			int _fading = 0;
			bool _active = true;
		};
		AllocatedDatabaseEntry GetAllocatedDatabaseEntry(unsigned setIdx, unsigned lightIdx);

		DynamicShadowProbeScheduler(
			std::shared_ptr<DynamicShadowProbes> shadowProbes,
			std::shared_ptr<PriorityShadowSchedulerUtil> shadowPreparers);
		~DynamicShadowProbeScheduler();
	private:
		using LightIndex = uint64_t;		// encoded set index and light index within that set

		std::vector<SharedProbeSceneSet> _sceneSets;
		std::shared_ptr<DynamicShadowProbes> _shadowProbes;
		std::vector<std::pair<LightIndex, AllocatedDatabaseEntry>> _activeLights[2];
		float _defaultNearRadius = 1.f;
		unsigned _fadeTransitionInFrames = 16;
		unsigned _probeSlotsCount = 0;
		uint64_t _unassociatedProbeSlots = 0ull;

		void UpdateActiveLights(const Float3& newViewPosition, float drawDistance, const Float4x4& worldToClipSpace);

		// ILightSceneComponent
		void RegisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx, ILightBase& light) override;
		void DeregisterLight(LightSetId setIdx, ILightScene::LightSourceId lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, unsigned setIdx) override;
		void* QueryInterface(LightSetId setIdx, ILightScene::LightSourceId lightIdx, uint64_t interfaceTypeCode) override;
	};

	class IDynamicShadowProjectionScheduler
	{
	public:
		virtual void SetDescriptorSetLayout(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
			PipelineType pipelineType) = 0;
		virtual ~IDynamicShadowProjectionScheduler() = default;
	};

	// PriorityShadowProjectionScheduler handles shadow projections that are always active, and are recalculated every frame
	// It's typically used for the dominant light, player held torches, or other important shadowing sources that should
	// never go inactive
	class PriorityShadowProjectionScheduler : public IDynamicShadowProjectionScheduler, public ILightSceneComponent
	{
	public:
		const IPreparedShadowResult* GetPreparedShadow(unsigned setIdx, unsigned lightIdx);

		struct PreparedShadow { unsigned _preparerIdx = ~0u; const IPreparedShadowResult* _preparedResult = nullptr; };
		std::vector<PreparedShadow> GetAllPreparedShadows();		// intended for debugging

		void SetDescriptorSetLayout(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
			PipelineType pipelineType) override;

		void DoShadowPrepare(
			SequenceIterator& iterator,
			Sequence& sequence);
		void ClearPreparedShadows();

		struct SceneSet;
		std::vector<SceneSet> _sceneSets;

		std::shared_ptr<PriorityShadowSchedulerUtil> _shadowPreparers;
		unsigned _totalProjectionCount;

		PriorityShadowProjectionScheduler(std::shared_ptr<PriorityShadowSchedulerUtil> shadowPreparers);
		~PriorityShadowProjectionScheduler();
	private:
		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) override;

		std::shared_ptr<Techniques::IFrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::IAttachmentPool> _shadowGenAttachmentPool;
		ViewPool _shadowGenViewPool;
		std::vector<unsigned> _operatorToPreparerIdMapping;
	};

	std::future<std::shared_ptr<PriorityShadowSchedulerUtil>> CreatePriorityShadowSchedulerUtil(
		IteratorRange<const std::pair<unsigned, ShadowOperatorDesc>*> shadowGenerators, 				// src light operator & shadow generator
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerator,
		const std::shared_ptr<SharedTechniqueDelegateBox>& delegatesBox);

	class DominantLightSet : public ILightSceneComponent
	{
	public:
		unsigned _setIdx = ~0u;
		bool _hasLight = false;
		ILightScene::LightOperatorId _lightOpId;

		DominantLightSet(ILightScene::LightOperatorId lightOpId);
		~DominantLightSet();
	private:
		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) override;
	};

	class ShaderResourceSplitter : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override;
		void WriteSamplers(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst) override;
		void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override;
		size_t GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override;

		ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one);
		ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one, std::shared_ptr<Techniques::IShaderResourceDelegate> two);
		ShaderResourceSplitter(std::shared_ptr<Techniques::IShaderResourceDelegate> zero, std::shared_ptr<Techniques::IShaderResourceDelegate> one, std::shared_ptr<Techniques::IShaderResourceDelegate> two, std::shared_ptr<Techniques::IShaderResourceDelegate> three);
	protected:
		std::shared_ptr<Techniques::IShaderResourceDelegate> _subDelegates[4];
		unsigned _srvOffsets[5], _samplerOffsets[5], _immDataOffsets[5];
		void Configure();
	};

	/////////////////////////// utility functions ////////////////////////
	template<typename Dest>
		const Dest& ChainedOperatorCast(const ChainedOperatorDesc& desc)
	{
		assert(desc._structureType == ctti::type_id<Dest>().hash());
		return ((const ChainedOperatorTemplate<Dest>*)&desc)->_desc;
	}

	template<typename Type, typename... Params>
		std::future<std::shared_ptr<Type>> SecondStageConstruction(
			Type& op, Params&&... params)
	{
		std::promise<std::shared_ptr<Type>> promise;
		auto future = promise.get_future();
		op.SecondStageConstruction(std::move(promise), std::forward<Params>(params)...);
		return future;
	}

	inline Techniques::FrameBufferTarget AsFrameBufferTarget(
		Sequence& sequence,
		Sequence::FragmentInterfaceRegistration regId)
	{
		auto resolvedFB = sequence.GetResolvedFrameBufferDesc(regId);
		return Techniques::FrameBufferTarget{resolvedFB.first, resolvedFB.second};
	}

	template<typename MarkerType, typename Time>
		bool MarkerTimesOut(std::future<MarkerType>& marker, Time timeoutTime) { return marker.wait_until(timeoutTime) == std::future_status::timeout; }
	template<typename MarkerType, typename Time>
		bool MarkerTimesOut(std::shared_future<MarkerType>& marker, Time timeoutTime) { return marker.wait_until(timeoutTime) == std::future_status::timeout; }
	template<typename MarkerType, typename Time>
		bool MarkerTimesOut(::Assets::Marker<MarkerType>& marker, Time timeoutTime)
	{
		auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
		if (remainingTime.count() <= 0) return true;
		auto t = marker.StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
		return t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending;
	}

	/////////////////////////////// inlines //////////////////////////////////
	class SequencerAddendums;
	struct PriorityShadowProjectionScheduler::SceneSet
	{
		using ShadowProjectionBasePtr = std::unique_ptr<ILightBase>;
		std::vector<ShadowProjectionBasePtr> _projections;
		std::vector<std::shared_ptr<IPreparedShadowResult>> _preparedResult;
		std::vector<SequencerAddendums> _addendums;
		BitHeap _activeProjections;
		bool _activeSet = false;
		std::shared_ptr<PriorityShadowSchedulerUtil> _preparers;
		unsigned _preparerId = ~0u;

		void RegisterLight(unsigned index, ILightBase& light);
		void DeregisterLight(unsigned index);
		SceneSet();
		SceneSet(SceneSet&&);
		SceneSet& operator=(SceneSet&&);
	};

	inline auto PriorityShadowProjectionScheduler::GetPreparedShadow(unsigned setIdx, unsigned lightIdx) -> const IPreparedShadowResult*
	{
		if (setIdx >= _sceneSets.size() || !_sceneSets[setIdx]._activeSet) return {};
		assert(_sceneSets[setIdx]._activeProjections.IsAllocated(lightIdx));
		return _sceneSets[setIdx]._preparedResult[lightIdx].get();
	}

}}}

