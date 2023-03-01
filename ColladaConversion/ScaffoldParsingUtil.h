// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Formatters/XmlFormatter.h"
#include "../Utility/Conversion.h"
#include "../Utility/FastParseValue.h"

namespace ColladaConversion
{
    inline bool Is(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const utf8 match[])
    {
        return XlEqString(section, match);
    }

    bool BeginsWith(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const utf8 match[]);
    bool EndsWith(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const utf8 match[]);

    template <typename Enum, unsigned Count>
        static Enum ParseEnum(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const std::pair<Enum, const utf8*> (&table)[Count])
    {
        static_assert(Count > 0, "Enum names table must have at least one entry");
        for (unsigned c=0; c<Count; ++c)
            if (Is(section, table[c].second))
                return table[c].first;

        return table[0].first;  // first one is the default
    }

    template<typename CharType>
        bool IsWhitespace(CharType chr)
    {
        return chr == 0x20 || chr == 0x9 || chr == 0xD || chr == 0xA;
    }
    
    template<typename Type>
        auto ParseXMLList(Type dest[], unsigned destCount, Formatters::XmlInputFormatter<utf8>::InteriorSection section, unsigned* outEleCount = nullptr) 
            -> decltype(Formatters::XmlInputFormatter<utf8>::InteriorSection::_start)
    {
        assert(destCount > 0);

        // in xml, lists are deliminated by white space.
        unsigned elementCount = 0;
        auto* eleStart = section._start;
        while (elementCount < destCount) {
            while (eleStart < section._end && IsWhitespace(*eleStart)) ++eleStart;

            auto* eleEnd = FastParseValue(MakeStringSection(eleStart, section._end), dest[elementCount]);
            if (eleStart == eleEnd) {
                if (outEleCount) *outEleCount = elementCount;
                return eleEnd;
            }
            ++elementCount;
            eleStart = eleEnd;
        }

        // skip forward over any trailing whitespace (which should bring us right to the end if the array ends in whitespace)
        while (eleStart < section._end && IsWhitespace(*eleStart)) ++eleStart;

        if (outEleCount) {
            // while there are remaining elements, we must count them...
            // we will return the correct number of elements, even if they don't
            // all fit in the destination array
            auto countingIterator = eleStart;
            Type temp;
            while (elementCount < destCount) {
                while (countingIterator < section._end && IsWhitespace(*countingIterator)) ++countingIterator;
                auto* eleEnd = FastParseValue(MakeStringSection(countingIterator, section._end), temp);
                if (countingIterator == eleEnd) break;
                ++elementCount;
                countingIterator = eleEnd;
            }
            *outEleCount = elementCount;
        }
        return eleStart;
    }

    template<typename Section>
        static std::string AsString(const Section& section)
    {
        using CharType = std::remove_const_t<std::remove_reference_t<decltype(*section._start)>>;
        return Conversion::Convert<std::string>(
            std::basic_string<CharType>(section._start, section._end));
    }

    template<typename Type>
        static Type Parse(const Formatters::XmlInputFormatter<utf8>::InteriorSection& section, const Type& def)
    {
            // ImpliedType::Parse is actually a fairly expensing parsing operation...
            // maybe we could get a faster result by just calling the standard library
            // type functions.
        // auto d = ImpliedTyping::Parse<Type>(section._start, section._end);
        // if (!d.first) return def;
        // return d.second;

        Type result;
        auto temp = FastParseValue(MakeStringSection(section._start, section._end), result);
        if (temp > section._start) return result;
        return def;
    }
}

namespace Formatters
{
    std::ostream& SerializationOperator(std::ostream& os, const StreamLocation& loc);
}

