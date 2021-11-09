// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Techniques.h"
#include "../Metal/Forward.h"
#include <unordered_map>
#include <string>

namespace ShaderSourceParser
{
	class ManualSelectorFiltering;
	class SelectorFilteringRules;
	class SelectorPreconfiguration;
}

namespace RenderCore { class ICompiledPipelineLayout; }

namespace RenderCore { namespace Techniques 
{
	class Technique;

	/// <summary>Filters shader variation construction parameters to avoid construction of duplicate shaders</summary>
	///
	/// Sometimes 2 different sets of construction parameters for a shader can result in equivalent final byte code.
	/// Ideally we want to minimize the number of different shaders; so this object will filter construction parameters
	/// to attempt to identify though which will result in duplicates.
	///
	/// UniqueShaderVariationSet maintains a list of previously generated shaders, which can be reused as appropriate.
	class UniqueShaderVariationSet
	{
	public:
		struct FilteredSelectorSet
		{
			uint64_t _hashValue;
			std::string _selectors;
		};

		const FilteredSelectorSet& FilterSelectors(
			IteratorRange<const ParameterBox* const*> selectors,
			const ShaderSourceParser::ManualSelectorFiltering& techniqueFiltering,
			IteratorRange<const ShaderSourceParser::SelectorFilteringRules**> automaticFiltering,
			const ShaderSourceParser::SelectorPreconfiguration* preconfiguration);

		UniqueShaderVariationSet();
		~UniqueShaderVariationSet();
	protected:
		std::vector<std::pair<uint64_t, FilteredSelectorSet>>		_globalToFiltered;
	};

	/// <summary>Provides convenient management of shader variations generated from a technique file</summary>
    class TechniqueShaderVariationSet
    {
    public:
		::Assets::PtrToFuturePtr<Metal::ShaderProgram> FindVariation(
			int techniqueIndex,
			const ParameterBox* shaderSelectors[SelectorStages::Max]);

		const Technique& GetTechnique() const { return *_technique; }

		TechniqueShaderVariationSet(
			const std::shared_ptr<Technique>& technique,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout);
		~TechniqueShaderVariationSet();

		///////////////////////////////////////

		const ::Assets::DependencyValidation& GetDependencyValidation() const;
		static void ConstructToPromise(
			std::promise<std::shared_ptr<TechniqueShaderVariationSet>>&& promise,
			StringSection<::Assets::ResChar> techniqueName,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout);

    protected:
		UniqueShaderVariationSet _variationSet;
		std::shared_ptr<Technique> _technique;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;

		class Variation;
		std::vector<std::pair<uint64_t, Variation>> _filteredSelectorsToVariation;
    };

}}
