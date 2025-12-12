// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExecuteScene.h"
#include "../RenderCore/LightingEngine/LightingEngine.h"
#include "../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/Drawables.h"
#include "../RenderCore/Techniques/Services.h"
#include "../Assets/Marker.h"

namespace SceneEngine
{
	void ExecuteSceneRaw(
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::ProjectionDesc& view, RenderCore::Techniques::Batch batch,
		IScene& scene)
    {
		RenderCore::Techniques::DrawablesPacket pkt;
		unsigned batchCount = RenderCore::Techniques::Services::GetInstance().BatchCodeCount();
		VLA(RenderCore::Techniques::DrawablesPacket*, pkts, batchCount);
		std::memset(pkts, 0, sizeof(void*)*batchCount);
		pkts[(unsigned)batch] = &pkt;
		ExecuteSceneContext executeContext{MakeIteratorRange(pkts, pkts+batchCount), nullptr, MakeIteratorRange(&view, &view+1)};
        scene.ExecuteScene(parserContext.GetThreadContext(), executeContext);
		parserContext.RequireCommandList(executeContext._completionCmdList);
		RenderCore::Techniques::Draw(parserContext, pipelineAccelerators, sequencerConfig, pkt);
    }

    RenderCore::LightingEngine::SequencePlayback BeginLightingTechnique(
		RenderCore::Techniques::ParsingContext& parsingContext,
		SceneEngine::ILightingStateDelegate& lightingState,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique)
	{
		if (auto* lightScene = RenderCore::LightingEngine::QueryInterface<RenderCore::LightingEngine::ILightScene>(compiledTechnique))
			lightingState.PreRender(parsingContext.GetProjectionDesc(), *lightScene);
		return RenderCore::LightingEngine::BeginLightingTechniquePlayback( parsingContext, compiledTechnique );
	}

	std::future<RenderCore::Techniques::PreparedResourcesVisibility> PrepareResources(
		RenderCore::IThreadContext& threadContext,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		IScene& scene)
	{
		std::promise<RenderCore::Techniques::PreparedResourcesVisibility> promise;
		auto result = promise.get_future();

		TRY {

			using namespace RenderCore;
			auto prepareLightingIterator = LightingEngine::BeginPrepareResourcesInstance(pipelineAccelerators, compiledTechnique);

			for (;;) {
				auto next = prepareLightingIterator.GetNextStep();
				if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
				if (next._type == LightingEngine::StepType::Signal) continue;
				assert(next._type == LightingEngine::StepType::ParseScene);
				assert(!next._pkts.empty());

				ExecuteSceneContext sceneExecuteContext{MakeIteratorRange(next._pkts), next._deformersPacket, next._multiViewDesc, next._complexCullingVolume};
				scene.ExecuteScene(threadContext, sceneExecuteContext);
			}

			prepareLightingIterator.FulfillWhenNotPending(std::move(promise));

		} CATCH(...) {

			promise.set_exception(std::current_exception());

		} CATCH_END

		return result;
	}

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> CreateAndActualizeLightingTechnique(
		RenderCore::LightingEngine::LightingEngineApparatus& apparatus,
		const RenderCore::LightingEngine::ChainedOperatorDesc* globalOperators,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregisteredAttachments)
	{
		return RenderCore::LightingEngine::CreationUtility{apparatus}
			.CreateTechniqueToFuture(globalOperators, {preregisteredAttachments}).get();
	}
}
