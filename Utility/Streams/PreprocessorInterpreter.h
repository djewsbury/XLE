// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StringUtils.h"
#include "../IteratorUtils.h"
#include <unordered_map>
#include <map>
#include <functional>

namespace Utility
{
	class ParameterBox;

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		const std::unordered_map<std::string, int>& definedTokens);

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		IteratorRange<const ParameterBox*const*> definedTokens);

	namespace Internal
	{
		using Token = unsigned;
		using ExpressionTokenList = std::vector<Token>;

		class TokenDictionary
		{
		public:
			enum class TokenType { UnaryMarker, Literal, Variable, IsDefinedTest, Operation };
			struct TokenDefinition
			{
				TokenType _type;
				std::string _value;
			};
			std::vector<TokenDefinition> _tokenDefinitions;

			void PushBack(
				ExpressionTokenList& tokenList,
				TokenType type, const std::string& value = {});

			ExpressionTokenList Translate(
				const TokenDictionary& otherDictionary,
				const ExpressionTokenList& tokenListForOtherDictionary);
			Token Translate(
				const TokenDictionary& otherDictionary,
				Token tokenForOtherDictionary);

			Token GetToken(TokenType type, const std::string& value = {});
			std::optional<Token> TryGetToken(TokenType type, StringSection<> value) const;

			int64_t EvaluateExpression(
				IteratorRange<const Token*> tokenList,
				IteratorRange<ParameterBox const*const*> environment) const;
			int64_t EvaluateExpression(
				IteratorRange<const Token*> tokenList,
				const std::function<std::optional<int64_t>(const TokenDefinition&, Token)>& lookupVariableFn) const;
			std::string AsString(IteratorRange<const Token*> tokenList) const;
			void Simplify(ExpressionTokenList&);

			uint64_t CalculateHash() const;

			TokenDictionary();
			~TokenDictionary();
		};

		const char* AsString(TokenDictionary::TokenType);
		TokenDictionary::TokenType AsTokenType(StringSection<>);

		using WorkingRelevanceTable = std::map<Token, ExpressionTokenList>;

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
			enum class Type { Define, Undefine, DefaultDefine };
			struct ConditionalSubstitutions
			{	
				std::string _symbol;
				Type _type;
				ExpressionTokenList _condition;
				ExpressionTokenList _substitution;
			};
			std::vector<ConditionalSubstitutions> _substitutions;
		};

		ExpressionTokenList AsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions = {});

		std::optional<ExpressionTokenList> TryAsExpressionTokenList(
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
        std::map<Internal::Token, Internal::ExpressionTokenList> _relevanceTable;
        Internal::PreprocessorSubstitutions _sideEffects;
    };

	class IPreprocessorIncludeHandler;

    PreprocessorAnalysis GeneratePreprocessorAnalysisFromString(
		StringSection<> input,
		StringSection<> filenameForRelativeIncludeSearch = {},
		IPreprocessorIncludeHandler* includeHandler = nullptr);

	PreprocessorAnalysis GeneratePreprocessorAnalysisFromFile(
		StringSection<> inputFilename,
		IPreprocessorIncludeHandler* includeHandler = nullptr);

	class IPreprocessorIncludeHandler
	{
	public:
		struct Result 
		{ 
			std::string _filename; 
			std::unique_ptr<uint8[]> _fileContents;
			size_t _fileContentsSize;
		};

		virtual Result OpenFile(
			StringSection<> requestString,
			StringSection<> fileIncludedFrom) = 0;
		virtual ~IPreprocessorIncludeHandler();
	};
}

using namespace Utility;

