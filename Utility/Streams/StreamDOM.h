// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StreamFormatter.h"
#include "OutputStreamFormatter.h"
#include "../StringUtils.h"     // (for StringSection)
#include "../Optional.h"
#include <vector>
#include <string>

#include "../Conversion.h"
#include "../ImpliedTyping.h"

namespace Utility
{
    class OutputStreamFormatter;

    template<typename Formatter> class StreamDOMElement;
    template<typename Formatter> class StreamDOMAttribute;
        
    namespace Internal
    {
        template<typename Formatter> class DocElementIterator;
        template<typename Formatter> class DocAttributeIterator;
    }

    template <typename Formatter>
        class StreamDOM
    {
    public:
        StreamDOMElement<Formatter> RootElement() const;

        StreamDOM();
        StreamDOM(Formatter& formatter);
        ~StreamDOM();

        StreamDOM(StreamDOM&& moveFrom) never_throws;
        StreamDOM& operator=(StreamDOM&& moveFrom) never_throws;
    protected:
        using Section = typename Formatter::InteriorSection;
        struct AttributeDesc
        {
            Section _name, _value;
            unsigned _nextSibling;
        };

        struct ElementDesc
        {
            Section _name;
            unsigned _firstAttribute;
            unsigned _firstChild;
            unsigned _nextSibling;
        };
        
        std::vector<ElementDesc>    _elements;
        std::vector<AttributeDesc>  _attributes;

        #if defined(_DEBUG)
            mutable unsigned _activeMarkerCount = 0;
        #endif

        unsigned ParseElement(Formatter& formatter, bool rootElement, StringSection<> name);

        friend class StreamDOMElement<Formatter>;
        friend class StreamDOMAttribute<Formatter>;
        friend class Internal::DocElementIterator<Formatter>;
        friend class Internal::DocAttributeIterator<Formatter>;
    };

    template<typename Formatter>
        StreamDOM<Formatter> MakeStreamDOM(Formatter& formatter) { return StreamDOM<Formatter>(formatter); }

    namespace Internal
    {
        template<typename Formatter> class DocElementIterator;
        template<typename Formatter> class DocAttributeIterator;
    }

    template<typename Formatter>
        struct StreamDOMElement
    {
        using char_type = typename Formatter::value_type;
        using Section = typename Formatter::InteriorSection;
        
        Section Name() const;

        template<typename Type, decltype(DeserializationOperator(std::declval<const StreamDOMElement&>(), std::declval<Type&>()))* =nullptr>
            Type As() const;

        // Find children elements
        StreamDOMElement Element(StringSection<char_type> name) const;

        // Find & query attributes
        StreamDOMAttribute<Formatter> Attribute(StringSection<char_type> name) const;
        template<typename Type>
            Type Attribute(StringSection<char_type> name, const Type& def) const;

        // Iterate over children elements
        Internal::DocElementIterator<Formatter> begin_children() const;
        Internal::DocElementIterator<Formatter> end_children() const;

        IteratorRange<Internal::DocElementIterator<Formatter>>
            children() const { return { begin_children(), end_children() }; }

        // Iterate over attributes
        Internal::DocAttributeIterator<Formatter> begin_attributes() const;
        Internal::DocAttributeIterator<Formatter> end_attributes() const;

        IteratorRange<Internal::DocAttributeIterator<Formatter>>
            attributes() const { return { begin_attributes(), end_attributes() }; }

        operator bool() const { return _index != ~0u; }
        bool operator!() const { return _index == ~0u; }

        friend bool operator==(const StreamDOMElement& lhs, const StreamDOMElement& rhs)
        {
            if (lhs._index != rhs._index)
                return false;
            if (lhs._index == ~0u)
                return true;
            return lhs._doc == rhs._doc;
        }

        friend bool operator!=(const StreamDOMElement& lhs, const StreamDOMElement& rhs)
        {
            if (lhs._index != rhs._index)
                return true;
            if (lhs._index == ~0u)
                return false;
            return lhs._doc != rhs._doc;
        }

        StreamDOMElement& operator=(const StreamDOMElement&);
        StreamDOMElement(const StreamDOMElement&);
        StreamDOMElement& operator=(StreamDOMElement&&) never_throws;
        StreamDOMElement(StreamDOMElement&&) never_throws;
        StreamDOMElement();
        ~StreamDOMElement();

    protected:
        StreamDOMElement(unsigned elementIndex, const StreamDOM<Formatter>& doc);

        unsigned FindAttribute(StringSection<char_type> name) const;

        const StreamDOM<Formatter>* _doc;
        friend class Internal::DocElementIterator<Formatter>;
        friend class StreamDOM<Formatter>;
        unsigned _index;
    };

    template<typename Formatter>
        class StreamDOMAttribute
    {
    public:
        using char_type = typename Formatter::value_type;

        template<typename Type>
            std::optional<Type> As() const;

        typename Formatter::InteriorSection Name() const;
        typename Formatter::InteriorSection Value() const;

        operator bool() const { return _index != ~0u; }
        bool operator!() const { return _index == ~0u; }

        friend bool operator==(const StreamDOMAttribute& lhs, const StreamDOMAttribute& rhs)
        {
            if (lhs._index != rhs._index)
                return false;
            if (lhs._index == ~0u)
                return true;
            return lhs._doc == rhs._doc;
        }

        friend bool operator!=(const StreamDOMAttribute& lhs, const StreamDOMAttribute& rhs)
        {
            if (lhs._index != rhs._index)
                return true;
            if (lhs._index == ~0u)
                return false;
            return lhs._doc != rhs._doc;
        }

        StreamDOMAttribute& operator=(const StreamDOMAttribute&);
        StreamDOMAttribute(const StreamDOMAttribute&);
        StreamDOMAttribute& operator=(StreamDOMAttribute&&) never_throws;
        StreamDOMAttribute(StreamDOMAttribute&&) never_throws;
        StreamDOMAttribute();
        ~StreamDOMAttribute();

    protected:
        const StreamDOM<Formatter>* _doc;
        unsigned _index;

        StreamDOMAttribute(unsigned attributeIndex, const StreamDOM<Formatter>& doc);

        friend class StreamDOMElement<Formatter>;
        friend class Internal::DocAttributeIterator<Formatter>;
    };

    namespace Internal
    {
        template<typename Formatter>
            class DocElementIterator
        {
        public:
            using Value = StreamDOMElement<Formatter>;

            Value& operator*()                  { return _value; }
            const Value& operator*() const      { return _value; }
            Value* operator->()                 { return &_value; }
            const Value* operator->() const     { return &_value; }

            DocElementIterator& operator++();
            DocElementIterator& operator++(int);

            friend bool operator==(
                const DocElementIterator<Formatter>& lhs,
                const DocElementIterator<Formatter>& rhs) { return lhs._value == rhs._value; }
            friend bool operator!=(
                const DocElementIterator<Formatter>& lhs,
                const DocElementIterator<Formatter>& rhs) { return lhs._value != rhs._value; }

            DocElementIterator() {}
        private:
            DocElementIterator(const Value& v) : _value(v) {}
            Value _value;
            friend class StreamDOMElement<Formatter>;
        };

        template<typename Formatter>
            class DocAttributeIterator
        {
        public:
            using Value = StreamDOMAttribute<Formatter>;

            Value& operator*()                  { return _value; }
            const Value& operator*() const      { return _value; }
            Value* operator->()                 { return &_value; }
            const Value* operator->() const     { return &_value; }

            DocAttributeIterator& operator++();
            DocAttributeIterator& operator++(int);

            friend bool operator==(
                const DocAttributeIterator<Formatter>& lhs,
                const DocAttributeIterator<Formatter>& rhs) { return lhs._value == rhs._value; }
            friend bool operator!=(
                const DocAttributeIterator<Formatter>& lhs,
                const DocAttributeIterator<Formatter>& rhs) { return lhs._value != rhs._value; }

            DocAttributeIterator() {}
        private:
            DocAttributeIterator(const Value& v) : _value(v) {}
            Value _value;
            friend class StreamDOMElement<Formatter>;
        };
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    namespace ImpliedTyping
    {
        template <typename Type>
            std::optional<Type> Parse(const char* expressionBegin, const char* expressionEnd);
    }
    
    template<typename Formatter>
        template<typename Type>
            Type StreamDOMElement<Formatter>::Attribute(StringSection<char_type> name, const Type& def) const
    {
        auto temp = Attribute(name).template As<Type>();
        if (temp.has_value()) return temp.value();
        return def;
    }

    template<typename Formatter>
        template<typename Type, decltype(DeserializationOperator(std::declval<const StreamDOMElement<Formatter>&>(), std::declval<Type&>()))*>
            Type StreamDOMElement<Formatter>::As() const
    {
        Type result;
        DeserializationOperator(*this, result);
        return result;
    }

    template<typename Formatter>
        template<typename Type>
            std::optional<Type> StreamDOMAttribute<Formatter>::As() const
    {
        if (_index == ~unsigned(0)) return {};
        const auto& attrib = _doc->_attributes[_index];
        return ImpliedTyping::ConvertFullMatch<Type>(attrib._value);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Type, typename CharType>
        inline void SerializationOperator(OutputStreamFormatter& formatter, const CharType name[], const Type& obj)
    {
        formatter.WriteKeyedValue(
            name, 
            Conversion::Convert<std::basic_string<CharType>>(ImpliedTyping::AsString(obj, true)));
    }

    template<typename CharType>
        inline void SerializationOperator(OutputStreamFormatter& formatter, const CharType name[], const CharType str[])
    {
        formatter.WriteKeyedValue(name, str);
    }

    template<typename CharType>
        inline void SerializationOperator(OutputStreamFormatter& formatter, const CharType name[], const std::basic_string<CharType>& str)
    {
        formatter.WriteKeyedValue(name, str);
    }

    template<typename FirstType, typename SecondType, typename CharType>
        inline void SerializationOperator(OutputStreamFormatter& formatter, const CharType name[], const std::pair<FirstType, SecondType>& obj)
    {
        auto ele = formatter.BeginKeyedElement(name);
        SerializationOperator(formatter, "First", obj.first);
        SerializationOperator(formatter, "Second", obj.second);
        formatter.EndElement(ele);
    }

#if 0
    template<typename Type, typename Formatter>
        inline Type Deserialize(const typename Internal::DocElementIterator<Formatter>::Value& ele, const typename Formatter::char_type name[], const Type& obj)
    {
        return ele(name, obj);
    }

    template<typename FirstType, typename SecondType, typename Formatter>
        inline std::pair<FirstType, SecondType> Deserialize(const typename Internal::DocElementIterator<Formatter>::Value& ele, const typename Formatter::char_type name[], const std::pair<FirstType, SecondType>& def)
    {
        auto subEle = ele.Element(name);
        if (!subEle) return def;
        return std::make_pair(
            Deserialize(subEle, "First", def.first),
            Deserialize(subEle, "Second", def.second));
    }
#endif

}

using namespace Utility;
