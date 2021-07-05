// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExecuteScene.h"
#include "../RenderCore/LightingEngine/LightingEngine.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../Assets/AssetFuture.h"

namespace SceneEngine
{
	void ExecuteSceneRaw(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const SceneView& view, RenderCore::Techniques::BatchFilter batchFilter,
		IScene& scene)
    {
		RenderCore::Techniques::DrawablesPacket pkt;
        scene.ExecuteScene(threadContext, ExecuteSceneContext{view, batchFilter, &pkt});
		RenderCore::Techniques::Draw(threadContext, parserContext, pipelineAccelerators, sequencerConfig, pkt);
    }

    RenderCore::LightingEngine::LightingTechniqueInstance BeginLightingTechnique(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		SceneEngine::ILightingStateDelegate& lightingState,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique)
	{
		auto& lightScene = RenderCore::LightingEngine::GetLightScene(compiledTechnique);
		lightingState.ConfigureLightScene(parsingContext.GetProjectionDesc(), lightScene);
		return RenderCore::LightingEngine::LightingTechniqueInstance { threadContext, parsingContext, pipelineAccelerators, compiledTechnique };
	}

	std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique,
		IScene& scene)
	{
		using namespace RenderCore;
		LightingEngine::LightingTechniqueInstance prepareLightingIterator(pipelineAccelerators, compiledTechnique);

		for (;;) {
			auto next = prepareLightingIterator.GetNextStep();
			if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
			if (next._type == LightingEngine::StepType::DrawSky) continue;
			assert(next._type == LightingEngine::StepType::ParseScene);
			assert(next._pkt);

			SceneView view { SceneView::Type::PrepareResources };
			scene.ExecuteScene(threadContext, ExecuteSceneContext{view, Techniques::BatchFilter::General, next._pkt});
		}

		return prepareLightingIterator.GetResourcePreparationMarker();
	}

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> StallAndActualize(::Assets::FuturePtr<RenderCore::LightingEngine::CompiledLightingTechnique>& future)
	{
		future.StallWhilePending();
		return future.Actualize();
	}
}
