// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "LightingEngine.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/CommonBindings.h"
#include "../ResourceDesc.h"		// for TextureSamples
#include "../../Assets/AssetsCore.h"

namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; class PipelineCollection; class IPipelineAcceleratorPool; } }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace RenderCore { class IDevice; class FrameBufferProperties; }
namespace std { template<typename T> class future; }

namespace RenderCore { namespace LightingEngine
{
	struct ChainedOperatorDesc;

	struct UtilityLightingTechniqueDesc
	{
		Techniques::UtilityDelegateType _type = Techniques::UtilityDelegateType::SolidWireframe;
		uint64_t _outputAttachment = Techniques::AttachmentSemantics::ColorLDR;
	};

	void CreateUtilityLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		CreationUtility&,
		const ChainedOperatorDesc* globalOperators,
		CreationUtility::OutputTarget outputTarget);
}}

