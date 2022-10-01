// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ConditionalPreprocessingTokenizer.h"
#include "PathUtils.h"
#include "../StringFormat.h"
#include <iostream>
#include <set>

namespace Utility
{
	static bool IsIdentifierOrNumericChar(char c)
    {
        return  (c >= 'a' && c <= 'z')
            ||  (c >= 'A' && c <= 'Z')
            ||  c == '_'
            ||  (c >= '0' && c <= '9')
            ;
    }

    struct ParserHelper
    {
        StringSection<>     _input;
        unsigned            _lineIndex;
        const void*         _lineStart;

        void SkipUntilNextPreproc()
        {
            bool preprocValid = true;
            // Note -- not supporting block comments or line extensions
            while (_input.begin() != _input.end()) {
                switch (*_input.begin()) {
                case '/':
                    if ((_input.begin()+1) != _input.end() && *(_input.begin()+1) == '/') {
                        // read upto, but don't swallow the next newline (line extension not supported for these comments)
                        _input._start += 2;
                        ReadUntilEndOfLine();
                        break;
                    } else {
                        ++_input._start;
                        break;
                    }

                case '\r':  // (could be an independant new line, or /r/n combo)
                    ++_input._start;
                    if (_input.begin() != _input.end() &&  *_input.begin() == '\n')
                        ++_input._start;
                    ++_lineIndex; _lineStart = _input.begin();
                    preprocValid = true;
                    break;

                case '\n':  // (independant new line. A following /r will be treated as another new line)
                    ++_input._start;
                    ++_lineIndex; _lineStart = _input.begin();
                    preprocValid = true;
                    break;

                case '#':
                    if (preprocValid) {
                        ++_input._start;
                        return;
                    }
                    // else intentional fall-through
                default:
                    if (*_input.begin() != '\t' && *_input.begin() != ' ')
                        preprocValid = false;
                    ++_input._start;
                    break;
                }
            }
        }

        using Token = ConditionalProcessingTokenizer::Token;

        StreamLocation GetLocation() const
        {
            StreamLocation result;
            result._charIndex = 1 + unsigned(size_t(_input.begin()) - size_t(_lineStart));
            result._lineIndex = 1 + _lineIndex;
            return result;
        }

        auto ReadUntilEndOfLine() -> Token
        {
            auto startLocation = GetLocation();
            auto startOfToken = _input.begin();

            while (_input.begin() != _input.end() && *_input.begin() != '\r' && *_input.begin() != '\n') {
                // Also terminate if we hit a "//" style comment
                if (*_input._start == '/' && (_input.begin()+1) != _input.end() && *(_input.begin()+1) == '/')
                    break;
                ++_input._start;
            }

            return Token { { startOfToken, _input.begin() }, startLocation, GetLocation() };
        }

        void PreProc_SkipWhitespace()
        {
            // Note -- not supporting block comments or line extensions
            while (_input.begin() != _input.end()) {
                switch (*_input.begin()) {
                case '/':
                    if ((_input.begin()+1) != _input.end() && *(_input.begin()+1) == '/') {
                        // read upto, but don't swallow the next newline (line extension not supported for these comments)s
                        _input._start += 2;
                        ReadUntilEndOfLine();
                    }
                    return;

                case ' ':
                case '\t':
                    ++_input._start;
                    continue;       // back to top of the loop

                default:
                    // something other than whitespace; time to get out
                    // (we also get here on a newline char)
                    return;
                }
            }
        }

        auto PreProc_GetNextToken() -> Token
        {
            // we need a special implementation for this for reading the preprocessor tokens themselves,
            // because the rules are slightly different (for example, we don't want to start parsing
            // preprocessor tokens recursively)
            PreProc_SkipWhitespace();

            auto startLocation = GetLocation();

            auto startOfToken = _input.begin();
            if (_input.begin() != _input.end()) {
                if (IsIdentifierOrNumericChar(*_input.begin())) {
                    while ((_input.begin() != _input.end()) && IsIdentifierOrNumericChar(*_input.begin()))
                        ++_input._start;
                } else {
                    if (*_input.begin() == '\r' || *_input.begin() == '\n')
                        return {};      // hit newline, no more tokens
                    ++_input._start; // non identifier tokens are always single chars (ie, for some kind of operator)
                }
            }

            return Token { { startOfToken, _input.begin() }, startLocation, GetLocation() };
        }

        auto PreProc_GetBrackettedToken() -> Token
        {
            // we need a special implementation for this for reading the preprocessor tokens themselves,
            // because the rules are slightly different (for example, we don't want to start parsing
            // preprocessor tokens recursively)
            PreProc_SkipWhitespace();

            auto startLocation = GetLocation();
            if (_input.begin() == _input.end())
                return Token { { _input.begin(), _input.begin() }, startLocation, GetLocation() };

            char expectingTerminator = '"';
            if (*_input.begin() == '"') {
                ++_input._start;
            } else if (*_input.begin() == '<') {
                expectingTerminator = '>';
                ++_input._start;
            } else {
                return Token { { _input.begin(), _input.begin() }, startLocation, GetLocation() };
            }

            auto startOfToken = _input.begin();
            for (;;) {
                if (_input.begin() == _input.end() || *_input.begin() == '\r' || *_input.begin() == '\n' || *_input.begin() == expectingTerminator)
                    break;
                ++_input._start;
            }

            auto endOfToken = _input.begin();
            if (_input.begin() != _input.end() && *_input.begin() == expectingTerminator)
                ++_input._start;

            return Token { { startOfToken, endOfToken }, startLocation, GetLocation() };
        }
    };

    struct ConditionalProcessingTokenizer::FileState
    {
        ParserHelper _helper;
        std::unique_ptr<uint8_t[]> _savedBlock;
        std::string _filenameForRelativeIncludeSearch;
        uint64_t _filenameHash;
    };

    void ConditionalProcessingTokenizer::SkipWhitespace()
    {
        assert(!_fileStates.empty());

        // Note -- not supporting block comments or line extensions
        while ((_fileStates.end()-1)->_helper._input.begin() != (_fileStates.end()-1)->_helper._input.end()) {
            auto& state = *(_fileStates.end()-1);

            switch (*state._helper._input.begin()) {
            case '/':
                if ((state._helper._input.begin()+1) != state._helper._input.end() && *(state._helper._input.begin()+1) == '/') {
                    // read upto, but don't swallow the next newline (line extension not supported for these comments)
                    state._helper._input._start += 2;
                    ReadUntilEndOfLine();
                    break;
                } else
                    return;

            case ' ':
            case '\t':
                ++state._helper._input._start;
                break;

            case '\r':  // (could be an independant new line, or /r/n combo)
                ++state._helper._input._start;
                if (state._helper._input.begin() != state._helper._input.end() &&  *state._helper._input.begin() == '\n')
                    ++state._helper._input._start;
                ++state._helper._lineIndex; state._helper._lineStart = state._helper._input.begin();
                _preprocValid = true;
                break;

            case '\n':  // (independant new line. A following /r will be treated as another new line)
                ++state._helper._input._start;
                ++state._helper._lineIndex; state._helper._lineStart = state._helper._input.begin();
                _preprocValid = true;
                break;

            case '#':
                if (!_preprocValid)
                    return;

                ++state._helper._input._start;
                ParsePreprocessorDirective();
                break;

            default:
                // something other than whitespace; time to get out
                // (we also get here on a newline char)
                return;
            }
        }

        // reached end of file. Might just need to pop off next thing in the stack
        // (though we never pop off the last entry in the stack)
        if (_fileStates.size() > 1) {
            _fileStates.erase(_fileStates.end()-1);
            // On the new top of the stack, we must be just after a #include statement. So we must read
            // until the end of the line to skip over anything that trails in the #include
            ReadUntilEndOfLine();
            SkipWhitespace();
        }
    }

    auto ConditionalProcessingTokenizer::GetNextToken() -> Token
    {
        SkipWhitespace();

        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        auto startLocation = GetLocation();

        auto startOfToken = state._helper._input.begin();
        if (state._helper._input.begin() != state._helper._input.end()) {
            if (IsIdentifierOrNumericChar(*state._helper._input.begin())) {
                while ((state._helper._input.begin() != state._helper._input.end()) && IsIdentifierOrNumericChar(*state._helper._input.begin()))
                    ++state._helper._input._start;
            } else {
                ++state._helper._input._start; // non identifier tokens are always single chars (ie, for some kind of operator)
            }
            _preprocValid = false;      // can't start a preprocessor directive if there is any non-whitespace content prior on the same line
        }

        return Token { { startOfToken, state._helper._input.begin() }, startLocation, GetLocation() };
    }

    auto ConditionalProcessingTokenizer::PeekNextToken() -> Token
    {
        // We do the destructive "SkipWhitespace" -- including parsing
        // any preprocessor directives before the next token, etc. This is a little more
        // efficient because PeekNextToken is usually followed by a GetNextToken, so we avoid having
        // to do this twice.
        SkipWhitespace();

        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        auto startLocation = GetLocation();

        auto startOfToken = state._helper._input.begin();
        auto iterator = startOfToken;
        if (iterator != state._helper._input.end()) {
            if (IsIdentifierOrNumericChar(*iterator)) {
                while ((iterator != state._helper._input.end()) && IsIdentifierOrNumericChar(*iterator))
                    ++iterator;
            } else {
                ++iterator; // non identifier tokens are always single chars (ie, for some kind of operator)
            }
        }

        auto endLocation = startLocation;
        endLocation._charIndex += unsigned(iterator-startOfToken);
        return Token { { startOfToken, iterator }, startLocation, endLocation };
    }

    void ConditionalProcessingTokenizer::PreProc_SkipWhitespace()
    {
        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        state._helper.PreProc_SkipWhitespace();
    }

    auto ConditionalProcessingTokenizer::PreProc_GetNextToken() -> Token
    {
        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        return state._helper.PreProc_GetNextToken();
    }

    void ConditionalProcessingTokenizer::ParsePreprocessorDirective()
    {
        //
        // There are only a few options here, after the hash:
        //      1) define, undef, include, line, error, pragma
        //      2) if, ifdef, ifndef
        //      3) elif, else, endif
        //
        //  (not case sensitive)
        //

        auto directive = PreProc_GetNextToken();
        if (    XlEqStringI(directive._value, "define") || XlEqStringI(directive._value, "undef")
            ||  XlEqStringI(directive._value, "line") || XlEqStringI(directive._value, "error") || XlEqStringI(directive._value, "pragma")) {

            Throw(FormatException(StringMeld<256>() << "Unexpected preprocessor directive: " << directive._value << ". This directive is not supported.", directive._start));

        } else if (XlEqStringI(directive._value, "if")) {

            // the rest of the line should be some preprocessor condition expression
            PreProc_SkipWhitespace();
            auto remainingLine = ReadUntilEndOfLine();
            _preprocessorContext._conditionsStack.push_back({remainingLine._value.AsString()});

        } else if (XlEqStringI(directive._value, "ifdef")) {

            auto symbol = PreProc_GetNextToken();
            if (symbol._value.IsEmpty())
                Throw(FormatException("Expected token in #ifdef", directive._start));

            _preprocessorContext._conditionsStack.push_back({"defined(" + symbol._value.AsString() + ")"});

            ReadUntilEndOfLine();       // skip rest of line

        } else if (XlEqStringI(directive._value, "ifndef")) {

            auto symbol = PreProc_GetNextToken();
            if (symbol._value.IsEmpty())
                Throw(FormatException("Expected token in #ifndef", directive._start));

            _preprocessorContext._conditionsStack.push_back({"!defined(" + symbol._value.AsString() + ")"});

            ReadUntilEndOfLine();       // skip rest of line

        } else if (XlEqStringI(directive._value, "elif") || XlEqStringI(directive._value, "else") || XlEqStringI(directive._value, "endif")) {

            if (_preprocessorContext._conditionsStack.empty())
                Throw(FormatException(
                    StringMeld<256>() << "endif/else/elif when there has been no if",
                    directive._start));

            auto prevCondition = *(_preprocessorContext._conditionsStack.end()-1);
            _preprocessorContext._conditionsStack.erase(_preprocessorContext._conditionsStack.end()-1);

            auto negCondition = prevCondition._positiveCond;
            if (!prevCondition._negativeCond.empty())
                negCondition = "(" + negCondition + ") || (" + prevCondition._negativeCond +  ")";

            if (XlEqStringI(directive._value, "elif")) {
                auto remainingLine = ReadUntilEndOfLine();
                _preprocessorContext._conditionsStack.push_back({remainingLine._value.AsString(), negCondition});
            } else if (XlEqStringI(directive._value, "else")) {
                _preprocessorContext._conditionsStack.push_back({"1", negCondition});

                ReadUntilEndOfLine();       // skip rest of line
            }

        } else if (XlEqStringI(directive._value, "include")) {

            assert(!_fileStates.empty());
            auto& state = *(_fileStates.end()-1);
            auto symbol = state._helper.PreProc_GetBrackettedToken();
            if (symbol._value.IsEmpty())
                Throw(FormatException("Expected file to include after #include directive", directive._start));

            if (!_includeHandler)
                Throw(FormatException("No include handler provided to handle #include directive", directive._start));

            auto newFile = _includeHandler->OpenFile(symbol._value, state._filenameForRelativeIncludeSearch);
            if (!newFile._fileContents || !newFile._fileContentsSize)
                Throw(FormatException(("Could not open included file (or empty file): " + symbol._value.AsString()).c_str(), directive._start));

            auto hashedName = HashFilenameAndPath(MakeStringSection(newFile._filename));
            auto existing = std::find_if(_fileStates.begin(), _fileStates.end(),
                [hashedName](const auto& c) { return c._filenameHash == hashedName; });
            if (existing == _fileStates.end()) {

                FileState newState;
                newState._helper._input = MakeStringSection((const char*)newFile._fileContents.get(), (const char*)PtrAdd(newFile._fileContents.get(), newFile._fileContentsSize));
                newState._helper._lineIndex = 0;
                newState._helper._lineStart = newState._helper._input.begin();
                newState._filenameHash = hashedName;
                newState._filenameForRelativeIncludeSearch = newFile._filename;
                newState._savedBlock = std::move(newFile._fileContents);
                _fileStates.push_back(std::move(newState));

            } else {
                // we're skipping this because it's a circular include
                ReadUntilEndOfLine();
            }

        } else {

            Throw(FormatException(
                StringMeld<256>() << "Unknown preprocessor directive: " << directive._value,
                directive._start));

        }

    }

    auto ConditionalProcessingTokenizer::ReadUntilEndOfLine() -> Token
    {
        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        return state._helper.ReadUntilEndOfLine();
    }

    StreamLocation ConditionalProcessingTokenizer::GetLocation() const
    {
        assert(!_fileStates.empty());
        auto& state = *(_fileStates.end()-1);
        return state._helper.GetLocation();
    }

    StringSection<> ConditionalProcessingTokenizer::Remaining() const 
    {
        if (_fileStates.size() != 1)
            Throw(std::runtime_error("ConditionalProcessingTokenizer::Remaining cannot return the entire remaining string when there are files #included from other files"));
        auto& state = *(_fileStates.end()-1);
        return state._helper._input; 
    }

    ConditionalProcessingTokenizer::ConditionalProcessingTokenizer(
        StringSection<> input,
        StringSection<> filenameForRelativeIncludeSearch,
        IPreprocessorIncludeHandler* includeHandler)
    : _includeHandler(includeHandler)
    {
        FileState initialState;
        initialState._helper._input = input;
        initialState._helper._lineIndex = 0;
        initialState._helper._lineStart = input.begin();
        initialState._filenameHash = HashFilenameAndPath(filenameForRelativeIncludeSearch);
        initialState._filenameForRelativeIncludeSearch = filenameForRelativeIncludeSearch.AsString();
        _fileStates.push_back(std::move(initialState));
    }

    ConditionalProcessingTokenizer::ConditionalProcessingTokenizer(
        IPreprocessorIncludeHandler::Result&& initialFile,
        IPreprocessorIncludeHandler* includeHandler)
    : _includeHandler(includeHandler)
    {
        FileState initialState;
        initialState._helper._input = MakeStringSection((const char*)initialFile._fileContents.get(), (const char*)PtrAdd(initialFile._fileContents.get(), initialFile._fileContentsSize));
        initialState._helper._lineIndex = 0;
        initialState._helper._lineStart = (const char*)initialFile._fileContents.get();
        initialState._filenameHash = HashFilenameAndPath(MakeStringSection(initialFile._filename));
        initialState._filenameForRelativeIncludeSearch = initialFile._filename;
        initialState._savedBlock = std::move(initialFile._fileContents);
        _fileStates.push_back(std::move(initialState));
    }

    ConditionalProcessingTokenizer::~ConditionalProcessingTokenizer()
    {
    }

    auto ConditionalProcessingTokenizer::PreprocessorParseContext::GetCurrentConditionString() -> Expression
    {
        Expression result;
        for (auto i=_conditionsStack.rbegin(); i!=_conditionsStack.rend(); ++i) {
            if (!i->_negativeCond.empty()) {
                if (!result.empty()) result += " && ";
                result += "!(" + i->_negativeCond + ")";
            }
            if (!i->_positiveCond.empty()) {
                if (!result.empty()) result += " && ";
                result += "(" + i->_positiveCond + ")";
            }
        }
        return result;
    }

    ConditionalProcessingTokenizer::PreprocessorParseContext::PreprocessorParseContext() {}
    ConditionalProcessingTokenizer::PreprocessorParseContext::~PreprocessorParseContext() {}

    

    struct Cond
    {
        Internal::ExpressionTokenList _positiveCond;
        Internal::ExpressionTokenList _negativeCond;
    };

    Internal::ExpressionTokenList GetCurrentCondition(IteratorRange<const Cond*> conditionStack)
    {
        Internal::ExpressionTokenList result;
        result.push_back(1);    // start with "true"

        for (const auto&c:conditionStack) {
            // The "_negativeCond" must be false and the _positiveCond must be true
            if (!c._negativeCond.empty())
                result = Internal::AndNotExpression(result, c._negativeCond);
            if (!c._positiveCond.empty())
                result = Internal::AndExpression(result, c._positiveCond);
        }

        return result;
    }

    static bool IsNotDefinedCheck(
        const Internal::TokenDictionary& dictionary,
        IteratorRange<const Internal::Token*> expression,
        Internal::Token definedCheckToken)
    {
        if (expression.size() != 3 || expression[1] != definedCheckToken) return false;

        auto& token0 = dictionary._tokenDefinitions[expression[0]];
        auto& token2 = dictionary._tokenDefinitions[expression[2]];
        return
            token0._type == Internal::TokenDictionary::TokenType::UnaryMarker
            && token2._type == Internal::TokenDictionary::TokenType::Operation && XlEqString(token2._value, "!");
    }

    static bool IsTrue(const Internal::ExpressionTokenList& expr)
    {
        return expr.size() == 1 && expr[0] == 1;
    }

    static bool IsFalse(const Internal::ExpressionTokenList& expr)
    {
        return expr.size() == 1 && expr[0] == 0;
    }

    class PreprocessAnalysisIncludeHelper
    {
    public:
        IPreprocessorIncludeHandler* _includeHandler = nullptr;
        std::set<uint64_t> _processingFilesSet;

        PreprocessorAnalysis GeneratePreprocessorAnalysisFromFileInternal(
			StringSection<> requestString,
			StringSection<> fileIncludedFrom)
        {
            auto includeHandlerResult = _includeHandler->OpenFile(requestString, fileIncludedFrom);
            if (!includeHandlerResult._fileContents || !includeHandlerResult._fileContentsSize)
                return {};

            auto hashedName = HashFilenameAndPath(MakeStringSection(includeHandlerResult._filename));
            if (_processingFilesSet.find(hashedName) != _processingFilesSet.end())
				return {};		// circular include -- trying to include a file while we're still processing it somewhere higher on the stack
			_processingFilesSet.insert(hashedName);

            auto result = GeneratePreprocessorAnalysisFromString(
				MakeStringSection((const char*)includeHandlerResult._fileContents.get(), (const char*)PtrAdd(includeHandlerResult._fileContents.get(), includeHandlerResult._fileContentsSize)),
				includeHandlerResult._filename);

            _processingFilesSet.erase(hashedName);
            return result;
        }

        void GeneratePreprocessorAnalysisFromFile(
            PreprocessorAnalysis& result,
			StringSection<> requestString,
			StringSection<> fileIncludedFrom)
        {
            auto unmerged = GeneratePreprocessorAnalysisFromFileInternal(requestString, fileIncludedFrom);
            if (result._relevanceTable.empty() && result._sideEffects._substitutions.empty()) {
                result = unmerged;
            } else {
                MergeInAnalysis(result._tokenDictionary, result._relevanceTable, result._sideEffects, unmerged, {});
            }
        }

        void MergeInAnalysis(
            Internal::TokenDictionary& tokenDictionary,
            Internal::WorkingRelevanceTable& relevanceTable,
            Internal::PreprocessorSubstitutions& activeSubstitutions,
            const PreprocessorAnalysis& includedAnalysis,
            const Internal::ExpressionTokenList& currentCondition)
        {
            // merge in the results we got from this included file
            if (includedAnalysis._relevanceTable.empty() && includedAnalysis._sideEffects._substitutions.empty())
                return;

            std::map<Internal::Token, Internal::ExpressionTokenList> translatedRelevanceTable;
            for (const auto& relevance:includedAnalysis._relevanceTable) {
                translatedRelevanceTable.insert(std::make_pair(
                    tokenDictionary.Translate(includedAnalysis._tokenDictionary, relevance.first),
                    tokenDictionary.Translate(includedAnalysis._tokenDictionary, relevance.second)));
            }
            relevanceTable = Internal::MergeRelevanceTables(
                relevanceTable, {},
                translatedRelevanceTable, currentCondition);

            if (!includedAnalysis._sideEffects._substitutions.empty()) {
                auto currentConditionInSideEffectDictionary = activeSubstitutions._dictionary.Translate(tokenDictionary, currentCondition);
                for (const auto& sideEffect:includedAnalysis._sideEffects._substitutions) {
                    auto newSubst = sideEffect;
                    newSubst._condition = activeSubstitutions._dictionary.Translate(includedAnalysis._sideEffects._dictionary, newSubst._condition);
                    newSubst._condition = Internal::AndExpression(currentConditionInSideEffectDictionary, newSubst._condition);
                    newSubst._substitution = activeSubstitutions._dictionary.Translate(includedAnalysis._sideEffects._dictionary, newSubst._substitution);
                    activeSubstitutions._substitutions.push_back(newSubst);
                }
            }
        }
        
        PreprocessorAnalysis GeneratePreprocessorAnalysisFromString(
            StringSection<> input, 
            StringSection<> filenameForRelativeIncludeSearch)
        {
            // Walk through the input string, extracting all preprocessor operations
            // We need to consider "//" comments, but we don't support block comments or line extensions

            ParserHelper helper;
            helper._input = input;
            helper._lineIndex = 0;
            helper._lineStart = input.begin();

            Internal::TokenDictionary tokenDictionary;
            std::vector<Cond> conditionsStack;

            Internal::WorkingRelevanceTable relevanceTable;
            Internal::PreprocessorSubstitutions activeSubstitutions;
            unsigned disablePushCount = 0;

            while (true) {
                helper.SkipUntilNextPreproc();
                auto directive = helper.PreProc_GetNextToken();
                if (directive._value.IsEmpty()) break;

                if (XlEqStringI(directive._value, "if")) {

                    if (!disablePushCount) {
                        // the rest of the line should be some preprocessor condition expression
                        helper.PreProc_SkipWhitespace();
                        auto remainingLine = helper.ReadUntilEndOfLine();

                        auto expr = Internal::AsExpressionTokenList(
                            tokenDictionary, remainingLine._value, activeSubstitutions);

                        auto exprRelevance = CalculatePreprocessorExpressionRevelance(
                            tokenDictionary, expr);

                        relevanceTable = Internal::MergeRelevanceTables(
                            relevanceTable, {},
                            exprRelevance, GetCurrentCondition(conditionsStack));

                        conditionsStack.push_back({expr});
                    } else {
                        helper.ReadUntilEndOfLine();
                    }

                } else if (XlEqStringI(directive._value, "ifdef")) {

                    if (!disablePushCount) {
                        auto symbol = helper.PreProc_GetNextToken();
                        if (symbol._value.IsEmpty())
                            Throw(FormatException("Expected token in #ifdef", directive._start));

                        Internal::ExpressionTokenList expr;
                        tokenDictionary.PushBack(expr, Internal::TokenDictionary::TokenType::IsDefinedTest, symbol._value.AsString());

                        auto exprRelevance = CalculatePreprocessorExpressionRevelance(
                            tokenDictionary, expr);

                        relevanceTable = Internal::MergeRelevanceTables(
                            relevanceTable, {},
                            exprRelevance, GetCurrentCondition(conditionsStack));

                        conditionsStack.push_back({expr});
                    }
                    helper.ReadUntilEndOfLine();       // skip rest of line

                } else if (XlEqStringI(directive._value, "ifndef")) {

                    if (!disablePushCount) {
                        auto symbol = helper.PreProc_GetNextToken();
                        if (symbol._value.IsEmpty())
                            Throw(FormatException("Expected token in #ifndef", directive._start));

                        Internal::ExpressionTokenList expr;
                        tokenDictionary.PushBack(expr, Internal::TokenDictionary::TokenType::IsDefinedTest, symbol._value.AsString());
                        expr = Internal::InvertExpression(expr);

                        auto exprRelevance = CalculatePreprocessorExpressionRevelance(
                            tokenDictionary, expr);

                        relevanceTable = Internal::MergeRelevanceTables(
                            relevanceTable, {},
                            exprRelevance, GetCurrentCondition(conditionsStack));

                        conditionsStack.push_back({expr});
                    }
                    helper.ReadUntilEndOfLine();       // skip rest of line

                } else if (XlEqStringI(directive._value, "elif") || XlEqStringI(directive._value, "else") || XlEqStringI(directive._value, "endif")) {

                    if (!disablePushCount) {
                        if (conditionsStack.empty())
                            Throw(FormatException(
                                StringMeld<256>() << "endif/else/elif when there has been no if",
                                directive._start));

                        // Since we're at the same layer of nesting as the #if that we're trailing,
                        // we wil actually modify the top condition on the stack, rather than pushing
                        // and going to a deaper level
                        auto prevCondition = *(conditionsStack.end()-1);
                        conditionsStack.erase(conditionsStack.end()-1);

                        // The "_negativeCond" must be false for this section to be used
                        // If the previous negative condition is true, then the new negative condition should continue to be true
                        //      !Z = !X && !Y
                        //       -> Z = X || Y
                        auto negCondition = Internal::OrExpression(prevCondition._positiveCond, prevCondition._negativeCond);

                        if (XlEqStringI(directive._value, "elif")) {

                            auto remainingLine = helper.ReadUntilEndOfLine();

                            auto expr = Internal::AsExpressionTokenList(
                                tokenDictionary, remainingLine._value, activeSubstitutions);

                            auto exprRelevance = CalculatePreprocessorExpressionRevelance(
                                tokenDictionary, expr);

                            auto conditions = Internal::AndNotExpression(GetCurrentCondition(conditionsStack), negCondition);
                            relevanceTable = Internal::MergeRelevanceTables(
                                relevanceTable, {},
                                exprRelevance, conditions);

                            conditionsStack.push_back({expr, negCondition});
                        } else if (XlEqStringI(directive._value, "else")) {
                            conditionsStack.push_back({{}, negCondition});
                            helper.ReadUntilEndOfLine();       // skip rest of line
                        }
                    } else {
                        helper.ReadUntilEndOfLine();
                    }

                } else if (XlEqStringI(directive._value, "define") || XlEqStringI(directive._value, "undef")) {

                    if (!disablePushCount) {
                        auto symbol = helper.PreProc_GetNextToken();
                        if (symbol._value.IsEmpty())
                            Throw(FormatException("Expected token in #define", directive._start));

                        auto remainingLine = helper.ReadUntilEndOfLine();
                        bool foundNonWhitespaceChar = false;
                        for (auto c:remainingLine._value)
                            if (c!=' ' && c!='\t') {
                                foundNonWhitespaceChar = true;
                                break;
                            }

                        auto definedCheck = tokenDictionary.TryGetToken(Internal::TokenDictionary::TokenType::IsDefinedTest, symbol._value);

                        bool gotNotDefinedCheck = false;
                        for (auto condition=conditionsStack.rbegin(); condition!=conditionsStack.rend(); ++condition) {
                            if (!condition->_negativeCond.empty() && !IsFalse(condition->_negativeCond)) {
                                break;
                            }
                            // We're looking for "just true" or "just !defined(X)"
                            if (!IsTrue(condition->_positiveCond)) {
                                if (definedCheck.has_value() && IsNotDefinedCheck(tokenDictionary, condition->_positiveCond, definedCheck.value())) {
                                    // Note that "gotNotDefinedCheck" will not be set to true if there are any non-true condition closer to the top
                                    // of the stack
                                    gotNotDefinedCheck = true;
                                } else {
                                    break;
                                }
                            }
                        }

                        // If we're defining something with no value, and there is a !defined() check for the same
                        // variable on the conditions stack, and there are no conditionals later on the stack than
                        // that check, then let's assume this is a header guard type pattern, and just wipe out
                        // the condition on the stack
                        // The header guard check needs to trigger even if there are conditions closer to the bottom
                        // of the stack to handle strings that have already had #includes expanded. Ie, if we see
                        // something like "#if <something>\n #include <something>\n #endif" then we might end up with
                        // a header guard inside of some conditional check
                        auto headerGuardDetection = !foundNonWhitespaceChar && XlEqStringI(directive._value, "define") && gotNotDefinedCheck;
                        if (!headerGuardDetection) {

                            Internal::PreprocessorSubstitutions::ConditionalSubstitutions subst;
                            subst._symbol = symbol._value.AsString();
                            subst._type = XlEqStringI(directive._value, "define") ? Internal::PreprocessorSubstitutions::Type::Define : Internal::PreprocessorSubstitutions::Type::Undefine;
                            subst._condition = activeSubstitutions._dictionary.Translate(tokenDictionary, GetCurrentCondition(conditionsStack));
                            bool expressionSubstitution = true;

                            if (foundNonWhitespaceChar) {
                                try {
                                    auto maybeTokenList = Internal::TryAsExpressionTokenList(activeSubstitutions._dictionary, remainingLine._value, activeSubstitutions);
                                    if (maybeTokenList.has_value()) {
                                        subst._substitution = std::move(maybeTokenList.value());
                                        expressionSubstitution = true;
                                    } else {
                                        expressionSubstitution = false;
                                    }
                                } catch (const std::exception&) {
                                    expressionSubstitution = false;
                                }
                            }

                            if (!expressionSubstitution) {
                                #if defined(_DEBUG)
                                    std::cout << "Substitution for " << symbol._value << " is not an expression" << std::endl;
                                #endif
                            }

                            if (expressionSubstitution) {
                                #if defined(_DEBUG)
                                    if (XlEqStringI(directive._value, "define") && XlEndsWith(symbol._value, MakeStringSection("_H"))) {
                                        std::cout << "Suspicious define for variable " << symbol._value << " found" << std::endl;
                                    }
                                #endif

                                if (!gotNotDefinedCheck) {
                                    activeSubstitutions._substitutions.push_back(subst);
                                } else {
                                    if (subst._type == Internal::PreprocessorSubstitutions::Type::Define) {
                                        subst._type = Internal::PreprocessorSubstitutions::Type::DefaultDefine;
                                        activeSubstitutions._substitutions.push_back(subst);
                                    } else {
                                        // We should only get here for something like "#ifdef SYMBOL\n #undef SYMBOL" -- which doesn't make much sense
                                        #if defined(_DEBUG)
                                            std::cout << "Found unusual #undef construction for " << symbol._value << ". Ignoring." << std::endl;
                                        #endif
                                    }
                                }
                            }

                        } else {
                            auto i = relevanceTable.find(definedCheck.value());
                            if (i != relevanceTable.end())
                                relevanceTable.erase(i);

                            // Clear out the !defined() checks for this header guard, because we won't want them to appear in the
                            // relevance conditions
                            for (auto& condition:conditionsStack)
                                if (condition._negativeCond.empty() && IsNotDefinedCheck(tokenDictionary, condition._positiveCond, definedCheck.value()))
                                    condition._positiveCond = {1};
                        }
                    } else {
                        helper.ReadUntilEndOfLine();
                    }

                } else if (XlEqStringI(directive._value, "include")) {

                    if (!disablePushCount) {
                        auto symbol = helper.PreProc_GetBrackettedToken();
                        if (symbol._value.IsEmpty())
                            Throw(FormatException("Expected file to include after #include directive", directive._start));

                        if (!_includeHandler)
                            Throw(FormatException("No include handler provided to handle #include directive", directive._start));

                        // todo -- do we need any #pragma once type functionality to prevent infinite recursion
                        // or just searching through too many files
                        auto currentCondition = GetCurrentCondition(conditionsStack);
                        auto includedAnalysis = GeneratePreprocessorAnalysisFromFileInternal(symbol._value, filenameForRelativeIncludeSearch);
                        MergeInAnalysis(tokenDictionary, relevanceTable, activeSubstitutions, includedAnalysis, currentCondition);
                        
                    } else {
                        helper.ReadUntilEndOfLine();
                    }

                } else if (XlEqStringI(directive._value, "pragma")) {

                    auto pragmaType = helper.PreProc_GetNextToken();
                    if (XlEqString(pragmaType._value, "selector_filtering")) {

                        auto token = helper.PreProc_GetNextToken();
                        if (!XlEqString(token._value, "(")) Throw(FormatException("Expecting #pragma selector_filtering(<cmd>)", directive._start));
                        token = helper.PreProc_GetNextToken();
                        if (XlEqString(token._value, "push_disable")) {
                            ++disablePushCount;
                        } else if (XlEqString(token._value, "pop")) {
                            if (disablePushCount == 0)
                                Throw(FormatException("Unbalanced pushes/pops for selector_filtering pragma", directive._start));
                            --disablePushCount;
                        } else {
                            Throw(FormatException("Expecting either push_disable or pop", directive._start));
                        }
                        token = helper.PreProc_GetNextToken();
                        if (!XlEqString(token._value, ")")) Throw(FormatException("Expecting #pragma selector_filtering(<cmd>)", directive._start));
                        helper.ReadUntilEndOfLine();

                    } else {
                        helper.ReadUntilEndOfLine();
                    }

                } else if (XlEqStringI(directive._value, "line") || XlEqStringI(directive._value, "error") || XlEqStringI(directive._value, "extension")) {

                    // These don't have any effects relevant to us. We can just go ahead and skip them
                    helper.ReadUntilEndOfLine();

                } else {

                    Throw(FormatException(
                        StringMeld<256>() << "Unknown preprocessor directive: " << directive._value,
                        directive._start));

                }
            }

            if (disablePushCount != 0)
                Throw(FormatException("Unbounded selector_filtering pragma", helper.GetLocation()));

            PreprocessorAnalysis result;
            result._tokenDictionary = std::move(tokenDictionary);
            result._relevanceTable = std::move(relevanceTable);
            result._sideEffects = std::move(activeSubstitutions);
            return result;
        }
    };

    PreprocessorAnalysis GeneratePreprocessorAnalysisFromString(
		StringSection<> input,
		StringSection<> filenameForRelativeIncludeSearch,
		IPreprocessorIncludeHandler* includeHandler)
    {
        PreprocessAnalysisIncludeHelper helper { includeHandler };
        return helper.GeneratePreprocessorAnalysisFromString(input, filenameForRelativeIncludeSearch);
    }

	PreprocessorAnalysis GeneratePreprocessorAnalysisFromFile(
		StringSection<> inputFilename,
		IPreprocessorIncludeHandler* includeHandler)
    {
        PreprocessAnalysisIncludeHelper helper { includeHandler };
        PreprocessorAnalysis result;
        helper.GeneratePreprocessorAnalysisFromFile(result, inputFilename, {});
        return result;
    }

    PreprocessorAnalysis GeneratePreprocessorAnalysisFromFile(
		StringSection<> inputFilename0,
        StringSection<> inputFilename1,
		IPreprocessorIncludeHandler* includeHandler)
    {
        PreprocessAnalysisIncludeHelper helper { includeHandler };
        PreprocessorAnalysis result;
        if (!inputFilename0.IsEmpty())
            helper.GeneratePreprocessorAnalysisFromFile(result, inputFilename0, {});
        if (!inputFilename1.IsEmpty())
            helper.GeneratePreprocessorAnalysisFromFile(result, inputFilename1, {});
        return result;
    }
}
