// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextFormatter.h"
#include "TextOutputFormatter.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/BitUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Conversion.h"
#include "../Core/Exceptions.h"
#include <assert.h>
#include <algorithm>

#pragma warning(disable:4702)		// warning C4702: unreachable code

namespace Formatters
{
    static const unsigned TabWidth = 4;
    
    template<typename CharType>
        struct FormatterConstants 
    {
        static const CharType EndLine[];
        static const CharType Tab;
        static const CharType ElementPrefix;
        
        static const CharType ProtectedNamePrefix[];
        static const CharType ProtectedNamePostfix[];

        static const CharType CommentPrefix[];
        static const CharType HeaderPrefix[];
    };
    
    template<> const utf8 FormatterConstants<utf8>::EndLine[] = { (utf8)'\r', (utf8)'\n' };
    template<> const utf8 FormatterConstants<utf8>::ProtectedNamePrefix[] = { (utf8)'<', (utf8)':', (utf8)'(' };
    template<> const utf8 FormatterConstants<utf8>::ProtectedNamePostfix[] = { (utf8)')', (utf8)':', (utf8)'>' };
    template<> const utf8 FormatterConstants<utf8>::CommentPrefix[] = { (utf8)'~', (utf8)'~' };
    template<> const utf8 FormatterConstants<utf8>::HeaderPrefix[] = { (utf8)'~', (utf8)'~', (utf8)'!' };
    template<> const utf8 FormatterConstants<utf8>::Tab = (utf8)'\t';
    template<> const utf8 FormatterConstants<utf8>::ElementPrefix = (utf8)'~';
    
    template<typename CharType, int Count> 
        static void WriteConst(OutputStream& stream, const CharType (&cnst)[Count], unsigned& lineLength)
    {
        stream.write(cnst, Count);
        lineLength += Count;
    }

    template<typename CharType, unsigned Format> 
        static bool FormattingChar(CharType c)
    {
        return c=='~' || c==';' || (Format==3?c==':':c=='=') || c=='\r' || c=='\n' || c == 0x0;
    }

    template<typename CharType> 
        static bool WhitespaceChar(CharType c)  // (excluding new line)
    {
        return c==' ' || c=='\t' || c==0x0B || c==0x0C || c==0x85 || c==0xA0 || c==0x0;
    }

    template<typename CharType> 
        static bool IsSimpleString(StringSection<CharType> str)
    {
        const unsigned format = 2;
            // if there are formatting chars anywhere in the string, it's not simple
        if (std::find_if(str.begin(), str.end(), FormattingChar<CharType, format>) != str.end()) return false;

            // If the string beings or ends with whitespace, it is also not simple.
            // This is because the parser will strip off leading and trailing whitespace.
            // (note that this test will also consider an empty string to be "not simple"
        if (str.IsEmpty()) return false;
        if (WhitespaceChar(*str.begin()) || WhitespaceChar(*(str.end()-1))) return false;
        if (*str.begin() == FormatterConstants<CharType>::ProtectedNamePrefix[0]) return false;
        return true;
    }

    auto TextOutputFormatter::BeginKeyedElement(StringSection<> name) -> ElementId
    {
        DoNewLine();

        // _hotLine = true; DoNewLine<CharType>(); // (force extra new line before new element)

            // in simple cases, we just write the name without extra formatting 
            //  (otherwise we have to write a string prefix and string postfix
        if (IsSimpleString(name)) {
            _stream->write(name.begin(), name.size());
        } else {
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
            _stream->write(name.begin(), name.size());
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
        }

        _stream->put('=');
        _stream->put(FormatterConstants<utf8>::ElementPrefix);

        _hotLine = true;
        _currentLineLength += unsigned(name.size() + 2);
        ++_currentIndentLevel;
		_indentLevelAtStartOfLine = _currentIndentLevel;

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            auto id = _nextElementId;
            _elementStack.push_back(id);
            return id;
        #else
            return 0;
        #endif
    }

    auto TextOutputFormatter::BeginSequencedElement() -> ElementId
    {
        DoNewLine();

        _stream->put('=');
        _stream->put(FormatterConstants<utf8>::ElementPrefix);

        _hotLine = true;
        _currentLineLength += 2;
        ++_currentIndentLevel;
		_indentLevelAtStartOfLine = _currentIndentLevel;

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            auto id = _nextElementId;
            _elementStack.push_back(id);
            return id;
        #else
            return 0;
        #endif
    }

    auto TextOutputFormatter::BeginElement() -> ElementId
    {
        DoNewLine();

        _stream->put(FormatterConstants<utf8>::ElementPrefix);

        _hotLine = true;
        _currentLineLength += 2;
        ++_currentIndentLevel;
		_indentLevelAtStartOfLine = _currentIndentLevel;

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            auto id = _nextElementId;
            _elementStack.push_back(id);
            return id;
        #else
            return 0;
        #endif
    }

    void TextOutputFormatter::DoNewLine()
    {
        if (_pendingHeader) {
            WriteConst(*_stream, FormatterConstants<utf8>::HeaderPrefix, _currentLineLength);
            StringMeld<128, utf8> buffer;
            buffer << "Format=2; Tab=" << TabWidth;
            _stream->write(buffer.AsStringSection().begin(), buffer.AsStringSection().size());

            _hotLine = true;
            _pendingHeader = false;
        }

        if (_hotLine) {
            WriteConst(*_stream, FormatterConstants<utf8>::EndLine, _currentLineLength);
            
            utf8 tabBuffer[64];
            if (_currentIndentLevel > dimof(tabBuffer))
                Throw(::Exceptions::BasicLabel("Excessive indent level found in OutputStreamFormatter (%i)", _currentIndentLevel));
            std::fill(tabBuffer, &tabBuffer[_currentIndentLevel], FormatterConstants<utf8>::Tab);
            _stream->write(tabBuffer, _currentIndentLevel);
            _hotLine = false;
            _currentLineLength = _currentIndentLevel * TabWidth;
        }
    }

    void TextOutputFormatter::WriteKeyedValue(
        StringSection<> name,
        StringSection<> value)
    {
        const unsigned idealLineLength = 100;
        bool forceNewLine = 
            (_currentLineLength + value.size() + name.size() + 3) > idealLineLength
            || _pendingHeader
			|| _currentIndentLevel < _indentLevelAtStartOfLine;

        if (forceNewLine) {
            DoNewLine();
        } else if (_hotLine) {
            _stream->put(';');
            _stream->put(' ');
            _currentLineLength += 2;
        }

        if (!name.IsEmpty()) {
            if (IsSimpleString(name)) {
                _stream->write(name.begin(), name.size());
            } else {
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
                _stream->write(name.begin(), name.size());
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
            }
        }

        _stream->put('=');

        if (IsSimpleString(value)) {
            _stream->write(value.begin(), value.size());
        } else {
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
            _stream->write(value.begin(), value.size());
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
        }

        _currentLineLength += unsigned(value.size() + name.size() + 1);
        _hotLine = true;
    }

    void TextOutputFormatter::WriteSequencedValue(
		StringSection<> value)
    {
        // it turns out this is identical to a "keyed" value, just with an empty name
        WriteKeyedValue({}, value);
    }

    void TextOutputFormatter::WriteValue(StringSection<> value)
    {
        const unsigned idealLineLength = 100;
        bool forceNewLine = 
            (_currentLineLength + value.size()) > idealLineLength
            || _pendingHeader
			|| _currentIndentLevel < _indentLevelAtStartOfLine;

        if (forceNewLine) {
            DoNewLine();
        } else if (_hotLine) {
            _stream->put(';');
            _stream->put(' ');
            _currentLineLength += 2;
        }

        if (IsSimpleString(value)) {
            _stream->write(value.begin(), value.size());
        } else {
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
            _stream->write(value.begin(), value.size());
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
        }

        _currentLineLength += unsigned(value.size());
        _hotLine = true;
    }

    auto TextOutputFormatter::BeginKeyedElement(StringSection<> name0, StringSection<> name1) -> ElementId
    {
        DoNewLine();

        if (!name0.IsEmpty()) {
            if (IsSimpleString(name0)) {
                _stream->write(name0.begin(), name0.size());
            } else {
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
                _stream->write(name0.begin(), name0.size());
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
            }
        }

        _stream->put('=');

        if (!name1.IsEmpty()) {
            if (IsSimpleString(name1)) {
                _stream->write(name1.begin(), name1.size());
            } else {
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
                _stream->write(name1.begin(), name1.size());
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
            }
        }

        _stream->put('=');
        _stream->put(FormatterConstants<utf8>::ElementPrefix);

        _hotLine = true;
        _currentLineLength += unsigned(name0.size() + 1 + name1.size() + 2);

        ++_currentIndentLevel;
		_indentLevelAtStartOfLine = _currentIndentLevel;

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            auto id = _nextElementId;
            _elementStack.push_back(id);
            return id;
        #else
            return 0;
        #endif
    }

    
    void TextOutputFormatter::WriteDanglingKey(StringSection<> name)
    {
        const unsigned idealLineLength = 100;
        bool forceNewLine = 
            (_currentLineLength + name.size() + 3) > idealLineLength
            || _pendingHeader
			|| _currentIndentLevel < _indentLevelAtStartOfLine;

        if (forceNewLine) {
            DoNewLine();
        } else if (_hotLine) {
            _stream->put(';');
            _stream->put(' ');
            _currentLineLength += 2;
        }

        if (!name.IsEmpty()) {
            if (IsSimpleString(name)) {
                _stream->write(name.begin(), name.size());
            } else {
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
                _stream->write(name.begin(), name.size());
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
            }
        }

        _stream->put('=');

        _currentLineLength += unsigned(name.size() + 1);
        _hotLine = false;   // not considered a "hot line" because we need to use this to get "A = B =~" type constructions
    }

    void TextOutputFormatter::EndElement(ElementId id)
    {
        if (_currentIndentLevel == 0)
            Throw(::Exceptions::BasicLabel("Unexpected EndElement in OutputStreamFormatter"));

        #if defined(STREAM_FORMATTER_CHECK_ELEMENTS)
            assert(_elementStack.size() == _currentIndentLevel);
            if (_elementStack[_elementStack.size()-1] != id)
                Throw(::Exceptions::BasicLabel("EndElement for wrong element id in OutputStreamFormatter"));
            _elementStack.erase(_elementStack.end()-1);
        #endif

        --_currentIndentLevel;
    }

    void TextOutputFormatter::NewLine()
    {
        DoNewLine();
    }

    void TextOutputFormatter::SuppressHeader() { _pendingHeader = false; }

    TextOutputFormatter::TextOutputFormatter(OutputStream& stream) 
    : _stream(&stream)
    {
        _currentIndentLevel = 0;
		_indentLevelAtStartOfLine = 0;
        _hotLine = false;
        _currentLineLength = 0;
        _pendingHeader = true;
    }

    TextOutputFormatter::~TextOutputFormatter()
    {
        assert(_currentIndentLevel == 0);
    }


    const ::Assets::DependencyValidation& FormatException::GetDependencyValidation() const { return _depVal; }
	const char* FormatException::what() const { return _msg.c_str(); }

    FormatException::FormatException(StringSection<> label, StreamLocation location)
    : _depVal(std::move(location._depVal))
    {
        std::vector<::Assets::DependentFileState> files;
        _depVal.CollateDependentFileStates(files);
        std::stringstream str;
        if (!files.empty())
            str << files[0]._filename;
        str << ":" << location._lineIndex << ":" << location._charIndex << ":" << label;
        _msg = str.str();
    }

    template<typename CharType, int Count>
        bool TryEat(TextStreamMarker<CharType>& marker, const CharType (&pattern)[Count])
    {
        if (marker.Remaining() < Count)
            return false;

        for (unsigned c=0; c<Count; ++c)
            if (marker[c] != pattern[c])
                return false;
        
        marker += Count;
        return true;
    }

    template<typename CharType, int Count>
        void Eat(TextStreamMarker<CharType>& marker, const CharType (&pattern)[Count], StreamLocation location)
    {
        if (marker.Remaining() < Count)
            Throw(FormatException("Blob prefix clipped", location));

        for (unsigned c=0; c<Count; ++c)
            if (marker[c] != pattern[c])
                Throw(FormatException("Malformed blob prefix", location));
        
        marker += Count;
    }

    template<typename CharType>
        const CharType* ReadToProtectedStringEnd(TextStreamMarker<CharType>& marker)
    {
        constexpr auto pattern = FormatterConstants<CharType>::ProtectedNamePostfix;
        constexpr auto patternLength = dimof(FormatterConstants<CharType>::ProtectedNamePostfix);

        const auto* end = marker.End() - patternLength;
        while (marker.Pointer() <= end) {
            for (unsigned c=0; c<patternLength; ++c)
                if (marker[c] != pattern[c])
                    goto advptr;

            {
                auto result = marker.Pointer();
                marker.SetPointer(marker.Pointer() + patternLength);
                return result;
            }

        advptr:
            marker.AdvanceCheckNewLine();   // we must check for newlines as we do this, otherwise line tracking will just be thrown off
        }

        Throw(FormatException("String deliminator not found", marker.GetLocation()));
        return nullptr;
    }

    template<typename CharType, unsigned Format>
        const CharType* ReadToStringEnd(
            TextStreamMarker<CharType>& marker, bool protectedStringMode)
    {
        if (protectedStringMode) {
            return ReadToProtectedStringEnd(marker);            
        } else {
                // we must read forward until we hit a formatting character
                // the end of the string will be the last non-whitespace before that formatting character
            const auto* end = marker.End();
            const auto* ptr = marker.Pointer();
            const auto* stringEnd = ptr;
            for (;;) {
                    // here, hitting EOF is the same as hitting a formatting char
                if (ptr == end || FormattingChar<CharType, Format>(*ptr)) {
                    marker.SetPointer(ptr);
                    return stringEnd;
                } else if (!WhitespaceChar(*ptr)) {
                    stringEnd = ptr+1;
                }
                ++ptr;
            }
        }
    }

    template<typename CharType>
        void EatWhitespace(TextStreamMarker<CharType>& marker)
    {
            // eat all whitespace (excluding new line)
        const auto* end = marker.End();
        const auto* ptr = marker.Pointer();
        while (ptr < end && WhitespaceChar(*ptr)) ++ptr;
        marker.SetPointer(ptr);
    }

    template<typename CharType>
        template<unsigned Format>
            auto TextInputFormatter<CharType>::PeekNext_Internal() -> Blob
    {
        if (_primed != FormatterBlob::None) return _primed;

        using Consts = FormatterConstants<CharType>;
        
        if (_pendingHeader) {
                // attempt to read file header
            if (TryEat(_marker, Consts::HeaderPrefix))
                ReadHeader();

            _pendingHeader = false;
            if (_format != Format)
                return PeekNext();
        }

        while (_marker.Remaining()) {
            const auto* next = _marker.Pointer();

            switch (unsigned(*next))
            {
            case '\t':
                ++_marker;
                _activeLineSpaces = CeilToMultiple(_activeLineSpaces+1, _tabWidth);
                break;
            case ' ': 
                ++_marker;
                ++_activeLineSpaces; 
                break;

            case 0: 
                Throw(FormatException("Unexpected null character", GetLocation()));

                // throw exception when using an extended unicode whitespace character
                // let's just stick to the ascii whitespace characters for simplicity
            case 0x0B:  // (line tabulation)
            case 0x0C:  // (form feed)
            case 0x85:  // (next line)
            case 0xA0:  // (no-break space)
                Throw(FormatException("Unsupported white space character", GetLocation()));

            case '\r':  // (could be an independent new line, or /r/n combo)
            case '\n':  // (independent new line. A following /r will be treated as another new line)
                _marker.AdvanceCheckNewLine();
                _activeLineSpaces = 0;
                _elementExtendedBySemicolon = false;
                break;

            case ';':
                    // deliminator is ignored here
                ++_marker;
                _elementExtendedBySemicolon = true;
                break;

            case (Format==3?':':'='):
                if (!_elementExtendedBySemicolon && _activeLineSpaces <= _parentBaseLine) {
                    _protectedStringMode = false;
                    return _primed = FormatterBlob::EndElement;
                }

                ++_marker;
                EatWhitespace<CharType>(_marker);

                // This is a sequence item. In other words, it's just the value part of a key/value pair
                // It functions like an element in an array
                // It can be either a value or an element. But an element will always be marked with
                // a '~'
                //
                // This construction is effectively 2 tokens:
                //    "+" and then either "~" or some value
                // Even still, we don't accept a newline between the 2 tokens. That would lead to extra
                // complications (such as, what happens if the indentation increases or decreases at that
                // point). Also, because there can't be any newlines, we also cannot support any comments
                // in this space
                //
                // So, we just call EatWhitespace (which jumps over any non-new-line whitespace) and expect
                // to find either a 
                if (!_marker.Remaining())
                    Throw(FormatException("Unexpected end of file in the middle of mapping pair", GetLocation()));

                if (*_marker == '\r' || *_marker == '\n')
                    Throw(FormatException("The value for a key/pair mapping pair must follow immediate after the separator. New lines can not appear here", GetLocation()));

                if (TryEat(_marker, Consts::CommentPrefix))
                    Throw(FormatException("The value for a key/pair mapping pair must follow immediate after the separator. Comments can not appear here", GetLocation()));

                if (*_marker == '~') {
                    _protectedStringMode = false;
                    ++_marker;
                    return _primed = FormatterBlob::BeginElement;
                } else {
                    _protectedStringMode = TryEat(_marker, Consts::ProtectedNamePrefix);
                    return _primed = FormatterBlob::Value;
                }

            case '~':
                if (TryEat(_marker, Consts::CommentPrefix)) {
                        // this is a comment... Read forward until the end of the line
                    _marker += 2;
                    {
                        const auto* end = _marker.End();
                        const auto* ptr = _marker.Pointer();
                        while (ptr < end && *ptr!='\r' && *ptr!='\n') ++ptr;
                        _marker.SetPointer(ptr);
                    }
                    break;
                }

                // else, this is a new element
                _protectedStringMode = false;
                if (_activeLineSpaces <= _parentBaseLine) {
                    return _primed = FormatterBlob::EndElement;
                }

                ++_marker;
                return _primed = FormatterBlob::BeginElement;

            default:
                // first, if our spacing has decreased, then we must consider it an "end element"
                // caller must follow with "TryEndElement" until _expectedLineSpaces matches _activeLineSpaces
                if (!_elementExtendedBySemicolon && _activeLineSpaces <= _parentBaseLine) {
                    _protectedStringMode = false;
                    if (_baseLineStackPtr == _terminatingBaseLineStackPtr)
                        return _primed = FormatterBlob::None;       // ending early because the baseline was set inside of the stream via ResetBaseLine
                    return _primed = FormatterBlob::EndElement;
                }

                    // now, _activeLineSpaces must be larger than _parentBaseLine. Anything that is 
                    // more indented than it's parent will become it's child
                    // let's see if there's a fully formed blob here
                _protectedStringMode = TryEat(_marker, Consts::ProtectedNamePrefix);

                // Unfortunately we have to roll forward a bit to see if there's a '=' after the
                // next token
                auto readForwardMarker = _marker;
                ReadToStringEnd<CharType, Format>(readForwardMarker, _protectedStringMode);
                EatWhitespace<CharType>(readForwardMarker);

                if (readForwardMarker.Remaining() && *readForwardMarker == (Format==3?':':'=')) {
                    return _primed = FormatterBlob::KeyedItem;
                } else {
                    return _primed = FormatterBlob::Value;
                }
            }
        }

            // we've reached the end of the stream...
            // while there are elements on our stack, we need to end them
        if (_baseLineStackPtr > _terminatingBaseLineStackPtr) return _primed = FormatterBlob::EndElement;
        return FormatterBlob::None;
    }

    template<typename CharType>
        StringSection<CharType> TextInputFormatter<CharType>::SkipElement()
    {
        _primed = FormatterBlob::None;
        if (_pendingHeader)
            Throw(std::runtime_error("Pending header must be processed before calling SkipElement()"));

        if (_protectedStringMode)
            Throw(std::runtime_error("Pending string must be processed before calling SkipElement()"));

        using Consts = FormatterConstants<CharType>;
        bool atLeastOneNewLine = false;
        auto start = _marker.Pointer();

        // note that there are fewer exceptions thrown by invalid characters in this path
        while (_marker.Remaining()) {
            switch (unsigned(*_marker.Pointer()))
            {
            case '\t':
                ++_marker;
                _activeLineSpaces = CeilToMultiple(_activeLineSpaces+1, _tabWidth);
                break;
            case ' ': 
                ++_marker;
                ++_activeLineSpaces; 
                break;

            case '\r':  // (could be an independent new line, or /r/n combo)
            case '\n':  // (independent new line. A following /r will be treated as another new line)
                _marker.AdvanceCheckNewLine();
                _activeLineSpaces = 0;
                _elementExtendedBySemicolon = false;
                atLeastOneNewLine = true;
                break;

            case ';':
                _elementExtendedBySemicolon = true;

                // intentional fall-through
            default:
                if (!_elementExtendedBySemicolon && _activeLineSpaces <= _parentBaseLine)
                    return { start, _marker.Pointer() };

                for (;;) {
                    if (TryEat(_marker, Consts::ProtectedNamePrefix))
                        ReadToProtectedStringEnd<CharType>(_marker);
                    else
                        ++_marker;

                    if (!_marker.Remaining() || *_marker.Pointer() == '\r' || *_marker.Pointer() == '\n' || *_marker.Pointer() == ';') break;
                }
                break;
            }
        }

        return { start, _marker.Pointer() };
    }

    template<typename CharType>
        auto TextInputFormatter<CharType>::PeekNext() -> Blob
    {
        if (_format == 3)
            return PeekNext_Internal<3>();
        else
            return PeekNext_Internal<2>();
    }

    template<typename CharType>
        void TextInputFormatter<CharType>::ReadHeader()
    {
        const CharType* aNameStart = nullptr;
        const CharType* aNameEnd = nullptr;

        while (_marker.Remaining()) {
            switch (*_marker)
            {
            case '\t':
            case ' ': 
            case ';':
                ++_marker;
                break;

            case 0x0B: case 0x0C: case 0x85: case 0xA0:
                Throw(FormatException("Unsupported white space character", GetLocation()));

            case '~':
                Throw(FormatException("Unexpected element in header", GetLocation()));

            case '\r':  // (could be an independant new line, or /r/n combo)
            case '\n':  // (independant new line. A following /r will be treated as another new line)
                return;

            case '=':
                ++_marker;
                EatWhitespace<CharType>(_marker);
                
                {
                    const auto* aValueStart = _marker.Pointer();
                    const auto* aValueEnd = ReadToStringEnd<CharType, 2>(_marker, false);

                    char convBuffer[12];
                    Conversion::Convert(convBuffer, dimof(convBuffer), aNameStart, aNameEnd);

                    if (!XlCompareStringI(convBuffer, "Format")) {
                        _format = Conversion::Convert<int>(MakeStringSection(aValueStart, aValueEnd));
                        if (_format !=2 && _format != 3)
                            Throw(FormatException("Unsupported format in input stream formatter header", GetLocation()));
                    } else if (!XlCompareStringI(convBuffer, "Tab")) {
                        _tabWidth = Conversion::Convert<unsigned>(MakeStringSection(aValueStart, aValueEnd));
                        if (_tabWidth==0)
                            Throw(FormatException("Bad tab width in input stream formatter header", GetLocation()));
                    }
                }
                break;

            default:
                aNameStart = _marker.Pointer();
                aNameEnd = ReadToStringEnd<CharType, 2>(_marker, false);
                break;
            }
        }
    }

    template<typename CharType>
        bool TextInputFormatter<CharType>::TryBeginElement()
    {
        if (PeekNext() != FormatterBlob::BeginElement) return false;

        // the new "parent base line" should be the indentation level of the line this element started on
        if ((_baseLineStackPtr+1) > dimof(_baseLineStack))
            Throw(FormatException(
                "Excessive indentation format in input stream formatter", GetLocation()));

        _baseLineStack[_baseLineStackPtr++] = _activeLineSpaces;
        _parentBaseLine = _activeLineSpaces;
        _primed = FormatterBlob::None;
        _protectedStringMode = false;
        return true;
    }

    template<typename CharType>
        bool TextInputFormatter<CharType>::TryEndElement()
    {
        if (PeekNext() != FormatterBlob::EndElement) return false;

        if (_baseLineStackPtr != _terminatingBaseLineStackPtr) {
            _parentBaseLine = (_baseLineStackPtr > 1) ? _baseLineStack[_baseLineStackPtr-2] : -1;
            --_baseLineStackPtr;
        }

        _primed = FormatterBlob::None;
        _protectedStringMode = false;
        return true;
    }

    template<typename CharType>
        bool TextInputFormatter<CharType>::TryKeyedItem(StringSection<CharType>& name)
    {
        if (PeekNext() != FormatterBlob::KeyedItem) return false;

        name._start = _marker.Pointer();
        if (_format == 3) name._end = ReadToStringEnd<CharType, 3>(_marker, _protectedStringMode);
        else name._end = ReadToStringEnd<CharType, 2>(_marker, _protectedStringMode);
        EatWhitespace<CharType>(_marker);

        _primed = FormatterBlob::None;
        _protectedStringMode = false;
        
        // After the name must come '='. Anything else is invalid in the syntax
        // "sequence items" (ie, values that don't have a key=value arrangement)
        // should begin with a "=", which will distinguish them from keyed items
        //
        // even though this makes up a series of tokens, we don't support newlines
        // before the '='. That would create complications with identation. And 
        // because we don't support newlines, we also don't support comments.
        // 
        // The same rules also apply for between the '=" and the start of the element/value

        if (!_marker.Remaining())
            Throw(FormatException("Unexpected end of file while looking for a separator to signify value for keyed item", GetLocation()));

        if (*_marker == '\r' || *_marker == '\n')
            Throw(FormatException("New lines can not appear before the separator in a mapping name/value pair", GetLocation()));

        if (TryEat(_marker, FormatterConstants<CharType>::CommentPrefix))
            Throw(FormatException("Comments can not appear before the separator in a mapping name/value pair", GetLocation()));

        if (*_marker != ((_format==3)?':':'='))
            Throw(FormatException("Missing separator to signify value for keyed item", GetLocation()));
        
        // this can be followed up with either an element (ie, new element containing within
        // itself more elements, sequences, or mapped pairs) or a value. But there must be one
        // or the other -- as so far we've only deserialized the "key" part of a key/value pair
        //
        // Note that we don't have to advance over the '=', because from this point on the 
        // deserialization is identical to what we get with a sequence value/element. PeekNext
        // should just be able to find either of those
        
        assert(PeekNext() == FormatterBlob::Value || PeekNext() == FormatterBlob::BeginElement);

        return true;
    }

    template<typename CharType>
        bool TextInputFormatter<CharType>::TryStringValue(StringSection<CharType>& value)
    {
        if (PeekNext() != FormatterBlob::Value) return false;

        value._start = _marker.Pointer();
        if (_format == 3) value._end = ReadToStringEnd<CharType, 3>(_marker, _protectedStringMode);
        else value._end = ReadToStringEnd<CharType, 2>(_marker, _protectedStringMode);
        EatWhitespace<CharType>(_marker);

        _primed = FormatterBlob::None;
        _protectedStringMode = false;

        return true;
    }

	template<typename CharType>
		bool TextInputFormatter<CharType>::TryCharacterData(StringSection<CharType>&)
	{
        // CharacterData never appears with in this format files. However it might appear in
        // XML or some other format
		return false;
	}

    template<typename CharType>
        StreamLocation TextInputFormatter<CharType>::GetLocation() const
    {
        return _marker.GetLocation();
    }

    template<typename CharType>
        TextInputFormatter<CharType> TextInputFormatter<CharType>::CreateChildFormatter()
    {
        TextInputFormatter<CharType> result = *this;
        // reset the baseline to be where we are now
        // _parentBaseLine must stay unchanged, because this still represents the indentation
        // for the end of this element
        result._terminatingBaseLineStackPtr = result._baseLineStackPtr;
        return result;    
    }

    template<typename CharType>
        TextInputFormatter<CharType>::TextInputFormatter(const TextStreamMarker<CharType>& marker) 
        : _marker(marker)
    {
        _primed = FormatterBlob::None;
        _activeLineSpaces = 0;
        _parentBaseLine = -1;
        _terminatingBaseLineStackPtr = _baseLineStackPtr = 0;
        _format = 2;
        _tabWidth = TabWidth;
        _pendingHeader = true;
        _protectedStringMode = false;
        _elementExtendedBySemicolon = false;
    }

    template<typename CharType>
        TextInputFormatter<CharType>::~TextInputFormatter()
    {}

	template<typename CharType>
		TextInputFormatter<CharType>::TextInputFormatter()
	{
		_primed = FormatterBlob::None;
		_activeLineSpaces = _parentBaseLine = 0;

		for (signed& s:_baseLineStack) s = 0;
		_terminatingBaseLineStackPtr = _baseLineStackPtr = 0u;

		_format = 2;
        _tabWidth = 0u;
		_pendingHeader = false;
        _protectedStringMode = false;
        _elementExtendedBySemicolon = false;
	}

	template<typename CharType>
		TextInputFormatter<CharType>::TextInputFormatter(const TextInputFormatter& cloneFrom)
	: _marker(cloneFrom._marker)
	, _primed(cloneFrom._primed)
	, _activeLineSpaces(cloneFrom._activeLineSpaces)
	, _parentBaseLine(cloneFrom._parentBaseLine)
	, _baseLineStackPtr(cloneFrom._baseLineStackPtr)
    , _terminatingBaseLineStackPtr(cloneFrom._terminatingBaseLineStackPtr)
	, _format(cloneFrom._format)
	, _tabWidth(cloneFrom._tabWidth)
	, _pendingHeader(cloneFrom._pendingHeader)
    , _protectedStringMode(cloneFrom._protectedStringMode)
    , _elementExtendedBySemicolon(cloneFrom._elementExtendedBySemicolon)
	{
		for (unsigned c=0; c<dimof(_baseLineStack); ++c)
			_baseLineStack[c] = cloneFrom._baseLineStack[c];
	}

	template<typename CharType>
		TextInputFormatter<CharType>& TextInputFormatter<CharType>::operator=(const TextInputFormatter& cloneFrom)
	{
		_marker = cloneFrom._marker;
		_primed = cloneFrom._primed;
		_activeLineSpaces = cloneFrom._activeLineSpaces;
		_parentBaseLine = cloneFrom._parentBaseLine;
		_baseLineStackPtr = cloneFrom._baseLineStackPtr;
        _terminatingBaseLineStackPtr = cloneFrom._terminatingBaseLineStackPtr;
		for (unsigned c=0; c<dimof(_baseLineStack); ++c)
			_baseLineStack[c] = cloneFrom._baseLineStack[c];
		_format = cloneFrom._format;
		_tabWidth = cloneFrom._tabWidth;
		_pendingHeader = cloneFrom._pendingHeader;
        _protectedStringMode = cloneFrom._protectedStringMode;
        _elementExtendedBySemicolon = cloneFrom._elementExtendedBySemicolon;
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename CharType>
        StreamLocation TextStreamMarker<CharType>::GetLocation() const
    {
        StreamLocation result;
        result._charIndex = 1 + unsigned(_ptr - _lineStart);
        result._lineIndex = 1 + _lineIndex;
        result._depVal = _depVal;
        return result;
    }

    template<typename CharType>
        inline void TextStreamMarker<CharType>::AdvanceCheckNewLine()
    {
        assert(Remaining() >= 1);

            // as per xml spec, 0xd0xa, 0xa or 0xd are all considered single new lines
        if (*_ptr == 0xd || *_ptr == 0xa) {
            if (Remaining()>=2 && *_ptr == 0xd && *(_ptr+1)==0xa) ++_ptr;
            _lineStart = _ptr+1;
            ++_lineIndex;
        }
                    
        ++_ptr;
    }

    template<typename CharType>
        TextStreamMarker<CharType>::TextStreamMarker(StringSection<CharType> source, ::Assets::DependencyValidation depVal)
    : _ptr(source.begin())
    , _end(source.end())
    , _depVal(std::move(depVal))
    {
        _lineIndex = 0;
        _lineStart = _ptr;
    }

    template<typename CharType>
        TextStreamMarker<CharType>::TextStreamMarker(IteratorRange<const void*> source, ::Assets::DependencyValidation depVal)
    : _ptr((const CharType*)source.begin())
    , _end((const CharType*)source.end())
    , _depVal(std::move(depVal))
    {
        assert((source.size() % sizeof(CharType)) == 0);
        _lineIndex = 0;
        _lineStart = _ptr;
    }

    template<typename CharType>
        TextStreamMarker<CharType>::TextStreamMarker()
    : _ptr(nullptr)
    , _end(nullptr)
    {
        _lineIndex = 0;
        _lineStart = nullptr;
    }

    template<typename CharType>
        TextStreamMarker<CharType>::~TextStreamMarker()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template class TextInputFormatter<utf8>;
    template class TextStreamMarker<utf8>;
}

