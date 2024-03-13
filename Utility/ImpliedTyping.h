// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "StringUtils.h"            // for StringSection
#include "Optional.h"
#include "../Core/Exceptions.h"

namespace Utility
{
    namespace ImpliedTyping
    {
        enum class TypeCat : uint8_t { Void, Bool, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float, Double };
        enum class TypeHint : uint8_t { None, Vector, Matrix, Color, String };
        enum class CastType : uint8_t { Narrowing, Equal, Widening};
        class alignas(uint64_t) TypeDesc
        {
        public:
            TypeCat     _type = TypeCat::UInt32;
            TypeHint    _typeHint = TypeHint::None;
            uint32_t    _arrayCount = 1;

            constexpr uint32_t GetSize() const;
            constexpr TypeDesc() = default;
            constexpr TypeDesc(TypeCat typeCat, uint32_t arrayCount = 1, TypeHint typeHint = TypeHint::None) : _type(typeCat), _typeHint(typeHint), _arrayCount(arrayCount) {}

            template<typename Stream> void SerializeMethod(Stream& serializer) const;
            friend constexpr bool operator==(const TypeDesc& lhs, const TypeDesc& rhs);
        };

        // Object should be formatted in one of the following C++ like patterns:
        // 
        //  "1u" (or "1ui" or "1ul", etc)
        //  "1b" (or "true" or "false")
        //  ".3" (or "0.3f", etc)
        //  "{1u, 2u, 3u}" (or "[1u, 2u, 3u]")
        //  "{1u, 2u, 3u}c" or "{1u, 2u, 3u}v"
        //
        // This is intended for storing common basic types in text files, and 
        // for use while entering data in tools. We want the type of the data to
        // be implied by the string representing the data (without needing an
        // extra field to describe the type).
        //
        // This kind of thing is useful when interfacing with scripting languages
        // like HLSL and Lua. There are only a few basic types that we need
        // to support.
        //
        // But sometimes we also want to had hints for out to interpret the data.
        // For example, 3 floats could be a vector or a colour. We will use C++
        // like postfix characters for this (eg, "{1,1,1}c" is a color)

        // Two similar breeds of functions below:
        //      Parse / ParseFullMatch
        //      Convert / ConvertFullMatch
        //
        // Parse does not take a type as a parameter and parses the string into
        // its "implied type" -- in other words, the type that is implied by the string
        // itself.
        //
        // Convert does take a type as a parameter, and attempts to convert the
        // value in the string to that type. It will try to do this as efficiently as
        // possible (ie, it's better than a Parse() followed by a Cast())

        template<typename CharType>
            struct ParseResult
        {
            const CharType*     _end = nullptr;
            TypeDesc            _type = {TypeCat::Void};
        };
        
        template<typename CharType>
            ParseResult<CharType> Parse(
                StringSection<CharType> expression,
                IteratorRange<void*> destinationBuffer);
                
        template<typename CharType>
            TypeDesc ParseFullMatch(
                StringSection<CharType> expression,
                IteratorRange<void*> destinationBuffer);



        template<typename CharType>
            struct ConvertResult
        {
            const CharType*     _end = nullptr;
            bool                _successfulConvert = false;
        };

        template<typename CharType>
             ConvertResult<CharType> Convert(
                StringSection<CharType> expression,
                IteratorRange<void*> destinationBuffer,
                const TypeDesc& destinationType);

        template<typename CharType>
             bool ConvertFullMatch(
                StringSection<CharType> expression,
                IteratorRange<void*> destinationBuffer,
                const TypeDesc& destinationType);

        template <typename Type>
            const char* Convert(StringSection<> expression, Type& destination);
            
        template <typename Type>
            std::optional<Type> ConvertFullMatch(StringSection<> expression);



        std::string AsString(IteratorRange<const void*> data, const TypeDesc&, bool strongTyping = false);

        template<typename Type>
            inline std::string AsString(const Type& type, bool strongTyping = false);

        const char* AsString(TypeCat);


        // note -- Cast() never does string parsing, even if src is a string and the dst is not
        bool Cast(
            IteratorRange<void*> dest, TypeDesc destType,
            IteratorRange<const void*> src, TypeDesc srcType);

        /// <summary>Similar to Cast(), but flips the endian of "src" before converting into the type for "dest"</summary>
        ///
        /// Similar to calling FlipEndian() and then Cast(), but can be more efficient when done in one go.
        ///
        /// Note that endian flipping happens on values of type "srcType". If you need to flip in the endian after converting
        /// to destType (eg, for an output option), call Cast() first to convert the types, then call FlipEndian()
        bool Cast_FlipEndian(
            IteratorRange<void*> dest, TypeDesc destType,
            IteratorRange<const void*> src, TypeDesc srcType);

        CastType CalculateCastType(TypeCat testType, TypeCat againstType);

        void FlipEndian(IteratorRange<void*> output, const void* src, const TypeDesc& type);

        struct VariantNonRetained
        {
            TypeDesc _type = TypeCat::Void;
            IteratorRange<const void*> _data;
            bool _reversedEndian = false;

            // convert into the destination type, with special case handling to-and-from strings
            template<typename DestType>
                DestType RequireCastValue() const;

            template<typename DestType>
                std::optional<DestType> TryCastValue() const;
        };
        TypeDesc TryBinaryOperator(
            IteratorRange<void*> dst,
            StringSection<> op,
            const VariantNonRetained& lhs,
            const VariantNonRetained& rhs);
        TypeDesc TryUnaryOperator(
            IteratorRange<void*> dst,
            StringSection<> op,
            const VariantNonRetained& operand);

        struct VariantRetained
        {
            TypeDesc _type = TypeCat::Void;
            char _smallBuffer[2*sizeof(uint64_t)];
            std::vector<uint8_t> _largeBuffer;
            bool _reverseEndian = false;

            operator VariantNonRetained() const;

            template<typename DestType>
                DestType RequireCastValue() const;

            template<typename DestType>
                std::optional<DestType> TryCastValue() const;

            template<typename SrcType>
                VariantRetained(SrcType);
            VariantRetained(TypeDesc, IteratorRange<const void*>, bool reverseEndian=false);
            VariantRetained(const VariantNonRetained&);
            VariantRetained() = default;
        };

        //////////////////////////////////////////////////////////////////////////////////////
            // Template implementations //
        //////////////////////////////////////////////////////////////////////////////////////
        constexpr TypeDesc InternalTypeOf(uint64_t const*)        { return TypeDesc{TypeCat::UInt64}; }
        constexpr TypeDesc InternalTypeOf(int64_t const*)         { return TypeDesc{TypeCat::Int64}; }
        constexpr TypeDesc InternalTypeOf(uint32_t const*)        { return TypeDesc{TypeCat::UInt32}; }
        constexpr TypeDesc InternalTypeOf(int32_t const*)         { return TypeDesc{TypeCat::Int32}; }
        constexpr TypeDesc InternalTypeOf(uint16_t const*)        { return TypeDesc{TypeCat::UInt16}; }
        constexpr TypeDesc InternalTypeOf(int16_t const*)         { return TypeDesc{TypeCat::Int16}; }
        constexpr TypeDesc InternalTypeOf(uint8_t const*)         { return TypeDesc{TypeCat::UInt8}; }
        constexpr TypeDesc InternalTypeOf(int8_t const*)          { return TypeDesc{TypeCat::Int8}; }
        constexpr TypeDesc InternalTypeOf(char const*)            { return TypeDesc{TypeCat::UInt8}; }
        constexpr TypeDesc InternalTypeOf(char16_t const*)        { return TypeDesc{TypeCat::UInt16}; }
        constexpr TypeDesc InternalTypeOf(char32_t const*)        { return TypeDesc{TypeCat::UInt32}; }
        constexpr TypeDesc InternalTypeOf(bool const*)            { return TypeDesc{TypeCat::Bool}; }
        constexpr TypeDesc InternalTypeOf(float const*)           { return TypeDesc{TypeCat::Float}; }
        constexpr TypeDesc InternalTypeOf(double const*)          { return TypeDesc{TypeCat::Double}; }

        constexpr TypeDesc InternalTypeOf(const utf8* const*)     { return TypeDesc{TypeCat::UInt8, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const utf16* const*)    { return TypeDesc{TypeCat::UInt16, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const utf32* const*)    { return TypeDesc{TypeCat::UInt32, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const ucs4* const*)     { return TypeDesc{TypeCat::UInt32, (uint32_t)~uint32_t(0), TypeHint::String}; }

        constexpr TypeDesc InternalTypeOf(const std::basic_string<utf8>*)     { return TypeDesc{TypeCat::UInt8, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const std::basic_string<utf16>*)    { return TypeDesc{TypeCat::UInt16, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const std::basic_string<utf32>*)    { return TypeDesc{TypeCat::UInt32, (uint32_t)~uint32_t(0), TypeHint::String}; }
        constexpr TypeDesc InternalTypeOf(const std::basic_string<ucs4>*)     { return TypeDesc{TypeCat::UInt32, (uint32_t)~uint32_t(0), TypeHint::String}; }

        template<typename Type> 
            constexpr decltype(InternalTypeOf(std::declval<Type const*>())) TypeOf() { return InternalTypeOf((Type const*)nullptr); }

        constexpr uint32_t TypeDesc::GetSize() const
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

        constexpr bool operator==(const TypeDesc& lhs, const TypeDesc& rhs)
        {
                // (note -- ignoring type hint for this comparison (because the hint isn't actually related to the structure of the data)
            return lhs._type == rhs._type
                && lhs._arrayCount == rhs._arrayCount;
        }

        template<typename Type>
            inline std::string AsString(const Type& type, bool strongTyping)
            {
                return AsString(MakeOpaqueIteratorRange(type), TypeOf<Type>(), strongTyping);
            }

        template <typename Type> std::optional<Type> ConvertFullMatch(StringSection<> expression) 
        {
            Type casted;
            if (ConvertFullMatch(expression, MakeOpaqueIteratorRange(casted), TypeOf<Type>()))
                return casted;
            return {};
        }

        template<typename Stream>
            void TypeDesc::SerializeMethod(Stream& serializer) const
        {
            static_assert(sizeof(TypeDesc) == sizeof(uint64_t));
            SerializationOperator(serializer, *(uint64_t*)this);
        }

        struct ReversedEndianHelper
        {
            IteratorRange<void*> _reversedData;
            ReversedEndianHelper(IteratorRange<const void*> srcData, const TypeDesc& type)
            {
                const unsigned stackSizeLimit = 1024;
                if (type.GetSize() <= stackSizeLimit) {
                    _reversedData.first = _alloca(type.GetSize());
                    _reversedData.second = PtrAdd(_reversedData.first, type.GetSize());
                } else {
                    _buffer.resize(type.GetSize());
                    _reversedData = MakeIteratorRange(_buffer);
                }
                assert(type.GetSize() == srcData.size());
                FlipEndian(_reversedData, srcData.begin(), type);
            }
        private:
            std::vector<uint8_t> _buffer;
        };

        template<typename DestType>
            DestType VariantNonRetained::RequireCastValue() const
        {
            bool srcIsString = ((_type._type == TypeCat::Int8) || (_type._type == TypeCat::UInt8)) && _type._typeHint == TypeHint::String;
            if constexpr (std::is_same_v<std::decay_t<DestType>, std::string>) {
                if (srcIsString) {
                    std::string result;
                    result.resize(std::min((size_t)_type._arrayCount, _data.size()));
                    std::memcpy(result.data(), _data.begin(), result.size());
                    return result;
                } else {
                    // note that void just becomes an empty string
                    if (_reversedEndian && _type._type > ImpliedTyping::TypeCat::UInt8) {
                        ReversedEndianHelper helper { _data, _type };
                        return AsString(helper._reversedData, _type);
                    } else
                        return AsString(_data, _type);
                }
            } else {
                if (srcIsString) {
                    DestType casted;
                    auto str = MakeStringSection((const char*)_data.begin(), (const char*)_data.end());
                    if (ConvertFullMatch(str, MakeOpaqueIteratorRange(casted), TypeOf<DestType>()))
                        return casted;
                    Throw(std::runtime_error("Could not interpret (" + str.AsString() + ") as " + typeid(DestType).name()));
                } else {
                    if (_type._type == TypeCat::Void)
                        Throw(std::runtime_error("Attempting to read void value in VariantNonRetained"));

                    if (_reversedEndian && _type._type > ImpliedTyping::TypeCat::UInt8) {
                        ReversedEndianHelper helper { _data, _type };
                        DestType result;
                        if (!Cast(MakeOpaqueIteratorRange(result), TypeOf<DestType>(), helper._reversedData, _type))
                            Throw(std::runtime_error(std::string{"Failed casting to "} + typeid(DestType).name()));
                        return result;
                    } else {
                        DestType result;
                        if (!Cast(MakeOpaqueIteratorRange(result), TypeOf<DestType>(), _data, _type))
                            Throw(std::runtime_error(std::string{"Failed casting to "} + typeid(DestType).name()));
                        return result;
                    }
                }
            }
        }

        template<typename DestType>
            std::optional<DestType> VariantNonRetained::TryCastValue() const
        {
            if (_type._type == TypeCat::Void)
                return {};

            bool srcIsString = ((_type._type == TypeCat::Int8) || (_type._type == TypeCat::UInt8)) && _type._typeHint == TypeHint::String;
            if constexpr (std::is_same_v<std::decay_t<DestType>, std::string>) {
                if (srcIsString) {
                    std::string result;
                    result.resize(std::min((size_t)_type._arrayCount, _data.size()));
                    std::memcpy(result.data(), _data.begin(), result.size());
                    return result;
                } else {
                    if (_reversedEndian && _type._type > ImpliedTyping::TypeCat::UInt8) {
                        ReversedEndianHelper helper { _data, _type };
                        return AsString(helper._reversedData, _type);
                    } else
                        return AsString(_data, _type);
                }
            } else {
                if (srcIsString) {
                    DestType casted;
                    auto str = MakeStringSection((const char*)_data.begin(), (const char*)_data.end());
                    if (ConvertFullMatch(str, MakeOpaqueIteratorRange(casted), TypeOf<DestType>()))
                        return casted;
                    return {};
                } else {
                    if (_reversedEndian && _type._type > ImpliedTyping::TypeCat::UInt8) {
                        ReversedEndianHelper helper { _data, _type };
                        DestType result;
                        if (!Cast(MakeOpaqueIteratorRange(result), TypeOf<DestType>(), helper._reversedData, _type))
                            return {};
                        return result;
                    } else {
                        DestType result;
                        if (!Cast(MakeOpaqueIteratorRange(result), TypeOf<DestType>(), _data, _type))
                            return {};
                        return result;
                    }
                }
            }
        }

        inline VariantRetained::operator VariantNonRetained() const
        {
            auto size = _type.GetSize();
            if (size <= sizeof(_smallBuffer)) {
                return VariantNonRetained { _type, MakeIteratorRange(_smallBuffer, _smallBuffer+size), _reverseEndian };
            } else {
                assert(_largeBuffer.size() == size);
                return VariantNonRetained { _type, MakeIteratorRange(_largeBuffer), _reverseEndian };
            }
        }

        inline VariantRetained::VariantRetained(TypeDesc type, IteratorRange<const void*> data, bool reverseEndian)
        : _type(type), _reverseEndian(reverseEndian)
        {
            assert(_type.GetSize() == data.size());
            if (data.size() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)data.begin(), (const uint8_t*)data.end(), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)data.begin(), (const uint8_t*)data.end());
        }

        inline VariantRetained::VariantRetained(const VariantNonRetained& copyFrom)
        : VariantRetained{copyFrom._type, copyFrom._data, copyFrom._reversedEndian} {}

        template<typename DestType>
            inline DestType VariantRetained::RequireCastValue() const { return operator VariantNonRetained().RequireCastValue<DestType>(); }

        template<typename DestType>
            inline std::optional<DestType> VariantRetained::TryCastValue() const { return operator VariantNonRetained().TryCastValue(); }

        template<typename SrcType>
            VariantRetained::VariantRetained(SrcType src)
        {
            using S = std::decay_t<SrcType>;
            static_assert(TypeOf<S>()._type != TypeCat::Void, "VariantRetained constructed with type that cannot be represented with the ImpliedTyping system");
            static_assert(TypeOf<S>().GetSize() == sizeof(SrcType));
            _type = TypeOf<S>();
            if constexpr (sizeof(SrcType) <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)&src, (const uint8_t*)(&src+1), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)&src, (const uint8_t*)(&src+1));
        }

        template<>
            inline VariantRetained::VariantRetained(const char s[])
        {
            _type = TypeOf<const char*>();
            _type._arrayCount = (uint32_t)XlStringSize(s);
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount));
        }

        template<>
            inline VariantRetained::VariantRetained(const utf16 s[])
        {
            _type = TypeOf<const utf16*>();
            _type._arrayCount = (uint32_t)XlStringSize(s);
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount));
        }

        template<>
            inline VariantRetained::VariantRetained(const utf32 s[])
        {
            _type = TypeOf<const utf32*>();
            _type._arrayCount = (uint32_t)XlStringSize(s);
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount));
        }

        template<>
            inline VariantRetained::VariantRetained(const ucs4 s[])
        {
            _type = TypeOf<const ucs4*>();
            _type._arrayCount = (uint32_t)XlStringSize(s);
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)s, (const uint8_t*)(s+_type._arrayCount));
        }

        template<>
            inline VariantRetained::VariantRetained(std::basic_string<char> str)
        {
            _type = TypeOf<std::basic_string<char>>();
            _type._arrayCount = (uint32_t)str.size();
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)str.data(), (const uint8_t*)(str.data()+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)AsPointer(str.begin()), (const uint8_t*)AsPointer(str.end()));
        }

        template<>
            inline VariantRetained::VariantRetained(std::basic_string<utf16> str)
        {
            _type = TypeOf<std::basic_string<utf16>>();
            _type._arrayCount = (uint32_t)str.size();
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)str.data(), (const uint8_t*)(str.data()+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)AsPointer(str.begin()), (const uint8_t*)AsPointer(str.end()));
        }

        template<>
            inline VariantRetained::VariantRetained(std::basic_string<utf32> str)
        {
            _type = TypeOf<std::basic_string<utf32>>();
            _type._arrayCount = (uint32_t)str.size();
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)str.data(), (const uint8_t*)(str.data()+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)AsPointer(str.begin()), (const uint8_t*)AsPointer(str.end()));
        }

        template<>
            inline VariantRetained::VariantRetained(std::basic_string<ucs4> str)
        {
            _type = TypeOf<std::basic_string<ucs4>>();
            _type._arrayCount = (uint32_t)str.size();
            if (_type.GetSize() <= sizeof(_smallBuffer)) {
                std::copy((const uint8_t*)str.data(), (const uint8_t*)(str.data()+_type._arrayCount), _smallBuffer);
            } else
                _largeBuffer.insert(_largeBuffer.begin(), (const uint8_t*)AsPointer(str.begin()), (const uint8_t*)AsPointer(str.end()));
        }
    }
}

using namespace Utility;