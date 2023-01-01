// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../../Assets/AssetsCore.h"

namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; class PipelineCollection; class IPipelineAcceleratorPool; } }
namespace RenderCore { class IDevice; class FrameBufferProperties; class ICompiledPipelineLayout; }
namespace RenderCore { namespace Assets { class PredefinedPipelineLayoutFile; class PredefinedDescriptorSetLayout; }}
namespace std { template<typename T> class future; }

namespace RenderCore { namespace LightingEngine
{
	struct LightSourceOperatorDesc;
	struct ShadowOperatorDesc;

	struct DeferredLightingTechniqueDesc
	{
	};

	namespace DeferredLightingTechniqueFlags
	{
		enum Enum { GenerateDebuggingTextures = 1<<0 };
		using BitField = unsigned;
	};

	void CreateDeferredLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelineCollection>& pipelineCollection,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const ChainedOperatorDesc* globalOperators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		DeferredLightingTechniqueFlags::BitField flags = 0);
}}

