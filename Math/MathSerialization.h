// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "Quaternion.h"
#include "../Assets/BlockSerializer.h"
#include "../Utility/ImpliedTyping.h"
#include <iosfwd>

namespace cml
{
    template <typename Type, int Count>
        constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(cml::vector<Type, cml::fixed<Count>> const*)
    { 
        return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeOf<Type>()._type, Count, Utility::ImpliedTyping::TypeHint::Vector};
    }

    constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(Float3x3 const*)      { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 9, Utility::ImpliedTyping::TypeHint::Matrix}; }
    constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(Float3x4 const*)      { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 12, Utility::ImpliedTyping::TypeHint::Matrix}; }
    constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(Float4x4 const*)      { return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 16, Utility::ImpliedTyping::TypeHint::Matrix}; }

	constexpr Utility::ImpliedTyping::TypeDesc InternalTypeOf(Quaternion const*)	{ return Utility::ImpliedTyping::TypeDesc{Utility::ImpliedTyping::TypeCat::Float, 4, Utility::ImpliedTyping::TypeHint::Vector}; }

	template<int Dimen, typename Primitive>
		inline void SerializationOperator(::Assets::BlockSerializer& serializer, const cml::vector< Primitive, cml::fixed<Dimen> >& vec)
	{
		for (unsigned j=0; j<Dimen; ++j)
			SerializationOperator(serializer, vec[j]);
	}
	
	inline void SerializationOperator(::Assets::BlockSerializer& serializer, const XLEMath::Float4x4& float4x4)
	{
		for (unsigned i=0; i<4; ++i)
			for (unsigned j=0; j<4; ++j)
				SerializationOperator(serializer, float4x4(i,j));
	}
}

namespace XLEMath
{
    std::ostream& CompactTransformDescription(std::ostream& str, const Float4x4& transform);
}

namespace cml
{
	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector2T<uint8_t> v)
	{
		return o << (unsigned)v[0] << ' ' << (unsigned)v[1];		// avoid issues streaming uint8_t by casting to a larger integral type
	}

	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector3T<uint8_t> v)
	{
		return o << (unsigned)v[0] << ' ' << (unsigned)v[1] << ' ' << (unsigned)v[2];		// avoid issues streaming uint8_t by casting to a larger integral type
	}

	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector4T<uint8_t> v)
	{
		return o << (unsigned)v[0] << ' ' << (unsigned)v[1] << ' ' << (unsigned)v[2] << ' ' << (unsigned)v[3];		// avoid issues streaming uint8_t by casting to a larger integral type
	}

	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector2T<int8_t> v)
	{
		return o << (signed)v[0] << ' ' << (signed)v[1];		// avoid issues streaming uint8_t by casting to a larger integral type
	}

	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector3T<int8_t> v)
	{
		return o << (signed)v[0] << ' ' << (signed)v[1] << ' ' << (signed)v[2];		// avoid issues streaming uint8_t by casting to a larger integral type
	}

	inline std::ostream& operator<<(std::ostream& o, XLEMath::Vector4T<int8_t> v)
	{
		return o << (signed)v[0] << ' ' << (signed)v[1] << ' ' << (signed)v[2] << ' ' << (signed)v[3];		// avoid issues streaming uint8_t by casting to a larger integral type
	}
}
