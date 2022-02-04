// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StateDesc.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"	// for DefaultSeed64
#include <string>
#include <vector>
#include <memory>

namespace Assets { class DirectorySearchRules; class DependencyValidation; }
namespace Utility { class ConditionalProcessingTokenizer; }
namespace RenderCore { enum class DescriptorType; class DescriptorSetSignature; class SamplerPool; }

namespace RenderCore { namespace Assets 
{
	class PredefinedCBLayout;

	class PredefinedDescriptorSetLayout
	{
	public:
		struct ConditionalDescriptorSlot
		{
			std::string _name;
			DescriptorType _type;
			unsigned _arrayElementCount = 0u;
			unsigned _slotIdx = ~0u;
			unsigned _cbIdx = ~0u;		// this is an idx into the _constantBuffers array for constant buffer types
			unsigned _fixedSamplerIdx = ~0u;
			std::string _conditions;
		};
		std::vector<ConditionalDescriptorSlot> _slots;
		std::vector<std::shared_ptr<RenderCore::Assets::PredefinedCBLayout>> _constantBuffers;
		std::vector<SamplerDesc> _fixedSamplers;

		DescriptorSetSignature MakeDescriptorSetSignature(SamplerPool*) const;

		uint64_t CalculateHash(uint64_t seed=DefaultSeed64) const;

		PredefinedDescriptorSetLayout(
			StringSection<> inputData,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
		PredefinedDescriptorSetLayout(
			Utility::ConditionalProcessingTokenizer&,
			const ::Assets::DependencyValidation&);
		PredefinedDescriptorSetLayout();
		~PredefinedDescriptorSetLayout();

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

	protected:
		void ParseSlot(Utility::ConditionalProcessingTokenizer& iterator, DescriptorType type);
		void Parse(Utility::ConditionalProcessingTokenizer& iterator);

		::Assets::DependencyValidation _depVal;
		friend class PredefinedPipelineLayoutFile;
	};

}}

