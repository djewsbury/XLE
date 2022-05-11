// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "../../Utility/ImpliedTyping.h"

namespace RenderCore { namespace Assets { class ModelScaffold; }}

namespace RenderCore { namespace Techniques
{
	std::shared_ptr<IDeformParametersAttachment> CreateDeformParametersAttachment(
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName = {});

	class AnimatedParameterBinding
	{
	public:
		uint64_t _name;
		ImpliedTyping::TypeDesc _type;
		unsigned _offset;
	};

	class ActualizedDescriptorSet;
	namespace Internal
	{
		inline unsigned GetDynamicPageResourceSize(const ActualizedDescriptorSet& descSet); // { return descSet._dynamicPageBufferSize; }
		bool PrepareDynamicPageResource(
			const ActualizedDescriptorSet& descSet,
			IteratorRange<const void*> animatedParameters,
			IteratorRange<void*> dynamicPageBuffer);
	}
}}
