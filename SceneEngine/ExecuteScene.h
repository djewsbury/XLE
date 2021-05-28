// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once 

#include "IScene.h"
#include <memory>

namespace RenderCore { namespace Techniques 
{
    class IPipelineAcceleratorPool;
    class SequencerContext;
	class ParsingContext;
}}

namespace RenderCore { namespace LightingEngine 
{
    class CompiledLightingTechnique;
    class LightingTechniqueInstance;
}}
namespace RenderCore { class IThreadContext; }
namespace Assets { class IAsyncMarker; template<typename Type> class AssetFuture;}

namespace SceneEngine
{
	void ExecuteSceneRaw(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		const RenderCore::Techniques::SequencerContext& sequencerTechnique,
		const SceneView& view, RenderCore::Techniques::BatchFilter batchFilter,
		IScene& scene);

    RenderCore::LightingEngine::LightingTechniqueInstance BeginLightingTechnique(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::ParsingContext& parsingContext,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		SceneEngine::ILightingStateDelegate& lightingState,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique);

	std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
		RenderCore::LightingEngine::CompiledLightingTechnique& compiledTechnique,
		IScene& scene);

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> StallAndActualize(::Assets::AssetFuture<RenderCore::LightingEngine::CompiledLightingTechnique>&);
}
