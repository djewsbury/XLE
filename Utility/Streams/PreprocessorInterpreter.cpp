// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4099) // 'Iterator': type name first seen using 'class' now seen using 'struct'
#pragma warning(disable:4180) // qualifier applied to function type has no meaning; ignored
#pragma warning(disable:4505) // 'preprocessor_operations::UndefinedOnUndefinedOperation': unreferenced local function has been removed

#include "PreprocessorInterpreter.h"
#include "../FastParseValue.h"
#include "../ParameterBox.h"
#include "../Threading/ThreadingUtils.h"
#include "../MemoryUtils.h"
#include "../../Core/Exceptions.h"
#include "../../Foreign/cparse/shunting-yard.h"
#include "../../Foreign/cparse/shunting-yard-exceptions.h"

#include <cmath>
#include <atomic>
#include <map>

namespace preprocessor_operations
{
	static packToken Equal(const packToken& left, const packToken& right, evaluationData* data)
	{
		// undefined tokens (ie, those with type VAR) behave as if they are zero
		// (even in the case with two undefined tokens, oddly enough)
		if (left->type == VAR) {
			if (right->type == VAR)
				return true;
			return packToken(0) == right;
		} else if (right->type == VAR)
			return left == packToken(0);

		return left == right;
	}

	static packToken Different(const packToken& left, const packToken& right, evaluationData* data)
	{
		// undefined tokens (ie, those with type VAR) behave as if they are zero
		// (even in the case with two undefined tokens, oddly enough)
		if (left->type == VAR) {
			if (right->type == VAR)
				return false;
			return packToken(0) != right;
		} else if (right->type == VAR)
			return left != packToken(0);

		return left != right;
	}

	static packToken UnaryNumeralOperation_Internal(const packToken& operand, const std::string& op)
	{
		if (op == "+") {
			return operand;
		} else if (op == "-") {
			return -operand.asDouble();
		} else if (op == "!") {
			return !operand.asBool();
		} else {
			throw undefined_operation(op, packToken{}, operand);
		}
	}

	static packToken UnaryNumeralOperation(const packToken& left, const packToken& right, evaluationData* data)
	{
		return UnaryNumeralOperation_Internal(right, data->op);
	}

	static packToken NumeralOperation_Internal(const packToken& left, const packToken& right, const std::string& op)
	{
		// Extract integer and real values of the operators:
		auto left_d = left.asDouble();
		auto left_i = left.asInt();

		auto right_d = right.asDouble();
		auto right_i = right.asInt();

		if (op == "+") {
			return left_d + right_d;
		} else if (op == "*") {
			return left_d * right_d;
		} else if (op == "-") {
			return left_d - right_d;
		} else if (op == "/") {
			return left_d / right_d;
		} else if (op == "<<") {
			return left_i << right_i;
		} else if (op == "**") {
			return pow(left_d, right_d);
		} else if (op == ">>") {
			return left_i >> right_i;
		} else if (op == "%") {
			return left_i % right_i;
		} else if (op == "<") {
			return left_d < right_d;
		} else if (op == ">") {
			return left_d > right_d;
		} else if (op == "<=") {
			return left_d <= right_d;
		} else if (op == ">=") {
			return left_d >= right_d;
		} else if (op == "&") {
			return left_i & right_i;
		} else if (op == "^") {
			return left_i ^ right_i;
		} else if (op == "|") {
			return left_i | right_i;
		} else if (op == "&&") {
			return left_i && right_i;
		} else if (op == "||") {
			return left_i || right_i;
		} else if (op == "==") {
			return Equal(left, right, nullptr);
		} else if (op == "!=") {
			return Different(left, right, nullptr);
		} else {
			throw undefined_operation(op, left, right);
		}
	}

	static StringSection<> NumeralOperation_FlippedOperandOperator(StringSection<char> op)
	{
		if (	XlEqString(op, "+")
			|| 	XlEqString(op, "&")
			|| 	XlEqString(op, "|")
			|| 	XlEqString(op, "^")
			|| 	XlEqString(op, "&&")
			|| 	XlEqString(op, "||")
			||  XlEqString(op, "==")
			||  XlEqString(op, "!=")) {
			return op;
		} else if (XlEqString(op, "<")) {
			return ">";
		} else if (XlEqString(op, ">")) {
			return "<";
		} else if (XlEqString(op, "<=")) {
			return ">=";
		} else if (XlEqString(op, ">=")) {
			return "<=";
		} else {
			return {};
		}
	}

	static StringSection<> NumeralOperation_NegatedOperator(StringSection<char> op)
	{
		if (XlEqString(op, "==")) {
			return "!=";
		} else if (XlEqString(op, "!=")) {
			return "==";
		} else if (XlEqString(op, "<")) {
			return ">=";
		} else if (XlEqString(op, ">")) {
			return "<=";
		} else if (XlEqString(op, "<=")) {
			return ">";
		} else if (XlEqString(op, ">=")) {
			return "<";
		} else {
			return {};
		}
	}

	static packToken NumeralOperation(const packToken& left, const packToken& right, evaluationData* data)
	{
		return NumeralOperation_Internal(left, right, data->op);
	}

	static packToken UndefinedOnNumberOperation(const packToken& left, const packToken& right, evaluationData* data)
	{
		// undefined symbols behave as if they are 0 in comparisons
		return NumeralOperation_Internal(packToken(0), right, data->op);
	}

	static packToken NumberOnUndefinedOperation(const packToken& left, const packToken& right, evaluationData* data)
	{
		// undefined symbols behave as if they are 0 in comparisons
		return NumeralOperation_Internal(left, packToken(0), data->op);
	}

	static packToken UndefinedOnUndefinedOperation(const packToken& left, const packToken& right, evaluationData* data)
	{
		// undefined symbols behave as if they are 0 in comparisons
		return NumeralOperation_Internal(packToken(0), packToken(0), data->op);
	}

	static packToken definedFunction(TokenMap scope)
	{
		auto* sym = scope.find("symbol");
		if (!sym) return false;

		// Tokens that look like identifiers, but aren't recognized by the shunting-yard library
		// are considered "variables". In effect, this means they haven't been defined beforehand.
		if (sym->token()->type == VAR)
			return false;

		return true;
	}

	struct Startup {
		Startup() {
			// Create the operator precedence map based on C++ default
			// precedence order as described on cppreference website:
			// http://en.cppreference.com/w/cpp/language/operator_precedence
			OppMap_t& opp = calculator::Default().opPrecedence;
			opp.add("*",  5); opp.add("/", 5); opp.add("%", 5);
			opp.add("+",  6); opp.add("-", 6);
			opp.add("<<", 7); opp.add(">>", 7);
			opp.add("<",  9); opp.add("<=", 9); opp.add(">=", 9); opp.add(">", 9);
			opp.add("==", 10); opp.add("!=", 10);
			opp.add("&", 11);
			opp.add("^", 12);
			opp.add("|", 13);
			opp.add("&&", 14);
			opp.add("||", 15);

			// Add unary operators:
			opp.addUnary("+",  3); opp.addUnary("-", 3); opp.addUnary("!", 3);

			// Link operations to respective operators:
			opMap_t& opMap = calculator::Default().opMap;
			opMap.add({ANY_TYPE, "==", ANY_TYPE}, &Equal);
			opMap.add({ANY_TYPE, "!=", ANY_TYPE}, &Different);

			// Note: The order is important:
			opMap.add({NUM, ANY_OP, NUM}, &NumeralOperation);
			opMap.add({UNARY, ANY_OP, NUM}, &UnaryNumeralOperation);
			opMap.add({VAR, ANY_OP, NUM}, &UndefinedOnNumberOperation);
			opMap.add({NUM, ANY_OP, VAR}, &NumberOnUndefinedOperation);
			opMap.add({VAR, ANY_OP, VAR}, &NumberOnUndefinedOperation);

			TokenMap& global = TokenMap::default_global();
			global["defined"] = CppFunction(&definedFunction, {"symbol"}, "defined()");
		}
	} __CPARSE_STARTUP;
}


namespace Utility
{
	static std::atomic_bool static_hasSetupPreprocOps { false };
	static std::atomic_bool static_setupThreadAssigned { false };

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		const std::unordered_map<std::string, int>& definedTokens)
	{
		if (!static_hasSetupPreprocOps.load()) {
			bool threadAssigned = static_setupThreadAssigned.exchange(true);
			if (!threadAssigned) {
				preprocessor_operations::Startup();
				static_hasSetupPreprocOps.store(true);
			} else {
				while (!static_hasSetupPreprocOps.load()) {
					Threading::YieldTimeSlice();
				}
			}
		}

		TokenMap vars;
		for (const auto&i:definedTokens)
			vars[i.first] = packToken(i.second);

		// symbols with no value can be defined like this: (but they aren't particularly useful in expressions, except when used with the defined() function)
		// vars["DEFINED_NO_VALUE"] = packToken(nullptr, NONE);

		return calculator::calculate(input.AsString().c_str(), &vars).asBool();

		// those that this can throw exceptions back to the caller (for example, if the input can't be parsed)
	}

	bool EvaluatePreprocessorExpression(
		StringSection<> input,
		IteratorRange<const ParameterBox*const*> definedTokens)
	{
		if (!static_hasSetupPreprocOps.load()) {
			bool threadAssigned = static_setupThreadAssigned.exchange(true);
			if (!threadAssigned) {
				preprocessor_operations::Startup();
				static_hasSetupPreprocOps.store(true);
			} else {
				while (!static_hasSetupPreprocOps.load()) {
					Threading::YieldTimeSlice();
				}
			}
		}

		TokenMap vars;
		for (const auto&b:definedTokens) {
			for (const auto&i:*b) {
				auto name = i.Name().AsString();
				auto type = i.Type();

				// For simple scalar types, attempt conversion to something
				// we can construct a packToken with
				if (type._arrayCount <= 1) {
					if (type._type == ImpliedTyping::TypeCat::Bool) {
						vars[name] = packToken(*(bool*)i.RawValue().begin());
						continue;
					} else if (type._type == ImpliedTyping::TypeCat::Int8
							|| type._type == ImpliedTyping::TypeCat::UInt8
							|| type._type == ImpliedTyping::TypeCat::Int16
							|| type._type == ImpliedTyping::TypeCat::UInt16
							|| type._type == ImpliedTyping::TypeCat::Int32) {
						int dest;
						ImpliedTyping::Cast(
							MakeOpaqueIteratorRange(dest), ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Int32},
							i.RawValue(), ImpliedTyping::TypeDesc{type._type});
						vars[name] = packToken(dest);
						continue;
					} else if (type._type == ImpliedTyping::TypeCat::UInt32
							|| type._type == ImpliedTyping::TypeCat::Int64
							|| type._type == ImpliedTyping::TypeCat::UInt64) {
						int64_t dest;
						ImpliedTyping::Cast(
							MakeOpaqueIteratorRange(dest), ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Int64},
							i.RawValue(), ImpliedTyping::TypeDesc{type._type});
						vars[name] = packToken(dest);
						continue;
					} else if (type._type == ImpliedTyping::TypeCat::Float) {
						vars[name] = packToken(*(float*)i.RawValue().begin());
						continue;
					} else if (type._type == ImpliedTyping::TypeCat::Double) {
						vars[name] = packToken(*(double*)i.RawValue().begin());
						continue;
					}
				}

				// If we didn't get a match with one of the above types, just 
				// treat it as a string
				vars[name] = packToken(i.ValueAsString());
			}
		}

		return calculator::calculate(input.AsString().c_str(), &vars).asBool();
	}

	namespace Internal
	{
		const char* AsString(TokenDictionary::TokenType input)
		{
			switch (input) {
			case TokenDictionary::TokenType::UnaryMarker: return "UnaryMarker";
			case TokenDictionary::TokenType::Literal: return "Literal";
			case TokenDictionary::TokenType::Variable: return "Variable";
			case TokenDictionary::TokenType::IsDefinedTest: return "IsDefinedTest";
			case TokenDictionary::TokenType::Operation: return "Operation";
			default: return "<<unknown>>";
			}
		}

		TokenDictionary::TokenType AsTokenType(StringSection<> input)
		{
			if (XlEqString(input, "UnaryMarker")) return TokenDictionary::TokenType::UnaryMarker;
			if (XlEqString(input, "Literal")) return TokenDictionary::TokenType::Literal;
			if (XlEqString(input, "Variable")) return TokenDictionary::TokenType::Variable;
			if (XlEqString(input, "IsDefinedTest")) return TokenDictionary::TokenType::IsDefinedTest;
			return TokenDictionary::TokenType::Operation;
		}

		static bool operator==(const TokenDictionary::TokenDefinition& lhs, const TokenDictionary::TokenDefinition& rhs)
		{
			return lhs._type == rhs._type && lhs._value == rhs._value;
		}

		static bool operator<(const TokenDictionary::TokenDefinition& lhs, const TokenDictionary::TokenDefinition& rhs)
		{
			if (lhs._type < rhs._type) return true;
			if (lhs._type > rhs._type) return false;
			return lhs._value < rhs._value;
		}

		static bool IsTrue(const ExpressionTokenList& expr) { return expr.size() == 1 && expr[0] == 1; }
		static bool IsFalse(const ExpressionTokenList& expr) { return expr.size() == 1 && expr[0] == 0; }

		static const Token s_fixedTokenFalse = 0;
		static const Token s_fixedTokenTrue = 1;
		static const Token s_fixedTokenLogicalAnd = 2;
		static const Token s_fixedTokenLogicalOr = 3;
		static const Token s_fixedTokenNot = 4;
		static const Token s_fixedTokenUnaryMarker = 5;

		ExpressionTokenList AsAbstractExpression(
			TokenDictionary& dictionary,
			TokenQueue_t&& input,
			const PreprocessorSubstitutions& substitutions)
		{
			TRY {
				ExpressionTokenList reversePolishOrdering;		// we use this indirection here because we're expecting tokens (particular variables) to be frequently reused
				reversePolishOrdering.reserve(input.size());

				while (!input.empty()) {
					TokenBase& base  = *input.front();
					
					if (base.type == OP) {
						auto op = static_cast<::Token<std::string>*>(&base)->val;

						if (op == "()") {
							Throw(std::runtime_error("Only defined() is supported in relevance checks. Other functions are not supported"));
						} else {
							dictionary.PushBack(reversePolishOrdering, TokenDictionary::TokenType::Operation, op);
						}

					} else if (base.type == UNARY) {

						dictionary.PushBack(reversePolishOrdering, TokenDictionary::TokenType::UnaryMarker);
					
					} else if (base.type == VAR) {

						std::string key = static_cast<::Token<std::string>*>(&base)->val;
						auto sub = std::find_if(substitutions._substitutions.rbegin(), substitutions._substitutions.rend(), [key](const auto& c) { return c._symbol == key; });
						if (sub == substitutions._substitutions.rend() || !IsTrue(sub->_condition) || sub->_type == PreprocessorSubstitutions::Type::Undefine) {
							dictionary.PushBack(reversePolishOrdering, TokenDictionary::TokenType::Variable, key);
						} else {
							// We need to substitute in the expression provided in the substitutions table
							// This is used for things like #define
							// Note that "key" never becomes a token in our output. So no relevance information
							// will be calculated for it -- but if the expression substituted in refers to variables,
							// then we can get relevance information for them
							assert(sub->_type == PreprocessorSubstitutions::Type::Define || sub->_type == PreprocessorSubstitutions::Type::DefaultDefine);
							auto translated = dictionary.Translate(substitutions._dictionary, sub->_substitution);
							if (!translated.empty()) {
								reversePolishOrdering.insert(
									reversePolishOrdering.end(),
									translated.begin(), translated.end());
							} else {
								// a symbol that is defined to nothing is treated as if it's defined to 1
								reversePolishOrdering.push_back(s_fixedTokenTrue);
							}
						}

					} else if (base.type & REF) {

						// This will appear when calling the "defined" pseudo-function
						// We want to transform the pattern
						//		<REF "&Function defined()"> <VARIABLE var> <Op "()">
						// to be just 
						//		<IsDefinedTest var>

						auto* resolvedRef = static_cast<RefToken*>(&base)->resolve();
						if (!resolvedRef || static_cast<CppFunction*>(resolvedRef)->name() != "defined()")
							Throw(std::runtime_error("Only defined() is supported in relevance checks. Other functions are not supported"));
						delete resolvedRef;

						delete input.front();
						input.pop();
						if (input.empty())
							Throw(std::runtime_error("Missing parameters to defined() function in token stream"));
						TokenBase& varToTest  = *input.front();
						if (varToTest.type != VAR)
							Throw(std::runtime_error("Missing parameters to defined() function in token stream"));
						std::string key = static_cast<::Token<std::string>*>(&varToTest)->val;
						delete input.front();
						input.pop();
						if (input.empty())
							Throw(std::runtime_error("Missing parameters to defined() function in token stream"));
						TokenBase& callOp  = *input.front();
						if (callOp.type != OP || static_cast<::Token<std::string>*>(&callOp)->val != "()")
							Throw(std::runtime_error("Missing call token for defined() function in token stream"));
						// (final pop still happens below)

						auto sub = std::find_if(substitutions._substitutions.rbegin(), substitutions._substitutions.rend(), [key](const auto& c) { return c._symbol == key; });
						if (sub == substitutions._substitutions.rend() || !IsTrue(sub->_condition) || sub->_type == PreprocessorSubstitutions::Type::Undefine) {
							dictionary.PushBack(reversePolishOrdering, TokenDictionary::TokenType::IsDefinedTest, key);
						} else {
							// This is actually doing a defined(...) check on one of our substitutions. We can treat it
							// as just "true"
							assert(sub->_type == PreprocessorSubstitutions::Type::Define || sub->_type == PreprocessorSubstitutions::Type::DefaultDefine);
							reversePolishOrdering.push_back(s_fixedTokenTrue);
						}
						
					} else {
						
						std::string literal = packToken::str(&base);
						dictionary.PushBack(reversePolishOrdering, TokenDictionary::TokenType::Literal, literal);

					}

					delete input.front();
					input.pop();
				}

				return reversePolishOrdering;
			} CATCH (...) {
				while (!input.empty()) {
					delete input.front();
					input.pop();
				}
				throw;
			} CATCH_END
		}

		ExpressionTokenList AsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions)
		{
			TokenMap vars;
			auto rpn = calculator::toRPN(input.AsString().c_str(), vars);
			return AsAbstractExpression(dictionary, std::move(rpn), substitutions);
		}

		std::optional<ExpressionTokenList> TryAsExpressionTokenList(
			TokenDictionary& dictionary,
			StringSection<> input,
			const PreprocessorSubstitutions& substitutions)
		{
			TokenMap vars;
			auto rpn = calculator::tryToRPN(input.AsString().c_str(), vars);
			if (rpn.has_value())
				return AsAbstractExpression(dictionary, rpn.get_value(), substitutions);
			return {};
		}

		ExpressionTokenList AndExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs)
		{
			if (lhs.empty()) return rhs;
			if (rhs.empty()) return lhs;

			if (lhs.size() == 1) {
				if (lhs[0] == s_fixedTokenTrue) return rhs;
				if (lhs[0] == s_fixedTokenFalse) return {s_fixedTokenFalse};
			}

			if (rhs.size() == 1) {
				if (rhs[0] == s_fixedTokenTrue) return lhs;
				if (rhs[0] == s_fixedTokenFalse) return {s_fixedTokenFalse};
			}

			ExpressionTokenList result;
			result.reserve(lhs.size() + rhs.size() + 1);
			result.insert(result.end(), lhs.begin(), lhs.end());
			result.insert(result.end(), rhs.begin(), rhs.end());
			result.push_back(s_fixedTokenLogicalAnd);
			return result;
		}

		ExpressionTokenList OrExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs)
		{
			if (lhs.empty()) return rhs;
			if (rhs.empty()) return lhs;

			if (lhs.size() == 1) {
				if (lhs[0] == s_fixedTokenTrue) return {s_fixedTokenTrue};
				if (lhs[0] == s_fixedTokenFalse) return rhs;
			}

			if (rhs.size() == 1) {
				if (rhs[0] == s_fixedTokenTrue) return {s_fixedTokenTrue};
				if (rhs[0] == s_fixedTokenFalse) return lhs;
			}

			ExpressionTokenList result;
			result.reserve(lhs.size() + rhs.size() + 1);
			result.insert(result.end(), lhs.begin(), lhs.end());
			result.insert(result.end(), rhs.begin(), rhs.end());
			result.push_back(s_fixedTokenLogicalOr);
			return result;
		}

		ExpressionTokenList AndNotExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs)
		{
			if (lhs.empty()) return InvertExpression(rhs);
			if (rhs.empty()) return lhs;

			if (lhs.size() == 1) {
				if (lhs[0] == s_fixedTokenTrue) return InvertExpression(rhs);
				if (lhs[0] == s_fixedTokenFalse) return {s_fixedTokenFalse};
			}

			if (rhs.size() == 1) {
				if (rhs[0] == s_fixedTokenFalse) return lhs;
				if (rhs[0] == s_fixedTokenTrue) return {s_fixedTokenFalse};
			}

			ExpressionTokenList result;
			result.reserve(lhs.size() + rhs.size() + 3);
			result.insert(result.end(), lhs.begin(), lhs.end());
			result.push_back(s_fixedTokenUnaryMarker);
			result.insert(result.end(), rhs.begin(), rhs.end());
			result.push_back(s_fixedTokenNot);
			result.push_back(s_fixedTokenLogicalAnd);
			return result;
		}

		ExpressionTokenList OrNotExpression(const ExpressionTokenList& lhs, const ExpressionTokenList& rhs)
		{
			if (lhs.empty()) return InvertExpression(rhs);
			if (rhs.empty()) return lhs;

			if (lhs.size() == 1) {
				if (lhs[0] == s_fixedTokenTrue) return {s_fixedTokenTrue};
				if (lhs[0] == s_fixedTokenFalse) return InvertExpression(rhs);
			}

			if (rhs.size() == 1) {
				if (rhs[0] == s_fixedTokenFalse) return {s_fixedTokenTrue};
				if (rhs[0] == s_fixedTokenTrue) return lhs;
			}

			ExpressionTokenList result;
			result.reserve(lhs.size() + rhs.size() + 3);
			result.insert(result.end(), lhs.begin(), lhs.end());
			result.push_back(s_fixedTokenUnaryMarker);
			result.insert(result.end(), rhs.begin(), rhs.end());
			result.push_back(s_fixedTokenNot);
			result.push_back(s_fixedTokenLogicalOr);
			return result;
		}

		ExpressionTokenList InvertExpression(const ExpressionTokenList& expr)
		{
			if (expr.empty()) return {};
			
			if (expr.size() == 1) {
				if (expr[0] == s_fixedTokenTrue) return {s_fixedTokenFalse};
				if (expr[0] == s_fixedTokenFalse) return {s_fixedTokenTrue};
			}

			ExpressionTokenList result;
			result.reserve(expr.size() + 2);
			result.push_back(s_fixedTokenUnaryMarker);
			result.insert(result.end(), expr.begin(), expr.end());
			result.push_back(s_fixedTokenNot);
			return result;
		}

		bool Equal(IteratorRange<std::vector<Token>::iterator> lhs, IteratorRange<std::vector<Token>::iterator> rhs)
		{
			if (lhs.size() != rhs.size()) return false;
			auto l = lhs.begin(), r = rhs.begin();
			while (l != lhs.end())
				if (*l++ != *r++) return false;
			return true;
		}

		void TokenDictionary::Simplify(ExpressionTokenList& expr)
		{
			struct Subexpression
			{
				size_t _begin, _end;
				Token _tokenWeight;
			};
			std::stack<Subexpression> evaluation;
			for (size_t idx=0; idx<expr.size();) {
				const auto& token = _tokenDefinitions[expr[idx]];
				if (token._type == TokenType::Operation) {
					auto rsub = std::move(evaluation.top()); evaluation.pop();
					auto lsub = std::move(evaluation.top()); evaluation.pop();

					auto rrange = MakeIteratorRange(expr.begin() + rsub._begin, expr.begin() + rsub._end);
					auto lrange = MakeIteratorRange(expr.begin() + lsub._begin, expr.begin() + lsub._end);
					bool identical = (rsub._tokenWeight == lsub._tokenWeight) && Equal(rrange, lrange);
					if (identical) {
						if (token._value == "&&" || token._value == "||") {
							assert(lsub._begin < rsub._begin && lsub._end == rsub._begin);
							expr.erase(expr.begin()+rsub._begin, expr.begin()+idx+1);
							idx = lsub._end;
							evaluation.push(lsub);
							continue;
						}
					} else {
						bool isUnary = ((lsub._end - lsub._begin) == 1) && _tokenDefinitions[expr[lsub._begin]]._type == TokenType::UnaryMarker;
						if (!isUnary && lsub._tokenWeight > rsub._tokenWeight) {
							// to try to encourage more identical matches, we will try to keep a consistent
							// order. This might mean reversing lhs and rhs where it makes sense
							// We will attempt to reverse as many operators as we can, but "&&" and "||" are going
							// to be the most important ones
							auto reversedOperator = preprocessor_operations::NumeralOperation_FlippedOperandOperator(token._value);
							if (!reversedOperator.IsEmpty()) {
								std::vector<Token> reversedPart;
								reversedPart.reserve(rrange.end() - lrange.begin());
								reversedPart.insert(reversedPart.end(), rrange.begin(), rrange.end());
								reversedPart.insert(reversedPart.end(), lrange.begin(), lrange.end());
								std::copy(reversedPart.begin(), reversedPart.end(), lrange.begin());
								expr[idx] = GetToken(TokenType::Operation, reversedOperator.AsString());

								// notice lsub & rsub reversed when calculating the "tokenWeight" just below
								Subexpression subexpr { lsub._begin, idx + 1, lsub._tokenWeight ^ (rsub._tokenWeight << Token(3)) };
								evaluation.push(subexpr);
					 			++idx;
								continue;
							}
						} else if (isUnary && token._value == "!" && !rrange.empty()) {
							// sometimes we can removed a "!" by just changing the operator
							// it applies to (ie; !(lhs < rhs) becomes (lhs >= rhs))
							const auto& internalOp = _tokenDefinitions[*(rrange.end()-1)];
							if (internalOp._type == TokenType::Operation) {
								auto negated = preprocessor_operations::NumeralOperation_NegatedOperator(internalOp._value);
								if (!negated.IsEmpty()) {
									*(rrange.end()-1) = GetToken(TokenType::Operation, negated.AsString());
									expr.erase(expr.begin()+idx);
									expr.erase(expr.begin()+lsub._begin);
									idx -= 1;	// back one because we erased the unary marker
									rsub._begin -= 1;
									rsub._end -= 1;
									evaluation.push(rsub);
									continue;
								}
							}
						}
					}

					Subexpression subexpr { lsub._begin, idx + 1, rsub._tokenWeight ^ (lsub._tokenWeight << 3u) };
					evaluation.push(subexpr);
					++idx;
				} else {
					Subexpression subexpr { idx, idx + 1, expr[idx] };
					evaluation.push(subexpr);
					++idx;
				}
			}
		}

		WorkingRelevanceTable MergeRelevanceTables(
			const WorkingRelevanceTable& lhs, const ExpressionTokenList& lhsCondition,
			const WorkingRelevanceTable& rhs, const ExpressionTokenList& rhsCondition)
		{
			WorkingRelevanceTable result;

			// note that we have to use an "ordered" map here to make the merging
			// efficient. Using an std::unordered_map here would probably result
			// in a significant amount of re-hashing

			auto lhsi = lhs.begin();
			auto rhsi = rhs.begin();
			for (;;) {
				if (lhsi == lhs.end() && rhsi == rhs.end()) break;
				while (lhsi != lhs.end() && (rhsi == rhs.end() || lhsi->first < rhsi->first)) {
					result.insert(std::make_pair(lhsi->first, AndExpression(lhsi->second, lhsCondition)));
					++lhsi;
				}
				while (rhsi != rhs.end() && (lhsi == lhs.end() || rhsi->first < lhsi->first)) {
					result.insert(std::make_pair(rhsi->first, AndExpression(rhsi->second, rhsCondition)));
					++rhsi;
				}
				if (lhsi != lhs.end() && rhsi != rhs.end() && lhsi->first == rhsi->first) {
					auto lhsPart = AndExpression(lhsi->second, lhsCondition);
					auto rhsPart = AndExpression(rhsi->second, rhsCondition);
					result.insert(std::make_pair(lhsi->first, OrExpression(lhsPart, rhsPart)));
					++lhsi;
					++rhsi;
				}
			}

			return result;
		}

		std::string TokenDictionary::AsString(IteratorRange<const Token*> subExpression) const
		{
			OppMap_t& opp = calculator::Default().opPrecedence;
			std::stack<std::pair<std::string, Token>> evaluation;
			for (auto tokenIdx:subExpression) {
				const auto& token = _tokenDefinitions[tokenIdx];

				if (token._type == TokenType::Operation) {
					auto r_token = std::move(evaluation.top()); evaluation.pop();
					auto l_token = std::move(evaluation.top()); evaluation.pop();

					int opPrecedence = 0;
					std::stringstream str;

					if (l_token.first.empty()) {	// we get an empty string for the unary marker
						opPrecedence = opp.prec("L"+token._value);
						bool rhsNeedsBrackets = r_token.second >= opPrecedence;

						str << token._value;
						if (rhsNeedsBrackets) {
							str << "(" << r_token.first << ")";
						} else 
							str << r_token.first;
					} else {
						opPrecedence = opp.prec(token._value);
						bool lhsNeedsBrackets = l_token.second > opPrecedence;
						bool rhsNeedsBrackets = r_token.second >= opPrecedence;

						if (lhsNeedsBrackets) {
							str << "(" << l_token.first << ")";
						} else 
							str << l_token.first;
						str << " " << token._value << " ";
						if (rhsNeedsBrackets) {
							str << "(" << r_token.first << ")";
						} else 
							str << r_token.first;
					}

					evaluation.push(std::make_pair(str.str(), opPrecedence));
				} else if (token._type == TokenType::UnaryMarker) {
					evaluation.push(std::make_pair(std::string{}, 0));
				} else if (token._type == TokenType::IsDefinedTest) {
					evaluation.push(std::make_pair(Concatenate("defined(", token._value, ")"), 0));
				} else {
					evaluation.push(std::make_pair(token._value, 0));
				}
			}
			assert(evaluation.size() == 1);
			return evaluation.top().first;
		}

		int64_t TokenDictionary::EvaluateExpression(
			IteratorRange<const Token*> tokenList,
			const std::function<std::optional<int64_t>(const TokenDefinition&, Token)>& lookupVariableFn) const
		{
			struct IntToken
			{
				TokenType _type;
				int64_t _value;
			};

			IntToken evaluatedVariables[_tokenDefinitions.size()];
			bool hasBeenEvaluated[_tokenDefinitions.size()];
			for (unsigned c=0; c<_tokenDefinitions.size(); ++c)
				hasBeenEvaluated[c] = false;

			std::stack<IntToken> evaluation;
			for (auto tokenIdx:tokenList) {
				const auto& token = _tokenDefinitions[tokenIdx];

				if (token._type == TokenType::Operation) {

					auto r_token = std::move(evaluation.top()); evaluation.pop();
					auto l_token = std::move(evaluation.top()); evaluation.pop();
					if (l_token._type == TokenType::UnaryMarker) {
						auto val = preprocessor_operations::UnaryNumeralOperation_Internal(
							packToken(r_token._value), token._value);
						evaluation.push(IntToken { TokenType::Literal, (int64_t)val.asInt() });
					} else {
						auto val = preprocessor_operations::NumeralOperation_Internal(
							packToken{l_token._value}, packToken{r_token._value}, token._value);
						evaluation.push(IntToken { TokenType::Literal, (int64_t)val.asInt() });
					}

				} else if (token._type == TokenType::Variable) {

					if (!hasBeenEvaluated[tokenIdx]) {
						
						auto lookup = lookupVariableFn(token, tokenIdx);
						if (lookup.has_value()) {
							evaluatedVariables[tokenIdx] = IntToken { TokenType::Literal, lookup.value() };
						} else {
							// undefined variables treated as 0
							evaluatedVariables[tokenIdx] = IntToken { TokenType::Literal, 0 };
						}
						hasBeenEvaluated[tokenIdx] = true;
					}

					evaluation.push(evaluatedVariables[tokenIdx]);
					
				} else if (token._type == TokenType::IsDefinedTest) {

					if (!hasBeenEvaluated[tokenIdx]) {
						auto lookup = lookupVariableFn(token, tokenIdx);
						evaluatedVariables[tokenIdx] = IntToken { TokenType::Literal, !!lookup.has_value() };
					}

					evaluation.push(evaluatedVariables[tokenIdx]);

				} else if (token._type == TokenType::Literal) {

					int intValue = 0;
					auto* end = FastParseValue(MakeStringSection(token._value), intValue);
					if (end != AsPointer(token._value.end()))
						Throw(std::runtime_error("Expression uses non-integer literal"));

					evaluation.push({token._type, intValue});
					
				} else {
					assert(token._value.empty());
					evaluation.push({token._type, 0});
				}
			}

			assert(evaluation.size() == 1);
			auto res = evaluation.top();
			assert(res._type == TokenType::Literal);
			return res._value;
		}

		int64_t TokenDictionary::EvaluateExpression(
			IteratorRange<const Token*> tokenList,
			IteratorRange<ParameterBox const*const*> environment) const
		{
			auto lookupVariableFn = [environment](const TokenDefinition& token, Token) -> std::optional<int64_t> {
				for (const auto&b:environment) {
					auto val = b->GetParameter<int>(MakeStringSection(token._value));
					if (val.has_value())
						return val.value();
				}
				return {};
			};
			return EvaluateExpression(tokenList, lookupVariableFn);
		}

		Token TokenDictionary::GetToken(TokenType type, const std::string& value)
		{
			TokenDictionary::TokenDefinition token { type, value };
			auto existing = std::find(_tokenDefinitions.begin(), _tokenDefinitions.end(), token);
			if (existing == _tokenDefinitions.end()) {
				_tokenDefinitions.push_back(token);
				return (Token)_tokenDefinitions.size() - 1;
			} else {
				return (Token)std::distance(_tokenDefinitions.begin(), existing);
			}
		}

		std::optional<Token> TokenDictionary::TryGetToken(TokenType type, StringSection<> value) const
		{
			auto existing = std::find_if(
				_tokenDefinitions.begin(), _tokenDefinitions.end(), 
				[type, value](const auto& c) { return c._type == type && XlEqString(value, c._value); });
			if (existing != _tokenDefinitions.end())
				return (Token)std::distance(_tokenDefinitions.begin(), existing);
			return {};
		}

		void TokenDictionary::PushBack(ExpressionTokenList& tokenList, TokenDictionary::TokenType type, const std::string& value)
		{
			tokenList.push_back(GetToken(type, value));
		}

		ExpressionTokenList TokenDictionary::Translate(
			const TokenDictionary& otherDictionary,
			const ExpressionTokenList& tokenListForOtherDictionary)
		{
			ExpressionTokenList result;
			result.reserve(tokenListForOtherDictionary.size());
			std::vector<Token> translated;
			translated.resize(otherDictionary._tokenDefinitions.size(), ~0u);
			for (const auto&token:tokenListForOtherDictionary) {
				auto& trns = translated[token];
				if (trns == ~0u)
					trns = GetToken(otherDictionary._tokenDefinitions[token]._type, otherDictionary._tokenDefinitions[token]._value);
				result.push_back(trns);
			}
			return result;
		}

		Token TokenDictionary::Translate(
			const TokenDictionary& otherDictionary,
			Token tokenForOtherDictionary)
		{
			return GetToken(otherDictionary._tokenDefinitions[tokenForOtherDictionary]._type, otherDictionary._tokenDefinitions[tokenForOtherDictionary]._value);
		}

		uint64_t TokenDictionary::CalculateHash() const
		{
			uint64_t result = DefaultSeed64;
			for (const auto& def:_tokenDefinitions)
				result = HashCombine(Hash64(def._value, (uint64_t)def._type), result);
			return result;
		}

		TokenDictionary::TokenDictionary()
		{
			// We have a few utility tokens which have universal values -- just for
			// convenience's sake
			_tokenDefinitions.push_back({TokenDictionary::TokenType::Literal, "0"});		// s_fixedTokenFalse
			_tokenDefinitions.push_back({TokenDictionary::TokenType::Literal, "1"});		// s_fixedTokenTrue
			_tokenDefinitions.push_back({TokenDictionary::TokenType::Operation, "&&"});		// s_fixedTokenLogicalAnd
			_tokenDefinitions.push_back({TokenDictionary::TokenType::Operation, "||"});		// s_fixedTokenLogicalOr
			_tokenDefinitions.push_back({TokenDictionary::TokenType::Operation, "!"});		// s_fixedTokenNot
			_tokenDefinitions.push_back({TokenDictionary::TokenType::UnaryMarker});			// s_fixedTokenUnaryMarker
		}

		TokenDictionary::~TokenDictionary() {}

		WorkingRelevanceTable CalculatePreprocessorExpressionRevelance(
			TokenDictionary& tokenDictionary,
			const ExpressionTokenList& abstractInput)
		{
			// For the given expression, we want to figure out how variables are used, and under what conditions
			// they impact the result of the evaluation

			struct PartialExpression
			{
				Internal::WorkingRelevanceTable _relevance;
				std::vector<Token> _subExpression;
			};

			using TokenType = Internal::TokenDictionary::TokenType;

			std::stack<PartialExpression> evaluation;
			for (auto tokenIdx:abstractInput) {
				const auto& token = tokenDictionary._tokenDefinitions[tokenIdx];

				if (token._type == TokenType::Operation) {

					PartialExpression r_token = std::move(evaluation.top()); evaluation.pop();
					PartialExpression l_token = std::move(evaluation.top()); evaluation.pop();

					// For logical operations, we need to carefully consider the left and right
					// relevance tables. For defined(), we will simplify the relevance to show
					// that we only care whether the symbol is defined or now.
					// For other operations, we will basically just merge together the relevance tables for both left and right

					PartialExpression newPartialExpression;
					if (token._value == "()")
						Throw(std::runtime_error("Only defined() is supported in relevance checks. Other functions are not supported"));
					
					if (token._value == "&&") {
						// lhs variables relevant when rhs expression is true
						// rhs variables relevant when lhs expression is true
						newPartialExpression._relevance = Internal::MergeRelevanceTables(
							l_token._relevance, r_token._subExpression,
							r_token._relevance, l_token._subExpression);
					} else if (token._value == "||") {
						// lhs variables relevant when rhs expression is false
						// rhs variables relevant when lhs expression is false
						newPartialExpression._relevance = Internal::MergeRelevanceTables(
							l_token._relevance, Internal::InvertExpression(r_token._subExpression),
							r_token._relevance, Internal::InvertExpression(l_token._subExpression));
					} else {
						newPartialExpression._relevance = Internal::MergeRelevanceTables(l_token._relevance, {}, r_token._relevance, {});
					}

					newPartialExpression._subExpression.reserve(l_token._subExpression.size() + r_token._subExpression.size() + 1);
					newPartialExpression._subExpression.insert(
						newPartialExpression._subExpression.end(),
						l_token._subExpression.begin(), l_token._subExpression.end());
					newPartialExpression._subExpression.insert(
						newPartialExpression._subExpression.end(),
						r_token._subExpression.begin(), r_token._subExpression.end());
					newPartialExpression._subExpression.push_back(tokenIdx);

					evaluation.push(std::move(newPartialExpression));

				} else if (token._type == TokenType::Variable) {

					PartialExpression newPartialExpression;
					newPartialExpression._relevance.insert(std::make_pair(tokenIdx, Internal::ExpressionTokenList{Internal::s_fixedTokenTrue}));
					newPartialExpression._subExpression = {tokenIdx};
					evaluation.push(std::move(newPartialExpression));

				} else if (token._type == TokenType::IsDefinedTest) {

					PartialExpression newPartialExpression;
					newPartialExpression._relevance.insert(std::make_pair(tokenIdx, Internal::ExpressionTokenList{Internal::s_fixedTokenTrue}));
					newPartialExpression._subExpression = {tokenIdx};
					evaluation.push(std::move(newPartialExpression));

				} else {

					PartialExpression newPartialExpression;
					newPartialExpression._subExpression = {tokenIdx};
					evaluation.push(std::move(newPartialExpression));
				
				}
			}

			assert(evaluation.size() == 1);
			return evaluation.top()._relevance;
		}
	}

	IPreprocessorIncludeHandler::~IPreprocessorIncludeHandler() {}

	/* TokenMap vars;
	vars["__VERSION__"] = 300;
	vars["DEFINED_NO_VALUE"] = packToken(nullptr, NONE);        // tokens with no value can't be part of expressions
	bool undefinedIsDefined = calculator::calculate("defined(NOT_DEFINED)", &vars).asBool();
	bool noValueIsDefined = calculator::calculate("defined(DEFINED_NO_VALUE)", &vars).asBool();
	bool noValueIsZero = calculator::calculate("NOT_DEFINED == 0", &vars).asBool();
	bool res = calculator::calculate("defined(__VERSION__) && defined(DEFINED_NO_VALUE) && !defined(NOT_DEFINED) && (__VERSION__ >= 300)", &vars).asBool();

	(void)res; (void)undefinedIsDefined; (void)noValueIsDefined; (void)noValueIsZero;
	return res;*/
}

