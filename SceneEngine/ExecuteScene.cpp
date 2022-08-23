// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExecuteScene.h"
#include "../RenderCore/LightingEngine/LightingEngine.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/Drawables.h"
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
		RenderCore::Techniques::DrawablesPacket* pkts[(unsigned)RenderCore::Techniques::Batch::Max];
		pkts[(unsigned)batch] = &pkt;
		ExecuteSceneContext executeContext{MakeIteratorRange(pkts), MakeIteratorRange(&view, &view+1)};
        scene.ExecuteScene(parserContext.GetThreadContext(), executeContext);
		parserContext.RequireCommandList(executeContext._completionCmdList);
		RenderCore::Techniques::Draw(parserContext, pipelineAccelerators, sequencerConfig, pkt);
    }

    RenderCore::LightingEngine::LightingTechniqueInstance BeginLightingTechnique(
		RenderCore::Techniques::ParsingContext& parsingContext,
		SceneEngine::ILightingStateDelegate& lightingState,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique)
	{
		auto& lightScene = RenderCore::LightingEngine::GetLightScene(compiledTechnique);
		lightingState.PreRender(parsingContext.GetProjectionDesc(), lightScene);
		return RenderCore::LightingEngine::LightingTechniqueInstance { parsingContext, compiledTechnique };
	}

	std::future<RenderCore::Techniques::PreparedResourcesVisibility> PrepareResources(
		RenderCore::IThreadContext& threadContext,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique,
		IScene& scene)
	{
		using namespace RenderCore;
		LightingEngine::LightingTechniqueInstance prepareLightingIterator(compiledTechnique);

		for (;;) {
			auto next = prepareLightingIterator.GetNextStep();
			if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
			if (next._type == LightingEngine::StepType::DrawSky) continue;
			assert(next._type == LightingEngine::StepType::ParseScene);
			assert(!next._pkts.empty());

			ExecuteSceneContext sceneExecuteContext{MakeIteratorRange(next._pkts), next._multiViewDesc, next._complexCullingVolume};
			scene.ExecuteScene(threadContext, sceneExecuteContext);
		}

		std::promise<RenderCore::Techniques::PreparedResourcesVisibility> promise;
		auto result = promise.get_future();
		prepareLightingIterator.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> StallAndActualize(::Assets::MarkerPtr<RenderCore::LightingEngine::CompiledLightingTechnique>& future)
	{
		future.StallWhilePending();
		return future.Actualize();
	}
}
