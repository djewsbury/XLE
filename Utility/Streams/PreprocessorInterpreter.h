// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StringUtils.h"
#include "../IteratorUtils.h"
#include <unordered_map>
#include <map>

namespace Utility
{
	class ParameterBox;

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		const std::unordered_map<std::string, int>& definedTokens);

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		IteratorRange<const ParameterBox**> definedTokens);

	namespace Internal
	{
		using ExpressionTokenList = std::vector<unsigned>;

		class TokenDictionary
		{
		public:
			enum class TokenType { UnaryMarker, Literal, Variable, IsDefinedTest, Operation };
			struct Token
			{
				TokenType _type;
				std::string _value;
			};
			std::vector<Token> _tokenDefinitions;

			void PushBack(
				ExpressionTokenList& tokenList,
				TokenType type, const std::string& value = {});

			ExpressionTokenList Translate(
				const TokenDictionary& otherDictionary,
				const ExpressionTokenList& tokenListForOtherDictionary);
			unsigned Translate(
				const TokenDictionary& otherDictionary,
				unsigned tokenForOtherDictionary);

			unsigned GetToken(TokenType type, const std::string& value = {});
			std::optional<unsigned> TryGetToken(TokenType type, StringSection<> value) const;

			bool EvaluateExpression(
				const ExpressionTokenList& tokenList,
				IteratorRange<ParameterBox const*const*> environment) const;
			std::string AsString(const ExpressionTokenList& tokenList) const;
			void Simplify(ExpressionTokenList&);

			uint64_t CalculateHash() const;

			TokenDictionary();
			~TokenDictionary();
		};

		const char* AsString(TokenDictionary::TokenType);
		TokenDictionary::TokenType AsTokenType(StringSection<>);

		using WorkingRelevanceTable = std::map<unsigned, ExpressionTokenList>;

		WorkingRelevanceTable MergeRelevanceTables(
			const WorkingRelevanceTable& lhs, const ExpressionTokenList& lhsCondition,
			const WorkingRelevanceTable& rhs, const ExpressionTokenList& rhsCondition);

		ExpressionTokenList InvertExpression(const ExpressionTokenList& expr);
		ExpressionTokenList AndExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs);
		ExpressionTokenList OrExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs);
		ExpressionTokenList AndNotExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs);

		struct PreprocessorSubstitutions
		{
			TokenDictionary _dictionary;
			std::unordered_map<std::string, ExpressionTokenList> _items;
			std::unordered_map<std::string, ExpressionTokenList> _defaultSets;
		};

		ExpressionTokenList AsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions = {});
		
		WorkingRelevanceTable CalculatePreprocessorExpressionRevelance(
			TokenDictionary& dictionary,
			const ExpressionTokenList& input);
	}

	class PreprocessorAnalysis
    {
    public:
        Internal::TokenDictionary _tokenDictionary;
        std::map<unsigned, Internal::ExpressionTokenList> _relevanceTable;
        Internal::PreprocessorSubstitutions _substitutionSideEffects;
    };

	class IPreprocessorIncludeHandler;

    PreprocessorAnalysis GeneratePreprocessorAnalysis(
		StringSection<> input,
		StringSection<> filenameForRelativeIncludeSearch = {},
		IPreprocessorIncludeHandler* includeHandler = nullptr);

	class IPreprocessorIncludeHandler
	{
	public:
		virtual PreprocessorAnalysis GeneratePreprocessorAnalysis(
			StringSection<> requestString,
			StringSection<> fileIncludedFrom) = 0;
		virtual ~IPreprocessorIncludeHandler();
	};
}

using namespace Utility;

