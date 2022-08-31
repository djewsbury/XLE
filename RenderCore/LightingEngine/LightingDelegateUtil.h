// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "StandardLightScene.h"		// for ILightSceneComponent
#include "ShadowProbes.h"
#include "ShadowPreparer.h"
#include "../Types.h"
#include "../../Assets/Marker.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
	class FrameBufferPool;
	class AttachmentPool;
	class IShaderResourceDelegate;
}}

namespace RenderCore { namespace LightingEngine
{
	class IPreparedShadowResult;
	class ShadowProbes;
	class LightingTechniqueIterator;
	class LightingTechniqueSequence;
	class IProbeRenderingInstance;
	class DynamicShadowPreparers;
}}
namespace RenderCore { class IThreadContext; class IDevice; }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase;
	class ILightSceneComponent;

	::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate();

	class SemiStaticShadowProbeScheduler : public ISemiStaticShadowProbeScheduler, public ILightSceneComponent
	{
	public:
		OnFrameBarrierResult OnFrameBarrier(const Float3& newViewPosition, float drawDistance) override;
		void SetNearRadius(float nearRadius) override;
		float GetNearRadius(float) override;

		std::shared_ptr<IProbeRenderingInstance> BeginPrepare(IThreadContext& threadContext, unsigned maxProbeCount) override;
		void EndPrepare(IThreadContext& threadContext) override;

		struct AllocatedDatabaseEntry
		{
			unsigned _databaseIndex = ~0u;
			int _fading = 0;
		};
		AllocatedDatabaseEntry GetAllocatedDatabaseEntry(unsigned setIdx, unsigned lightIdx);

		SemiStaticShadowProbeScheduler(
			std::shared_ptr<ShadowProbes> shadowProbes,
			ILightScene::ShadowOperatorId operatorId);
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
		void CommitBackgroundChangesAlreadyLocked();

		std::vector<std::pair<LightIndex, AllocatedDatabaseEntry>> _allocatedDatabaseEntries;

		std::shared_ptr<ShadowProbes> _shadowProbes;

		struct SceneSet;
		std::vector<SceneSet> _sceneSets;
		ILightScene::ShadowOperatorId _operatorId;
		float _defaultNearRadius = 1.f;

		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) override;
	};

	class DynamicShadowProjectionScheduler : public IDynamicShadowProjectionScheduler, public ILightSceneComponent
	{
	public:
		const IPreparedShadowResult* GetPreparedShadow(unsigned setIdx, unsigned lightIdx);

		struct PreparedShadow { unsigned _preparerIdx = ~0u; const IPreparedShadowResult* _preparedResult = nullptr; };
		std::vector<PreparedShadow> GetAllPreparedShadows();		// intended for debugging

		void SetDescriptorSetLayout(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& descSetLayout,
			PipelineType pipelineType) override;

		void DoShadowPrepare(
			LightingTechniqueIterator& iterator,
			LightingTechniqueSequence& sequence);
		void ClearPreparedShadows();

		struct SceneSet;
		std::vector<SceneSet> _sceneSets;

		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		unsigned _totalProjectionCount;

		DynamicShadowProjectionScheduler(
			std::shared_ptr<IDevice> device, std::shared_ptr<DynamicShadowPreparers> shadowPreparers, 
			IteratorRange<const unsigned*> operatorToPreparerIdMapping);
		~DynamicShadowProjectionScheduler();
	private:
		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) override;

		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::vector<unsigned> _operatorToPreparerIdMapping;
	};

	class DominantLightSet : public ILightSceneComponent
	{
	public:
		unsigned _setIdx = ~0u;
		bool _hasLight = false;
		ILightScene::LightOperatorId _lightOpId;
		ILightScene::ShadowOperatorId _shadowOpId;

		DominantLightSet(ILightScene::LightOperatorId lightOpId, ILightScene::ShadowOperatorId shadowOpId);
		~DominantLightSet();
	private:
		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) override;
	};

	/////////////////////////////// inlines //////////////////////////////////
	class SequencerAddendums;
	struct DynamicShadowProjectionScheduler::SceneSet
	{
		using ShadowProjectionBasePtr = std::unique_ptr<ILightBase>;
		std::vector<ShadowProjectionBasePtr> _projections;
		std::vector<std::shared_ptr<IPreparedShadowResult>> _preparedResult;
		std::vector<SequencerAddendums> _addendums;
		BitHeap _activeProjections;
		bool _activeSet = false;
		std::shared_ptr<DynamicShadowPreparers> _preparers;
		unsigned _preparerId = ~0u;

		void RegisterLight(unsigned index, ILightBase& light);
		void DeregisterLight(unsigned index);
		SceneSet();
		SceneSet(SceneSet&&);
		SceneSet& operator=(SceneSet&&);
	};

	inline auto DynamicShadowProjectionScheduler::GetPreparedShadow(unsigned setIdx, unsigned lightIdx) -> const IPreparedShadowResult*
	{
		if (setIdx >= _sceneSets.size() || !_sceneSets[setIdx]._activeSet) return {};
		assert(_sceneSets[setIdx]._activeProjections.IsAllocated(lightIdx));
		return _sceneSets[setIdx]._preparedResult[lightIdx].get();
	}
	
}}}

