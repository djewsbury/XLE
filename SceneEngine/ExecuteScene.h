// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once 

#include "IScene.h"
#include <memory>

namespace RenderCore { namespace Techniques 
{
    class IPipelineAcceleratorPool;
	class ParsingContext;
	class SequencerConfig;
	class ProjectionDesc;
	enum class Batch;
	struct PreparedResourcesVisibility;
	struct PreregisteredAttachment;
}}

namespace RenderCore { namespace LightingEngine 
{
    class CompiledLightingTechnique;
    class LightingTechniqueInstance;
	class LightingEngineApparatus;
	class LightSourceOperatorDesc;
	class ShadowOperatorDesc;
	class AmbientLightOperatorDesc;
}}
namespace RenderCore { class IThreadContext; class FrameBufferProperties; }
namespace std { template<typename Type> class future; }

namespace SceneEngine
{
	void ExecuteSceneRaw(
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::ProjectionDesc& view, RenderCore::Techniques::Batch batch,
		IScene& scene);

    RenderCore::LightingEngine::LightingTechniqueInstance BeginLightingTechnique(
		RenderCore::Techniques::ParsingContext& parsingContext,
		SceneEngine::ILightingStateDelegate& lightingState,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique);

	std::future<RenderCore::Techniques::PreparedResourcesVisibility> PrepareResources(
		RenderCore::IThreadContext& threadContext,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique,
		IScene& scene);

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> CreateAndActualizeForwardLightingTechnique(
		const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& apparatus,
		IteratorRange<const RenderCore::LightingEngine::LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const RenderCore::LightingEngine::ShadowOperatorDesc*> shadowGenerators,
		const RenderCore::LightingEngine::AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const RenderCore::FrameBufferProperties& fbProps);
}
