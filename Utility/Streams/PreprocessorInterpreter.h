// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ImpliedTyping.h"
#include "../StringUtils.h"
#include "../IteratorUtils.h"
#include <unordered_map>
#include <map>
#include <stack>
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

			std::string AsString(IteratorRange<const Token*> tokenList) const;
			void Simplify(ExpressionTokenList&);

			uint64_t CalculateHash() const;

			TokenDictionary();
			~TokenDictionary();
		};

		class ExpressionEvaluator
		{
		public:
			enum class StepType { LookupVariable, End };
			struct Step
			{ 
				StepType _type = StepType::End;
				StringSection<> _name; unsigned _nameTokenIndex = ~0u;
				ImpliedTyping::VariantNonRetained* _queryResult = nullptr;
				operator bool() const  { return _type != StepType::End; }

				// Note that values blocks passed to *all* variants of SetQueryResult must be retained
				// throughout the entire lifetime of the ExpressionEvaluator.
				// It's a special restriction, but it allows us to avoid copies across the interface for
				// larger values (such as arrays)
				template<typename Type> void SetQueryResult(const Type& value);
				template<typename Type, int N> void SetQueryResult(Type (&value)[N]);
				void SetQueryResult(const ImpliedTyping::TypeDesc& typeDesc, IteratorRange<const void*> data);
			};

			Step GetNextStep();
			ImpliedTyping::VariantNonRetained GetResult() const;

			ExpressionEvaluator(const TokenDictionary&, IteratorRange<const Token*>);
			~ExpressionEvaluator();
			ExpressionEvaluator& operator=(ExpressionEvaluator&&) = delete;
			ExpressionEvaluator(ExpressionEvaluator&&) = delete;

			struct EvaluatedValue;
		private:
			std::vector<uint8_t> _evalBlock;
			const TokenDictionary* _dictionary;
			IteratorRange<const Token*> _remainingExpression;
			std::vector<std::pair<TokenDictionary::TokenType, EvaluatedValue>> _evaluation;
			std::optional<ImpliedTyping::VariantNonRetained> _lastReturnedStep;
		};

		template<typename Type>
			void ExpressionEvaluator::Step::SetQueryResult(const Type& value)
			{
				_queryResult->_type = ImpliedTyping::TypeOf<Type>();
				_queryResult->_data = MakeOpaqueIteratorRange(value);
			}
		template<typename Type, int N>
			void ExpressionEvaluator::Step::SetQueryResult(Type (&value)[N])
			{
				_queryResult->_type = { ImpliedTyping::TypeOf<Type>()._type, N};
				_queryResult->_data = MakeOpaqueIteratorRange(value);
			}
		inline void ExpressionEvaluator::Step::SetQueryResult(const ImpliedTyping::TypeDesc& typeDesc, IteratorRange<const void*> data)
		{
			_queryResult->_type = typeDesc;
			_queryResult->_data = {const_cast<void*>(data.begin()), const_cast<void*>(data.end())};		// we promise to be good
		}

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
		ExpressionTokenList OrNotExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs);

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

	PreprocessorAnalysis GeneratePreprocessorAnalysisFromFile(
		StringSection<> inputFilename0,
		StringSection<> inputFilename1,
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

