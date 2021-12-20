// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExecuteScene.h"
#include "../RenderCore/LightingEngine/LightingEngine.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../Assets/Marker.h"

namespace SceneEngine
{
	void ExecuteSceneRaw(
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const SceneView& view, RenderCore::Techniques::BatchFilter batchFilter,
		IScene& scene)
    {
		RenderCore::Techniques::DrawablesPacket pkt;
        scene.ExecuteScene(parserContext.GetThreadContext(), ExecuteSceneContext{view, batchFilter, &pkt});
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

	std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
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
			assert(next._pkt);

			SceneView view { SceneView::Type::PrepareResources };
			scene.ExecuteScene(threadContext, ExecuteSceneContext{view, Techniques::BatchFilter::General, next._pkt});
		}

		return prepareLightingIterator.GetResourcePreparationMarker();
	}

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> StallAndActualize(::Assets::MarkerPtr<RenderCore::LightingEngine::CompiledLightingTechnique>& future)
	{
		future.StallWhilePending();
		return future.Actualize();
	}
}
