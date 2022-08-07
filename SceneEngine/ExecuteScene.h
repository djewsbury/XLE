// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once 

#include "IScene.h"
#include "../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { namespace Techniques 
{
    class IPipelineAcceleratorPool;
	class ParsingContext;
	class SequencerConfig;
	class ProjectionDesc;
	enum class Batch;
	struct PreparedResourcesVisibility;
}}

namespace RenderCore { namespace LightingEngine 
{
    class CompiledLightingTechnique;
    class LightingTechniqueInstance;
}}
namespace RenderCore { class IThreadContext; }
namespace Assets { class IAsyncMarker; }
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

	std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> StallAndActualize(::Assets::MarkerPtr<RenderCore::LightingEngine::CompiledLightingTechnique>&);
}
