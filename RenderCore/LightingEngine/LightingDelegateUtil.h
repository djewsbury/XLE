// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
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
	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		PipelineType descSetPipelineType,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool);

	::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate();

	class SemiStaticShadowProbeScheduler : public ISemiStaticShadowProbeScheduler
	{
	public:
		void AddLight(ILightScene::LightSourceId);
		void RemoveLight(ILightScene::LightSourceId);

		OnFrameBarrierResult OnFrameBarrier(const Float3& newViewPosition, float drawDistance) override;
		void SetNearRadius(float nearRadius) override;
		float GetNearRadius(float) override;

		std::shared_ptr<IProbeRenderingInstance> BeginPrepare(IThreadContext& threadContext, unsigned maxProbeCount) override;
		void EndPrepare(IThreadContext& threadContext) override;

		SemiStaticShadowProbeScheduler(
			std::shared_ptr<ShadowProbes> shadowProbes,
			ILightScene* lightScene) ;
	private:
		Threading::Mutex _lock;
		std::vector<ILightScene::LightSourceId> _lastEvalBestRenders;
		uint64_t _lastEvalAvailableProbeSlots = 0;
		uint64_t _unassociatedProbeSlots = 0ull;
		unsigned _probeSlotsCount = 0;

		uint64_t _probeSlotsReservedInBackground = 0ull;
		std::vector<std::pair<ILightScene::LightSourceId, unsigned>> _probeSlotsPreparedInBackground;

		struct AssociatedLight
		{
			ShadowProbes::Probe _probeDesc;
			unsigned _attachedProbe = ~0u;
			int _fading = 0;
		};
		std::vector<std::pair<ILightScene::LightSourceId, AssociatedLight>> _associatedLights;

		std::shared_ptr<ShadowProbes> _shadowProbes;
		ILightScene* _lightScene;
		float _defaultNearRadius = 1.f;
	};
	
}}}

