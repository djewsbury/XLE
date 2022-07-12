// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "StringUtils.h"            // for StringSection
#include "Optional.h"
        
namespace Utility
{
    namespace ImpliedTyping
    {
        enum class TypeCat : uint8_t { Void, Bool, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float, Double };
        enum class TypeHint : uint8_t { None, Vector, Matrix, Color, String };
        enum class CastType : uint8_t { Narrowing, Equal, Widening};
#pragma pack(push,1)
        class 
            #if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
                __attribute__((packed)) 
            #endif
            TypeDesc
        {
        public:
            TypeCat     _type = TypeCat::UInt32;
            uint16_t    _arrayCount = 1;
            TypeHint    _typeHint = TypeHint::None;

            uint32_t GetSize() const;

            template<typename Stream> void SerializeMethod(Stream& serializer) const;
            friend bool operator==(const TypeDesc& lhs, const TypeDesc& rhs);
        };
#pragma pack(pop)

        /// Calculate type of an object given in string form.
        /// Object should be formatted in one of the following C++ like patterns:
        /// 
        ///  "1u" (or "1ui" or "1ul", etc)
        ///  "1b" (or "true" or "false")
        ///  ".3" (or "0.3f", etc)
        ///  "{1u, 2u, 3u}" (or "[1u, 2u, 3u]")
        ///  "{1u, 2u, 3u}c" or "{1u, 2u, 3u}v"
        ///
        /// This is intended for storing common basic types in text files, and 
        /// for use while entering data in tools. We want the type of the data to
        /// be implied by the string representing the data (without needing an
        /// extra field to describe the type).
        ///
        /// This kind of thing is useful when interfacing with scripting languages
        /// like HLSL and Lua. There are only a few basic types that we need
        /// to support.
        ///
        /// But sometimes we also want to had hints for out to interpret the data.
        /// For example, 3 floats could be a vector or a colour. We will use C++
        /// like postfix characters for this (eg, "{1,1,1}c" is a color)
        TypeDesc TypeOf(const char expression[]);

        // "template<typename Type> TypeDesc TypeOf()" is declared below

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


        bool Cast(
            IteratorRange<void*> dest, TypeDesc destType,
            IteratorRange<const void*> src, TypeDesc srcType);
        
        CastType CalculateCastType(TypeCat testType, TypeCat againstType);

        struct VariantNonRetained
        {
            ImpliedTyping::TypeDesc _type = ImpliedTyping::TypeCat::Void;
            IteratorRange<const void*> _data;
        };
        ImpliedTyping::TypeDesc TryBinaryOperator(
            IteratorRange<void*> dst,
            StringSection<> op,
            const VariantNonRetained& lhs,
            const VariantNonRetained& rhs);
        ImpliedTyping::TypeDesc TryUnaryOperator(
            IteratorRange<void*> dst,
            StringSection<> op,
            const ImpliedTyping::VariantNonRetained& operand);

        //////////////////////////////////////////////////////////////////////////////////////
            // Template implementations //
        //////////////////////////////////////////////////////////////////////////////////////
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(uint64_t const*)        { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt64}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(int64_t const*)         { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int64}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(uint32_t const*)        { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(int32_t const*)         { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int32}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(uint16_t const*)        { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt16}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(int16_t const*)         { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int16}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(uint8_t const*)         { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt8}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(int8_t const*)          { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Int8}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(char const*)            { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt8}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(char16_t const*)        { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt16}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(char32_t const*)        { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(bool const*)            { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Bool}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(float const*)           { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(double const*)          { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Double}; }

        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const utf8* const*)     { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt8, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const utf16* const*)    { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt16, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const utf32* const*)    { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const ucs4* const*)     { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }

        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const std::basic_string<utf8>*)     { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt8, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const std::basic_string<utf16>*)    { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt16, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const std::basic_string<utf32>*)    { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(const std::basic_string<ucs4>*)     { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::UInt32, (uint16_t)~uint16_t(0), Utility::ImpliedTyping::TypeHint::String}; }

        template<typename Type> 
            decltype(InternalTypeOf(std::declval<Type const*>())) TypeOf() { return InternalTypeOf((Type const*)nullptr); }

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
            static_assert(sizeof(TypeDesc) == sizeof(uint32_t));
            SerializationOperator(serializer, *(uint32_t*)this);
        }
    }
}

using namespace Utility;