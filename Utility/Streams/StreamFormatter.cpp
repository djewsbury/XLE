// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StreamFormatter.h"
#include "OutputStreamFormatter.h"
#include "Stream.h"
#include "../BitUtils.h"
#include "../PtrUtils.h"
#include "../StringFormat.h"
#include "../Conversion.h"
#include "../../Core/Exceptions.h"
#include <assert.h>
#include <algorithm>

#pragma warning(disable:4702)		// warning C4702: unreachable code

namespace Utility
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
        stream.Write(StringSection<CharType>(cnst, &cnst[Count]));
        lineLength += Count;
    }

    template<typename CharType> 
        static bool FormattingChar(CharType c)
    {
        return c=='~' || c==';' || c=='=' || c=='\r' || c=='\n' || c == 0x0;
    }

    template<typename CharType> 
        static bool WhitespaceChar(CharType c)  // (excluding new line)
    {
        return c==' ' || c=='\t' || c==0x0B || c==0x0C || c==0x85 || c==0xA0 || c==0x0;
    }

    template<typename CharType> 
        static bool IsSimpleString(StringSection<CharType> str)
    {
            // if there are formatting chars anywhere in the string, it's not simple
        if (std::find_if(str.begin(), str.end(), FormattingChar<CharType>) != str.end()) return false;

            // If the string beings or ends with whitespace, it is also not simple.
            // This is because the parser will strip off leading and trailing whitespace.
            // (note that this test will also consider an empty string to be "not simple"
        if (str.IsEmpty()) return false;
        if (WhitespaceChar(*str.begin()) || WhitespaceChar(*(str.end()-1))) return false;
        if (*str.begin() == FormatterConstants<CharType>::ProtectedNamePrefix[0]) return false;
        return true;
    }

    auto OutputStreamFormatter::BeginKeyedElement(StringSection<utf8> name) -> ElementId
    {
        DoNewLine();

        // _hotLine = true; DoNewLine<CharType>(); // (force extra new line before new element)

            // in simple cases, we just write the name without extra formatting 
            //  (otherwise we have to write a string prefix and string postfix
        if (IsSimpleString(name)) {
            _stream->Write(name);
        } else {
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
            _stream->Write(name);
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
        }

        _stream->WriteChar('=');
        _stream->WriteChar(FormatterConstants<utf8>::ElementPrefix);

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

    auto OutputStreamFormatter::BeginSequencedElement() -> ElementId
    {
        DoNewLine();

        _stream->WriteChar('=');
        _stream->WriteChar(FormatterConstants<utf8>::ElementPrefix);

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

    void OutputStreamFormatter::DoNewLine()
    {
        if (_pendingHeader) {
            WriteConst(*_stream, FormatterConstants<utf8>::HeaderPrefix, _currentLineLength);
            StringMeld<128, utf8> buffer;
            buffer << "Format=2; Tab=" << TabWidth;
            _stream->Write(buffer.get());

            _hotLine = true;
            _pendingHeader = false;
        }

        if (_hotLine) {
            WriteConst(*_stream, FormatterConstants<utf8>::EndLine, _currentLineLength);
            
            utf8 tabBuffer[64];
            if (_currentIndentLevel > dimof(tabBuffer))
                Throw(::Exceptions::BasicLabel("Excessive indent level found in OutputStreamFormatter (%i)", _currentIndentLevel));
            std::fill(tabBuffer, &tabBuffer[_currentIndentLevel], FormatterConstants<utf8>::Tab);
            _stream->Write(StringSection<utf8>(tabBuffer, &tabBuffer[_currentIndentLevel]));
            _hotLine = false;
            _currentLineLength = _currentIndentLevel * TabWidth;
        }
    }

    void OutputStreamFormatter::WriteKeyedValue(
        StringSection<utf8> name,
        StringSection<utf8> value)
    {
        const unsigned idealLineLength = 100;
        bool forceNewLine = 
            (_currentLineLength + value.size() + name.size() + 3) > idealLineLength
            || _pendingHeader
			|| _currentIndentLevel < _indentLevelAtStartOfLine;

        if (forceNewLine) {
            DoNewLine();
        } else if (_hotLine) {
            _stream->WriteChar(';');
            _stream->WriteChar(' ');
            _currentLineLength += 2;
        }

        if (!name.IsEmpty()) {
            if (IsSimpleString(name)) {
                _stream->Write(name);
            } else {
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
                _stream->Write(name);
                WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
            }
        }

        _stream->WriteChar('=');

        if (IsSimpleString(value)) {
            _stream->Write(value);
        } else {
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePrefix, _currentLineLength);
            _stream->Write(value);
            WriteConst(*_stream, FormatterConstants<utf8>::ProtectedNamePostfix, _currentLineLength);
        }

        _currentLineLength += unsigned(value.size() + name.size() + 1);
        _hotLine = true;
    }

    void OutputStreamFormatter::WriteSequencedValue(
		StringSection<utf8> value)
    {
        // it turns out this is identical to a "keyed" value, just with an empty name
        WriteKeyedValue({}, value);
    }

    void OutputStreamFormatter::EndElement(ElementId id)
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

    void OutputStreamFormatter::Flush()
    {
        DoNewLine();  // finish on a new line (just for neatness)
        assert(_currentIndentLevel == 0);
    }

    void OutputStreamFormatter::NewLine()
    {
        DoNewLine();
    }

    OutputStreamFormatter::OutputStreamFormatter(OutputStream& stream) 
    : _stream(&stream)
    {
        _currentIndentLevel = 0;
		_indentLevelAtStartOfLine = 0;
        _hotLine = false;
        _currentLineLength = 0;
        _pendingHeader = true;
    }

    OutputStreamFormatter::~OutputStreamFormatter()
    {}


    
    FormatException::FormatException(const char label[], StreamLocation location)
        : ::Exceptions::BasicLabel("Format Exception: (%s) in file (%s) at line (%i), char (%i)", label, location._filename.c_str(), location._lineIndex, location._charIndex) {}

    template<typename CharType, int Count>
        bool TryEat(TextStreamMarker<CharType>& marker, const CharType (&pattern)[Count])
    {
        if (marker.Remaining() < Count)
            return false;

        const auto* test = marker.Pointer();
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

        const auto* test = marker.Pointer();
        for (unsigned c=0; c<Count; ++c)
            if (marker[c] != pattern[c])
                Throw(FormatException("Malformed blob prefix", location));
        
        marker += Count;
    }

    template<typename CharType>
        const CharType* ReadToStringEnd(
            TextStreamMarker<CharType>& marker, bool protectedStringMode)
    {
        const auto pattern = FormatterConstants<CharType>::ProtectedNamePostfix;
        const auto patternLength = dimof(FormatterConstants<CharType>::ProtectedNamePostfix);

        if (protectedStringMode) {
            const auto* end = marker.End() - patternLength;
            const auto* ptr = marker.Pointer();
            while (ptr <= end) {
                for (unsigned c=0; c<patternLength; ++c)
                    if (ptr[c] != pattern[c])
                        goto advptr;

                marker.SetPointer(ptr + patternLength);
                return ptr;
            advptr:
                ++ptr;
            }

            Throw(FormatException("String deliminator not found", marker.GetLocation()));
            return nullptr;
        } else {
                // we must read forward until we hit a formatting character
                // the end of the string will be the last non-whitespace before that formatting character
            const auto* end = marker.End();
            const auto* ptr = marker.Pointer();
            const auto* stringEnd = ptr;
            for (;;) {
                    // here, hitting EOF is the same as hitting a formatting char
                if (ptr == end || FormattingChar(*ptr)) {
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
        auto InputStreamFormatter<CharType>::PeekNext() -> Blob
    {
        if (_primed != FormatterBlob::None) return _primed;

        using Consts = FormatterConstants<CharType>;
        
        if (_pendingHeader) {
                // attempt to read file header
            if (TryEat(_marker, Consts::HeaderPrefix))
                ReadHeader();

            _pendingHeader = false;
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

            case '\r':  // (could be an independant new line, or /r/n combo)
            case '\n':  // (independant new line. A following /r will be treated as another new line)
                _marker.AdvanceCheckNewLine();
                _activeLineSpaces = 0;
                break;

            case ';':
                    // deliminator is ignored here
                ++_marker;
                break;

            case '=':
                if (_activeLineSpaces <= _parentBaseLine) {
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
                // complications (such as, what happens if the identation increases or decreases at that
                // point). Also, because there can't be any newlines, we also cannot support any comments
                // in this space
                //
                // So, we just call EatWhitespace (which jumps over any non-new-line whitespace) and expect
                // to find either a 
                if (!_marker.Remaining())
                    Throw(FormatException("Unexpected end of file in the middle of mapping pair", GetLocation()));

                if (*_marker == '\r' || *_marker == '\n')
                    Throw(FormatException("The value for a key/pair mapping pair must follow immediate after the '='. New lines can not appear here", GetLocation()));

                if (TryEat(_marker, Consts::CommentPrefix))
                    Throw(FormatException("The value for a key/pair mapping pair must follow immediate after the '='. Comments can not appear here", GetLocation()));

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
                if (_activeLineSpaces <= _parentBaseLine) {
                    _protectedStringMode = false;
                    if (_baseLineStackPtr == 0)
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
                ReadToStringEnd<CharType>(readForwardMarker, _protectedStringMode);
                EatWhitespace<CharType>(readForwardMarker);

                if (*readForwardMarker == '=') {
                    return _primed = FormatterBlob::KeyedItem;
                } else {
                    return _primed = FormatterBlob::Value;
                }
            }
        }

            // we've reached the end of the stream...
            // while there are elements on our stack, we need to end them
        if (_baseLineStackPtr > 0) return _primed = FormatterBlob::EndElement;
        return FormatterBlob::None;
    }

    template<typename CharType>
        void InputStreamFormatter<CharType>::ReadHeader()
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
                    const auto* aValueEnd = ReadToStringEnd<CharType>(_marker, false);

                    char convBuffer[12];
                    Conversion::Convert(convBuffer, dimof(convBuffer), aNameStart, aNameEnd);

                    if (!XlCompareStringI(convBuffer, "Format")) {
                        if (Conversion::Convert<int>(MakeStringSection(aValueStart, aValueEnd))!=2)
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
                aNameEnd = ReadToStringEnd<CharType>(_marker, false);
                break;
            }
        }
    }

    template<typename CharType>
        bool InputStreamFormatter<CharType>::TryBeginElement()
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
        bool InputStreamFormatter<CharType>::TryEndElement()
    {
        if (PeekNext() != FormatterBlob::EndElement) return false;

        if (_baseLineStackPtr != 0) {
            _parentBaseLine = (_baseLineStackPtr > 1) ? _baseLineStack[_baseLineStackPtr-2] : -1;
            --_baseLineStackPtr;
        }

        _primed = FormatterBlob::None;
        _protectedStringMode = false;
        return true;
    }

    template<typename CharType>
        bool InputStreamFormatter<CharType>::TryKeyedItem(StringSection<CharType>& name)
    {
        if (PeekNext() != FormatterBlob::KeyedItem) return false;

        name._start = _marker.Pointer();
        name._end = ReadToStringEnd<CharType>(_marker, _protectedStringMode);
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
            Throw(FormatException("Unexpected end of file while looking for a '=' to signify value for keyed item", GetLocation()));

        if (*_marker == '\r' || *_marker == '\n')
            Throw(FormatException("New lines can not appear before the '=' in a mapping name/value pair", GetLocation()));

        if (TryEat(_marker, FormatterConstants<CharType>::CommentPrefix))
            Throw(FormatException("Comments can not appear before the '=' in a mapping name/value pair", GetLocation()));

        if (*_marker != '=')
            Throw(FormatException("Missing '=' to signify value for keyed item", GetLocation()));
        
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
        bool InputStreamFormatter<CharType>::TryStringValue(StringSection<CharType>& value)
    {
        if (PeekNext() != FormatterBlob::Value) return false;

        value._start = _marker.Pointer();
        value._end = ReadToStringEnd<CharType>(_marker, _protectedStringMode);
        EatWhitespace<CharType>(_marker);

        _primed = FormatterBlob::None;
        _protectedStringMode = false;

        return true;
    }

	template<typename CharType>
		bool InputStreamFormatter<CharType>::TryCharacterData(StringSection<CharType>&)
	{
        // CharacterData never appears with in this format files. However it might appear in
        // XML or some other format
		return false;
	}

    template<typename CharType>
        StreamLocation InputStreamFormatter<CharType>::GetLocation() const
    {
        return _marker.GetLocation();
    }

    template<typename CharType>
        InputStreamFormatter<CharType> InputStreamFormatter<CharType>::CreateChildFormatter()
    {
        InputStreamFormatter<CharType> result = *this;
        // reset the baseline to be where we are now
        // _parentBaseLine must stay unchanged, because this still represents the indentation
        // for the end of this element
        result._baseLineStackPtr = 0;
        return result;    
    }

    template<typename CharType>
        InputStreamFormatter<CharType>::InputStreamFormatter(const TextStreamMarker<CharType>& marker) 
        : _marker(marker)
    {
        _primed = FormatterBlob::None;
        _activeLineSpaces = 0;
        _parentBaseLine = -1;
        _baseLineStackPtr = 0;
        _protectedStringMode = false;
        _tabWidth = TabWidth;
        _pendingHeader = true;
    }

    template<typename CharType>
        InputStreamFormatter<CharType>::~InputStreamFormatter()
    {}

	template<typename CharType>
		InputStreamFormatter<CharType>::InputStreamFormatter()
	{
		_primed = FormatterBlob::None;
		_activeLineSpaces = _parentBaseLine = 0;

		for (signed& s:_baseLineStack) s = 0;
		_baseLineStackPtr = 0u;

		_protectedStringMode = false;
		_format = _tabWidth = 0u;
		_pendingHeader = false;
	}

	template<typename CharType>
		InputStreamFormatter<CharType>::InputStreamFormatter(const InputStreamFormatter& cloneFrom)
	: _marker(cloneFrom._marker)
	, _primed(cloneFrom._primed)
	, _activeLineSpaces(cloneFrom._activeLineSpaces)
	, _parentBaseLine(cloneFrom._parentBaseLine)
	, _baseLineStackPtr(cloneFrom._baseLineStackPtr)
	, _protectedStringMode(cloneFrom._protectedStringMode)
	, _format(cloneFrom._format)
	, _tabWidth(cloneFrom._tabWidth)
	, _pendingHeader(cloneFrom._pendingHeader)
	{
		for (unsigned c=0; c<dimof(_baseLineStack); ++c)
			_baseLineStack[c] = cloneFrom._baseLineStack[c];
	}

	template<typename CharType>
		InputStreamFormatter<CharType>& InputStreamFormatter<CharType>::operator=(const InputStreamFormatter& cloneFrom)
	{
		_marker = cloneFrom._marker;
		_primed = cloneFrom._primed;
		_activeLineSpaces = cloneFrom._activeLineSpaces;
		_parentBaseLine = cloneFrom._parentBaseLine;
		_baseLineStackPtr = cloneFrom._baseLineStackPtr;
		for (unsigned c=0; c<dimof(_baseLineStack); ++c)
			_baseLineStack[c] = cloneFrom._baseLineStack[c];
		_protectedStringMode = cloneFrom._protectedStringMode;
		_format = cloneFrom._format;
		_tabWidth = cloneFrom._tabWidth;
		_pendingHeader = cloneFrom._pendingHeader;
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename CharType>
        StreamLocation TextStreamMarker<CharType>::GetLocation() const
    {
        StreamLocation result;
        result._charIndex = 1 + unsigned(_ptr - _lineStart);
        result._lineIndex = 1 + _lineIndex;
        result._filename = _filename;
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
        TextStreamMarker<CharType>::TextStreamMarker(StringSection<CharType> source, const std::string& filename)
    : _ptr(source.begin())
    , _end(source.end())
    , _filename(filename)
    {
        _lineIndex = 0;
        _lineStart = _ptr;
    }

    template<typename CharType>
        TextStreamMarker<CharType>::TextStreamMarker(IteratorRange<const void*> source, const std::string& filename)
    : _ptr((const CharType*)source.begin())
    , _end((const CharType*)source.end())
    , _filename(filename)
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

    template class InputStreamFormatter<utf8>;
    template class TextStreamMarker<utf8>;
}

