// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BinarySchemata.h"
#include "../Utility/ImpliedTyping.h"
#include "../Utility/ParameterBox.h"
#include <stack>

namespace Formatters
{
	auto BinarySchemata::FindBlockDefinition(StringSection<> name, BlockDefinitionId scope) const -> BlockDefinitionId
	{
		auto i = std::find_if(_blockDefinitions.begin(), _blockDefinitions.end(), [name, scope](const auto& c) { return c._scope == scope && XlEqString(name, c._name); });
		if (i != _blockDefinitions.end())
			return (BlockDefinitionId)std::distance(_blockDefinitions.begin(), i);
		if (scope != BlockDefinitionId_Invalid)
			return FindBlockDefinition(name, _blockDefinitions[scope]._scope);	// check parent's scope
		return BlockDefinitionId_Invalid;
	}

	auto BinarySchemata::FindAlias(StringSection<> name, BlockDefinitionId scope) const -> AliasId
	{
		auto i = std::find_if(_aliases.begin(), _aliases.end(), [name, scope](const auto& c) { return c._scope == scope && XlEqString(name, c._name); });
		if (i != _aliases.end())
			return (AliasId)std::distance(_aliases.begin(), i);
		if (scope != BlockDefinitionId_Invalid)
			return FindAlias(name, _blockDefinitions[scope]._scope);	// check parent's scope
		return AliasId_Invalid;
	}

	auto BinarySchemata::FindBitField(StringSection<> name, BlockDefinitionId scope) const -> BitFieldId
	{
		auto i = std::find_if(_bitFields.begin(), _bitFields.end(), [name, scope](const auto& c) { return c._scope == scope && XlEqString(name, c._name); });
		if (i != _bitFields.end())
			return (BitFieldId)std::distance(_bitFields.begin(), i);
		if (scope != BlockDefinitionId_Invalid)
			return FindBitField(name, _blockDefinitions[scope]._scope);	// check parent's scope
		return ~0u;
	}

	auto BinarySchemata::FindLiterals(StringSection<> name, BlockDefinitionId scope) const -> LiteralsId
	{
		auto i = std::find_if(_literals.begin(), _literals.end(), [name, scope](const auto& c) { return c._scope == scope && XlEqString(name, c._name); });
		if (i != _literals.end())
			return (LiteralsId)std::distance(_literals.begin(), i);
		if (scope != BlockDefinitionId_Invalid)
			return FindLiterals(name, _blockDefinitions[scope]._scope);	// check parent's scope
		return ~0u;
	}

	static void Require(ConditionalProcessingTokenizer& tokenizer, StringSection<> next)
	{
		auto token = tokenizer.GetNextToken();
		if (!XlEqString(token._value, next))
			Throw(FormatException(("Expecting '" + next.AsString() + "', but got '" + token._value.AsString() + "'").c_str(), token._start));
	}

	static bool operator==(const ConditionalProcessingTokenizer::Token& t, const char* comparison) { return XlEqString(t._value, comparison); }
	static bool operator!=(const ConditionalProcessingTokenizer::Token& t, const char* comparison) { return !XlEqString(t._value, comparison); }

	static TemplateParameterType RequireTemplateParameterPrefix(ConditionalProcessingTokenizer& tokenizer)
	{
		auto token = tokenizer.GetNextToken();
		if (token == "typename") {
			return TemplateParameterType::Typename;
		} else if (token == "expr") {
			return TemplateParameterType::Expression;
		} else
			Throw(FormatException("Expecting either 'typename' or 'expr' keywords", token._start));
	}

	std::string BinarySchemata::ParseExpressionStr(ConditionalProcessingTokenizer& tokenizer)
	{
		auto start = tokenizer.PeekNextToken()._value.begin();
		ConditionalProcessingTokenizer::Token lastToken;
		bool atLeastOne = false;

		std::stack<const char*> openBraces;
		for (;;) {
			auto next = tokenizer.PeekNextToken();
			if (next == ";") {
				break;
			} else if (next == "]" || next == ")" || next == "}") {
				if (openBraces.empty())
					break;
				next = tokenizer.GetNextToken();
				if (next != openBraces.top())
					Throw(FormatException("Braces unbalanced or unclosed in expression", next._start));
				openBraces.pop();
			} else if (next == "," && openBraces.empty()) {
				break;
			} else {
				next = tokenizer.GetNextToken();
				if (next == "[") openBraces.push("]");
				if (next == "(") openBraces.push(")");
				if (next == "{") openBraces.push("}");
			}
			lastToken = next;
			atLeastOne = true;
		}

		if (!atLeastOne) return {};

		if (!openBraces.empty())
			Throw(FormatException("Braces unbalanced or unclosed in expression", tokenizer.GetLocation()));

		return { start, lastToken._value.end() };
	}

	void BinarySchemata::PushExpression(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer)
	{
		auto str = ParseExpressionStr(tokenizer);
		auto tokenList = Utility::Internal::AsExpressionTokenList(workingDefinition._tokenDictionary, str, {}, Utility::Internal::ExpressionTokenListFlags::RecordHashes);
		if (tokenList.empty())
			Throw(FormatException("Expecting expression", tokenizer.GetLocation()));
		workingDefinition._cmdList.push_back((unsigned)Cmd::EvaluateExpression);
		workingDefinition._cmdList.push_back(tokenList.size());
		workingDefinition._cmdList.insert(workingDefinition._cmdList.end(), tokenList.begin(), tokenList.end());
	}

	void BinarySchemata::PushComplexType(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer)
	{
		auto baseName = ParseTypeBaseName(tokenizer);

		std::vector<TemplateParameterType> templateParams;
		if (tokenizer.PeekNextToken() == "(") {
			tokenizer.GetNextToken();
			if (tokenizer.PeekNextToken() != ")") {
				for (;;) {
					auto type = RequireTemplateParameterPrefix(tokenizer);
					if (type == TemplateParameterType::Typename) {
						PushComplexType(workingDefinition, tokenizer);
					} else {
						PushExpression(workingDefinition, tokenizer);
					}
					templateParams.push_back(type);
					auto endOrSep = tokenizer.GetNextToken();
					if (endOrSep == ",") continue;
					else if (endOrSep == ")") break;
					else Throw(FormatException("Expecting either ',' or ')'", endOrSep._start));
				}
			} else {
				tokenizer.GetNextToken();
			}
		}

		workingDefinition._cmdList.push_back((unsigned)Cmd::LookupType);
		auto baseNameAsToken = workingDefinition._tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, baseName, Hash64(baseName));
		workingDefinition._cmdList.push_back(baseNameAsToken);
		workingDefinition._cmdList.push_back(templateParams.size());
		for (auto t=templateParams.rbegin(); t!=templateParams.rend(); ++t)
			workingDefinition._cmdList.push_back((unsigned)*t);
	}

	static void ParseTemplateDeclaration(
		ConditionalProcessingTokenizer& tokenizer, 
		Utility::Internal::TokenDictionary& tokenDictionary, std::vector<unsigned>& templateParameterNames, uint32_t& templateParameterTypeField)
	{
		Require(tokenizer, "(");
		if (tokenizer.PeekNextToken() != ")") {
			for (;;) {
				auto paramType = RequireTemplateParameterPrefix(tokenizer);
				auto paramName = tokenizer.GetNextToken();

				templateParameterNames.push_back(
					tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, paramName._value.AsString(), Hash64(paramName._value)));
				if (paramType == TemplateParameterType::Typename)
					templateParameterTypeField |= 1u<<unsigned(templateParameterNames.size()-1);

				auto endOrSep = tokenizer.GetNextToken();
				if (endOrSep == ",") continue;
				else if (endOrSep == ")") break;
				else Throw(FormatException("Expecting either ',' or ')'", endOrSep._start));
			}
		} else {
			tokenizer.GetNextToken();
		}
	}

	void BinarySchemata::ParseBlock(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		BlockDefinition workingDefinition;

		auto blockName = tokenizer.GetNextToken();
		if (blockName == "template") {
			ParseTemplateDeclaration(tokenizer, workingDefinition._tokenDictionary, workingDefinition._templateParameterNames, workingDefinition._templateParameterTypeField);
			blockName = tokenizer.GetNextToken();
		}

		auto next = tokenizer.GetNextToken();
		if (next != "{")
			Throw(FormatException("Expecting '{'", next._start));

		// Note that we can't have duplicate block definitions even if they are bracketed in non-overlapping #if's
		// (because we can't distinguish between adding members that are controlled by completely-non overlapping symbols, or where we might actually be appending members)
		for (const auto& b:_blockDefinitions)
			if (XlEqString(blockName._value, b._name))
				Throw(FormatException("Duplicate block definition (" + blockName._value.AsString() + ")", tokenizer.GetLocation()));

		auto reservedBlockId = (unsigned)_blockDefinitions.size();
		_blockDefinitions.push_back({});

		for (;;) {
			auto peekNext = tokenizer.PeekNextToken();
			if (peekNext == "}") {
				tokenizer.GetNextToken();
				break;
			}

			if (TryDeclaration(tokenizer, reservedBlockId, peekNext)) {
				// we don't allow embedded declaration within template types because the scoping rules would just get too complicated
				if (!workingDefinition._templateParameterNames.empty())
					Throw(FormatException("Embedded declarations within template types are not supported", peekNext._start));
			} else {
				size_t writeJumpHere = 0;
				auto currentCondition = tokenizer._preprocessorContext.GetCurrentConditionString();
				if (!currentCondition.empty()) {
					auto tokenList = Utility::Internal::AsExpressionTokenList(workingDefinition._tokenDictionary, currentCondition, {}, Utility::Internal::ExpressionTokenListFlags::RecordHashes);
					if (tokenList.empty())
						Throw(FormatException("Could not parse condition as expression", tokenizer.GetLocation()));
					workingDefinition._cmdList.push_back((unsigned)Cmd::EvaluateExpression);
					workingDefinition._cmdList.push_back(tokenList.size());
					workingDefinition._cmdList.insert(workingDefinition._cmdList.end(), tokenList.begin(), tokenList.end());
					workingDefinition._cmdList.push_back((unsigned)Cmd::IfFalseThenJump);
					writeJumpHere = workingDefinition._cmdList.size();
					workingDefinition._cmdList.push_back(0);
					workingDefinition._cmdList.push_back((unsigned)_conditionSymbolLines.size());
					_conditionSymbolLines.push_back(peekNext._start._lineIndex);
				}

				PushComplexType(workingDefinition, tokenizer);

				for (;;) {
					auto name = tokenizer.GetNextToken();
					auto nameAsToken = workingDefinition._tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, name._value.AsString(), Hash64(name._value));

					next = tokenizer.GetNextToken();
					if (next == "[") {
						PushExpression(workingDefinition, tokenizer);
						Require(tokenizer, "]");
						next = tokenizer.GetNextToken();

						workingDefinition._cmdList.push_back((unsigned)Cmd::InlineArrayMember);
						workingDefinition._cmdList.push_back(nameAsToken);
					} else {
						workingDefinition._cmdList.push_back((unsigned)Cmd::InlineIndividualMember);
						workingDefinition._cmdList.push_back(nameAsToken);
					}

					if (next != ",") break;		// use comma to separate a list of variables with the same type
				}

				if (next != ";")
					Throw(FormatException("Expecting ';'", next._start));

				workingDefinition._cmdList.push_back((unsigned)Cmd::PopTypeStack);

				if (writeJumpHere)
					workingDefinition._cmdList[writeJumpHere] = (unsigned)workingDefinition._cmdList.size();
			}
		}
		Require(tokenizer, ";");

		_blockDefinitions[reservedBlockId] = {blockName._value.AsString(), scope, std::move(workingDefinition)};
	}

	void BinarySchemata::ParseLiterals(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto condition = tokenizer._preprocessorContext.GetCurrentConditionString();
		auto name = tokenizer.GetNextToken();
		Require(tokenizer, "{");

		ParameterBox literals;
		for (;;) {
			auto literalName = tokenizer.GetNextToken();
			if (literalName == "}") break;
			Require(tokenizer, "=");
			literals.SetParameter(literalName._value, tokenizer.GetNextToken()._value);
			Require(tokenizer, ";");
		}
		Require(tokenizer, ";");

		_literals.push_back({name._value.AsString(), scope, std::move(literals)});
	}

	void BinarySchemata::ParseAlias(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto condition = tokenizer._preprocessorContext.GetCurrentConditionString();

		Alias workingDefinition;
		bool gotTemplate = false, gotDecoder = false;
		for (;;) {
			if (tokenizer.PeekNextToken() == "template") {
				if (gotTemplate) Throw(FormatException("Multiple template declarations while parsing alias", tokenizer.GetLocation()));
				gotTemplate = true;
				tokenizer.GetNextToken();
				ParseTemplateDeclaration(tokenizer, workingDefinition._tokenDictionary, workingDefinition._templateParameterNames, workingDefinition._templateParameterTypeField);			
			} else if (tokenizer.PeekNextToken() == "decoder") {
				if (gotDecoder) Throw(FormatException("Multiple decoder declarations while parsing alias", tokenizer.GetLocation()));
				gotDecoder = true;
				tokenizer.GetNextToken();
				Require(tokenizer, "(");
				auto decoderName = tokenizer.GetNextToken();
				auto bitField = FindBitField(decoderName._value);
				if (bitField != ~0u) {
					workingDefinition._bitFieldDecoder = bitField;
				} else {
					auto literals = FindLiterals(decoderName._value);
					if (literals == ~0u)
						Throw(FormatException(("Unknown decoder (" + decoderName._value.AsString() + ")").c_str(), tokenizer.GetLocation()));
					workingDefinition._enumDecoder = literals;
				}
				Require(tokenizer, ")");
			} else break;
		}

		auto name = tokenizer.GetNextToken();

		Require(tokenizer, "=");
		workingDefinition._aliasedType = ParseTypeBaseName(tokenizer);

		Require(tokenizer, ";");
		_aliases.push_back({name._value.AsString(), scope, workingDefinition});
	}

	static uint64_t RequireIntegerLiteral(ConditionalProcessingTokenizer& tokenizer)
	{
		auto token = tokenizer.GetNextToken();
		alignas(uint64_t) char buffer[256];
		*(uint64_t*)buffer = 0;
		auto type = ImpliedTyping::ParseFullMatch(token._value, MakeIteratorRange(buffer));
		if (type._arrayCount != 1 || 
			(	type._type != ImpliedTyping::TypeCat::Int8 && type._type != ImpliedTyping::TypeCat::UInt8
			&& 	type._type != ImpliedTyping::TypeCat::Int16 && type._type != ImpliedTyping::TypeCat::UInt16
			&& 	type._type != ImpliedTyping::TypeCat::Int32 && type._type != ImpliedTyping::TypeCat::UInt32
			&& 	type._type != ImpliedTyping::TypeCat::Int64 && type._type != ImpliedTyping::TypeCat::UInt64))
			Throw(FormatException("Expecting integer literal", token._start));
		return *(uint64_t*)buffer;
	}

	std::string BinarySchemata::ParseTypeBaseName(ConditionalProcessingTokenizer& tokenizer)
	{
		return tokenizer.GetNextToken()._value.AsString();
	}

	void BinarySchemata::ParseBitField(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto condition = tokenizer._preprocessorContext.GetCurrentConditionString();
		auto name = tokenizer.GetNextToken();
		Require(tokenizer, "{");

		BitFieldDefinition bitField;
		for (;;) {
			auto next = tokenizer.GetNextToken();
			if (next == "}") break;
			if (next != "bits") Throw(FormatException("Expecting 'bits'", next._start));
			auto openBrace = tokenizer.GetNextToken();
			if (openBrace != "{" && openBrace != "(" && openBrace != "[") Throw(FormatException("Expecting open brace", next._start));
			auto firstLimit = RequireIntegerLiteral(tokenizer);
			std::optional<uint64_t> secondLimit;
			next = tokenizer.GetNextToken();
			if (next == ",") {
				secondLimit = RequireIntegerLiteral(tokenizer);
				next = tokenizer.GetNextToken();
			} 

			if (next != "}" && next != ")" && next != "}")
				Throw(FormatException("Expecting close brace", next._start));

			if (openBrace == "{") {
				if (next != "}" || secondLimit.has_value())
					Throw(FormatException("Bitfield entries that start with '{' must close with '}' and contain only a single bit", next._start));
			} else if (next == "}") {
				Throw(FormatException("Bitfield entries that start with '(' or '[' must close with ')' or ']'", next._start));
			} else if (!secondLimit.has_value())
				Throw(FormatException("Bitfield entries that start with '(' or '[' must have an upper bound specified", next._start));

			BitFieldDefinition::BitRange range;
			if (openBrace == "{") {
				range._min = firstLimit;
				range._count = 1;
			} else {
				range._min = (openBrace == "[") ? firstLimit : firstLimit+1;
				auto lastPlusOne = (next == "]") ? secondLimit.value() : secondLimit.value()-1;
				if (lastPlusOne <= range._min)
					Throw(FormatException("Bit range specified does not include any bits, or is inverted", next._start));
				range._count = lastPlusOne - range._min;
			}

			Require(tokenizer, ":");

			range._storageType = ParseTypeBaseName(tokenizer);
			range._name = tokenizer.GetNextToken()._value.AsString();
			Require(tokenizer, ";");

			bitField._bitRanges.push_back(range);
		}
		Require(tokenizer, ";");

		_bitFields.push_back({name._value.AsString(), scope, std::move(bitField)});
	}

	bool BinarySchemata::TryDeclaration(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, const ConditionalProcessingTokenizer::Token& peekNext)
	{
		if (peekNext == "block") {
			tokenizer.GetNextToken();
			ParseBlock(tokenizer, scope);
			return true;
		} else if (peekNext == "literals") {
			tokenizer.GetNextToken();
			ParseLiterals(tokenizer, scope);
			return true;
		} else if (peekNext == "alias") {
			tokenizer.GetNextToken();
			ParseAlias(tokenizer, scope);
			return true;
		} else if (peekNext == "bitfield") {
			tokenizer.GetNextToken();
			ParseBitField(tokenizer, scope);
			return true;
		}
		return false;
	}

	void BinarySchemata::Parse(ConditionalProcessingTokenizer& tokenizer)
	{
		for (;;) {
			auto peekNext = tokenizer.PeekNextToken();
			if (peekNext._value.IsEmpty()) {
				tokenizer.GetNextToken();
				break;
			}

			if (!TryDeclaration(tokenizer, BlockDefinitionId_Invalid, peekNext))
				Throw(FormatException("Expecting a top-level declaration", peekNext._start));
		}

		if (!tokenizer.Remaining().IsEmpty())
			Throw(FormatException("Additional tokens found, expecting end of file", tokenizer.GetLocation()));
	}

	BinarySchemata::ConditionSymbol BinarySchemata::GetConditionSymbol(unsigned idx) const
	{
		assert(idx < _conditionSymbolLines.size());
		return { _conditionSymbolLines[idx] };
	}

	BinarySchemata::BinarySchemata(
		StringSection<> inputData,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
	{
		ConditionalProcessingTokenizer tokenizer(inputData);
		Parse(tokenizer);
	}

	BinarySchemata::BinarySchemata(
		Utility::IPreprocessorIncludeHandler::Result&& initialFile,
		Utility::IPreprocessorIncludeHandler* includeHandler)
	{
		ConditionalProcessingTokenizer tokenizer(std::move(initialFile), includeHandler);
		Parse(tokenizer);
	}

	BinarySchemata::BinarySchemata() {}
	BinarySchemata::~BinarySchemata() {}
}

