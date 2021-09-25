// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "XmlStreamFormatter.h"

namespace Utility
{

    template<typename CharType>
        bool IsWhitespace(CharType chr)
    {
        return chr == 0x20 || chr == 0x9 || chr == 0xD || chr == 0xA;
    }

    template<typename CharType>
        bool IsNameStartChar(CharType chr)
    {
        //      Formal XML standard requires the following characters for "NameStartChar"
        // NameStartChar	   ::=   	":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6] | [#xF8-#x2FF] | [#x370-#x37D] | [#x37F-#x1FFF] | [#x200C-#x200D] | [#x2070-#x218F] | [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] | [#xFDF0-#xFFFD] | [#x10000-#xEFFFF]
        //
        // we're going to simplify the rules a little bit, for parser efficiency.
        // the standard excludes:
        //      0xD7 (multiplication symbol)
        //      0xF7 (division symbol)
        // but we will include these.
        return  (chr == CharType(':'))
            ||  (chr >= CharType('A') && chr <= CharType('Z'))
            ||  (chr == CharType('_'))
            ||  (chr >= CharType('a') && chr <= CharType('z'))
            ||  (chr >= 0xC0);
    }

    template<typename CharType>
        bool IsNameChar(CharType chr)
    {
        //      Formal XML is -- 
        // NameChar	   ::=   	NameStartChar | "-" | "." | [0-9] | #xB7 | [#x0300-#x036F] | [#x203F-#x2040]
        // we're going to permit a few extra chars
        // we skip: 0x2F ('/')
        //          0x3B-0x40 (';' '<' '=' '>' '?' '@')
        //          0x5B-0x5E ('[' '\' ']' '^')

        // return  (chr >= 0x2D && chr <= 0x2E)    // - and .
        //     ||  (chr >= 0x30 && chr <= 0x3A)    // 0-9 and :
        //     ||  (chr >= CharType('A') && chr <= CharType('Z'))
        //     ||  (chr == CharType('_') && chr <= CharType('z'))      // _ ` and lower case
        //     ||  (chr == 0xB7)
        //     ||  (chr >= 0xC0);

        if (chr > 0x40) {
            if (chr > 0xC0) return true;
            return (chr < 0x5B) || (chr > 0x5E && chr <= 0x7A);
        } else {
            return (chr >= 0x2D) && (chr < 0x3B) && (chr != 0x2F);
        }
    }

    template<typename CharType>
        struct FormatterConstants 
    {
        static CharType PIStart[2];
        static CharType PIEnd[2];
        static CharType CommentPrefix[2];
        static CharType CommentEnd[3];
        static CharType CDataPrefix[7];
        static CharType CDataEnd[3];
        static CharType CloseAngleBracket[1];
    };

    template<typename CharType>
        CharType FormatterConstants<CharType>::PIStart[] = { CharType('<'), CharType('?') };
    template<typename CharType>
        CharType FormatterConstants<CharType>::PIEnd[] = { CharType('?'), CharType('>') };

    template<typename CharType>
        CharType FormatterConstants<CharType>::CommentPrefix[] = { CharType('-'), CharType('-') };
    template<typename CharType>
        CharType FormatterConstants<CharType>::CommentEnd[] = { CharType('-'), CharType('-'), CharType('>') };

    template<typename CharType>
        CharType FormatterConstants<CharType>::CDataPrefix[] = { CharType('['), CharType('C'), CharType('D'), CharType('A'), CharType('T'), CharType('A'), CharType('[') };
    template<typename CharType>
        CharType FormatterConstants<CharType>::CDataEnd[] = { CharType(']'), CharType(']'), CharType('>') };

    template<typename CharType>
        CharType FormatterConstants<CharType>::CloseAngleBracket[] = { CharType('>') };


    enum TryEatResult { NoMatch, Match, Clipped };

    template<typename CharType, int Count>
        static TryEatResult TryEat(CharType (&pattern)[Count], TextStreamMarker<CharType>& mark)
    {
        auto* ptr = mark.Pointer();
        if (mark.Remaining() < Count) return Clipped;
        for (unsigned c=0; c<Count; ++c)
            if (ptr[c] != pattern[c]) return NoMatch;
        mark.SetPointer(mark.Pointer() + Count);
        return Match;
    }

    template<typename CharType, int Count>
        static void ScanToClosing(
            CharType(&endPattern)[Count], TextStreamMarker<CharType>& mark,
            const char exceptionMsg[], const StreamLocation& exceptionLoc)
    {
        for (;;) {
            auto er = TryEat(endPattern, mark);
            if (er == Clipped)
                Throw(FormatException(exceptionMsg, exceptionLoc));
            if (er == Match) return;
            mark.AdvanceCheckNewLine();
        }
    }

    template<typename CharType>
        static void EatWhitespace(TextStreamMarker<CharType>& mark, const char exceptionMsg[])
    {
            // scan forward over any whitespace or "character data"
            // we need to record line breaks, however
        for (;;) {
            if (mark.Remaining() < 1)
                Throw(FormatException(exceptionMsg, mark.GetLocation()));

            if (!IsWhitespace(*mark)) break;
            mark.AdvanceCheckNewLine();
        }
    }

    template<typename CharType>
        auto XmlInputStreamFormatter<CharType>::PeekNext() -> Blob
    {
        if (_primed != Blob::None) return _primed;

        using Const = FormatterConstants<CharType>;
        TextStreamMarker<CharType> mark = _marker;

        if (_pendingHeader) {

                // Skip whitespace, then look for <?xml... type header
                // currently only supporting "processing instruction"
                // objects for the xml header. If a processing instruction
                // appears elsewhere in the file, it will fail to parse
            while (mark.Remaining() >= 1 && IsWhitespace(*mark))
                mark.AdvanceCheckNewLine();
            
            for (;;) {
                auto piStart = mark.GetLocation();
                if (TryEat(Const::PIStart, mark) == Match) {
                        // this is a <? ... ?> "processing instruction
                        // we will ignore everything until we find the closing ?>
                    for (;;) {
                        auto er = TryEat(Const::PIEnd, mark);
                        if (er == Clipped)
                            Throw(FormatException("End of file found in processing exception", piStart));
                        if (er == Match) break;

                        mark.AdvanceCheckNewLine();
                    }
                } else break;
            }

            _marker = mark;
            _pendingHeader = false;
        }

        auto scopeType = _scopeStack.top()._type;
        if (scopeType == Scope::Type::None || scopeType == Scope::Type::Element) {

            if (_allowCharacterData) {
				// Scan forward to look for the next non-whitespace element
				// If it's not a new element, we'll consider the whole block (including the whitespace we
				// just skipped) to be a character data blob
				while (mark.Remaining() >= 1 && IsWhitespace(*mark))
					mark.AdvanceCheckNewLine();
				if (mark.Remaining() >= 1 && *mark != '<') {
					// (_marker remains at the start of the whitespace block)
					return _primed = Blob::CharacterData;
				}
            }
            
            for (;;) {

                CharType testChar;

                {
                        // scan forward over any whitespace or "character data"
                        // we need to record line breaks, however
                    for (;;) {
                        if (mark.Remaining() < 1) { 
                                // reached end of tile
                            if (scopeType == Scope::Type::None) { _marker = mark; return Blob::None; }
                            Throw(FormatException("Unexpected end of file in element", mark.GetLocation()));
                        }
                        if (*mark == '<') break;
                        mark.AdvanceCheckNewLine();
                    }

                    ++mark;
                    if (mark.Remaining() < 1)
                        Throw(FormatException("Unexpected end of file after open bracket", mark.GetLocation()));
                    testChar = *mark;
                }

                if (IsNameStartChar(testChar)) {

                    // openning a new element (ie <eleName...) should generate FormatterBlob::KeyedItem first, then
                    // FormatterBlob::BeginElement
                    _marker = mark;
                    _scopeStack.push(Scope{Scope::Type::ElementName, {}});
                    return _primed = Blob::KeyedItem;

                } else if (testChar == '!') {

                    auto blobStart = mark.GetLocation();
                    ++mark;

                        // this is a system object or comment <! ... >
                    if (TryEat(Const::CommentPrefix, mark) == Match) {
                            // comments always terminated by "-->"
                            // note that a hyphen preceeding "-->" is an error... But we can ignore that for now
                        ScanToClosing(Const::CommentEnd, mark, "Unexpected end of file in comment", blobStart);
                    } else if (TryEat(Const::CDataPrefix, mark) == Match) {
                            // "cdata" is special, because it can contain '>' within it. We need to look for the
                            // special deliminator.
                            // Note -- this cdata form won't result in a CharacterData type blob!
                        ScanToClosing(Const::CDataEnd, mark, "Unexpected end of file in cdata", blobStart);
                    } else {
                            // some other special blob should end in '>'
                        ScanToClosing(Const::CloseAngleBracket, mark, "Unexpected end of file in <! blob", blobStart);
                    }

                } else if (testChar == '/') {

                    // we're expecting an end of element marker </elementName>
                    ++mark;
                    _marker = mark;
                    return _primed = Blob::EndElement;

                } else {
                    Throw(FormatException("Bad first character in element name", mark.GetLocation()));
                }

            }
        } else if (scopeType == Scope::Type::AttributeList) {

            EatWhitespace(mark, "Unexpected end of file in attribute list");

            if (IsNameStartChar(*mark)) {
                _marker = mark;
                return _primed = Blob::KeyedItem;
            } else if (*mark == '/') {
                ++mark;
                _marker = mark;
                return _primed = Blob::EndElement;
            } else if (*mark == '>') {
                ++mark;
                _marker = mark;
                _scopeStack.top()._type = Scope::Type::Element;
                return PeekNext();
            } else {
                Throw(FormatException("Bad character in attribute list", mark.GetLocation()));
            }

        } else if (scopeType == Scope::Type::ElementName) {

            // elements will always begin with name, which we return via TryKeyedItem
            _marker = mark;
            return _primed = Blob::KeyedItem;

        } else if (scopeType == Scope::Type::PendingBeginElement) {

            // this is a placeholder to ensure that Blob::BeginElement is returned
            // after we've read the <eleName part of the element begin
            _marker = mark;
            return _primed = Blob::BeginElement;

        } else if (scopeType == Scope::Type::AttributeValue) {

            // this is a placeholder to ensure that Blob::Value is returned
            // after we've read the key= part of an attribute in the attribute list
            _marker = mark;
            return _primed = Blob::Value;

        } else {
            assert(0);
        }

        return _primed = Blob::None;
    }

    template<typename CharType>
        bool XmlInputStreamFormatter<CharType>::TryBeginElement()
        {
            if (PeekNext() != Blob::BeginElement) return false;

             assert(_scopeStack.top()._type == Scope::Type::PendingBeginElement);
             // next should come either an attribute list, or ">" or "/>" style deliminator

            for (;;) {
                if (_marker.Remaining() < 1)
                    Throw(FormatException("Unexpected end of file in element", _marker.GetLocation()));

                if (!IsWhitespace(*_marker)) break;
                _marker.AdvanceCheckNewLine();
            }

            if (IsNameStartChar(*_marker)
                || (*_marker == '/' && _marker.Remaining() >= 2 && _marker[1] == '>')) {

                _scopeStack.top()._type = Scope::Type::AttributeList;
            } else if (*_marker == '>') {
                _scopeStack.top()._type = Scope::Type::Element;
                ++_marker;
            } else {
                Throw(FormatException("Bad character after element name", _marker.GetLocation()));
            }

            _primed = Blob::None;
            return true;
        }

    template<typename CharType>
        bool XmlInputStreamFormatter<CharType>::TryKeyedItem(StringSection<CharType>& name)
        {
            if (PeekNext() != Blob::KeyedItem) return false;

            auto scopeType = _scopeStack.top()._type;
            if (scopeType == Scope::Type::ElementName) {
                name._start = _marker.Pointer();
                ++_marker;

                for (;;) {
                    if (_marker.Remaining() < 1)
                        Throw(FormatException("Unexpected end of file in element", _marker.GetLocation()));
                    if (!IsNameChar(*_marker)) break;
                    ++_marker;  // no need to check for new-line because new lines are not name chars
                }

                name._end = _marker.Pointer();
                _scopeStack.top()._elementName = name;
                _scopeStack.top()._type = Scope::Type::PendingBeginElement;
                _primed = Blob::None;
                return true;
                   
            } else if (scopeType == Scope::Type::AttributeList) {
                name._start = _marker.Pointer();
                ++_marker;

                for (;;) {
                    if (_marker.Remaining() < 1 || !IsNameChar(*_marker))
                        break;
                    ++_marker;
                }
                name._end = _marker.Pointer();

                EatWhitespace(_marker, "Unexpected end of file in attribute");
                if (*_marker != '=')
                    Throw(FormatException("Expected = after attribute name", _marker.GetLocation()));
                ++_marker;
                EatWhitespace(_marker, "Unexpected end of file in attribute");

                // a value must follow this
                _scopeStack.top()._type = Scope::Type::AttributeValue;

                _primed = Blob::None;
                return true;
            } else {
                Throw(FormatException("Unexpected scoped while reading keyed item name", _marker.GetLocation()));
            }
        }

    template<typename CharType>
        bool XmlInputStreamFormatter<CharType>::TryEndElement() 
    {
        if (PeekNext() != Blob::EndElement) return false;

        auto& scope = _scopeStack.top();
        if (scope._type != Scope::Type::Element && scope._type != Scope::Type::AttributeList)
            Throw(FormatException("End element occurred outside of an element", _marker.GetLocation()));

        if (_marker.Remaining() >= 1 && *_marker == '>') {
                // we'll assume this is the '/>' style of short element.
                // no validation required. Just pop the scope;
            ++_marker;
            _scopeStack.pop();
        } else {
            auto* eleNameStart = _marker.Pointer();
            for (;;) {
                if (_marker.Remaining() < 1) break;
                if (!IsNameChar(*_marker)) break;
                ++_marker;
            }
            auto *eleNameEnd = _marker.Pointer();

                // validate begin/end element tags match
            {
                if ((eleNameEnd - eleNameStart) != (scope._elementName._end - scope._elementName._start))
                    Throw(FormatException("End element tag mismatch", _marker.GetLocation()));
                auto*i = eleNameStart;
                auto*i2 = scope._elementName._start;
                for (;i<eleNameEnd; ++i, ++i2)
                    if (*i != *i2)
                        Throw(FormatException("End element tag mismatch", _marker.GetLocation()));
            }

            EatWhitespace(_marker, "Unexpected end of file in end element");
            if (_marker.Remaining() < 1 || *_marker != '>')
                Throw(FormatException("Expected > in end element", _marker.GetLocation()));
            _scopeStack.pop();
            ++_marker;
        }

        _primed = Blob::None;
        return true;
    }

    template<typename CharType>
        bool XmlInputStreamFormatter<CharType>::TryStringValue(InteriorSection& value)
    {
        if (PeekNext() != Blob::Value) return false;

        assert(_scopeStack.top()._type == Scope::Type::AttributeValue);

        CharType openner[1] = { *_marker };
        if (openner[0] != '"' && openner[0] != '\'')
            Throw(FormatException("Expected ' or \" around attribute value", _marker.GetLocation()));
        ++_marker;

        value._start = _marker.Pointer();
            // < and & are illegal in the following. But we will not enforce that.
        ScanToClosing(openner, _marker, "Unexpected end of file in attribute value", _marker.GetLocation());
        value._end = _marker.Pointer()-1;

        _scopeStack.top()._type = Scope::Type::AttributeList;
        _primed = Blob::None;
        return true;
    }

    template<typename CharType>
        bool XmlInputStreamFormatter<CharType>::TryCharacterData(StringSection<CharType>& cdata)
    {
        assert(_allowCharacterData);
        if (PeekNext() != Blob::CharacterData) return false;

        cdata._start = _marker.Pointer();

        bool gotEnd = false;
        while (_marker.Remaining() >= 64 && !gotEnd) {
            for (unsigned c=0; c<64; ++c) {
                if (*_marker == '<') { gotEnd = true; break; }
                _marker.AdvanceCheckNewLine();
            }
        }

        if (!gotEnd) {
            for (;;) {
                if (_marker.Remaining() < 1) { 
                        // reached end of tile
                    if (_scopeStack.top()._type != Scope::Type::None) {
                        Throw(FormatException("Unexpected end of file in element", _marker.GetLocation()));
                        break;
                    }
                }
                if (*_marker == '<') break;
                _marker.AdvanceCheckNewLine();
            }
        }

        cdata._end = _marker.Pointer();

        _primed = Blob::None;
        return true;
    }

    template<typename CharType>
        StreamLocation XmlInputStreamFormatter<CharType>::GetLocation() const
    {
        return _marker.GetLocation();
    }

    template<typename CharType>
        XmlInputStreamFormatter<CharType>::XmlInputStreamFormatter(const TextStreamMarker<CharType>& marker)
        : _marker(marker)
    {
        _primed = Blob::None;
        _pendingHeader = true;
        _scopeStack.push(Scope{Scope::Type::None, InteriorSection()});
    }

    template<typename CharType>
        XmlInputStreamFormatter<CharType>::~XmlInputStreamFormatter() {}

    template class XmlInputStreamFormatter<utf8>;
}

