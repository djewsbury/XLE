// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/ParameterBox.h"
#include "../Utility/Streams/StreamFormatter.h"
#include <unordered_map>
#include <string>
#include <set>

namespace Assets { class DirectorySearchRules; }
namespace RenderCore { class SourceCodeWithRemapping; }

namespace ShaderSourceParser
{
	RenderCore::SourceCodeWithRemapping ExpandIncludes(
		StringSection<> src,
		const std::string& srcName,
		const ::Assets::DirectorySearchRules& searchRules);

	class ManualSelectorFiltering
    {
    public:
		uint64_t GetHash() const;
		void MergeIn(const ManualSelectorFiltering&);

		template<typename Type>
			void SetSelector(StringSection<> name, Type value);

		const ParameterBox& GetSetSelectors() const { return _setValues; }
		const std::unordered_map<std::string, std::string>& GetRelevanceMap() const { return _relevanceMap; }

		ManualSelectorFiltering();
		~ManualSelectorFiltering();
		ManualSelectorFiltering(InputStreamFormatter<>&);
	private:
		ParameterBox _setValues;
		std::unordered_map<std::string, std::string> _relevanceMap;

		void GenerateHash() const;
		mutable uint64_t _hash = 0ull;
    };

	class SelectorFilteringRules;

	ParameterBox FilterSelectors(
		IteratorRange<const ParameterBox* const*> selectors,
		const std::unordered_map<std::string, std::string>& manualRevelanceMap,
		IteratorRange<const SelectorFilteringRules**> automaticFiltering);

	ParameterBox FilterSelectors(
		const ParameterBox& selectors,
		const ManualSelectorFiltering& manualFiltering,
		const SelectorFilteringRules& automaticFiltering);

	template<typename Type>
		void ManualSelectorFiltering::SetSelector(StringSection<> name, Type value)
	{
		_setValues.SetParameter(name, std::move(value));
		_hash = 0ull;
	}

}
