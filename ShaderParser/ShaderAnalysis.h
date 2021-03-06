// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/MinimalShaderSource.h"
#include "../Utility/ParameterBox.h"
#include <unordered_map>
#include <string>
#include <set>

namespace Assets { class DirectorySearchRules; }

namespace ShaderSourceParser
{
	RenderCore::ISourceCodePreprocessor::SourceCodeWithRemapping ExpandIncludes(
		StringSection<> src,
		const std::string& srcName,
		const ::Assets::DirectorySearchRules& searchRules);

	class ManualSelectorFiltering
    {
    public:
		ParameterBox    _setValues;
		std::unordered_map<std::string, std::string> _relevanceMap;

		uint64_t GetHash() const { return _hash; }

		void GenerateHash();
		uint64_t _hash = 0ull;
    };

	class SelectorFilteringRules;

	ParameterBox FilterSelectors(
		IteratorRange<const ParameterBox**> selectors,
		const ManualSelectorFiltering& manualFiltering,
		const SelectorFilteringRules& automaticFiltering);

	ParameterBox FilterSelectors(
		const ParameterBox& selectors,
		const ManualSelectorFiltering& manualFiltering,
		const SelectorFilteringRules& automaticFiltering);

#if 0
	class ShaderSelectorAnalysis
	{
	public:
		std::unordered_map<std::string, std::string> _selectorRelevance;
	};

	ShaderSelectorAnalysis AnalyzeSelectors(const std::string& sourceCode);

	ParameterBox FilterSelectors(
		const ParameterBox& unfiltered,
		const std::unordered_map<std::string, std::string>& relevance);

	namespace Utility
	{
		void MergeRelevance(
			std::unordered_map<std::string, std::string>& result,
			const std::unordered_map<std::string, std::string>& src);

		::Assets::DepValPtr MergeRelevanceFromShaderFiles(
			std::unordered_map<std::string, std::string>& result,
			const std::set<std::string>& shaderFileSet);
	}
#endif

}
