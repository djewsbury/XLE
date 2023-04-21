// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "XLEMath.h"

#if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML
    #pragma warning(push)
        #pragma warning(disable:4512)       // assignment operator could not be generated
        #include <cml/vector.h>
    #pragma warning(pop)
#endif

namespace XLEMath
{
    #if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML

        using Float2 = cml::vector< float, cml::fixed<2> >;
        using Float3 = cml::vector< float, cml::fixed<3> >;
        using Float4 = cml::vector< float, cml::fixed<4> >;

        using Double2 = cml::vector< double, cml::fixed<2> >;
        using Double3 = cml::vector< double, cml::fixed<3> >;
        using Double4 = cml::vector< double, cml::fixed<4> >;

        using Int2 = cml::vector< int, cml::fixed<2> >;
        using Int3 = cml::vector< int, cml::fixed<3> >;
        using Int4 = cml::vector< int, cml::fixed<4> >;

        using UInt2 = cml::vector< unsigned, cml::fixed<2> >;
        using UInt3 = cml::vector< unsigned, cml::fixed<3> >;
        using UInt4 = cml::vector< unsigned, cml::fixed<4> >;

        template<typename Type> using Vector2T = cml::vector<Type, cml::fixed<2>>;
        template<typename Type> using Vector3T = cml::vector<Type, cml::fixed<3>>;
        template<typename Type> using Vector4T = cml::vector<Type, cml::fixed<4>>;
        template<typename Type, int N> using VectorTT = cml::vector<Type, cml::fixed<N>>;

        template <typename Type, int N>
            cml::vector<Type, cml::fixed<N+1>>      Expand(const cml::vector<Type, cml::fixed<N>>& input, Type extra);

        template <typename Type>
            cml::vector<Type, cml::fixed<4>>        Expand(const cml::vector<Type, cml::fixed<3>>& input, Type extra)
            {
                return cml::vector<Type, cml::fixed<4>>(input[0], input[1], input[2], extra);
            }

        template <typename Type>
            cml::vector<Type, cml::fixed<3>>        Expand(const cml::vector<Type, cml::fixed<2>>& input, Type extra)
            {
                return cml::vector<Type, cml::fixed<3>>(input[0], input[1], extra);
            }

        template <typename Type>
            cml::vector<Type, cml::fixed<4>>        Expand(const cml::vector<Type, cml::fixed<2>>& input, const cml::vector<Type, cml::fixed<2>>& extra)
            {
                return cml::vector<Type, cml::fixed<4>>(input[0], input[1], extra[0], extra[1]);
            }

        template <typename Type, int N>
            cml::vector<Type, cml::fixed<N-1>>      Truncate(const cml::vector<Type, cml::fixed<N>>& input);

        template <typename Type>
            cml::vector<Type, cml::fixed<3>>        Truncate(const cml::vector<Type, cml::fixed<4>>& input)
            {
                return cml::vector<Type, cml::fixed<3>>(input[0], input[1], input[2]);
            }

        template <typename Type>
            cml::vector<Type, cml::fixed<2>>        Truncate(const cml::vector<Type, cml::fixed<3>>& input)
            {
                return cml::vector<Type, cml::fixed<2>>(input[0], input[1]);
            }

        template <typename Vector>
            inline auto Magnitude(const Vector& vector) -> decltype(cml::length(vector))
                { return cml::length(vector); }

        template <typename Vector>
            inline auto MagnitudeSquared(const Vector& vector) -> decltype(cml::length_squared(vector))
                { return cml::length_squared(vector); }

        template <typename Vector>
            inline auto Normalize(const Vector& vector) -> decltype(cml::normalize(vector))
                { return cml::normalize(vector); }

        template <typename Vector>
            inline bool Normalize_Checked(Vector* result, const Vector& vector)
        { 
            auto magSquared = MagnitudeSquared(vector);
            float rsqrtMSq;
            if (!XlRSqrt_Checked(&rsqrtMSq, magSquared)) return false;
            *result = vector * rsqrtMSq;
            return true;
        }

        template <typename LHSType, typename RHSType>
            inline auto Cross(const LHSType& lhs, const RHSType& rhs) -> decltype(cml::cross(lhs, rhs))
                { return cml::cross(lhs, rhs); }

        template <typename LHSType, typename RHSType>
            inline auto Dot(const LHSType& lhs, const RHSType& rhs) -> decltype(cml::dot(lhs, rhs)) 
                { return cml::dot(lhs, rhs); }

        template<typename BasicType, int Count>
            inline cml::vector<BasicType, cml::fixed<Count>>
                LinearInterpolate(cml::vector<BasicType, cml::fixed<Count>> lhs, cml::vector<BasicType, cml::fixed<Count>> rhs, BasicType alpha)
        {
            return (rhs - lhs) * alpha + lhs;
        }


		template<typename BasicType, int Count>
			inline bool Equivalent(
				cml::vector<BasicType, cml::fixed<Count>> lhs, 
				cml::vector<BasicType, cml::fixed<Count>> rhs, BasicType tolerance)
			{
				for (unsigned i=0; i<Count; ++i)
					if (!Equivalent(lhs[i], rhs[i], tolerance))
						return false;
				return true;
			}

        template<typename Primitive>
		    bool AdaptiveEquivalent(Vector2T<Primitive> A, Vector2T<Primitive> B, Primitive epsilon)
        {
            return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon);
        }

        template<typename Primitive>
            bool AdaptiveEquivalent(Vector3T<Primitive> A, Vector3T<Primitive> B, Primitive epsilon)
        {
            return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon) && AdaptiveEquivalent(A[2], B[2], epsilon);
        }

        template<typename Primitive>
            bool AdaptiveEquivalent(Vector4T<Primitive> A, Vector4T<Primitive> B, Primitive epsilon)
        {
            return AdaptiveEquivalent(A[0], B[0], epsilon) && AdaptiveEquivalent(A[1], B[1], epsilon) && AdaptiveEquivalent(A[2], B[2], epsilon) && AdaptiveEquivalent(A[3], B[3], epsilon);
        }

        template<typename BasicType, int Count>
			inline cml::vector<BasicType, cml::fixed<Count>> MultiplyAcross(
				const cml::vector<BasicType, cml::fixed<Count>>& lhs, 
				const cml::vector<BasicType, cml::fixed<Count>>& rhs)
			{
                cml::vector<BasicType, cml::fixed<Count>> result;
				for (unsigned i=0; i<Count; ++i)
                    result[i] = lhs[i] * rhs[i];
				return result;
			}
    #endif

    template<> inline const Float2& Zero<Float2>()
    {
        const Float2::value_type zero = Float2::value_type(0);
        static Float2 result(zero, zero);
        return result;
    }
    
    template<> inline const Float3& Zero<Float3>()
    {
        const Float3::value_type zero = Float3::value_type(0);
        static Float3 result(zero, zero, zero);
        return result;
    }

    template<> inline const Float4& Zero<Float4>()
    {
        const Float4::value_type zero = Float4::value_type(0);
        static Float4 result(zero, zero, zero, zero);
        return result;
    }

    template<> inline const Double2& Zero<Double2>()
    {
        const Double2::value_type zero = Double2::value_type(0);
        static Double2 result(zero, zero);
        return result;
    }
    
    template<> inline const Double3& Zero<Double3>()
    {
        const Double3::value_type zero = Double3::value_type(0);
        static Double3 result(zero, zero, zero);
        return result;
    }

    template<> inline const Double4& Zero<Double4>()
    {
        const Double4::value_type zero = Double4::value_type(0);
        static Double4 result(zero, zero, zero, zero);
        return result;
    }

    template<> inline const UInt2& Zero<UInt2>()
    {
        const UInt2::value_type zero = UInt2::value_type(0);
        static UInt2 result(zero, zero);
        return result;
    }
    
    template<> inline const UInt3& Zero<UInt3>()
    {
        const UInt3::value_type zero = UInt3::value_type(0);
        static UInt3 result(zero, zero, zero);
        return result;
    }

    template<> inline const UInt4& Zero<UInt4>()
    {
        const UInt4::value_type zero = UInt4::value_type(0);
        static UInt4 result(zero, zero, zero, zero);
        return result;
    }

    template<> inline const Int2& Zero<Int2>()
    {
        const Int2::value_type zero = Int2::value_type(0);
        static Int2 result(zero, zero);
        return result;
    }
    
    template<> inline const Int3& Zero<Int3>()
    {
        const Int3::value_type zero = Int3::value_type(0);
        static Int3 result(zero, zero, zero);
        return result;
    }

    template<> inline const Int4& Zero<Int4>()
    {
        const Int4::value_type zero = Int4::value_type(0);
        static Int4 result(zero, zero, zero, zero);
        return result;
    }
    

}

namespace std
{
        // override for std::size for XLEMath::VectorTT
        // Note that std::size is part of the C++17. None of our compilers
        // support it yet; but we can still make use of the syntax.
    #if (__cplusplus >= 201700L)
        template<typename ValueType, int N>
            constexpr std::size_t size(const XLEMath::VectorTT<ValueType, N>& c) never_throws
                { return (std::size_t)c.dimension; }
    #else
        template<typename ValueType, int N>
            /*constexpr*/ std::size_t size(const XLEMath::VectorTT<ValueType, N>& c) never_throws
                { return (std::size_t)c.dimension; }
    #endif
}
