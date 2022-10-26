// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ExecuteScene.h"
#include "../RenderCore/LightingEngine/LightingEngine.h"
#include "../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../RenderCore/LightingEngine/LightingEngineApparatus.h"
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
		RenderCore::Techniques::DrawablesPacket* pkts[(unsigned)RenderCore::Techniques::Batch::Max] {};
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

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> CreateAndActualizeForwardLightingTechnique(
		RenderCore::LightingEngine::LightingEngineApparatus& apparatus,
		std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const RenderCore::FrameBufferProperties& fbProps)
	{
		std::promise<std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique>> promise;
		auto future = promise.get_future();
		RenderCore::LightingEngine::CreateForwardLightingTechnique(
			std::move(promise),
			apparatus._pipelineAccelerators, apparatus._lightingOperatorCollection, apparatus._sharedDelegates,
			std::move(lightScene),
			preregisteredAttachments, fbProps);
		return future.get();
	}

	std::shared_ptr<RenderCore::LightingEngine::ILightScene> CreateAndActualizeForwardLightingScene(
		RenderCore::LightingEngine::LightingEngineApparatus& apparatus,
		IteratorRange<const RenderCore::LightingEngine::LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const RenderCore::LightingEngine::ShadowOperatorDesc*> shadowGenerators,
		const RenderCore::LightingEngine::AmbientLightOperatorDesc& ambientLightOperator)
	{
		std::promise<std::shared_ptr<RenderCore::LightingEngine::ILightScene>> promise;
		auto future = promise.get_future();
		RenderCore::LightingEngine::CreateForwardLightingScene(
			std::move(promise),
			apparatus._pipelineAccelerators, apparatus._lightingOperatorCollection, apparatus._sharedDelegates,
			resolveOperators, shadowGenerators, ambientLightOperator);
		return future.get();
	}
}
