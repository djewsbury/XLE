// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StringUtils.h"        // (for StringSection)

namespace Utility
{
    /*
        FastParseValue does essentially the same thing as std::from_chars, and should be replaced with
        that standard library function where it's available.
        However, it's not fully supported everywhere yet; so we need something to get us by

        The floating point versions are particularly important, but also particularly lagging in
        support. They also seem surprisingly difficult to implement with all of the edge cases.
        So the implementation here isn't actually 100%... but it works in at least some cases, and
        is quick. Just be careful with it -- because it's not guaranteed to be correct
    */
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst, unsigned radix);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst, unsigned radix);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst, unsigned radix);
    template<typename CharType> constexpr const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst, unsigned radix);
    template<typename CharType> const CharType* FastParseValue(StringSection<CharType> input, float& dst);
    template<typename CharType> const CharType* FastParseValue(StringSection<CharType> input, double& dst);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst)
    {
        bool positive = true;
        auto iterator = input.begin();

        if (iterator >= input.end()) return iterator;
        if (*iterator == '-') { positive = false; ++iterator; }
        else if (*iterator == '+') ++iterator;

        uint32_t result = 0;
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator < '0' || *iterator > '9') break;

            result = (result * 10) + uint32_t((*iterator) - '0');
            ++iterator;
        }
        if (iterator != input.begin())
            dst = positive ? result : -int32_t(result);
        return iterator;
    }
    
    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst)
    {
        bool positive = true;
        auto iterator = input.begin();

        if (iterator >= input.end()) return iterator;
        if (*iterator == '-') { positive = false; ++iterator; }
        else if (*iterator == '+') ++iterator;

        uint64_t result = 0;
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator < '0' || *iterator > '9') break;

            result = (result * 10ull) + uint64_t((*iterator) - '0');
            ++iterator;
        }
        if (iterator != input.begin())
            dst = positive ? result : -int64_t(result);
        return iterator;
    }

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst)
    {
        uint64_t result = 0;
        auto iterator = input.begin();
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator < '0' || *iterator > '9') break;

            result = (result * 10ull) + uint64_t((*iterator) - '0');
            ++iterator;
        }
        if (iterator != input.begin())
            dst = result;
        return iterator;
    }

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst)
    {
        uint32_t result = 0;
        auto iterator = input.begin();
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator < '0' || *iterator > '9') break;

            result = (result * 10u) + uint32_t((*iterator) - '0');
            ++iterator;
        }
        if (iterator != input.begin())
            dst = result;
        return iterator;
    }

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst, unsigned radix)
    {
        bool positive = true;
        auto iterator = input.begin();

        if (iterator >= input.end()) return iterator;
        if (*iterator == '-') { positive = false; ++iterator; }
        else if (*iterator == '+') ++iterator;

        uint32_t result = 0;
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator >= '0' && *iterator <= '9') {
                if (((*iterator) - '0') >= CharType(radix)) break;
                result = (result * radix) + uint32_t((*iterator) - '0');
            } else if (*iterator >= 'a' && *iterator <= CharType('a'+radix-11)) {
                result = (result * radix) + uint32_t((*iterator) - 'a' + 10);
            } else if (*iterator >= 'A' && *iterator <= CharType('A'+radix-11)) {
                result = (result * radix) + uint32_t((*iterator) - 'A' + 10);
            } else
                break;
            ++iterator;
        }
        if (iterator != input.begin())
            dst = positive ? result : -int32_t(result);
        return iterator;
    }
    
    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst, unsigned radix)
    {
        bool positive = true;
        auto iterator = input.begin();

        if (iterator >= input.end()) return iterator;
        if (*iterator == '-') { positive = false; ++iterator; }
        else if (*iterator == '+') ++iterator;

        uint64_t result = 0;
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator >= '0' && *iterator <= '9') {
                if (((*iterator) - '0') >= CharType(radix)) break;
                result = (result * radix) + uint64_t((*iterator) - '0');
            } else if (*iterator >= 'a' && *iterator <= CharType('a'+radix-11)) {
                result = (result * radix) + uint64_t((*iterator) - 'a' + 10);
            } else if (*iterator >= 'A' && *iterator <= CharType('A'+radix-11)) {
                result = (result * radix) + uint64_t((*iterator) - 'A' + 10);
            } else
                break;
            ++iterator;
        }
        if (iterator != input.begin())
            dst = positive ? result : -int64_t(result);
        return iterator;
    }

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst, unsigned radix)
    {
        uint64_t result = 0;
        auto iterator = input.begin();
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator >= '0' && *iterator <= '9') {
                if (((*iterator) - '0') >= CharType(radix)) break;
                result = (result * radix) + uint64_t((*iterator) - '0');
            } else if (*iterator >= 'a' && *iterator <= CharType('a'+radix-11)) {
                result = (result * radix) + uint64_t((*iterator) - 'a' + 10);
            } else if (*iterator >= 'A' && *iterator <= CharType('A'+radix-11)) {
                result = (result * radix) + uint64_t((*iterator) - 'A' + 10);
            } else
                break;
            ++iterator;
        }
        if (iterator != input.begin())
            dst = result;
        return iterator;
    }

    template<typename CharType>
        constexpr const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst, unsigned radix)
    {
        uint32_t result = 0;
        auto iterator = input.begin();
        for (;;) {
            if (iterator >= input.end()) break;
            if (*iterator >= '0' && *iterator <= '9') {
                if (((*iterator) - '0') >= CharType(radix)) break;
                result = (result * radix) + uint32_t((*iterator) - '0');
            } else if (*iterator >= 'a' && *iterator <= CharType('a'+radix-11)) {
                result = (result * radix) + uint32_t((*iterator) - 'a' + 10);
            } else if (*iterator >= 'A' && *iterator <= CharType('A'+radix-11)) {
                result = (result * radix) + uint32_t((*iterator) - 'A' + 10);
            } else
                break;
            ++iterator;
        }
        if (iterator != input.begin())
            dst = result;
        return iterator;
    }

}

using namespace Utility;
