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
#include <variant>

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
			enum class TokenType { UnaryMarker, Literal, Variable, IsDefinedTest, Operation, UserOperation };
			using TokenValueVariant = std::variant<std::monostate, std::string, std::pair<std::string, uint64_t>, int64_t>;
			struct TokenDefinition
			{
				TokenType _type;
				TokenValueVariant _value;

				StringSection<> AsStringSection() const;
				uint64_t AsHashValue() const;
				std::string CastToString() const;
				friend bool operator==(const TokenDefinition& lhs, const TokenDefinition& rhs);
			};
			std::vector<TokenDefinition> _tokenDefinitions;

			ExpressionTokenList Translate(
				const TokenDictionary& otherDictionary,
				const ExpressionTokenList& tokenListForOtherDictionary);
			Token Translate(
				const TokenDictionary& otherDictionary,
				Token tokenForOtherDictionary);

			Token GetOrAddToken(TokenType type, const TokenValueVariant& v = std::monostate{});
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
			enum class StepType { LookupVariable, UserOp, End };
			struct Step
			{ 
				StepType _type = StepType::End;
				StringSection<> _name; unsigned _nameTokenIndex = ~0u;

				// Note that values blocks passed to *all* variants of SetQueryResult must be retained
				// throughout the entire lifetime of the ExpressionEvaluator.
				// It's a special restriction, but it allows us to avoid copies across the interface for
				// larger values (such as arrays)
				template<typename Type> void Return(const Type& value);
				template<typename Type> void ReturnNonRetained(const Type& value);
				template<typename Type, int N> void ReturnNonRetained(Type (&value)[N]);
				void Return(ImpliedTyping::VariantNonRetained nonRetained);

				operator bool() const  { return _type != StepType::End; }

				// used internally
				struct Undefined{};
				using ReturnSled = std::variant<std::monostate, Undefined, ImpliedTyping::VariantNonRetained, ImpliedTyping::VariantRetained>;
				ReturnSled* _returnSled = nullptr;
				friend class ExpressionEvaluator;
			};

			Step GetNextStep();
			ImpliedTyping::VariantNonRetained GetResult() const;

			ImpliedTyping::VariantRetained PopParameter();
			const TokenDictionary& GetDictionary() const { return *_dictionary; }

			ExpressionEvaluator(const TokenDictionary&, IteratorRange<const Token*>);
			~ExpressionEvaluator();
			ExpressionEvaluator& operator=(ExpressionEvaluator&&) = delete;
			ExpressionEvaluator(ExpressionEvaluator&&) = delete;

		private:
			std::vector<uint8_t> _evalBlock;
			const TokenDictionary* _dictionary;
			IteratorRange<const Token*> _remainingExpression;
			std::vector<std::pair<TokenDictionary::TokenType, ImpliedTyping::VariantRetained>> _evaluation;

			Step::ReturnSled _lastReturnSled;
		};

		template<typename Type> 
			void ExpressionEvaluator::Step::Return(const Type& value)
		{
			assert(_returnSled);
			*_returnSled = ImpliedTyping::VariantRetained { value };
		}
		template<typename Type>
			void ExpressionEvaluator::Step::ReturnNonRetained(const Type& value)
		{
			assert(_returnSled);
			*_returnSled = ImpliedTyping::VariantNonRetained { ImpliedTyping::TypeOf<Type>(), MakeOpaqueIteratorRange(value) };
		}
		template<typename Type, int N>
			void ExpressionEvaluator::Step::ReturnNonRetained(Type (&value)[N])
		{
			assert(_returnSled);
			*_returnSled = ImpliedTyping::VariantNonRetained{ {ImpliedTyping::TypeOf<Type>()._type, N}, MakeOpaqueIteratorRange(value) };
		}
		inline void ExpressionEvaluator::Step::Return(ImpliedTyping::VariantNonRetained nonRetained)
		{
			assert(_returnSled);
			*_returnSled = nonRetained;
		}

		inline ImpliedTyping::VariantRetained ExpressionEvaluator::PopParameter()
		{
			assert(!_evaluation.empty());
			auto i = std::move(_evaluation.back());
			assert(i.first == TokenDictionary::TokenType::Literal);
			_evaluation.pop_back();
			return i.second;
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

		namespace ExpressionTokenListFlags
		{
			enum Flags { RecordHashes = 1u<<0u };
			using BitField = unsigned;
		}

		ExpressionTokenList AsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions = {},
			ExpressionTokenListFlags::BitField flags = 0);

		std::optional<ExpressionTokenList> TryAsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions = {},
			ExpressionTokenListFlags::BitField flags = 0);
		
		WorkingRelevanceTable CalculatePreprocessorExpressionRelevance(
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

