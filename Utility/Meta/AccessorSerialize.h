// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>       // for std::move

namespace Utility
{
    class ClassAccessors;
    class ParameterBox;
}

namespace Formatters { class TextOutputFormatter; }
template<typename Type> const Utility::ClassAccessors& Legacy_GetAccessors();

namespace Utility
{
    template<typename Formatter>
        void AccessorDeserialize(
            Formatter& formatter,
            void* obj, const ClassAccessors& props);

    void AccessorSerialize(
        Formatters::TextOutputFormatter& formatter,
        const void* obj, const ClassAccessors& props);

    void SetParameters(
        void* obj, const ClassAccessors& accessors,
        const ParameterBox& paramBox);

    /// <summary>Deserializes a type with attached ClassAccessors</summary>
    /// Class accesses must be registed for the given type (by implementing
    /// Legacy_GetAccessors<>). The system will deserialize all properties with a set
    /// accessor, and a create children in child lists, where necessary.
    ///
    /// Type parsing and conversion is handled automatically. Properties 
    /// that don't exist in the input stream will not be touched at all. 
    ///
    /// The deserialization is slower than a hand written deserialization 
    /// function. This is intended for text serialization of relatively small types.
    /// Very complex types (or types that are deserialized frequently) may 
    /// benefit from a custom hand written replacement function.
    template<typename Formatter, typename Type>
        void AccessorDeserialize(
            Formatter& formatter,
            Type& obj)
        {
            const auto& props = Legacy_GetAccessors<Type>();
            AccessorDeserialize(formatter, &obj, props);
        }

    /// <summary>Serializes a type with attached ClassAccessors</summary>
    /// Class accesses must be registed for the given type (by implementing
    /// Legacy_GetAccessors<>). The system will serialize all properties with a get
    /// accessor, and all objects in child lists.
    ///
    /// Type conversions are handled automatically.
    ///
    /// The serialization is slower
    /// than a hand written serialization function. This is intended for text
    /// serialization of relatively small types.
    template<typename Type>
        void AccessorSerialize(
            Formatters::TextOutputFormatter& formatter,
            const Type& obj)
        {
            const auto& props = Legacy_GetAccessors<Type>();
            AccessorSerialize(formatter, &obj, props);
        }

    template<typename Type>
        Type CreateFromParameters(const ParameterBox& paramBox)
        {
            Type result;
            SetParameters(&result, Legacy_GetAccessors<Type>(), paramBox);
            return std::move(result);
        }


}

using namespace Utility;


