// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FastParseValue.h"
#include "ArithmeticUtils.h"
#include <math.h>
#include <cmath>

namespace Utility
{
    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst)
    {
        bool positive = true;
        dst = 0;
        auto start = input.begin();

        if (start >= input.end()) return start;
        if (*start == '-') { positive = false; ++start; }
        else if (*start == '+') ++start;

        uint32_t result = 0;
        for (;;) {
            if (start >= input.end()) break;
            if (*start < '0' || *start > '9') break;

            result = (result * 10) + uint32_t((*start) - '0');
            ++start;
        }
        dst = positive ? result : -int32_t(result);
        return start;
    }
    
    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst)
    {
        bool positive = true;
        dst = 0;
        auto start = input.begin();

        if (start >= input.end()) return start;
        if (*start == '-') { positive = false; ++start; }
        else if (*start == '+') ++start;

        uint64_t result = 0;
        for (;;) {
            if (start >= input.end()) break;
            if (*start < '0' || *start > '9') break;

            result = (result * 10ull) + uint64_t((*start) - '0');
            ++start;
        }
        dst = positive ? result : -int64_t(result);
        return start;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst)
    {
        uint64_t result = 0;
        auto start = input.begin();
        for (;;) {
            if (start >= input.end()) break;
            if (*start < '0' || *start > '9') break;

            result = (result * 10ull) + uint64_t((*start) - '0');
            ++start;
        }
        dst = result;
        return start;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst)
    {
        uint32_t result = 0;
        auto start = input.begin();
        for (;;) {
            if (start >= input.end()) break;
            if (*start < '0' || *start > '9') break;

            result = (result * 10u) + uint32_t((*start) - '0');
            ++start;
        }
        dst = result;
        return start;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, int32_t& dst, unsigned radix)
    {
        bool positive = true;
        dst = 0;
        auto start = input.begin();

        if (start >= input.end()) return start;
        if (*start == '-') { positive = false; ++start; }
        else if (*start == '+') ++start;

        uint32_t result = 0;
        for (;;) {
            if (start >= input.end()) break;
            if (*start >= '0' && *start <= '9') {
                if (((*start) - '0') >= radix) break;
                result = (result * radix) + uint32_t((*start) - '0');
            } else if (*start >= 'a' && *start <= 'a'+radix-11) {
                result = (result * radix) + uint32_t((*start) - 'a' + 10);
            } else if (*start >= 'A' && *start <= 'A'+radix-11) {
                result = (result * radix) + uint32_t((*start) - 'A' + 10);
            } else
                break;
            ++start;
        }
        dst = positive ? result : -int32_t(result);
        return start;
    }
    
    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, int64_t& dst, unsigned radix)
    {
        bool positive = true;
        dst = 0;
        auto start = input.begin();

        if (start >= input.end()) return start;
        if (*start == '-') { positive = false; ++start; }
        else if (*start == '+') ++start;

        uint64_t result = 0;
        for (;;) {
            if (start >= input.end()) break;
            if (*start >= '0' && *start <= '9') {
                if (((*start) - '0') >= radix) break;
                result = (result * radix) + uint64_t((*start) - '0');
            } else if (*start >= 'a' && *start <= 'a'+radix-11) {
                result = (result * radix) + uint64_t((*start) - 'a' + 10);
            } else if (*start >= 'A' && *start <= 'A'+radix-11) {
                result = (result * radix) + uint64_t((*start) - 'A' + 10);
            } else
                break;
            ++start;
        }
        dst = positive ? result : -int64_t(result);
        return start;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, uint64_t& dst, unsigned radix)
    {
        uint64_t result = 0;
        auto start = input.begin();
        for (;;) {
            if (start >= input.end()) break;
            if (*start >= '0' && *start <= '9') {
                if (((*start) - '0') >= radix) break;
                result = (result * radix) + uint64_t((*start) - '0');
            } else if (*start >= 'a' && *start <= 'a'+radix-11) {
                result = (result * radix) + uint64_t((*start) - 'a' + 10);
            } else if (*start >= 'A' && *start <= 'A'+radix-11) {
                result = (result * radix) + uint64_t((*start) - 'A' + 10);
            } else
                break;
            ++start;
        }
        dst = result;
        return start;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, uint32_t& dst, unsigned radix)
    {
        uint32_t result = 0;
        auto start = input.begin();
        for (;;) {
            if (start >= input.end()) break;
            if (*start >= '0' && *start <= '9') {
                if (((*start) - '0') >= radix) break;
                result = (result * radix) + uint32_t((*start) - '0');
            } else if (*start >= 'a' && *start <= 'a'+radix-11) {
                result = (result * radix) + uint32_t((*start) - 'a' + 10);
            } else if (*start >= 'A' && *start <= 'A'+radix-11) {
                result = (result * radix) + uint32_t((*start) - 'A' + 10);
            } else
                break;
            ++start;
        }
        dst = result;
        return start;
    }

    template<typename Type, typename Type2>
        inline Type SignedRShift(Type input, Type2 shift)
        {
            return (shift < 0) ? (input << (-shift)) : (input >> shift);
        }

    template<typename CharType>
        bool IsWhitespace(CharType chr)
    {
        return chr == 0x20 || chr == 0x9 || chr == 0xD || chr == 0xA;
    }

    template<typename CharType>
        static const CharType* ExperimentalFloatParser(float& dst, const CharType* start, const CharType* end)
    {
            // This is an alternative to std::strtof designed to solve these problems:
            //      1) works on strings that aren't null terminated
            //      2) template CharType
            //      3) performance closer to strlen
            // But it's not complete! This is just a rough experimental implementation
            // to test performance. A correct implementation must be much more precise,
            // deal with subnormal numbers, and check for overflow/underflow conditions.
            // This implementation is very imprecise -- but at least it's quick.

        uint64_t beforePoint;
        uint64_t afterPoint;
        unsigned afterPointPrec;

        bool positive;
        auto iterator = start;
        if (*iterator == '-') { positive = false; ++iterator; } else { positive = true; }

        iterator = FastParseValue(MakeStringSection(iterator, end), beforePoint);
        if (iterator < end && *iterator=='.') {
            ++iterator;

                // some printf implementations will write special values in the form
                // "-1.#IND". We need to to at least detect these cases, and skip over
                // them. Maybe it's not critical to return the exact error type referenced.
            if (iterator < end && *iterator == '#') {
                ++iterator;
                while (iterator!=end && !IsWhitespace(*iterator)) ++iterator;
                dst = std::numeric_limits<float>::quiet_NaN();
                return iterator;
            }

            auto t = iterator;
            iterator = FastParseValue(MakeStringSection(iterator, end), afterPoint);
            afterPointPrec = unsigned(iterator - t);
        } else {
            afterPoint = 0;
            afterPointPrec = 0;
        }

        int64_t explicitExponent;
        if (iterator < end && (*iterator == 'e' || *iterator == 'E')) {
            ++iterator;
            iterator = FastParseValue(MakeStringSection(iterator, end), explicitExponent);
        } else
            explicitExponent = 0;

        if (iterator != end && !IsWhitespace(*iterator)) {
            // Simple parse failed ... We need to use standard library function
            char* newEnd = nullptr;
            dst = std::strtof((const char*)start, &newEnd);
            return (const CharType*)newEnd;
        }

        auto sigBits = 64ll - (int32)xl_clz8(beforePoint);
        auto shift = (int32)sigBits - 24ll;
        auto mantissa = SignedRShift(beforePoint, shift);
        auto exponent = shift+23;
        
        uint32_t result;
        if (beforePoint) {
            result = (((127+exponent) << 23) & 0x7F800000) | (mantissa & 0x7FFFFF);
        } else result = 0;

        if (afterPoint) {
            static std::tuple<int32, uint64_t, double> ExponentTable[32];
            static bool ExponentTableBuilt = false;
            int32 bias = 40;
            if (!ExponentTableBuilt) {
                for (unsigned c=0; c<dimof(ExponentTable); ++c) {
                    auto temp = std::log2(10.);
                    auto base2Exp = -double(c) * temp;
                    auto integerBase2Exp = std::ceil(base2Exp); // - .5);
                    auto fractBase2Exp = base2Exp - integerBase2Exp;
                    assert(fractBase2Exp <= 0.f);   // (std::powf(2.f, fractBase2Exp) must be smaller than 1 for precision reasons)
                    auto multiplier = uint64_t(std::exp2(fractBase2Exp + bias));

                    ExponentTable[c] = std::make_tuple(int32(integerBase2Exp), multiplier, fractBase2Exp);
                }
                ExponentTableBuilt = true;
            }

            const int32 idealBias = (int32)xl_clz8(afterPoint);

                // We must factor the fractional part of the exponent
                // into the mantissa (since the exponent must be an integer)
                // But we want to do this using integer math on the CPU, so
                // that we can get the maximum precision. Double precision
                // FPU math is accurate enough, but single precision isn't.
                // ideally we should do it completely on the CPU.
            uint64_t rawMantissaT;
            if (idealBias < bias) {
                auto multiplier = uint64_t(std::exp2(std::get<2>(ExponentTable[afterPointPrec]) + bias));
                rawMantissaT = afterPoint * multiplier;
                bias = idealBias;
            } else {
                rawMantissaT = afterPoint * std::get<1>(ExponentTable[afterPointPrec]);
            }
            auto sigBitsT = 64ll - (int32)xl_clz8(rawMantissaT);
            auto shiftT = (int32)sigBitsT - 24ll;

            auto expForFractionalPart = int32(std::get<0>(ExponentTable[afterPointPrec])+23+shiftT-bias);

                // note --  No rounding performed! Just truncating the part of the number that
                //          doesn't fit into our precision.
            if (!beforePoint) {
                auto mantissaT = SignedRShift(rawMantissaT, shiftT);
                result = 
                      (((127+expForFractionalPart) << 23) & 0x7F800000) 
                    | (mantissaT & 0x7FFFFF);
            } else {
                assert((shiftT + exponent - expForFractionalPart) >= 0);
                result |= (rawMantissaT >> (shiftT + exponent - expForFractionalPart)) & 0x7FFFFF;
            }
        }

        dst = *reinterpret_cast<float*>(&result);

            // Explicit exponent isn't handled well... We just multiplying with the FPU
            // it's simple, but it may not be the most accurate method. We
        if (explicitExponent)
            dst *= std::pow(10.f, float(explicitExponent));

        if (!positive) dst = -dst;

        #if defined(_DEBUG)
            float compare = strtof((const char*)start, nullptr);
            auto t0 = *(uint32_t*)&compare;
            auto t1 = *(uint32_t*)&dst;
            const uint32_t expectedAccuracy = explicitExponent ? 4 : 1;
            assert((t0-t1) <= expectedAccuracy || (t1-t0) <= expectedAccuracy);
        #endif

        return iterator;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, float& dst)
    {
        // this code found on stack exchange...
        //      (http://stackoverflow.com/questions/98586/where-can-i-find-the-worlds-fastest-atof-implementation)
        // But there are some problems!
        // Most importantly:
        //      Sub-normal numbers are not handled properly. Subnormal numbers happen when the exponent
        //      is the smallest is can be. In this case, values in the mantissa are evenly spaced around
        //      zero. 
        //
        // It does other things right. But I don't think it's reliable enough to use. It's a pity because
        // the standard library functions require null terminated strings, and seems that it may be possible
        // to get a big performance improvement loss of few features.

            // to avoid making a copy, we're going do a hack and 
            // We're assuming that "end" is writable memory. But this won't be the case if
            // we're reading from a memory mapped file (opened for read only)!
            // Also, consider that there might be threading implications in some cases!
            // HACK -- let's just assume that we're not going to end a file with a floating
            // point number, and ignore the deliminator case.
            // Also note that this won't work with character types larger than a 1 byte!
        static_assert(sizeof(CharType) == 1, "Support for long character types not implemented");

        const CharType* newEnd = nullptr;
        const bool UseExperimentalFloatParser = true;
        if (constant_expression<UseExperimentalFloatParser>::result()) {
            newEnd = ExperimentalFloatParser(dst, input.begin(), input.end());
        } else {
            // CharType replaced = *end;
            // *const_cast<CharType*>(end) = '\0';
            char* newEndT = nullptr;
            dst = std::strtof((const char*)input.begin(), &newEndT);        // note -- there is a risk of running off the string, beyond "end" in some cases!
            // *const_cast<CharType*>(end) = replaced;
            newEnd = (const CharType*)newEndT;
        }

        assert(newEnd <= input.end());
        return newEnd;
    }

    template<typename CharType>
        const CharType* FastParseValue(StringSection<CharType> input, double& dst)
    {
        assert(0);      // not implemented
        return input.begin();
    }

    template const utf8* FastParseValue(StringSection<utf8> input, int32_t& dst);
    template const utf8* FastParseValue(StringSection<utf8> input, int64_t& dst);
    template const utf8* FastParseValue(StringSection<utf8> input, uint64_t& dst);
    template const utf8* FastParseValue(StringSection<utf8> input, uint32_t& dst);
    template const utf8* FastParseValue(StringSection<utf8> input, int32_t& dst, unsigned);
    template const utf8* FastParseValue(StringSection<utf8> input, int64_t& dst, unsigned);
    template const utf8* FastParseValue(StringSection<utf8> input, uint64_t& dst, unsigned);
    template const utf8* FastParseValue(StringSection<utf8> input, uint32_t& dst, unsigned);
    template const utf8* FastParseValue(StringSection<utf8> input, float& dst);
    template const utf8* FastParseValue(StringSection<utf8> input, double& dst);
}
