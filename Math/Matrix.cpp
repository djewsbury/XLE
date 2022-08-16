// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Matrix.h"
#include "Quaternion.h"

/*!
   \page cmlAutoExp CML library and AutoExp.dat

   In Visual Studio 2010, you can add the following lines to the "[Visualizer]" section of your autoexp.dat to
   get reasonable previews of vector types.
   This is highly recommended, otherwise the watch window becomes a little cluttered.

   \code
   cml::vector<*> {
	    preview(
		    #switch ($e.dimension)
		    #case 2 ( #("[", $e.m_data[0], ", ", $e.m_data[1], "]") )
		    #case 3 ( #("[", $e.m_data[0], ", ", $e.m_data[1], ", ", $e.m_data[2], "]") )
		    #case 4 ( #("[", $e.m_data[0], ", ", $e.m_data[1], ", ", $e.m_data[2], ", ", $e.m_data[3], "]") )
		    #default ( "..." )
	    )
	    children(
		    #array(expr: $e.m_data[$i], size: $e.dimension)
	    )
    }

    cml::fixed_1D<*> {
	    preview(
		    #switch ($e.array_size)
		    #case 2 ( #("[", $e.m_data[0], ", ", $e.m_data[1], "]") )
		    #case 3 ( #("[", $e.m_data[0], ", ", $e.m_data[1], ", ", $e.m_data[2], "]") )
		    #case 4 ( #("[", $e.m_data[0], ", ", $e.m_data[1], ", ", $e.m_data[2], ", ", $e.m_data[3], "]") )
		    #default ( "..." )
	    )
	    children(
		    #array(expr: $e.m_data[$i], size: $e.array_size)
	    )
    }
    \endcode

*/

namespace XLEMath
{
    Float4x4 MakeFloat4x4(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33 )
    {
        Float4x4 result;
        result(0,0) = m00; result(0,1) = m01; result(0,2) = m02; result(0,3) = m03;
        result(1,0) = m10; result(1,1) = m11; result(1,2) = m12; result(1,3) = m13;
        result(2,0) = m20; result(2,1) = m21; result(2,2) = m22; result(2,3) = m23;
        result(3,0) = m30; result(3,1) = m31; result(3,2) = m32; result(3,3) = m33;
        return result;
    }

    Float4x4 AsFloat4x4(const float a[])
    {
        return Float4x4(
            a[ 0], a[ 1], a[ 2], a[ 3],
            a[ 4], a[ 5], a[ 6], a[ 7],
            a[ 8], a[ 9], a[10], a[11],
            a[12], a[13], a[14], a[15]);
    }

    Float4x4 Expand(const Float3x3& rotationScalePart, const Float3& translationPart)
    {
        return Float4x4(
            rotationScalePart(0,0), rotationScalePart(0,1), rotationScalePart(0,2), translationPart[0],
            rotationScalePart(1,0), rotationScalePart(1,1), rotationScalePart(1,2), translationPart[1],
            rotationScalePart(2,0), rotationScalePart(2,1), rotationScalePart(2,2), translationPart[2],
            0, 0, 0, 1);
    }

    Float3x4 MakeFloat3x4(
        float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23)
    {
        Float3x4 result;
        result(0,0) = m00; result(0,1) = m01; result(0,2) = m02; result(0,3) = m03;
        result(1,0) = m10; result(1,1) = m11; result(1,2) = m12; result(1,3) = m13;
        result(2,0) = m20; result(2,1) = m21; result(2,2) = m22; result(2,3) = m23;
        return result;
    }

    // Float3x3 matrix multiplication:
    //      result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0)
    //      result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0)
    //      result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0)
    //
    //      result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1)
    //      result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1)
    //      result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1)
    //
    //      result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2)
    //      result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2)
    //      result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2)


    Float3x3        LeftMultiplyByTranspose(Float3x3& input)
    {
            // Multiply the transpose of input
            // eg; result = Tranpose(input) * input
            // The result is symmetric, which means we can reduce the number of multiplications slightly...
            // Also note that the diagonal parts are just the dot products of the columns.
        Float3x3 result;
        result(0,0) = input(0,0) * input(0,0) + input(1,0) * input(1,0) + input(2,0) * input(2,0);
        result(0,1) = result(1,0) = input(0,1) * input(0,0) + input(1,1) * input(1,0) + input(2,1) * input(2,0);
        result(0,2) = result(2,0) = input(0,2) * input(0,0) + input(1,2) * input(1,0) + input(2,2) * input(2,0);
        
        result(1,1) = input(0,1) * input(0,1) + input(1,1) * input(1,1) + input(2,1) * input(2,1);
        result(1,2) = result(2,1) = input(0,2) * input(0,1) + input(1,2) * input(1,1) + input(2,2) * input(2,1);
        
        result(2,2) = input(0,2) * input(0,2) + input(1,2) * input(1,2) + input(2,2) * input(2,2);
        return result;
    }

    #if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML
        static Float3x4 MakeIdentity3x4() { Float3x4 temp; temp.identity(); return temp; }
        template<> const Float3x4& Identity<Float3x4>()
        {
            static Float3x4 result = MakeIdentity3x4();
            return result;
        }
        
        template<> const Float4x4& Identity<Float4x4>()
        {
            const Float4x4::value_type one = Float4x4::value_type(1);
            const Float4x4::value_type zero = Float4x4::value_type(0);
            static Float4x4 result(
                one, zero, zero, zero,
                zero, one, zero, zero,
                zero, zero, one, zero,
                zero, zero, zero, one );
            return result;
        }

        template<> const Float3x3& Identity<Float3x3>()
        {
            const Float3x3::value_type one = Float3x3::value_type(1);
            const Float3x3::value_type zero = Float3x3::value_type(0);
            static Float3x3 result(
                one, zero, zero,
                zero, one, zero,
                zero, zero, one );
            return result;
        }

        template<> const Float3x3& Zero<Float3x3>()
        {
            const Float3x3::value_type zero = Float3x3::value_type(0);
            static Float3x3 result(
                zero, zero, zero,
                zero, zero, zero,
                zero, zero, zero );
            return result;
        }

        Float3x4 MakeZero3x4() { Float3x4 result; result.zero(); return result; }
        
        template<> const Float3x4& Zero<Float3x4>()
        {
            static Float3x4 result = MakeZero3x4();
            return result;
        }

        template<> const Float4x4& Zero<Float4x4>()
        {
            const Float4x4::value_type zero = Float4x4::value_type(0);
            static Float4x4 result(
                zero, zero, zero, zero,
                zero, zero, zero, zero,
                zero, zero, zero, zero,
                zero, zero, zero, zero );
            return result;
        }

        Float4x4        Transpose(const Float4x4& input)        { return cml::transpose(input); }
        Float3x3        Transpose(const Float3x3& input)        { return cml::transpose(input); }

        Float3x3        Inverse(const Float3x3& input)          { return cml::inverse(input); }
        Float4x4        Inverse(const Float4x4& input)          { return cml::inverse(input); }

        float           Determinant(const Float3x3& input)      { return cml::determinant(input); }
        float           Determinant(const Float4x4& input)      { return cml::determinant(input); }

        static Quaternion MakeIdentityQuaternion() { Quaternion temp; temp.identity(); return temp; }
        template<> const Quaternion& Identity<Quaternion>()
        {
            static Quaternion result = MakeIdentityQuaternion();
            return result;
        }

    #endif
    
}
