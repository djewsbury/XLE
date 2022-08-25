// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "StandardLightScene.h"
#include "ShadowProbes.h"
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
}}
namespace RenderCore { class IThreadContext; }

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase;
	class ILightSceneComponent;

	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		PipelineType descSetPipelineType,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool);

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

		class SceneSet;
		std::vector<SceneSet> _sceneSets;
		ILightScene::ShadowOperatorId _operatorId;
		float _defaultNearRadius = 1.f;

		// ILightSceneComponent
		void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) override;
		void DeregisterLight(unsigned setIdx, unsigned lightIdx) override;
		bool BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned setIdx) override;
		void* QueryInterface(unsigned lightIdx, uint64_t interfaceTypeCode) override;
	};
	
}}}

