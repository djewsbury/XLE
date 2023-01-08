// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NodeGraphSignature.h"
#include "../RenderCore/Assets/PredefinedDescriptorSetLayout.h"
#include "../RenderCore/ShaderLangUtil.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <memory>
#include <ios>

namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}

namespace ShaderSourceParser
{
	std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> MakeMaterialDescriptorSet(
		IteratorRange<const GraphLanguage::NodeGraphSignature::Parameter*> captures,
		RenderCore::ShaderLanguage shaderLanguage,
		std::ostream& warningStream);

	namespace LinkToFixedLayoutFlags
	{
		enum {
			// When AllowSlotTypeModification is on, "pipelineLayoutVersion" is used as a rough template only. We reuse compatible
			// slots in "pipelineLayoutVersion" when possible, but otherwise change slots, remove slots and add new slots as necessary
			// With this flag, "pipelineLayoutVersion" can be empty
			AllowSlotTypeModification = 1<<1
		};
		using BitField = unsigned;
	}

	std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> LinkToFixedLayout(
		const RenderCore::Assets::PredefinedDescriptorSetLayout& input,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& pipelineLayoutVersion,
		LinkToFixedLayoutFlags::BitField = LinkToFixedLayoutFlags::AllowSlotTypeModification);

	RenderCore::DescriptorType CalculateDescriptorType(StringSection<> type);
}
