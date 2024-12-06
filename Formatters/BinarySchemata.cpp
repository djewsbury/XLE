// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BinarySchemata.h"
#include "../Assets/PreprocessorIncludeHandler.h"
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

	void BinarySchemata::PushComplexType(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, std::string baseName)
	{
		// handle template parameters (which can be recursive)
		std::vector<TemplateParameterType> templateParams;
		if (tokenizer.PeekNextToken() == "(") {
			tokenizer.GetNextToken();
			if (tokenizer.PeekNextToken() != ")") {
				for (;;) {
					auto type = RequireTemplateParameterPrefix(tokenizer);
					if (type == TemplateParameterType::Typename) {
						auto baseName = ParseTypeBaseName(tokenizer, scope);
						PushComplexType(workingDefinition, tokenizer, scope, baseName);
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
		auto baseNameAsToken = workingDefinition._tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, std::make_pair(baseName, Hash64(baseName)));
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
					tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, std::make_pair(paramName._value.AsString(), Hash64(paramName._value))));
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
	
	size_t BinarySchemata::WriteJumpBlock(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, unsigned lineIdx)
	{
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
			_conditionSymbolLines.push_back(lineIdx);
		}
		return writeJumpHere;
	}

	std::string BinarySchemata::ParseBlock(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		BlockDefinition workingDefinition;

		auto next = tokenizer.GetNextToken();
		if (next == "template") {
			ParseTemplateDeclaration(tokenizer, workingDefinition._tokenDictionary, workingDefinition._templateParameterNames, workingDefinition._templateParameterTypeField);
			next = tokenizer.GetNextToken();
		}

		std::string blockName;

		if (next == "{") {
			blockName = "Unnamed" + std::to_string(_nextUnnamedSymbolIdx++);
		} else {
			blockName = next._value.AsString();

			next = tokenizer.GetNextToken();
			if (next != "{")
				Throw(FormatException("Expecting '{'", next._start));
		}

		// Note that we can't have duplicate block definitions even if they are bracketed in non-overlapping #if's
		// (because we can't distinguish between adding members that are controlled by completely-non overlapping symbols, or where we might actually be appending members)
		for (const auto& b:_blockDefinitions)
			if (blockName == b._name)
				Throw(FormatException("Duplicate block definition (" + blockName + ")", tokenizer.GetLocation()));

		auto reservedBlockId = (unsigned)_blockDefinitions.size();
		_blockDefinitions.push_back({});
		_blockDefinitions[reservedBlockId]._scope = scope;

		for (;;) {
			auto peekNext = tokenizer.PeekNextToken();
			if (peekNext == "}") {
				tokenizer.GetNextToken();
				break;
			}

			std::string typeBaseName;
			if (typeBaseName = TryDeclaration(tokenizer, reservedBlockId, peekNext); !typeBaseName.empty()) {
				// we don't allow embedded declaration within template types because the scoping rules would just get too complicated
				if (!workingDefinition._templateParameterNames.empty())
					Throw(FormatException("Embedded declarations within template types are not supported", peekNext._start));

				if (tokenizer.PeekNextToken() == ";") {
					tokenizer.GetNextToken();	// just a declaration
					continue;
				}
			} else if (TryCommand(workingDefinition, tokenizer, reservedBlockId, peekNext)) {
				continue;
			} else {
				typeBaseName = ParseTypeBaseName(tokenizer, reservedBlockId);
			}

			assert(!typeBaseName.empty());
			size_t writeJumpHere = WriteJumpBlock(workingDefinition, tokenizer, peekNext._start._lineIndex);
			PushComplexType(workingDefinition, tokenizer, reservedBlockId, typeBaseName);

			for (;;) {
				auto name = tokenizer.GetNextToken();
				auto nameAsToken = workingDefinition._tokenDictionary.GetOrAddToken(Utility::Internal::TokenDictionary::TokenType::Variable, std::make_pair(name._value.AsString(), Hash64(name._value)));

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

		_blockDefinitions[reservedBlockId] = {blockName, scope, std::move(workingDefinition)};
		return blockName;
	}

	std::string BinarySchemata::ParseLiterals(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto condition = tokenizer._preprocessorContext.GetCurrentConditionString();
		std::string name;
		auto next = tokenizer.GetNextToken();
		if (next == "{") {
			name = "Unnamed" + std::to_string(_nextUnnamedSymbolIdx++);
		} else {
			name = next._value.AsString();
			Require(tokenizer, "{");
		}

		ParameterBox literals;
		for (;;) {
			auto literalName = tokenizer.GetNextToken();
			if (literalName == "}") break;
			Require(tokenizer, "=");
			literals.SetParameter(literalName._value, tokenizer.GetNextToken()._value);
			Require(tokenizer, ";");
		}

		_literals.push_back({name, scope, std::move(literals)});
		return name;
	}

	void BinarySchemata::ParseDecoder(ConditionalProcessingTokenizer& tokenizer, Alias& workingDefinition, BlockDefinitionId scope)
	{
		Require(tokenizer, "(");
		auto decoderName = tokenizer.GetNextToken();
		auto bitField = FindBitField(decoderName._value, scope);
		if (bitField != ~0u) {
			workingDefinition._bitFieldDecoder = bitField;
		} else {
			auto literals = FindLiterals(decoderName._value, scope);
			if (literals == ~0u)
				Throw(FormatException(("Unknown decoder (" + decoderName._value.AsString() + ")").c_str(), tokenizer.GetLocation()));
			workingDefinition._enumDecoder = literals;
		}
		Require(tokenizer, ")");
	}

	std::string BinarySchemata::ParseAlias(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
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
				ParseDecoder(tokenizer, workingDefinition, scope);
			} else break;
		}

		auto name = tokenizer.GetNextToken();

		Require(tokenizer, "=");
		workingDefinition._aliasedType = ParseTypeBaseName(tokenizer, scope);

		auto result = name._value.AsString();
		_aliases.push_back({result, scope, workingDefinition});
		return result;
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

	static bool operator==(const Alias& lhs, const Alias& rhs)
	{
		return lhs._aliasedType == rhs._aliasedType
			&& lhs._tokenDictionary._tokenDefinitions == rhs._tokenDictionary._tokenDefinitions
			&& lhs._templateParameterNames == rhs._templateParameterNames
			&& lhs._templateParameterTypeField == rhs._templateParameterTypeField
			&& lhs._bitFieldDecoder == rhs._bitFieldDecoder
			&& lhs._enumDecoder == rhs._enumDecoder
			;
	}

	std::string BinarySchemata::ParseTypeBaseName(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto next = tokenizer.GetNextToken();
		if (next == "decoder") {

			// inline alias
			Alias workingDefinition;
			ParseDecoder(tokenizer, workingDefinition, scope);

			workingDefinition._aliasedType = tokenizer.GetNextToken()._value.AsString();

			// look for an existing alias we can reuse
			for (auto& a:_aliases)
				if (a._scope == scope && a._def == workingDefinition)
					return a._name;

			auto name = "Unnamed" + std::to_string(_nextUnnamedSymbolIdx++);
			_aliases.emplace_back(Def<Alias>{name, scope, std::move(workingDefinition)});
			return name;

		} else
			return next._value.AsString();
	}

	std::string BinarySchemata::ParseBitField(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope)
	{
		auto condition = tokenizer._preprocessorContext.GetCurrentConditionString();
		auto next = tokenizer.GetNextToken();
		std::string name;
		if (next == "{") {
			name = "Unnamed" + std::to_string(_nextUnnamedSymbolIdx++);
		} else {
			name = next._value.AsString();
			Require(tokenizer, "{");
		}

		BitFieldDefinition bitField;
		for (;;) {
			next = tokenizer.GetNextToken();
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

			range._storageType = ParseTypeBaseName(tokenizer, scope);
			range._name = tokenizer.GetNextToken()._value.AsString();
			Require(tokenizer, ";");

			bitField._bitRanges.push_back(range);
		}

		_bitFields.push_back({name, scope, std::move(bitField)});
		return name;
	}

	std::string BinarySchemata::TryDeclaration(ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, const ConditionalProcessingTokenizer::Token& peekNext)
	{
		if (peekNext == "block") {
			tokenizer.GetNextToken();
			return ParseBlock(tokenizer, scope);
		} else if (peekNext == "literals") {
			tokenizer.GetNextToken();
			return ParseLiterals(tokenizer, scope);
		} else if (peekNext == "alias") {
			tokenizer.GetNextToken();
			return ParseAlias(tokenizer, scope);
		} else if (peekNext == "bitfield") {
			tokenizer.GetNextToken();
			return ParseBitField(tokenizer, scope);
		}
		return {};
	}

	bool BinarySchemata::TryCommand(BlockDefinition& workingDefinition, ConditionalProcessingTokenizer& tokenizer, BlockDefinitionId scope, const ConditionalProcessingTokenizer::Token& peekNext)
	{
		if (peekNext == "throw") {
			tokenizer.GetNextToken();

			size_t writeJumpHere = WriteJumpBlock(workingDefinition, tokenizer, peekNext._start._lineIndex);

			std::vector<int> pendingCmds;
			unsigned expressionCount = 0;

			for (;;) {
				auto next = tokenizer.GetNextToken();
				if (next == ";")
					break;

				if (next == "[") {
					PushExpression(workingDefinition, tokenizer);
					Require(tokenizer, "]");

					pendingCmds.push_back(-1);
					++expressionCount;
				} else if (next == "\"") {
					auto startToken = next;
					// skip forward until the end quote;
					for (;;) {
						next = tokenizer.GetNextToken();
						if (next._value.IsEmpty())
							Throw(FormatException("Unterminated quote", startToken._start));
						if (next == "\"") break;
					}
					auto string = MakeStringSection(startToken._value.begin(), next._value.end());
					if (!string.IsEmpty()) {
						assert(*string._start == '\"'); ++string._start;
						assert(*(string._end-1) == '\"'); --string._end;
					}
					if (!string.IsEmpty()) {
						auto alignedSize = (string.size()+4-1+1)/4;		// note additional +1 for the null terminator
						pendingCmds.push_back(alignedSize);
						pendingCmds.insert(pendingCmds.end(), alignedSize, 0);
						std::copy(string._start, string._end, (char*)AsPointer(pendingCmds.end()-alignedSize));
					}
				}
			}

			// reorder the expressions
			if (expressionCount) {
				int c=expressionCount-1;
				for (auto p=pendingCmds.begin(); p!=pendingCmds.end(); ++p) {
					if (int(*p) < 0) {
						*p -= c;
						--c;
					} else {
						assert(*p!=0);
						p += *p;
					}
				}
			}

			workingDefinition._cmdList.push_back((unsigned)Cmd::Throw);
			workingDefinition._cmdList.push_back(expressionCount);
			workingDefinition._cmdList.insert(workingDefinition._cmdList.end(), pendingCmds.begin(), pendingCmds.end());
			workingDefinition._cmdList.push_back(0);	// to end

			if (writeJumpHere)
				workingDefinition._cmdList[writeJumpHere] = (unsigned)workingDefinition._cmdList.size();
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

			if (TryDeclaration(tokenizer, BlockDefinitionId_Invalid, peekNext).empty())
				Throw(FormatException("Expecting a top-level declaration", peekNext._start));

			Require(tokenizer, ";");
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
	: _depVal(depVal)
	{
		::Assets::PreprocessorIncludeHandler includeHandler;
		ConditionalProcessingTokenizer tokenizer(inputData, searchRules.GetBaseFile(), &includeHandler);
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

