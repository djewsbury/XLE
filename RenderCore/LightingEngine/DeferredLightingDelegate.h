// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../../Assets/AssetsCore.h"

namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; class GraphicsPipelineCollection; } }
namespace RenderCore { class IDevice; class FrameBufferProperties; }
namespace RenderCore { namespace Assets { class PredefinedPipelineLayoutFile; }}

namespace RenderCore { namespace LightingEngine
{
	class LightSourceOperatorDesc;
	class ShadowOperatorDesc;

	namespace DeferredLightingTechniqueFlags
	{
		enum Enum { GenerateDebuggingTextures = 1<<0 };
		using BitField = unsigned;
	};

	::Assets::FuturePtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags = 0);

	::Assets::FuturePtr<CompiledLightingTechnique> CreateDeferredLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<Techniques::GraphicsPipelineCollection>& pipelineCollection,
		const std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile>& lightingOperatorsPipelineLayoutFile,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		DeferredLightingTechniqueFlags::BitField flags = 0);
}}

