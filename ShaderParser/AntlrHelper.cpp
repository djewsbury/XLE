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

        error._lineStart = error._lineEnd = exc->line;
        error._charStart = error._charEnd = exc->charPositionInLine;

        std::basic_stringstream<CharType> str;
        if (tokenNames && exc->expectingSet) {

            auto* bitset = (ANTLR3_BITSET*)exc->expectingSet;
            auto* expectedIntList = (*bitset->toIntList)(bitset);
            auto expectedCount = (*bitset->size)(bitset);

            bool printExpectedList = false;
            switch (exc->type) {
            case ANTLR3_UNWANTED_TOKEN_EXCEPTION:
                str << "Extraneous input - expected any of the following tokens:" << std::endl;
                printExpectedList = true;
                break;

            case ANTLR3_MISSING_TOKEN_EXCEPTION:
                str << "Missing token -- expected any of the following tokens:" << std::endl;
                printExpectedList = true;
                break;

            case ANTLR3_MISMATCHED_TOKEN_EXCEPTION:
                str << "Mismatched token. Eexpected any of the following tokens:" << std::endl;
                printExpectedList = true;
                break;

            case ANTLR3_RECOGNITION_EXCEPTION: str << "Syntax error"; break;
            case ANTLR3_NO_VIABLE_ALT_EXCEPTION: str << "No viable alternative"; break;
            case ANTLR3_MISMATCHED_SET_EXCEPTION: str << "Mismatched set"; break;
            case ANTLR3_EARLY_EXIT_EXCEPTION: str << "Early exit exception"; break;
            default: str << "Syntax not recognized"; break;
            }

            if (printExpectedList) {
                for (unsigned c=0; c<expectedCount; ++c)
                    str << "\t" << tokenNames[expectedIntList[c]] << std::endl;
            }

            free(expectedIntList);

        } else {
            switch (exc->type) {
            case ANTLR3_UNWANTED_TOKEN_EXCEPTION:
                str << "Extraneous input - expected (" << (tokenNames ? tokenNames[exc->expecting] : (const ANTLR3_UINT8*)"<<unknown>>") << ")";
                break;

            case ANTLR3_MISSING_TOKEN_EXCEPTION:
                str << "Missing (" << (tokenNames ? tokenNames[exc->expecting] : (const ANTLR3_UINT8*)"<<unknown>>") << ")";
                break;

            case ANTLR3_RECOGNITION_EXCEPTION:
                str << "Syntax error";
                break;

            case ANTLR3_MISMATCHED_TOKEN_EXCEPTION:
                str << "Expected (" << (tokenNames ? tokenNames[exc->expecting] : (const ANTLR3_UINT8*)"<<unknown>>") << ")";
                break;

            case ANTLR3_NO_VIABLE_ALT_EXCEPTION: str << "No viable alternative"; break;
            case ANTLR3_MISMATCHED_SET_EXCEPTION: str << "Mismatched set"; break;
            case ANTLR3_EARLY_EXIT_EXCEPTION: str << "Early exit exception"; break;
            default: str << "Syntax not recognized"; break;
            }
        }

        // assert(recognizer->type == ANTLR3_TYPE_PARSER);
        ANTLR3_COMMON_TOKEN* token = (ANTLR3_COMMON_TOKEN*)exc->token;
		if (token) {
			auto text = AsString<CharType>(token->getText(token));
            str << ". Near token: (" << text << " at " << token->getLine(token) << ":" << token->getCharPositionInLine(token) << ")";
        }

        str << ". Msg: " << (const char*)exc->message << " (" << exc->type << ")";

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
