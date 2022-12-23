// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../../Assets/AssetsCore.h"

namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; class PipelineCollection; class IPipelineAcceleratorPool; } }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace RenderCore { class IDevice; class FrameBufferProperties; }
namespace std { template<typename T> class future; }

namespace RenderCore { namespace LightingEngine
{
	struct ShadowOperatorDesc;
	struct LightSourceOperatorDesc;
	struct AmbientLightOperatorDesc;
	struct ChainedOperatorDesc;

	void CreateForwardLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const ChainedOperatorDesc& globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps);

#if 0
	void CreateForwardLightingScene(
		std::promise<std::shared_ptr<ILightScene>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator);
#endif

	// Simplified construction --
	std::future<std::shared_ptr<CompiledLightingTechnique>> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc& globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps);

	bool ForwardLightingTechniqueIsCompatible(
		CompiledLightingTechnique& technique,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator);
}}

