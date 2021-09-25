// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ImpliedTyping.h"
#include "Conversion.h"
#include "FastParseValue.h"
#include "../Core/Types.h"
#include <sstream>
#include <charconv>

#if defined(_DEBUG)
    // #define VERIFY_NEW_IMPLEMENTATION
#endif

#if defined(VERIFY_NEW_IMPLEMENTATION)
    #include <regex>
#endif

namespace Utility { namespace ImpliedTyping
{
    uint32_t TypeDesc::GetSize() const
    {
        switch (_type) {
        case TypeCat::Bool: return sizeof(bool)*unsigned(_arrayCount);

        case TypeCat::Int8:
        case TypeCat::UInt8: return sizeof(uint8_t)*unsigned(_arrayCount);

        case TypeCat::Int16:
        case TypeCat::UInt16: return sizeof(uint16_t)*unsigned(_arrayCount);

        case TypeCat::Int32:
        case TypeCat::UInt32:
        case TypeCat::Float: return sizeof(uint32_t)*unsigned(_arrayCount);

        case TypeCat::Int64:
        case TypeCat::UInt64:
        case TypeCat::Double: return sizeof(uint64_t)*unsigned(_arrayCount);

        case TypeCat::Void:
        default: return 0;
        }
    }

    bool operator==(const TypeDesc& lhs, const TypeDesc& rhs)
    {
            // (note -- ignoring type hint for this comparison (because the hint isn't actually related to the structure of the data)
        return lhs._type == rhs._type
            && lhs._arrayCount == rhs._arrayCount;
    }

    TypeDesc TypeOf(const char expression[]) 
    {
            // not implemented
        assert(0);
        return TypeDesc{};
    }

    bool Cast(
        IteratorRange<void*> dest, TypeDesc destType,
        IteratorRange<const void*> rawSrc, TypeDesc srcType)
    {
        assert(rawSrc.size() >= srcType.GetSize());
        assert(dest.size() >= destType.GetSize());
        IteratorRange<const void*> src = rawSrc;
        if (destType._arrayCount <= 1) {
#if defined(__arm__) && !defined(__aarch64__)
            // Only 32-bit ARM, we may get unaligned access reading directly from rawSrc
            // Therefore, we check if it is 4-byte unaligned and copy if so

            // Setup the stack buffer if we need it
            // Right now maximum size of a type is 8 bytes, adjust if necessary
            const size_t MAXIMUM_SRC_SIZE = 8;
            const size_t srcSize = srcType.GetSize();
            assert(srcSize > 0);
            assert(srcSize <= MAXIMUM_SRC_SIZE);
            uint8_t srcBuffer[MAXIMUM_SRC_SIZE];

            // Check if unaligned
            if (uintptr_t(rawSrc.begin()) & 3u) {
                // If unaligned, copy to the srcBuffer (memcpy is safe to do unaligned access)
                memcpy(&srcBuffer[0], src.begin(), srcSize);
                // Set src to the srcBuffer
                src = { &srcBuffer[0], &srcBuffer[srcSize] };
            }
#endif
                // casting single element. Will we read the first element
                // of the 
            switch (destType._type) {
            case TypeCat::Bool:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(bool*)dest.begin() = *(bool*)src.begin(); return true;
                    case TypeCat::Int8: *(bool*)dest.begin() = !!*(int8_t*)src.begin(); return true;
                    case TypeCat::UInt8: *(bool*)dest.begin() = !!*(uint8_t*)src.begin(); return true;
                    case TypeCat::Int16: *(bool*)dest.begin() = !!*(int16_t*)src.begin(); return true;
                    case TypeCat::UInt16: *(bool*)dest.begin() = !!*(uint16_t*)src.begin(); return true;
                    case TypeCat::Int32: *(bool*)dest.begin() = !!*(int32_t*)src.begin(); return true;
                    case TypeCat::UInt32: *(bool*)dest.begin() = !!*(uint32_t*)src.begin(); return true;
                    case TypeCat::Int64: *(bool*)dest.begin() = !!*(int64_t*)src.begin(); return true;
                    case TypeCat::UInt64: *(bool*)dest.begin() = !!*(uint64_t*)src.begin(); return true;
                    case TypeCat::Float: *(bool*)dest.begin() = !!*(float*)src.begin(); return true;
                    case TypeCat::Double: *(bool*)dest.begin() = !!*(double*)src.begin(); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::Int8:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(int8_t*)dest.begin() = int8_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(int8_t*)dest.begin() = int8_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(int8_t*)dest.begin() = int8_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(int8_t*)dest.begin() = int8_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(int8_t*)dest.begin() = int8_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(int8_t*)dest.begin() = int8_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(int8_t*)dest.begin() = int8_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(int8_t*)dest.begin() = int8_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(int8_t*)dest.begin() = int8_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(int8_t*)dest.begin() = int8_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(int8_t*)dest.begin() = int8_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::UInt8:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(uint8_t*)dest.begin() = uint8_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(uint8_t*)dest.begin() = uint8_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(uint8_t*)dest.begin() = uint8_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(uint8_t*)dest.begin() = uint8_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(uint8_t*)dest.begin() = uint8_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(uint8_t*)dest.begin() = uint8_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(uint8_t*)dest.begin() = uint8_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(uint8_t*)dest.begin() = uint8_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(uint8_t*)dest.begin() = uint8_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(uint8_t*)dest.begin() = uint8_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(uint8_t*)dest.begin() = uint8_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;
            
            case TypeCat::Int16:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(int16_t*)dest.begin() = int16_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(int16_t*)dest.begin() = int16_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(int16_t*)dest.begin() = int16_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(int16_t*)dest.begin() = int16_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(int16_t*)dest.begin() = int16_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(int16_t*)dest.begin() = int16_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(int16_t*)dest.begin() = int16_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(int16_t*)dest.begin() = int16_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(int16_t*)dest.begin() = int16_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(int16_t*)dest.begin() = int16_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(int16_t*)dest.begin() = int16_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::UInt16:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(uint16_t*)dest.begin() = uint16_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(uint16_t*)dest.begin() = uint16_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(uint16_t*)dest.begin() = uint16_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(uint16_t*)dest.begin() = uint16_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(uint16_t*)dest.begin() = uint16_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(uint16_t*)dest.begin() = uint16_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(uint16_t*)dest.begin() = uint16_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(uint16_t*)dest.begin() = uint16_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(uint16_t*)dest.begin() = uint16_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(uint16_t*)dest.begin() = uint16_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(uint16_t*)dest.begin() = uint16_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;
            
            case TypeCat::Int32:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(int32_t*)dest.begin() = int32_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(int32_t*)dest.begin() = int32_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(int32_t*)dest.begin() = int32_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(int32_t*)dest.begin() = int32_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(int32_t*)dest.begin() = int32_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(int32_t*)dest.begin() = *(int32_t*)src.begin(); return true;
                    case TypeCat::UInt32: *(int32_t*)dest.begin() = int32_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(int32_t*)dest.begin() = int32_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(int32_t*)dest.begin() = int32_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(int32_t*)dest.begin() = int32_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(int32_t*)dest.begin() = int32_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::UInt32:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(uint32_t*)dest.begin() = uint32_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(uint32_t*)dest.begin() = uint32_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(uint32_t*)dest.begin() = uint32_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(uint32_t*)dest.begin() = uint32_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(uint32_t*)dest.begin() = uint32_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(uint32_t*)dest.begin() = uint32_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(uint32_t*)dest.begin() = *(uint32_t*)src.begin(); return true;
                    case TypeCat::Int64: *(uint32_t*)dest.begin() = uint32_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(uint32_t*)dest.begin() = uint32_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(uint32_t*)dest.begin() = uint32_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(uint32_t*)dest.begin() = uint32_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::Int64:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(int64_t*)dest.begin() = int64_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(int64_t*)dest.begin() = int64_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(int64_t*)dest.begin() = int64_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(int64_t*)dest.begin() = int64_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(int64_t*)dest.begin() = int64_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(int64_t*)dest.begin() = int64_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(int64_t*)dest.begin() = int64_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(int64_t*)dest.begin() = int64_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(int64_t*)dest.begin() = int64_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(int64_t*)dest.begin() = int64_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(int64_t*)dest.begin() = int64_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::UInt64:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(uint64_t*)dest.begin() = uint64_t(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(uint64_t*)dest.begin() = uint64_t(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(uint64_t*)dest.begin() = uint64_t(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(uint64_t*)dest.begin() = uint64_t(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(uint64_t*)dest.begin() = uint64_t(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(uint64_t*)dest.begin() = uint64_t(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(uint64_t*)dest.begin() = uint64_t(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(uint64_t*)dest.begin() = uint64_t(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(uint64_t*)dest.begin() = uint64_t(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(uint64_t*)dest.begin() = uint64_t(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(uint64_t*)dest.begin() = uint64_t(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::Float:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(float*)dest.begin() = float(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(float*)dest.begin() = float(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(float*)dest.begin() = float(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(float*)dest.begin() = float(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(float*)dest.begin() = float(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(float*)dest.begin() = float(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(float*)dest.begin() = float(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(float*)dest.begin() = float(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(float*)dest.begin() = float(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(float*)dest.begin() = float(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(float*)dest.begin() = float(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;

            case TypeCat::Double:
                {
                    switch (srcType._type) {
                    case TypeCat::Bool: *(double*)dest.begin() = double(*(bool*)src.begin()); return true;
                    case TypeCat::Int8: *(double*)dest.begin() = double(*(int8_t*)src.begin()); return true;
                    case TypeCat::UInt8: *(double*)dest.begin() = double(*(uint8_t*)src.begin()); return true;
                    case TypeCat::Int16: *(double*)dest.begin() = double(*(int16_t*)src.begin()); return true;
                    case TypeCat::UInt16: *(double*)dest.begin() = double(*(uint16_t*)src.begin()); return true;
                    case TypeCat::Int32: *(double*)dest.begin() = double(*(int32_t*)src.begin()); return true;
                    case TypeCat::UInt32: *(double*)dest.begin() = double(*(uint32_t*)src.begin()); return true;
                    case TypeCat::Int64: *(double*)dest.begin() = double(*(int64_t*)src.begin()); return true;
                    case TypeCat::UInt64: *(double*)dest.begin() = double(*(uint64_t*)src.begin()); return true;
                    case TypeCat::Float: *(double*)dest.begin() = double(*(float*)src.begin()); return true;
                    case TypeCat::Double: *(double*)dest.begin() = double(*(double*)src.begin()); return true;
                    case TypeCat::Void: break;
                    }
                }
                break;
                    
            case TypeCat::Void: break;
            }
        } else {

                // multiple array elements. We might need to remap elements
                // First -- trival cases can be completed with a memcpy
            if (    srcType._arrayCount == destType._arrayCount
                &&  srcType._type == destType._type) {
                std::memcpy(dest.begin(), src.begin(), std::min(dest.size(), size_t(srcType.GetSize())));
                return true;
            }
                
            auto destIterator = dest;
            auto srcIterator = src;
            for (unsigned c=0; c<destType._arrayCount; ++c) {
                if (destIterator.size() < TypeDesc{destType._type}.GetSize()) {
                    return false;
                }
                if (c < srcType._arrayCount) {
                    if (!Cast(destIterator, TypeDesc{destType._type},
                        srcIterator, TypeDesc{srcType._type})) {
                        return false;
                    }

                    destIterator.first = PtrAdd(destIterator.first, TypeDesc{destType._type}.GetSize());
                    srcIterator.first = PtrAdd(srcIterator.first, TypeDesc{srcType._type}.GetSize());
                } else {
                        // using HLSL rules for filling in blanks:
                        //  element 3 is 1, but others are 0
                    unsigned value = (c==3)?1:0;
                    if (!Cast(destIterator, TypeDesc{destType._type},
                        MakeOpaqueIteratorRange(value), TypeDesc{TypeCat::UInt32})) {
                        return false;
                    }
                    destIterator.first = PtrAdd(destIterator.first, TypeDesc{destType._type}.GetSize());
                }
            }
            return true;
        }

        return false;
    }
    
    
    CastType CalculateCastType(TypeCat testType, TypeCat againstType) 
    {
        // todo -- "uint64_t" and "float" should get widened to "double"

        if (testType == againstType)
            return CastType::Equal;
        
        bool isWidening = false;
        switch (againstType) {
            case TypeCat::Bool:
            case TypeCat::UInt8:
            case TypeCat::UInt16:
            case TypeCat::UInt32:
            case TypeCat::UInt64:
                isWidening = (testType == TypeCat::Bool ||
                        testType == TypeCat::UInt8 ||
                        testType == TypeCat::UInt16 ||
                        testType == TypeCat::UInt32 ||
                        testType == TypeCat::UInt64) && (testType < againstType);
                break;
            case TypeCat::Float:
            case TypeCat::Double:
                isWidening = testType < againstType;
                break;
            case TypeCat::Int8:
                isWidening = testType <= TypeCat::UInt8;
                break;
            case TypeCat::Int16:
                isWidening = testType <= TypeCat::UInt16;
                break;
            case TypeCat::Int32:
                isWidening = testType <= TypeCat::UInt32;
                break;
            case TypeCat::Int64:
                isWidening = testType <= TypeCat::UInt64;
                break;
            default:
                assert(false); // Unknown type
                isWidening = false;
                break;
        }
        
        return isWidening ? CastType::Widening : CastType::Narrowing;
    }
    

    // test -- widening element at different locations within an array

    template<typename CharType>
        bool IsTokenBreak(CharType c) { return !( (c>='0' && c<='9') || (c>='A' && c<='Z') || (c>='a' && c<'z') ); };

    template<typename CharType>
        ParseResult<CharType> Parse(
            StringSection<CharType> expression,
            IteratorRange<void*> dest)
    {
        auto* begin = expression.begin();
        while (begin != expression.end() && (*begin == ' ' || *begin == '\t')) ++begin;
        if (begin == expression.end()) return {begin};

        auto firstChar = *begin;
        bool negate = false;
        unsigned integerBase = 10;

        unsigned boolCandidateLength = 0;
        bool boolValue = false;
        switch (firstChar) {
        case 't':
        case 'T':
            if (XlBeginsWith(expression, "true") || XlBeginsWith(expression, "True") || XlBeginsWith(expression, "TRUE")) {
                boolValue = true;
                boolCandidateLength = 4;
            }
            goto finalizeBoolCandidate;

        case 'y':
        case 'Y':
            if (XlBeginsWith(expression, "yes") || XlBeginsWith(expression, "Yes") || XlBeginsWith(expression, "YES")) {
                boolCandidateLength = 3;
                boolValue = true;
            } else {
                boolCandidateLength = 1;
                boolValue = true;
            }
            goto finalizeBoolCandidate;

        case 'f':
        case 'F':
            if (XlEqString(expression, "false") || XlEqString(expression, "False") || XlEqString(expression, "FALSE")) {
                boolValue = false;
                boolCandidateLength = 5;
            }
            goto finalizeBoolCandidate;

        case 'n':
        case 'N':
            if (XlEqString(expression, "no") || XlEqString(expression, "No") || XlEqString(expression, "NO")) {
                boolCandidateLength = 2;
                boolValue = false;
            } else {
                boolCandidateLength = 1;
                boolValue = false;
            }
            goto finalizeBoolCandidate;

        finalizeBoolCandidate:
            if (boolCandidateLength && ((expression.begin() + boolCandidateLength) == expression.end() || IsTokenBreak(*(expression.begin()+boolCandidateLength)))) {
                assert(dest.size() >= sizeof(bool));
                *(bool*)dest.begin() = boolValue;
                return { expression.begin() + boolCandidateLength, TypeDesc{TypeCat::Bool} };
            }
            return { expression.begin() };      // looks a little like a bool, but ultimately failed parse

        case '-':
            ++begin;
            negate = true;
            
        case '0':
            if ((begin+1) < expression.end() && *(begin+1) == 'x') {
                integerBase = 16;
                begin += 2;
            }
            // intentional fall-through to below

        case '.':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            {
                uint64_t value = 0;
                auto fcr = std::from_chars(begin, expression.end(), value, integerBase);
                // ignore fcr.ec, because it will be a failure condition for things like ".5f"

                if (fcr.ptr < expression.end() && (*fcr.ptr == '.' || *fcr.ptr == 'e' || *fcr.ptr == 'f' || *fcr.ptr == 'F')) {
                    // this might be a floating point number
                    // scan forward to try to find a precision specifier
                    // Note that this won't work correctly for special values written in the form "-1.#IND", etc
                    unsigned precision = 32;
                    while (fcr.ptr < expression.end() && ((*fcr.ptr >= '0' && *fcr.ptr <= '9') || *fcr.ptr == 'e' || *fcr.ptr == 'E' || *fcr.ptr == '+' || *fcr.ptr == '-' || *fcr.ptr == '.'))
                        ++fcr.ptr;

                    auto* endOfNumber = fcr.ptr;
                    bool precisionSpecifierFound = false;
                    if (fcr.ptr != expression.end() && (*fcr.ptr == 'f' || *fcr.ptr == 'F')) {
                        precisionSpecifierFound = true;
                        ++fcr.ptr;
                        if (fcr.ptr != expression.end()) {
                            fcr = std::from_chars(fcr.ptr, expression.end(), precision);
                            bool endsOnATokenBreak = fcr.ptr == expression.end() || IsTokenBreak(*fcr.ptr);
                            if (!endsOnATokenBreak || (precision != 32 && precision != 64)) {
                                assert(0);  // unknown precision
                                return { expression.begin() };
                            }
                        }
                    }

                    // Note that we reset back to the start of expression for the from_chars() below -- potentially meaning
                    // parsing over the same ground again
                    if (precision == 32) {
                        assert(dest.size() >= sizeof(f32));
                        auto a = FastParseValue(MakeStringSection(expression.begin(), endOfNumber), *(f32*)dest.begin());
                        if (a != endOfNumber)
                            return { expression.begin() }; // we didn't actually parse over everything we expected to read
                        return { fcr.ptr, TypeDesc{TypeCat::Float} };
                    } else {
                        assert(precision == 64);
                        assert(dest.size() >= sizeof(f64));
                        auto a = FastParseValue(MakeStringSection(expression.begin(), endOfNumber), *(f64*)dest.begin());
                        if (a != endOfNumber)
                            return { expression.begin() }; // we didn't actually parse over everything we expected to read
                        return { fcr.ptr, TypeDesc{TypeCat::Double} };
                    }
                } else {
                    // Didn't match a floating point number, try to match integer
                    //
                    // due to two's complement, everything should work out regardless of the precision and whether the 
                    // final result is signed or unsigned
                    if (negate) 
                        value = -value;

                    unsigned precision = 32;
                    bool isUnsigned = !negate;

                    if (fcr.ptr < expression.end() &&
                            (*fcr.ptr == 'u' || *fcr.ptr == 'U'
                        ||  *fcr.ptr == 'i' || *fcr.ptr == 'I')) {
                        
                        if (*fcr.ptr == 'u' || *fcr.ptr == 'U') {
                            isUnsigned = true;
                        } else 
                            isUnsigned = false;
                        ++fcr.ptr;

                        // if the from_chars fails here, we will just keep the default precision
                        // that's ok so long as we still end up on a token break
                        fcr = std::from_chars(fcr.ptr, expression.end(), precision);
                    }

                    if (fcr.ptr != expression.end() && !IsTokenBreak(*fcr.ptr))
                        return { expression.begin() };      // did not end on a token break

                    if (precision == 8) {
                        assert(dest.size() >= sizeof(uint8_t));
                        *(uint8_t*)dest.begin() = (uint8_t)value;
                        return { fcr.ptr, TypeDesc{isUnsigned ? TypeCat::UInt8 : TypeCat::Int8} };
                    } else if (precision == 16) {
                        assert(dest.size() >= sizeof(uint16_t));
                        *(uint16_t*)dest.begin() = (uint16_t)value;
                        return { fcr.ptr, TypeDesc{isUnsigned ? TypeCat::UInt16 : TypeCat::Int16} };
                    } else if (precision == 32) {
                        assert(dest.size() >= sizeof(uint32_t));
                        *(uint32_t*)dest.begin() = (uint32_t)value;
                        return { fcr.ptr, TypeDesc{isUnsigned ? TypeCat::UInt32 : TypeCat::Int32} };
                    } else if (precision == 64) {
                        assert(dest.size() >= sizeof(uint64_t));
                        *(uint64_t*)dest.begin() = (uint64_t)value;
                        return { fcr.ptr, TypeDesc{isUnsigned ? TypeCat::UInt64 : TypeCat::Int64} };
                    } else {
                        // assert(0);  // unknown precision, even though the integer itself parsed correctly
                        return { expression.begin() };
                    }
                }
            }
            break;

        case '{':
            {
                auto i = begin;
                ++i; // past '{'

                struct Element
                {
                    StringSection<CharType> _section;
                    IteratorRange<const void*> _valueInDest;
                    TypeCat _type;
                };
                std::vector<Element> elements;
                elements.reserve(8);
                bool needCastPass = false;
                TypeCat widestArrayType = TypeCat::Void;

                auto dstIterator = dest.begin();
                auto dstIteratorSize = ptrdiff_t(dest.size());

                bool needSeparator = false;
                for (;;) {
                    while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;

                    if (i == expression.end()) {
                        // hit the end of the array without a proper terminator
                        return { expression.begin() };
                    }

                    if (*i == '}')  {
                        ++i;
                        break;      // good terminator
                    }

                    if (needSeparator) {
                        if (*i != ',')
                            return { expression.begin() };
                        ++i;
                        while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;
                    }

                    auto currentElementBegin = i;

                    {
                        auto subType = Parse(MakeStringSection(currentElementBegin, expression.end()), MakeIteratorRange(dstIterator, PtrAdd(dstIterator, dstIteratorSize)));

                        Element newElement;
                        newElement._section = MakeStringSection(currentElementBegin, subType._end);
                        if (newElement._section.IsEmpty())
                            return { expression.begin() };      // failed parse while reading element

                        assert(subType._type._arrayCount <= 1);
                        assert(subType._type._type != TypeCat::Void);

                        auto size = subType._type.GetSize();
                        newElement._valueInDest = MakeIteratorRange(dstIterator, PtrAdd(dstIterator, size));
                        newElement._type = subType._type._type;

                        if (widestArrayType != TypeCat::Void) {
                            auto castType = CalculateCastType(subType._type._type, widestArrayType);
                            if (castType == CastType::Widening) {
                                // We know we will have to widen this type. 
                                // If we haven't already scheduled a full cast of the entire array,
                                // let's just go ahead and widen it now.
                                // Otherwise, if we are going to do a cast pass; it doesn't matter,
                                // it's going to be fixed up at the end either way
                                // still, we can end up queuing a full cast pass afterwards, which 
                                // might cause a second cast
                                if (!needCastPass) {
                                    // note cast in place here!
                                    auto newSize = TypeDesc{widestArrayType}.GetSize();
                                    assert(dstIteratorSize >= newSize);
                                    bool castSuccess = Cast(
                                        { dstIterator, PtrAdd(dstIterator, std::min((ptrdiff_t)newSize, dstIteratorSize)) }, TypeDesc{widestArrayType}, 
                                        { dstIterator, PtrAdd(dstIterator, size) }, subType._type);
                                    assert(castSuccess);
                                    (void)castSuccess;

                                    newElement._type = widestArrayType;
                                    size = newSize;
                                }
                            } else if (castType == CastType::Narrowing) {
                                widestArrayType = subType._type._type;
                                needCastPass = true;
                            } else {
                                assert(TypeDesc{subType._type}.GetSize() >= TypeDesc{widestArrayType}.GetSize());
                            }
                        } else {
                            widestArrayType = subType._type._type;
                        }

                        elements.push_back(newElement);
                        dstIterator = PtrAdd(dstIterator, size);
                        dstIteratorSize -= size;
                        i = subType._end;
                    }

                    needSeparator = true;
                }

                // Since all of the elements of an array must be the same type, we can't be sure 
                // what type that will be until we've discovered the types of all of the elements
                // Essentially we will try to promote each type until we find the type which is the
                // "most promoted" or "widest", and that will become the type for all elements in
                // the array. 
                // However it means we need to do another pass right now to ensure that all of the
                // elements get promoted to our final type
                if (needCastPass) {
                    auto finalElementSize = TypeDesc{widestArrayType}.GetSize();
                    const size_t cpySize = size_t(dstIterator) - ptrdiff_t(dest.begin());
                    std::unique_ptr<uint8_t[]> tempCpy = std::make_unique<uint8_t[]>(cpySize);
                    std::memcpy(tempCpy.get(), dest.begin(), cpySize);
                    
                    dstIterator = dest.begin();
                    dstIteratorSize = ptrdiff_t(dest.size());
                    for (const auto&e:elements) {
                        auto srcInCpyArray = MakeIteratorRange(
                            tempCpy.get() + (ptrdiff_t)e._valueInDest.begin() - (ptrdiff_t)dest.begin(),
                            tempCpy.get() + (ptrdiff_t)e._valueInDest.end() - (ptrdiff_t)dest.begin());
                        bool castSuccess = Cast(
                            { dstIterator, PtrAdd(dstIterator, finalElementSize) }, TypeDesc{widestArrayType}, 
                            srcInCpyArray, TypeDesc{e._type});
                        assert(castSuccess);
                        (void)castSuccess;
                        
                        dstIterator = PtrAdd(dstIterator, finalElementSize);
                        dstIteratorSize -= finalElementSize;
                    }
                }

                // check for trailing 'v' or 'c'
                auto hint = TypeHint::None;
                if (i != expression.end() && (*i == 'v' || *i == 'V')) { hint = TypeHint::Vector; ++i; }
                else if (i != expression.end() && (*i == 'c' || *i == 'C')) { hint = TypeHint::Color; ++i; }

                return { i, TypeDesc{widestArrayType, uint16_t(elements.size()), hint} };
            }
            break;

        default:
            break;
        }

        return {expression.begin()};
    }

    #if defined(VERIFY_NEW_IMPLEMENTATION)
        template<typename CharType>
            TypeDesc Parse_OldImplementation(
                StringSection<CharType> expression,
                void* dest, size_t destSize);
    #endif

    template<typename CharType>
        TypeDesc ParseFullMatch(
            StringSection<CharType> expression,
            IteratorRange<void*> destinationBuffer)
    {
        auto parse = Parse(expression, destinationBuffer);

        #if defined(VERIFY_NEW_IMPLEMENTATION)
            {
                uint8_t testBuffer[destSize];
                auto verifyType = Parse_OldImplementation(expression, testBuffer, destSize);
                if (parse._end == expression.end()) {
                    assert(verifyType == parse._type);
                    assert(memcmp(dest, testBuffer, verifyType.GetSize()) == 0);
                } else {
                    assert(verifyType._type == TypeCat::Void);
                }
            }
        #endif

        while (parse._end != expression.end() && (*parse._end == ' ' || *parse._end == '\t')) ++parse._end;
        if (parse._end == expression.end())
            return parse._type;
        return { TypeCat::Void };
    }

    template<typename CharType>
        const CharType* FastParseBool(
            StringSection<CharType> expression,
            bool& destination)
    {
        if (expression.IsEmpty()) return expression.begin();

        unsigned boolCandidateLength = 0;
        bool boolValue = false;
        switch (*expression.begin()) {
        case 't':
        case 'T':
            if (XlBeginsWith(expression, "true") || XlBeginsWith(expression, "True") || XlBeginsWith(expression, "TRUE")) {
                boolValue = true;
                boolCandidateLength = 4;
            }
            goto finalizeBoolCandidate;

        case 'y':
        case 'Y':
            if (XlBeginsWith(expression, "yes") || XlBeginsWith(expression, "Yes") || XlBeginsWith(expression, "YES")) {
                boolCandidateLength = 3;
                boolValue = true;
            } else {
                boolCandidateLength = 1;
                boolValue = true;
            }
            goto finalizeBoolCandidate;

        case 'f':
        case 'F':
            if (XlEqString(expression, "false") || XlEqString(expression, "False") || XlEqString(expression, "FALSE")) {
                boolValue = false;
                boolCandidateLength = 5;
            }
            goto finalizeBoolCandidate;

        case 'n':
        case 'N':
            if (XlEqString(expression, "no") || XlEqString(expression, "No") || XlEqString(expression, "NO")) {
                boolCandidateLength = 2;
                boolValue = false;
            } else {
                boolCandidateLength = 1;
                boolValue = false;
            }
            goto finalizeBoolCandidate;

        default:
            return expression.begin();

        finalizeBoolCandidate:
            // we always require a token break after the bool here. This avoids odd situations like "nothing" begin considered a partial match against "no"
            // when calling Convert(...)
            if (boolCandidateLength && ((expression.begin() + boolCandidateLength) == expression.end() || IsTokenBreak(*(expression.begin()+boolCandidateLength)))) {
                destination = boolValue;
                return expression.begin() + boolCandidateLength;
            }
            return expression.begin();      // looks a little like a bool, but ultimately failed parse
        }
    }

    template<typename CharType>
        static bool IsIntegerTrailer(CharType chr) { return chr == 'u' || chr == 'U' || chr == 'i' || chr == 'I' || chr == 'f' || chr == 'F'; }

    template<typename CharType>
        static bool IsArrayTrailer(CharType chr) { return chr == 'v' || chr == 'V' || chr == 'c' || chr == 'C'; }

    template<typename DestinationType, typename CharType>
        ConvertResult<CharType> ConvertSignedIntegerHelper(StringSection<CharType> expression, DestinationType& destBuffer)
    {
        int64_t i64;
        auto parseEnd = FastParseValue(expression, i64);

        if (parseEnd != expression.end() && (*parseEnd == '.' || *parseEnd == 'e')) {
            // This may actually a floating point number
            double d;
            parseEnd = FastParseValue(expression, d);
            if (parseEnd != expression.begin())
                i64 = int64_t(d); // fall through
        } else if (parseEnd != expression.end() && (*parseEnd == 'x') && i64 == 0) {
            // this was actually a "0x" hex prefix
            if ((parseEnd+1) == expression.end() || *(parseEnd+1) == '+'  || *(parseEnd+1) == '-')      // can't follow this with either empty string, + or -
                return {expression.begin(), false};
            parseEnd = FastParseValue(MakeStringSection(parseEnd+1, expression.end()), i64, 16);
            if (*expression.begin() == '-')      // "-0x" is still possible
                i64 = -i64;
        }

        if (parseEnd != expression.begin()) {
            //if (i64 > std::numeric_limits<DestinationType>::max() || i64 < std::numeric_limits<DestinationType>::min())
                //return {parseEnd, false};       // overflow / underflow
            if (parseEnd != expression.end() && IsIntegerTrailer(*parseEnd)) ++parseEnd;
            destBuffer = i64;
            return {parseEnd, true};
        } else {

            // attempt bool to integer version
            bool b;
            parseEnd = FastParseBool(expression, b);
            if (parseEnd != expression.begin()) {
                destBuffer = DestinationType(b);
                return {parseEnd, true};
            }

            return {parseEnd, false};
        }
    }

    template<typename DestinationType, typename CharType>
        ConvertResult<CharType> ConvertUnsignedIntegerHelper(StringSection<CharType> expression, DestinationType& destBuffer)
    {
        uint64_t ui64;
        auto parseEnd = FastParseValue(expression, ui64);

        if (parseEnd != expression.end() && (*parseEnd == '.' || *parseEnd == 'e')) {
            // This may actually a floating point number
            double d;
            parseEnd = FastParseValue(expression, d);
            if (parseEnd != expression.begin())
                ui64 = uint64_t(d); // fall through
        } else if (parseEnd != expression.end() && (*parseEnd == '-')) {
            // this could be a negative number read as unsigned
            int64_t i64;
            parseEnd = FastParseValue(expression, i64);
            if (parseEnd != expression.begin())
                ui64 = uint64_t(i64); // fall through
        } else if (parseEnd != expression.end() && (*parseEnd == 'x') && ui64 == 0) {
            // this was actually a "0x" hex prefix
            if ((parseEnd+1) == expression.end() || *(parseEnd+1) == '+'  || *(parseEnd+1) == '-')      // can't follow this with either empty string, + or -
                return {expression.begin(), false};
            parseEnd = FastParseValue(MakeStringSection(parseEnd+1, expression.end()), ui64, 16);
            if (*expression.begin() == '-')      // "-0x" is still possible
                ui64 = -int64_t(ui64);
        }

        if (parseEnd != expression.begin()) {
            //if (ui64 > std::numeric_limits<DestinationType>::max())
                //return {parseEnd, false};       // overflow / underflow
            if (parseEnd != expression.end() && IsIntegerTrailer(*parseEnd)) ++parseEnd;
            destBuffer = ui64;
            return {parseEnd, true};
        } else {

            // attempt bool to integer version
            bool b;
            parseEnd = FastParseBool(expression, b);
            if (parseEnd != expression.begin()) {
                destBuffer = DestinationType(b);
                return {parseEnd, true};
            }

            return {parseEnd, false};
        }
    }

    template<typename CharType>
        ConvertResult<CharType> Convert(
            StringSection<CharType> expression,
            IteratorRange<void*> destinationBuffer,
            const TypeDesc& destinationType)
    {
        assert(destinationBuffer.size() >= destinationType.GetSize());
        assert(destinationType._arrayCount != 0);

        auto i = expression.begin();
        while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;
        if (!expression.size()) return {expression.begin(), false};
        if (*i == '{') {
            i++;  // past '{'

            auto elementType = destinationType;
            elementType._arrayCount = 1;
            unsigned elementsRead = 0;

            auto destinationBufferIterator = destinationBuffer;
            bool needSeparator = false;
            for (;;) {
                while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;
                if (i == expression.end())
                    return {expression.begin(), false};

                if (*i == '}')  {
                    return {expression.begin(), false};      // hit terminator at an unexpected time (empty array not supported)
                }

                if (needSeparator) {
                    if (*i != ',')
                        return { expression.begin() };
                    ++i;
                    while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;
                }

                if (destinationBufferIterator.size() < elementType.GetSize())
                    return {expression.begin(), false};     // too many elements to fit in the destination buffer

                auto elementConversion = Convert(MakeStringSection(i, expression.end()), destinationBufferIterator, elementType);
                if (elementConversion._successfulConvert) {
                    i = elementConversion._end;
                    destinationBufferIterator.first = PtrAdd(destinationBufferIterator.first, elementType.GetSize());
                    ++elementsRead;
                    if (elementsRead == destinationType._arrayCount) {
                        // we need a good terminator to follow
                        while (i < expression.end() && (*i == ' '|| *i == '\t')) ++i;
                        if (i == expression.end() || *i != '}')
                            return {expression.begin(), false};     // no terminator
                        ++i;
                        if (i != expression.end() && IsArrayTrailer(*i)) ++i;
                        return {i, true};
                    }

                    needSeparator = true;
                } else {
                    return {expression.begin(), false};  // element couldn't be understood
                }
            }
        } else {
            if (destinationType._arrayCount > 1)
                return {i, false};

            if (destinationType._type == TypeCat::Void)
                return {i, true};

            const CharType* parseEnd = nullptr;
            int32_t i32; uint32_t ui32;
            int64_t i64; uint64_t ui64;
            switch (destinationType._type) {
            case TypeCat::Bool:
                {
                    uint8_t midway;
                    auto res = ConvertUnsignedIntegerHelper(MakeStringSection(i, expression.end()), midway);
                    if (res._successfulConvert)
                        *(bool*)destinationBuffer.begin() = midway;
                    return res;
                }

            case TypeCat::Int8:
                return ConvertSignedIntegerHelper<int8_t>(MakeStringSection(i, expression.end()), *(int8_t*)destinationBuffer.begin());

            case TypeCat::UInt8:
                return ConvertUnsignedIntegerHelper<uint8_t>(MakeStringSection(i, expression.end()), *(uint8_t*)destinationBuffer.begin());

            case TypeCat::Int16:
                return ConvertSignedIntegerHelper<int16_t>(MakeStringSection(i, expression.end()), *(int16_t*)destinationBuffer.begin());

            case TypeCat::UInt16:
                return ConvertUnsignedIntegerHelper<uint16_t>(MakeStringSection(i, expression.end()), *(uint16_t*)destinationBuffer.begin());
                
            case TypeCat::Int32:
                return ConvertSignedIntegerHelper<int32_t>(MakeStringSection(i, expression.end()), *(int32_t*)destinationBuffer.begin());

            case TypeCat::UInt32:
                return ConvertUnsignedIntegerHelper<uint32_t>(MakeStringSection(i, expression.end()), *(uint32_t*)destinationBuffer.begin());
                
            case TypeCat::Int64:
                return ConvertSignedIntegerHelper<int64_t>(MakeStringSection(i, expression.end()), *(int64_t*)destinationBuffer.begin());

            case TypeCat::UInt64:
                return ConvertUnsignedIntegerHelper<uint64_t>(MakeStringSection(i, expression.end()), *(uint64_t*)destinationBuffer.begin());

            case TypeCat::Float:
                {
                    float f;
                    parseEnd = FastParseValue(MakeStringSection(i, expression.end()), f);
                    if (parseEnd != i) {
                        *(float*)destinationBuffer.begin() = f;
                        if (parseEnd != destinationBuffer.end() && (*parseEnd == 'f' || *parseEnd == 'F')) ++parseEnd;
                        return {parseEnd, true};
                    } else {
                        return {parseEnd, false};
                    }
                }

            case TypeCat::Double:
                {
                    double d;
                    parseEnd = FastParseValue(MakeStringSection(i, expression.end()), d);
                    if (parseEnd != i) {
                        *(double*)destinationBuffer.begin() = d;
                        if (parseEnd != destinationBuffer.end() && (*parseEnd == 'f' || *parseEnd == 'F')) ++parseEnd;
                        return {parseEnd, true};
                    } else {
                        return {parseEnd, false};
                    }
                }

            default:
                assert(0);
                return { parseEnd, false };
            }
        }
    }

    template<typename CharType>
        bool ConvertFullMatch(
            StringSection<CharType> expression,
            IteratorRange<void*> destinationBuffer,
            const TypeDesc& destinationType)
    {
        auto parse = Convert(expression, destinationBuffer, destinationType);
        while (parse._end != expression.end() && (*parse._end == ' ' || *parse._end == '\t')) ++parse._end;
        return parse._successfulConvert && (parse._end == expression.end());
    }

    std::string AsString(IteratorRange<const void*> data, const TypeDesc& desc, bool strongTyping)
    {
        if (desc._typeHint == TypeHint::String) {
            if (desc._type == TypeCat::UInt8 || desc._type == TypeCat::Int8) {
                return std::string((const char*)data.begin(), (const char*)PtrAdd(data.begin(), desc._arrayCount * sizeof(char)));
            }
            if (desc._type == TypeCat::UInt16 || desc._type == TypeCat::Int16) {
                return Conversion::Convert<std::string>(std::basic_string<utf16>((const utf16*)data.begin(), (const utf16*)PtrAdd(data.begin(), desc._arrayCount * sizeof(utf16))));
            }
        }

        std::stringstream result;
        assert(data.size() >= desc.GetSize());
        auto arrayCount = unsigned(desc._arrayCount);
        if (arrayCount > 1) result << "{";

        for (auto i=0u; i<arrayCount; ++i) {
            if (i!=0) result << ", ";

            if (strongTyping) {
                switch (desc._type) {
                case TypeCat::Bool:     if (*(bool*)data.begin()) { result << "true"; } else { result << "false"; }; break;
                case TypeCat::Int8:     result << (int32_t)*(int8_t*)data.begin() << "i8"; break;
                case TypeCat::UInt8:    result << (uint32_t)*(uint8_t*)data.begin() << "u8"; break;
                case TypeCat::Int16:    result << *(int16_t*)data.begin() << "i16"; break;
                case TypeCat::UInt16:   result << *(uint16_t*)data.begin() << "u16"; break;
                case TypeCat::Int32:    result << *(int32_t*)data.begin() << "i"; break;
                case TypeCat::UInt32:   result << *(uint32_t*)data.begin() << "u"; break;
                case TypeCat::Int64:    result << *(int64_t*)data.begin() << "i64"; break;
                case TypeCat::UInt64:   result << *(uint64_t*)data.begin() << "u64"; break;
                case TypeCat::Float:    result << *(float*)data.begin() << "f"; break;
                case TypeCat::Double:   result << *(double*)data.begin() << "f64"; break;
                case TypeCat::Void:     result << ""; break;
                default:                result << "<<error>>"; break;
                }
            } else {
                switch (desc._type) {
                case TypeCat::Bool:     result << *(bool*)data.begin(); break;
                case TypeCat::Int8:     result << (int32_t)*(int8_t*)data.begin(); break;
                case TypeCat::UInt8:    result << (uint32_t)*(uint8_t*)data.begin(); break;
                case TypeCat::Int16:    result << *(int16_t*)data.begin(); break;
                case TypeCat::UInt16:   result << *(uint16_t*)data.begin(); break;
                case TypeCat::Int32:    result << *(int32_t*)data.begin(); break;
                case TypeCat::UInt32:   result << *(uint32_t*)data.begin(); break;
                case TypeCat::Int64:    result << *(int64_t*)data.begin(); break;
                case TypeCat::UInt64:   result << *(uint64_t*)data.begin(); break;
                case TypeCat::Float:    result << *(float*)data.begin(); break;
                case TypeCat::Double:   result << *(double*)data.begin(); break;
                case TypeCat::Void:     result << ""; break;
                default:                result << "<<error>>"; break;
                }
            }

                // skip forward one element
            data.first = PtrAdd(data.begin(), TypeDesc{desc._type}.GetSize());
        }

        if (arrayCount > 1) {
            result << "}";
            switch (desc._typeHint) {
            case TypeHint::Color: result << "c"; break;
            case TypeHint::Vector: result << "v"; break;
            default: break;
            }
        }

        return result.str();
    }

    template TypeDesc ParseFullMatch(StringSection<utf8> expression, IteratorRange<void*> dest);
    template ParseResult<utf8> Parse(StringSection<utf8> expression, IteratorRange<void*> dest);

    template bool ConvertFullMatch(StringSection<utf8> expression, IteratorRange<void*>, const TypeDesc&);
    template ConvertResult<utf8> Convert(StringSection<utf8> expression, IteratorRange<void*>, const TypeDesc&);

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(VERIFY_NEW_IMPLEMENTATION)
    template<typename CharType>
        class ParsingRegex
    {
    public:
        std::basic_regex<CharType> s_booleanTrue;
        std::basic_regex<CharType> s_booleanFalse;
        std::basic_regex<CharType> s_unsignedPattern;
        std::basic_regex<CharType> s_signedPattern;
        std::basic_regex<CharType> s_floatPattern;
        std::basic_regex<CharType> s_arrayPattern;
        std::basic_regex<CharType> s_arrayElementPattern;
    
        ParsingRegex()
        : s_booleanTrue((const CharType*)R"(^(true)|(True)|(y)|(Y)|(yes)|(Yes)|(TRUE)|(Y)|(YES)$)")
        , s_booleanFalse((const CharType*)R"(^(false)|(False)|(n)|(N)|(no)|(No)|(FALSE)|(N)|(NO)$)")
        , s_unsignedPattern((const CharType*)R"(^\+?(([\d]+)|(0x[\da-fA-F]+))(u|U|(u8)|(u16)|(u32)|(u64)|(U8)|(U16)|(U32)|(U64))?$)")
        , s_signedPattern((const CharType*)R"(^[-\+]?(([\d]+)|(0x[\da-fA-F]+))(i|I|(i8)|(i16)|(i32)|(i64)|(I8)|(I16)|(I32)|(I64))?$)")
        , s_floatPattern((const CharType*)R"(^[-\+]?(([\d]*\.?[\d]+)|([\d]+\.))([eE][-\+]?[\d]+)?(f|F|(f32)|(F32)|(f64)|(F64))?$)")
        , s_arrayPattern((const CharType*)R"(\{\s*([^,\s]+(?:\s*,\s*[^,\s]+)*)\s*\}([vcVC]?))")
        , s_arrayElementPattern((const CharType*)R"(\s*([^,\s]+)\s*(?:,|$))")
        {}
        ~ParsingRegex() {}
    };
    
    static std::unique_ptr<ParsingRegex<char>> s_parsingChar;

    void Cleanup()
    {
        s_parsingChar.reset();
    }

    template<typename CharType>
        TypeDesc Parse_OldImplementation(
            StringSection<CharType> expression,
            void* dest, size_t destSize)
    {
        if (!s_parsingChar) {
            s_parsingChar = std::make_unique<ParsingRegex<char>>();
        }
            // parse string expression into native types.
            // We'll write the native object into _tthe buffer and return a type desc
        if (std::regex_match(expression.begin(), expression.end(), s_parsingChar->s_booleanTrue)) {
            assert(destSize >= sizeof(bool));
            *(bool*)dest = true;
            return TypeDesc{TypeCat::Bool};
        } else if (std::regex_match(expression.begin(), expression.end(), s_parsingChar->s_booleanFalse)) {
            assert(destSize >= sizeof(bool));
            *(bool*)dest = false;
            return TypeDesc{TypeCat::Bool};
        }

        std::match_results<const CharType*> cm; 
        if (std::regex_match(expression.begin(), expression.end(), s_parsingChar->s_unsignedPattern)) {
            unsigned precision = 32;
            if (cm.size() >= 4 && cm[4].length() > 1)
                precision = XlAtoUI32(&cm[2].str()[1]);

            uint64_t value;
            auto len = expression.size();
            if (len > 2 && (expression.begin()[0] == '0' && expression.begin()[1] == 'x')) {
                    // hex form
                value = XlAtoUI64(&expression.begin()[2], nullptr, 16);
            } else {
                value = XlAtoUI64(expression.begin());
            }

            if (precision == 8) {
                assert(destSize >= sizeof(uint8_t));
                *(uint8_t*)dest = (uint8_t)value;
                return TypeDesc{TypeCat::UInt8};
            } else if (precision == 16) {
                assert(destSize >= sizeof(uint16_t));
                *(uint16_t*)dest = (uint16_t)value;
                return TypeDesc{TypeCat::UInt16};
            } else if (precision == 32) {
                assert(destSize >= sizeof(uint32_t));
                *(uint32_t*)dest = (uint32_t)value;
                return TypeDesc{TypeCat::UInt32};
            } else if (precision == 64) {
                assert(destSize >= sizeof(uint64_t));
                *(uint64_t*)dest = (uint64_t)value;
                return TypeDesc{TypeCat::UInt64};
            }

            assert(0);
        }

        if (std::regex_match(expression.begin(), expression.end(), cm, s_parsingChar->s_signedPattern)) {
            unsigned precision = 32;
            if (cm.size() >= 4 && cm[4].length() > 1)
                precision = XlAtoUI32(&cm[2].str()[1]);

            int64_t value;
            auto len = expression.end() - expression.begin();
            if (len > 2 && (expression.begin()[0] == '0' && expression.begin()[1] == 'x')) {
                    // hex form
                value = XlAtoI64(&expression.begin()[2], nullptr, 16);
            } else {
                value = XlAtoI64(expression.begin());
            }

            if (precision == 8) {
                assert(destSize >= sizeof(int8_t));
                *(int8_t*)dest = (int8_t)value;
                return TypeDesc{TypeCat::Int8};
            } else if (precision == 16) {
                assert(destSize >= sizeof(int16_t));
                *(int16_t*)dest = (int16_t)value;
                return TypeDesc{TypeCat::Int16};
            } else if (precision == 32) {
                assert(destSize >= sizeof(int32_t));
                *(int32_t*)dest = (int32_t)value;
                return TypeDesc{TypeCat::Int32};
            } else if (precision == 64) {
                assert(destSize >= sizeof(int64_t));
                *(int64_t*)dest = (int64_t)value;
                return TypeDesc{TypeCat::Int64};
            }

            assert(0);
        }

        if (std::regex_match(expression.begin(), expression.end(), s_parsingChar->s_floatPattern)) {
            bool doublePrecision = false;
            if (cm.size() >= 4 && cm[4].length() > 1) {
                auto precision = XlAtoUI32(&cm[2].str()[1]);
                doublePrecision = precision == 64;
            }

            if (doublePrecision) {
                assert(destSize >= sizeof(double));
                *(double*)dest = Conversion::Convert<double>(expression);
                return TypeDesc{TypeCat::Double};
            } else {
                assert(destSize >= sizeof(float));
                *(float*)dest = Conversion::Convert<float>(expression);
                return TypeDesc{TypeCat::Float};
            }
        }

        {
                // match for float array:
            // R"(^\{\s*[-\+]?(([\d]*\.?[\d]+)|([\d]+\.))([eE][-\+]?[\d]+)?[fF]\s*(\s*,\s*([-\+]?(([\d]*\.?[\d]+)|([\d]+\.))([eE][-\+]?[\d]+)?[fF]))*\s*\}[vc]?$)"

            // std::match_results<typename std::basic_string<CharType>::const_iterator> cm;
            if (std::regex_match(expression.begin(), expression.end(), cm, s_parsingChar->s_arrayPattern)) {
                const auto& subMatch = cm[1];
                std::regex_iterator<const CharType*> rit(
                    subMatch.first, subMatch.second,
                    s_parsingChar->s_arrayElementPattern);
                std::regex_iterator<const CharType*> rend;
                
                auto startIt = rit;

                auto dstIterator = dest;
                auto dstIteratorSize = ptrdiff_t(destSize);

                TypeCat cat = TypeCat::Void;
                unsigned count = 0;
                if (rit != rend) {
                    assert(rit->size() >= 1);
                    const auto& eleMatch = (*rit)[1];

                    auto subType = Parse_OldImplementation(
                        MakeStringSection(eleMatch.first, eleMatch.second),
                        dstIterator, dstIteratorSize);

                    assert(subType._arrayCount <= 1);
                    assert(subType._type != TypeCat::Void);
                    cat = subType._type;

                    auto size = subType.GetSize();
                    dstIterator = PtrAdd(dstIterator, size);
                    dstIteratorSize -= size;
                    ++rit;
                    ++count;
                }

                assert(cat != TypeCat::Void);
                for (;rit != rend; ++rit) {
                    assert(rit->size() >= 1);
                    const auto& eleMatch = (*rit)[1];

                    auto subType = Parse_OldImplementation(
                        MakeStringSection(eleMatch.first, eleMatch.second),
                        dstIterator, dstIteratorSize);

                    if (CalculateCastType(subType._type, cat) != CastType::Narrowing) {
                        bool castSuccess = Cast(   
                            { dstIterator, PtrAdd(dstIterator, TypeDesc{cat}.GetSize()) }, TypeDesc{cat},
                            { dstIterator, PtrAdd(dstIterator, subType.GetSize()) }, subType);
                        (void)castSuccess;
                        subType._type = cat;
                    } else {
                        // If the cast would narrow the type, we would corrupt the input
                        // Therefore, instead we modify the type we are reading and cast
                        // the previously read values to the new type
                        assert(CalculateCastType(cat, subType._type) == CastType::Widening);
                        const auto catType = TypeDesc{cat};
                        const size_t cpySize = catType.GetSize() * count;
                        std::unique_ptr<uint8_t[]> tempCpy = std::make_unique<uint8_t[]>(cpySize);
                        auto tempCpyIterator = tempCpy.get();
                        std::memcpy(tempCpy.get(), dest, cpySize);
                        
                        dstIterator = dest;
                        assert(ptrdiff_t(destSize) - dstIteratorSize == ptrdiff_t(cpySize));
                        dstIteratorSize = ptrdiff_t(destSize);
                        for (auto redoIt = startIt; redoIt != rit; ++redoIt) {
                            bool castSuccess = Cast(
                                { dstIterator, PtrAdd(dstIterator, subType.GetSize()) }, subType, 
                                { tempCpyIterator, PtrAdd(tempCpyIterator, catType.GetSize()) }, catType);
                            assert(castSuccess);
                            (void)castSuccess;
                            
                            dstIterator = PtrAdd(dstIterator, subType.GetSize());
                            dstIteratorSize -= subType.GetSize();
                            
                            tempCpyIterator = tempCpyIterator + catType.GetSize();
                        }
                        
                        cat = subType._type;
                    }

                    assert(subType._arrayCount <= 1);
                    dstIterator = PtrAdd(dstIterator, subType.GetSize());
                    dstIteratorSize -= subType.GetSize();
                    ++count;
                }

                TypeHint hint = TypeHint::None;
                if (cm.size() >= 2 && cm[2].length() >= 1) {
                    if (tolower(cm[2].str()[0]) == 'v') hint = TypeHint::Vector;
                    if (tolower(cm[2].str()[0]) == 'c') hint = TypeHint::Color;
                }

                return TypeDesc{cat, uint16_t(count), hint};
            }
        }

        return TypeDesc{TypeCat::Void};
    }
#endif

}}

