// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Utility/Streams/TextFormatter.h"
#include "../Utility/Streams/PreprocessorInterpreter.h"
#include "../Utility/MemoryUtils.h"
#include <memory>
#include <iosfwd>

namespace Utility { class TextOutputFormatter; }
namespace Assets { class DirectorySearchRules; }

namespace ShaderSourceParser
{
	class SelectorFilteringRules
	{
	public:
		Utility::Internal::TokenDictionary _tokenDictionary;
		std::map<Utility::Internal::Token, Utility::Internal::ExpressionTokenList> _relevanceTable;
		std::map<Utility::Internal::Token, Utility::Internal::ExpressionTokenList> _defaultSets;

		uint64_t GetHash() const { return _hash; }
		operator bool() const { return !_relevanceTable.empty() || !_defaultSets.empty(); }

		bool IsRelevant(
			StringSection<> symbol, StringSection<> value = {},
			IteratorRange<const ParameterBox*const*> environment = {}) const;

		void MergeIn(const SelectorFilteringRules& source);

		static constexpr auto CompileProcessType = ConstHash64Legacy<'Filt', 'erRu', 'les'>::Value;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		friend void SerializationOperator(
			Utility::TextOutputFormatter& formatter,
			const SelectorFilteringRules& input);

		SelectorFilteringRules(
			TextInputFormatter<utf8>& formatter, 
			const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal);
		SelectorFilteringRules(const std::unordered_map<std::string, std::string>& relevanceStrings);
		SelectorFilteringRules();
		~SelectorFilteringRules();

	private:
		::Assets::DependencyValidation _depVal;
		uint64_t _hash = 0ull;
		void RecalculateHash();
	};

	class SelectorPreconfiguration
	{
	public:
		Utility::Internal::PreprocessorSubstitutions _preconfigurationSideEffects;

		uint64_t GetHash() const { return _hash; }

		ParameterBox Preconfigure(ParameterBox&&) const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		
		SelectorPreconfiguration(StringSection<> filename0, StringSection<> filename1 = {});
		~SelectorPreconfiguration();
	private:
		::Assets::DependencyValidation _depVal;
		uint64_t _hash = 0ull;
	};

	::Assets::CompilerRegistration RegisterShaderSelectorFilteringCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	SelectorFilteringRules GenerateSelectorFilteringRules(StringSection<> sourceCode);

	std::ostream& SerializationOperator(std::ostream&, const SelectorFilteringRules& rules);
	std::ostream& SerializationOperator(std::ostream&, const SelectorPreconfiguration& preconfig);

}

