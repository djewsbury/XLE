// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StreamFormatter.h"        // (for FormatException and StreamLocation)
#include <stack>

namespace Utility
{
    /// <summary>Deserializes element and attribute data from xml</summary>
    /// This is an input deserializer for xml data that handles just element and
    /// attributes. The interface is compatible with InputStreamFormatter, and
    /// can be used as a drop-in replacement when required.
    /// 
    /// It's a hand-written perform oriented parser. It should perform reasonable
    /// well even for large files.
    ///
    /// Note that this is a subset of true xml. Many XML features (like processing
    /// instructions, references and character data) aren't fully supported. But it will
    /// read elements and attributes -- handy for applications of XML that use only 
    /// these things. There is some support for reading character data. But it is limited
    /// and intended for simple tasks.
    template<typename CharType>
        class XL_UTILITY_API XmlInputStreamFormatter
    {
    public:
        FormatterBlob PeekNext();

        bool TryBeginElement();
        bool TryEndElement();
        bool TryKeyedItem(StringSection<CharType>& name);
        bool TryStringValue(StringSection<CharType>& value);
        bool TryCharacterData(StringSection<CharType>& cdata);

        StreamLocation GetLocation() const;

        using value_type = CharType;
        using InteriorSection = StringSection<CharType>;
        using Blob = FormatterBlob;

        bool _allowCharacterData = false;

        XmlInputStreamFormatter(const TextStreamMarker<CharType>& marker);
        XmlInputStreamFormatter(StringSection<CharType> source, ::Assets::DependencyValidation = {});
		XmlInputStreamFormatter(IteratorRange<const void*> source, ::Assets::DependencyValidation = {});
        ~XmlInputStreamFormatter();
    protected:
        TextStreamMarker<CharType> _marker;
        Blob _primed;

        bool _pendingHeader;

        class Scope
        {
        public:
            enum class Type { AttributeList, Element, ElementName, PendingBeginElement, AttributeValue, None };
            Type _type;
            InteriorSection _elementName;
        };

        std::stack<Scope> _scopeStack;
    };

    template<typename CharType>
		inline XmlInputStreamFormatter<CharType>::XmlInputStreamFormatter(StringSection<CharType> source, ::Assets::DependencyValidation depVal)
		: XmlInputStreamFormatter(TextStreamMarker<CharType>{source, std::move(depVal)}) {}
	template<typename CharType>
		inline XmlInputStreamFormatter<CharType>::XmlInputStreamFormatter(IteratorRange<const void*> source, ::Assets::DependencyValidation depVal)
		: XmlInputStreamFormatter(TextStreamMarker<CharType>{source, std::move(depVal)}) {}
}

using namespace Utility;
