// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "../../Utility/ImpliedTyping.h"

namespace RenderCore { namespace Assets { class RendererConstruction; }}

namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	class DescriptorSetLayoutAndBinding;
	
	struct AnimatedUniform
	{
		uint64_t _name;
		ImpliedTyping::TypeDesc _type;
		unsigned _offset;
	};
	
	void ConfigureDeformUniformsAttachment(
		DeformerConstruction& deformerConstruction,
		const Assets::RendererConstruction& rendererConstruction,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		IteratorRange<const AnimatedUniform*> animatedUniforms,
		IteratorRange<const void*> defaultInstanceData);

	class ActualizedDescriptorSet;
	namespace Internal
	{
		inline unsigned GetDynamicPageResourceSize(const ActualizedDescriptorSet& descSet); // { return descSet._dynamicPageBufferSize; }
		bool PrepareDynamicPageResource(
			const ActualizedDescriptorSet& descSet,
			IteratorRange<const void*> animatedParameters,
			IteratorRange<void*> dynamicPageBuffer);
	}

	struct UniformDeformerToRendererBinding
	{
		struct MaterialBinding
		{
			using DescSetSlotAndPageOffset = std::pair<unsigned, unsigned>;
			std::vector<DescSetSlotAndPageOffset> _animatedSlots;
		};
		using ElementAndMaterialGuid = std::pair<unsigned, uint64_t>;
		std::vector<std::pair<ElementAndMaterialGuid, MaterialBinding>> _materialBindings;
	}; 

}}
