// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "../../Utility/ImpliedTyping.h"

namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	class ICompiledLayoutPool;
	class ModelRendererConstruction;
	
	struct AnimatedUniform
	{
		uint64_t _name;
		ImpliedTyping::TypeDesc _type;
		unsigned _instanceValuesOffset;		// offset 
	};
	
	void ConfigureDeformUniformsAttachment(
		DeformerConstruction& deformerConstruction,
		const ModelRendererConstruction& rendererConstruction,
		RenderCore::Techniques::ICompiledLayoutPool& compiledLayoutPool,
		IteratorRange<const AnimatedUniform*> animatedUniforms,
		IteratorRange<const void*> defaultInstanceData);

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
