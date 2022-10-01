// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StreamDOM.h"
#include "StreamFormatter.h"
#include "XmlStreamFormatter.h"
#include "../StringFormat.h"

namespace Utility
{

    template <typename Formatter>
        unsigned StreamDOM<Formatter>::ParseElement(Formatter& formatter, bool rootElement, StringSection<> name)
    {
        if (!rootElement && !formatter.TryBeginElement())
            return ~0u;

        unsigned e = ~0u;
        {
            ElementDesc newElement;
            newElement._name = name;
            newElement._firstAttribute = ~0u;
            newElement._firstChild = ~0u;
            newElement._nextSibling = ~0u;
            _elements.push_back(newElement);
            e = (unsigned)_elements.size()-1;
        }

        auto lastChild = ~0u;
        auto lastAttribute = ~0u;
        for (;;) {
            auto next = formatter.PeekNext();
            if (next == Formatter::Blob::KeyedItem) {
                typename Formatter::InteriorSection name;
                if (!formatter.TryKeyedItem(name))
                    Throw(FormatException(
                        "Error while reading keyed item name in StreamDOM", formatter.GetLocation()));

                next = formatter.PeekNext();
                if (next == Formatter::Blob::BeginElement) {
                    auto newElement = ParseElement(formatter, false, name);
                    if (lastChild == ~0u) {
                        _elements[e]._firstChild = newElement;
                    } else {
                        _elements[lastChild]._nextSibling = newElement;
                    }
                    lastChild = newElement;
                } else if (next == Formatter::Blob::Value) {
                    typename Formatter::InteriorSection value;
                        if (!formatter.TryStringValue(value))
                        Throw(FormatException(
                            "Error while reading attribute in StreamDOM", formatter.GetLocation()));

                    AttributeDesc attrib;
                    attrib._name = name;
                    attrib._value = value;
                    attrib._nextSibling = ~0u;
                    _attributes.push_back(attrib);
                    auto a = (unsigned)_attributes.size()-1;
                    if (lastAttribute == ~0u) {
                        _elements[e]._firstAttribute = a;
                    } else {
                        _attributes[lastAttribute]._nextSibling = a;
                    }
                    lastAttribute = a;
                } else {
                    Throw(FormatException(
                        "Expecting either a value or element for keyed item in StreamDOM", formatter.GetLocation()));
                }
            } else if (next == Formatter::Blob::Value) {
                Throw(FormatException(
                        "Sequence values not supported by StreamDOM (use a mapping)", formatter.GetLocation()));
            } else if (next == Formatter::Blob::BeginElement) {
                Throw(FormatException(
                        "Sequence elements not supported by StreamDOM (use a mapping)", formatter.GetLocation()));
            } else if (next == Formatter::Blob::EndElement || next == Formatter::Blob::None) {
                break;
            } else if (next == Formatter::Blob::CharacterData) {
				typename Formatter::InteriorSection dummy;
                if (!formatter.TryCharacterData(dummy))
                    Throw(FormatException(
                        "Error while reading character data in StreamDOM", formatter.GetLocation()));
            } else {
                Throw(FormatException(
                    StringMeld<128>() << "Got unexpected blob type (" << unsigned(next) << ") while parsing element in StreamDOM",
                    formatter.GetLocation()));
            }
        }

        // For the root element, we'll get "None" when we hit the end of the file,
        // at which point we should exit
        if (rootElement) {
            // We can exit a rootElement after hitting either Blob::EndElement or Blob::None
            // The Blob::EndElement case happens because we can use StreamDOM to deserialize
            // only part of the hierarchy of am input file (for example, the rest of the 
            // deserialization might use the StreamFormatter directly)
        } else {
            if (!formatter.TryEndElement()) 
                Throw(FormatException(
                    "Expected end element in StreamDOM", formatter.GetLocation()));
        }

        return e;
    }

    template <typename Formatter>
        StreamDOMElement<Formatter> StreamDOM<Formatter>::RootElement() const
    {
        if (_elements.empty())
            return {};
        return StreamDOMElement<Formatter>{0, *this};
    }

    template <typename Formatter>
        StreamDOM<Formatter>::StreamDOM(Formatter& formatter)
    {
        _elements.reserve(32);
        _attributes.reserve(64);

            // Parse the input formatter, building a tree
            // of elements and a list of attributes.
            // We can start with several top-level elements
        ParseElement(formatter, true, {});
    }

    template <typename Formatter>
        StreamDOM<Formatter>::StreamDOM() 
    {
    }

    template <typename Formatter>
        StreamDOM<Formatter>::~StreamDOM()
    {
        assert(_activeMarkerCount==0);
    }

    template <typename Formatter>
        StreamDOM<Formatter>::StreamDOM(StreamDOM&& moveFrom) never_throws
    {
        assert(!moveFrom._activeMarkerCount);
        _elements = std::move(moveFrom._elements);
        _attributes = std::move(moveFrom._attributes);
    }

    template <typename Formatter>
        auto StreamDOM<Formatter>::operator=(StreamDOM&& moveFrom) never_throws -> StreamDOM&
    {
        assert(!_activeMarkerCount && !moveFrom._activeMarkerCount);
        _elements = std::move(moveFrom._elements);
        _attributes = std::move(moveFrom._attributes);
        return *this;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::Element(StringSection<char_type> name) const -> StreamDOMElement<Formatter>
    {
        if (_index == ~0u) return {};
        auto& ele2 = _doc->_elements[_index];

        for (unsigned e=ele2._firstChild; e!=~0u;) {
            const auto& ele = _doc->_elements[e];
            if (XlEqString(ele._name, name))
                return StreamDOMElement<Formatter>{e, *_doc};

            e=ele._nextSibling;
        }

        return {};
    }

    template<typename Formatter>
        StreamDOMAttribute<Formatter> StreamDOMElement<Formatter>::Attribute(StringSection<char_type> name) const
    {
        if (_index == ~0u) return {};
        return StreamDOMAttribute<Formatter>{FindAttribute(name), *_doc};
    }

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::begin_children() const -> Internal::DocElementIterator<Formatter>
    {
        if (_index == ~0u) return {};
        auto& ele = _doc->_elements[_index];
        return Internal::DocElementIterator<Formatter>(
            StreamDOMElement<Formatter>{ele._firstChild, *_doc});
    }

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::end_children() const -> Internal::DocElementIterator<Formatter>
    {
        return {};
    }

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::Name() const -> typename Formatter::InteriorSection
    {
        if (_index == ~0u) return {};
        auto& ele = _doc->_elements[_index];
        return ele._name;
    }

    template<typename Formatter>
        unsigned StreamDOMElement<Formatter>::FindAttribute(StringSection<char_type> name) const
    {
        if (_index == ~0u) return ~0u;
        
        auto& ele = _doc->_elements[_index];
        for (unsigned a=ele._firstAttribute; a!=~0u;) {
            const auto& attrib = _doc->_attributes[a];
            if (XlEqString(attrib._name, name))
                return a;

            a=attrib._nextSibling;
        }

        return ~0u;
    }

    template<typename Formatter>
        Internal::DocAttributeIterator<Formatter> StreamDOMElement<Formatter>::begin_attributes() const
    {
        if (_index == ~0u) return {};
        const auto& e = _doc->_elements[_index];
        return Internal::DocAttributeIterator<Formatter>(
            StreamDOMAttribute<Formatter>{e._firstAttribute, *_doc});
    }

    template<typename Formatter>
        Internal::DocAttributeIterator<Formatter> StreamDOMElement<Formatter>::end_attributes() const
    {
        return {};
    }

    template<typename Formatter>
        StreamDOMElement<Formatter>::StreamDOMElement(unsigned elementIndex, const StreamDOM<Formatter>& doc)
    {
        _doc = &doc;
        _index = elementIndex;
        DEBUG_ONLY( ++_doc->_activeMarkerCount; )
    }

    template<typename Formatter>
        StreamDOMElement<Formatter>::StreamDOMElement()
    {
        _index = ~0u;
        _doc = nullptr;
    }

    template<typename Formatter>
        StreamDOMElement<Formatter>::~StreamDOMElement()
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
    }

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::operator=(const StreamDOMElement& cloneFrom) -> StreamDOMElement&
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
        _doc = cloneFrom._doc;
        _index = cloneFrom._index;
        #if defined(_DEBUG)
            if (_doc)
                ++_doc->_activeMarkerCount;
        #endif
        return *this;
    }

    template<typename Formatter>
        StreamDOMElement<Formatter>::StreamDOMElement(const StreamDOMElement& cloneFrom)
    {
        _doc = cloneFrom._doc;
        _index = cloneFrom._index;
        #if defined(_DEBUG)
            if (_doc)
                ++_doc->_activeMarkerCount;
        #endif
    }

    template<typename Formatter>
        auto StreamDOMElement<Formatter>::operator=(StreamDOMElement&& moveFrom) never_throws -> StreamDOMElement&
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
        _doc = moveFrom._doc;
        _index = moveFrom._index;
        moveFrom._doc = nullptr;
        moveFrom._index = ~0u;
        return *this;
    }

    template<typename Formatter>
        StreamDOMElement<Formatter>::StreamDOMElement(StreamDOMElement&& moveFrom) never_throws
    {
        _doc = moveFrom._doc;
        _index = moveFrom._index;
        moveFrom._doc = nullptr;
        moveFrom._index = ~0u;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Formatter>
        typename Formatter::InteriorSection StreamDOMAttribute<Formatter>::Name() const
    {
        if (_index == ~unsigned(0)) return {};
        return _doc->_attributes[_index]._name;
    }

    template<typename Formatter>
        typename Formatter::InteriorSection StreamDOMAttribute<Formatter>::Value() const
    {
        if (_index == ~unsigned(0)) return {};
        return _doc->_attributes[_index]._value;
    }

    template<typename Formatter>
        StreamDOMAttribute<Formatter>::StreamDOMAttribute(
            unsigned attributeIndex, 
            const StreamDOM<Formatter>& doc)
    : _doc(&doc), _index(attributeIndex)
    {
        DEBUG_ONLY( ++_doc->_activeMarkerCount; )
    }

    template<typename Formatter>
        StreamDOMAttribute<Formatter>::StreamDOMAttribute() 
    : _doc(nullptr), _index(~0u) {}

    template<typename Formatter>
        StreamDOMAttribute<Formatter>::~StreamDOMAttribute()
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
    }

    template<typename Formatter>
        auto StreamDOMAttribute<Formatter>::operator=(const StreamDOMAttribute& cloneFrom) -> StreamDOMAttribute&
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
        _doc = cloneFrom._doc;
        _index = cloneFrom._index;
        #if defined(_DEBUG)
            if (_doc)
                ++_doc->_activeMarkerCount;
        #endif
        return *this;
    }

    template<typename Formatter>
        StreamDOMAttribute<Formatter>::StreamDOMAttribute(const StreamDOMAttribute& cloneFrom)
    {
        _doc = cloneFrom._doc;
        _index = cloneFrom._index;
        #if defined(_DEBUG)
            if (_doc)
                ++_doc->_activeMarkerCount;
        #endif
    }

    template<typename Formatter>
        auto StreamDOMAttribute<Formatter>::operator=(StreamDOMAttribute&& moveFrom) never_throws -> StreamDOMAttribute&
    {
        #if defined(_DEBUG)
            if (_doc) {
                assert(_doc->_activeMarkerCount > 0);
                --_doc->_activeMarkerCount;
            }
        #endif
        _doc = moveFrom._doc;
        _index = moveFrom._index;
        moveFrom._doc = nullptr;
        moveFrom._index = ~0u;
        return *this;
    }

    template<typename Formatter>
        StreamDOMAttribute<Formatter>::StreamDOMAttribute(StreamDOMAttribute&& moveFrom) never_throws
    {
        _doc = moveFrom._doc;
        _index = moveFrom._index;
        moveFrom._doc = nullptr;
        moveFrom._index = ~0u;
    }

    namespace Internal
    {
        template<typename Formatter>
            auto DocElementIterator<Formatter>::operator++() -> DocElementIterator<Formatter>&
        {
            if (_value._index != ~0u) {
                auto& ele = _value._doc->_elements[_value._index];
                _value._index = ele._nextSibling;
            }
            return *this;
        }

        template<typename Formatter>
            auto DocElementIterator<Formatter>::operator++(int) -> DocElementIterator<Formatter>&
        {
            if (_value._index != ~0u) {
                auto& ele = _value._doc->_elements[_value._index];
                _value._index = ele._nextSibling;
            }
            return *this;
        }


        template<typename Formatter>
            auto DocAttributeIterator<Formatter>::operator++() -> DocAttributeIterator<Formatter>&
        {
            if (_value._index != ~0u) {
                const auto& a = _value._doc->_attributes[_value._index];
                _value._index = a._nextSibling;
            }
            return *this;
        }

        template<typename Formatter>
            auto DocAttributeIterator<Formatter>::operator++(int) -> DocAttributeIterator<Formatter>&
        {
            if (_value._index != ~0u) {
                const auto& a = _value._doc->_attributes[_value._index];
                _value._index = a._nextSibling;
            }
            return *this;
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template class StreamDOM<InputStreamFormatter<utf8>>;
    template class StreamDOM<XmlInputStreamFormatter<utf8>>;

    template struct StreamDOMElement<InputStreamFormatter<utf8>>;
    template struct StreamDOMElement<XmlInputStreamFormatter<utf8>>;

    template class StreamDOMAttribute<InputStreamFormatter<utf8>>;
    template class StreamDOMAttribute<XmlInputStreamFormatter<utf8>>;
    
    template class Internal::DocElementIterator<InputStreamFormatter<utf8>>;
    template class Internal::DocElementIterator<XmlInputStreamFormatter<utf8>>;

    template class Internal::DocAttributeIterator<InputStreamFormatter<utf8>>;
    template class Internal::DocAttributeIterator<XmlInputStreamFormatter<utf8>>;

}
