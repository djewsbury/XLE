// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if XLE_ANTLR_ENABLE

#include "../Core/Exceptions.h"
#include "Exceptions.h"
#include "AntlrHelper.h"
#include "Grammar/ShaderLexer.h"
#include "Grammar/ShaderParser.h"
#include "../Utility/FunctionUtils.h"
#include "../Utility/Conversion.h"
#include <sstream>

namespace ShaderSourceParser { namespace AntlrHelper
{

	namespace Internal
	{
		template<> void DestroyAntlrObject<>(struct ANTLR3_INPUT_STREAM_struct* object)
		{
			if (object) object->close(object);
		}
	}

	pANTLR3_BASE_TREE GetChild(pANTLR3_BASE_TREE node, ANTLR3_UINT32 childIndex)
	{
		return pANTLR3_BASE_TREE(node->getChild(node, childIndex));
	}

	unsigned GetChildCount(pANTLR3_BASE_TREE node)
	{
		return node->getChildCount(node);
	}

	pANTLR3_COMMON_TOKEN GetToken(pANTLR3_BASE_TREE node)
	{
		return node->getToken(node);
	}

	ANTLR3_UINT32 GetType(pANTLR3_COMMON_TOKEN token)
	{
		return token->getType(token);
	}

	template<typename CharType>
		std::basic_string<CharType> AsString(ANTLR3_STRING* antlrString)
	{
		auto tempString = antlrString->toUTF8(antlrString);
		auto result = std::basic_string<utf8>((utf8*)tempString->chars);
		tempString->factory->destroy(tempString->factory, tempString);
		return Conversion::Convert<std::basic_string<CharType>>(result);
	}

	template<typename CharType>
		std::basic_string<CharType> AsString(pANTLR3_COMMON_TOKEN token)
	{
		ANTLR3_STRING* str = (token->getText)(token);  // (str seems to be retained in the factory...?)
		return AsString(str);
	}

	template std::basic_string<char> AsString(ANTLR3_STRING* antlrString);
	template std::basic_string<char> AsString(ANTLR3_COMMON_TOKEN* token);
		
	void Description(std::ostream& str, pANTLR3_COMMON_TOKEN token)
	{
		ANTLR3_STRING* strng = token->toString(token);
		str << AsString<char>(strng);
		strng->factory->destroy(strng->factory, strng);
	}

	void StructureDescription(std::ostream& str, pANTLR3_BASE_TREE node, unsigned indent)
	{
		auto indentBuffer = std::make_unique<char[]>(indent+1);
		std::fill(indentBuffer.get(), &indentBuffer[indent], ' ');
		indentBuffer[indent] = '\0';

		str << indentBuffer.get();
		auto* token = GetToken(node);
		if (token) {
			Description(str, token);
		} else {
			str << "<<no token>>";
		}
		str << std::endl;

		auto childCount = GetChildCount(node);
		for (unsigned c=0; c<childCount; ++c)
			StructureDescription(str, GetChild(node, c), indent+4);
	}

	namespace
	{
		// Print the source line containing the error, with a caret pointing at the failing character
		// (Not handling tabs correctly, unfortunately)
		void AppendSourceLine(std::basic_stringstream<char>& str, StringSection<> sourceText, const ANTLR3_EXCEPTION* exc)
		{
			if (sourceText.IsEmpty() || exc->line == 0) return;

			// Find the start of the 1-based line `exc->line`
			auto lineStart = sourceText.begin();
			ANTLR3_UINT32 currentLine = 1;
			for (const char* p = sourceText.begin(); p < sourceText.end(); ++p) {
				if (*p != '\n') continue;
				if (currentLine == exc->line) break;
				++currentLine;
				lineStart = p + 1;
			}
			if (currentLine < exc->line) return;	// error line is beyond the end of the source

			const char* lineEnd = lineStart;
			while (lineEnd < sourceText.end() && *lineEnd != '\n' && *lineEnd != '\r')
				++lineEnd;

			auto lineLength = (unsigned)(lineEnd - lineStart);
			unsigned column = (exc->charPositionInLine > 0) ? (unsigned)exc->charPositionInLine : 0;
			if (column > lineLength) column = lineLength;

			str << std::endl;
			str.write(lineStart, (std::streamsize)lineLength);
			str << std::endl;
			str << std::string((size_t)column, ' ');
			str << '^';
		}
	}

	void __cdecl ExceptionSet::HandleException(
		ExceptionSet* pimpl,
		const ANTLR3_EXCEPTION* exc,
		const ANTLR3_UINT8 ** tokenNames)
	{
		assert(pimpl && exc);

		Error error;

		// We have a problem with character types here. Since we're using
		// basic_stringstream, we can only use one of the built-in C++
		// character types!
		// Since antlr has support for different character types, ideally we
		// would like to have a bit more flexibility here (such as using UTF8
		// for the error message strings)
		using CharType = decltype(error._message)::value_type;

		std::basic_stringstream<CharType> str;

		switch (exc->type) {
		case ANTLR3_UNWANTED_TOKEN_EXCEPTION:   str << "Extraneous input"; break;
		case ANTLR3_MISSING_TOKEN_EXCEPTION:    str << "Missing token"; break;
		case ANTLR3_MISMATCHED_TOKEN_EXCEPTION: str << "Mismatched token"; break;
		case ANTLR3_NO_VIABLE_ALT_EXCEPTION:    str << "No viable alternative"; break;
		case ANTLR3_MISMATCHED_SET_EXCEPTION:   str << "Mismatched set of tokens"; break;
		case ANTLR3_EARLY_EXIT_EXCEPTION:       str << "Early exit from loop"; break;
		case ANTLR3_FAILED_PREDICATE_EXCEPTION: str << "Failed semantic predicate"; break;
		default:                                str << "Syntax error"; break;
		}

		// The set of tokens the parser was expecting at the point of failure.
		if (tokenNames && exc->expectingSet) {

			auto* list = (const ANTLR3_BITSET_LIST*)exc->expectingSet;
			std::vector<const ANTLR3_UINT8*> expected;
			ANTLR3_UINT32 total = 0;

			for (ANTLR3_UINT32 w = 0; w < list->length; ++w) {
				for (int b = 0; b < 64; ++b) {
					if (!(list->bits[w] & ((ANTLR3_BITWORD)1 << b))) continue;
					auto type = w * 64 + (ANTLR3_UINT32)b;
					++total;
					// tokenNames is a small NULL-padded array; never index it with sentinel token types (such as EOR)
					if (type < 0x10000 && tokenNames[type] && expected.size() < 12)
						expected.push_back(tokenNames[type]);
				}
			}

			if (!expected.empty()) {
				str << ". Expected any of:";
				for (auto name : expected)
					str << " " << name;
				if (total > expected.size())
					str << " (and " << (total - (ANTLR3_UINT32)expected.size()) << " more)";
			}

		} else if (tokenNames && exc->expecting) {
			str << ". Expected: " << tokenNames[exc->expecting];
		}

		// The token at which the parser failed
		ANTLR3_COMMON_TOKEN* token = (ANTLR3_COMMON_TOKEN*)exc->token;
		if (token) {
			auto text = AsString<CharType>(token->getText(token));
			str << ". Near token: (" << text << " at " << token->getLine(token) << ":" << token->getCharPositionInLine(token) << ")";
		}

		auto messageText = (const CharType*)exc->message;
		if (messageText && messageText[0] != 0)
			str << ". " << messageText;

		// Show the offending source line, with a caret at the failure position
		AppendSourceLine(str, pimpl->_sourceText, exc);

		error._lineStart = error._lineEnd = exc->line;
		error._charStart = error._charEnd = (unsigned)exc->charPositionInLine;
		error._message = str.str();

		pimpl->_errors.push_back(error);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	ExceptionContext::ExceptionContext()
	{
		// Antlr stuff is in 'C' -- so we have to drop back to a C way of doing things
		// these globals means we can only do a single parse at a time.
		_previousExceptionHandler = SetShaderParserExceptionHandler(
			ExceptionHandlerAndUserData {
				(ExceptionHandler*)&ExceptionSet::HandleException,
				&_exceptions
			});
	}

	ExceptionContext::~ExceptionContext()
	{
		SetShaderParserExceptionHandler(_previousExceptionHandler);
	}

}}


namespace ShaderSourceParser
{
	namespace Exceptions
	{
		ParsingFailure::ParsingFailure(IteratorRange<Error*> errors) never_throws
		: _errors(errors.begin(), errors.end())
		{}
		ParsingFailure::~ParsingFailure() {}

		const char* ParsingFailure::what() const never_throws
		{
			if (_cachedStr.empty()) {
				std::stringstream str;
				str << "Parsing Failure" << std::endl;
				for (const auto&e:_errors)
					str << "(line:" << e._lineStart << ", char:" << e._charStart << ") " << e._message << std::endl;
				_cachedStr = str.str();
			}
			return _cachedStr.c_str();
		}
	}
}

#endif
