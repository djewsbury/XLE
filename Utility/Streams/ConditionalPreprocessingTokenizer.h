// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StreamFormatter.h"
#include "PreprocessorInterpreter.h"
#include "../StringUtils.h"
#include <string>
#include <vector>

namespace Utility
{
	/// <summary>A stream of tokens from a file that uses a C-like preprocessor for conditional statements</summary>
	/// The C preprocessor has some well understood syntax for wrapping conditions around lines of text. It's often
	/// useful to combine this with text written in some arbitrary language in order to create variations on the
	/// basic structure of the file
	///
	/// This class provides a stream of tokens from text, but it will interpret and remove C-preprocessor 
	/// statements from the text as it goes along. #if directives in the source file can mean that any given
	/// token is conditionally included in the file output, based on the particular conditions in the directive.
	///
	/// GetNextToken() itself will never return the preprocessor directive text, but it returns every other token
	/// in the source file (ie, without evaluating the #if statements)
	/// However, we can check on the  conditions under which that token is included in the preprocessed output 
	/// at any time byt calling _preprocessorContext.GetCurrentConditionString()
	///
	/// For example, imagine we had the following input:
	///
	/// <code>
	///		Token0 Token1
	///		#if SELECTOR_0 || SELECTOR_1
	///			#if SELECTOR_2
	///				Token2
	///			#endif
	///			Token3
	///		#endif
	/// </code>
	///
	/// As we call GetNextToken(), we will see sequence [Token0, Token1, Token2, Token3]
	/// But for each token, we can check for the conditions that enable that token. For Token0 & Token1, the
	/// conditions will be empty (meaning they are always enabled. For Token2, the condition will be 
	/// "SELECTOR_2 && (SELECTOR_0 || SELECTOR_1)" and for Token3 it will be "SELECTOR_0 || SELECTOR_1"
    class ConditionalProcessingTokenizer
    {
    public:
        struct Token
        {
            StringSection<> _value;
            StreamLocation _start, _end;
        };

        Token GetNextToken();
        Token PeekNextToken();

        StreamLocation GetLocation() const;
        StringSection<> Remaining() const;

        class PreprocessorParseContext
        {
        public:
            using Expression = std::string;
            struct Cond
            {
                Expression _positiveCond;
                Expression _negativeCond;
                bool _elseCondition = false;
            };
            std::vector<Cond> _conditionsStack;

            Expression GetCurrentConditionString();

            PreprocessorParseContext();
            ~PreprocessorParseContext();
        };
        PreprocessorParseContext _preprocessorContext;

        ConditionalProcessingTokenizer(
            StringSection<> input,
            StringSection<> filenameForRelativeIncludeSearch = {},
            IPreprocessorIncludeHandler* includeHandler = nullptr);
        ConditionalProcessingTokenizer(
            IPreprocessorIncludeHandler::Result&& initialFile,
            IPreprocessorIncludeHandler* includeHandler = nullptr);
        ~ConditionalProcessingTokenizer();

    private:
        struct FileState;
        std::vector<FileState> _fileStates;
        bool                _preprocValid = true;
        IPreprocessorIncludeHandler* _includeHandler = nullptr;

        void SkipWhitespace();
        Token ReadUntilEndOfLine();

        void ParsePreprocessorDirective();
        void PreProc_SkipWhitespace();
        auto PreProc_GetNextToken() -> Token;
    };
}

