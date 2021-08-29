// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Transformations.h"
#include "Matrix.h"
#include "EigenVector.h"
#include <assert.h>

namespace XLEMath
{

    Float4x4 Combine(const Float3x3& rotation, const Float4x4& transform)
    {
            //      But our "transform" is a geometric transform... We just want
            //      to multiply the top-left 3x3 part of the 4x4 matrix by the
            //      input 3x3 rotation matrix.

        Float4x4 result;
        const Float4x4& lhs = transform;
		const Float3x3& rhs = rotation;
        result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0) ;
        result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1) ;
        result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2) ;
        result(0,3) =                                                                  + lhs(0,3);

        result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0) ;
        result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1) ;
        result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2) ;
        result(1,3) =                                                                  + lhs(1,3);

        result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0) ;
        result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1) ;
        result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2) ;
        result(2,3) =                                                                  + lhs(2,3);

        result(3,0) = lhs(3,0);
        result(3,1) = lhs(3,1);
        result(3,2) = lhs(3,2);
        result(3,3) = lhs(3,3);

        return result;
    }

	Float4x4 Combine(const Float4x4& transform, const Float3x3& rotation)
    {
        Float4x4 result;
        const Float4x4& rhs          = transform;
		const Float3x3& lhs = rotation;

        result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0);
        result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1);
        result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2);
        result(0,3) = lhs(0,0) * rhs(0,3) + lhs(0,1) * rhs(1,3) + lhs(0,2) * rhs(2,3);
        
        result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0);
        result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1);
        result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2);
        result(1,3) = lhs(1,0) * rhs(0,3) + lhs(1,1) * rhs(1,3) + lhs(1,2) * rhs(2,3);
        
        result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0);
        result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1);
        result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2);
        result(2,3) = lhs(2,0) * rhs(0,3) + lhs(2,1) * rhs(1,3) + lhs(2,2) * rhs(2,3);
        
        result(3,0) = rhs(3,0);
        result(3,1) = rhs(3,1);
        result(3,2) = rhs(3,2);
        result(3,3) = rhs(3,3);

        return result;
    }

    Float3x4 Combine(const Float3x3& rotation, const Float3x4& transform)
    {
            //      But our "transform" is a geometric transform... We just want
            //      to multiply the top-left 3x3 part of the 4x4 matrix by the
            //      input 3x3 rotation matrix.

        Float3x4 result;
        const auto& lhs = transform;
		const Float3x3& rhs = rotation;
        result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0) ;
        result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1) ;
        result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2) ;
        result(0,3) =                                                                  + lhs(0,3);

        result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0) ;
        result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1) ;
        result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2) ;
        result(1,3) =                                                                  + lhs(1,3);

        result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0) ;
        result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1) ;
        result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2) ;
        result(2,3) =                                                                  + lhs(2,3);

        result(3,0) = lhs(3,0);
        result(3,1) = lhs(3,1);
        result(3,2) = lhs(3,2);
        result(3,3) = lhs(3,3);

        return result;
    }

	Float3x4 Combine(const Float3x4& transform, const Float3x3& rotation)
    {
        Float3x4 result;
        const auto& rhs = transform;
		const Float3x3& lhs = rotation;

        result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0);
        result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1);
        result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2);
        result(0,3) = lhs(0,0) * rhs(0,3) + lhs(0,1) * rhs(1,3) + lhs(0,2) * rhs(2,3);
        
        result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0);
        result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1);
        result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2);
        result(1,3) = lhs(1,0) * rhs(0,3) + lhs(1,1) * rhs(1,3) + lhs(1,2) * rhs(2,3);
        
        result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0);
        result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1);
        result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2);
        result(2,3) = lhs(2,0) * rhs(0,3) + lhs(2,1) * rhs(1,3) + lhs(2,2) * rhs(2,3);
        
        result(3,0) = rhs(3,0);
        result(3,1) = rhs(3,1);
        result(3,2) = rhs(3,2);
        result(3,3) = rhs(3,3);

        return result;
    }

        //
        //      4x4 matrix transform (for reference)
        //
        //          result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0) + lhs(0,3) * rhs(3,0);
        //          result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1) + lhs(0,3) * rhs(3,1);
        //          result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2) + lhs(0,3) * rhs(3,2);
        //          result(0,3) = lhs(0,0) * rhs(0,3) + lhs(0,1) * rhs(1,3) + lhs(0,2) * rhs(2,3) + lhs(0,3) * rhs(3,3);
        // 
        //          result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0) + lhs(1,3) * rhs(3,0);
        //          result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1) + lhs(1,3) * rhs(3,1);
        //          result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2) + lhs(1,3) * rhs(3,2);
        //          result(1,3) = lhs(1,0) * rhs(0,3) + lhs(1,1) * rhs(1,3) + lhs(1,2) * rhs(2,3) + lhs(1,3) * rhs(3,3);
        // 
        //          result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0) + lhs(2,3) * rhs(3,0);
        //          result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1) + lhs(2,3) * rhs(3,1);
        //          result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2) + lhs(2,3) * rhs(3,2);
        //          result(2,3) = lhs(2,0) * rhs(0,3) + lhs(2,1) * rhs(1,3) + lhs(2,2) * rhs(2,3) + lhs(2,3) * rhs(3,3);
        // 
        //          result(3,0) = lhs(3,0) * rhs(0,0) + lhs(3,1) * rhs(1,0) + lhs(3,2) * rhs(2,0) + lhs(3,3) * rhs(3,0);
        //          result(3,1) = lhs(3,0) * rhs(0,1) + lhs(3,1) * rhs(1,1) + lhs(3,2) * rhs(2,1) + lhs(3,3) * rhs(3,1);
        //          result(3,2) = lhs(3,0) * rhs(0,2) + lhs(3,1) * rhs(1,2) + lhs(3,2) * rhs(2,2) + lhs(3,3) * rhs(3,2);
        //          result(3,3) = lhs(3,0) * rhs(0,3) + lhs(3,1) * rhs(1,3) + lhs(3,2) * rhs(2,3) + lhs(3,3) * rhs(3,3);
        //
        //      [4x4] . [4x1] transform (for reference)
        //
        //          result(0,0) = lhs(0,0) * rhs(0) + lhs(0,1) * rhs(1) + lhs(0,2) * rhs(2) + lhs(0,3) * rhs(3);
        //          result(1,0) = lhs(1,0) * rhs(0) + lhs(1,1) * rhs(1) + lhs(1,2) * rhs(2) + lhs(1,3) * rhs(3);
        //          result(2,0) = lhs(2,0) * rhs(0) + lhs(2,1) * rhs(1) + lhs(2,2) * rhs(2) + lhs(2,3) * rhs(3);
        //          result(3,0) = lhs(3,0) * rhs(0) + lhs(3,1) * rhs(1) + lhs(3,2) * rhs(2) + lhs(3,3) * rhs(3);
        //
        //      [1x4] . [4x4] transform (for reference)
        //
        //          result(0,0) = lhs(0) * rhs(0,0) + lhs(1) * rhs(1,0) + lhs(2) * rhs(2,0) + lhs(3) * rhs(3,0);
        //          result(0,1) = lhs(0) * rhs(0,1) + lhs(1) * rhs(1,1) + lhs(2) * rhs(2,1) + lhs(3) * rhs(3,1);
        //          result(0,2) = lhs(0) * rhs(0,2) + lhs(1) * rhs(1,2) + lhs(2) * rhs(2,2) + lhs(3) * rhs(3,2);
        //          result(0,3) = lhs(0) * rhs(0,3) + lhs(1) * rhs(1,3) + lhs(2) * rhs(2,3) + lhs(3) * rhs(3,3);
        //

    Float3x4    Combine(const Float3x4& firstTransform, const Float3x4& secondTransform)
    {
        const Float3x4& lhs = secondTransform;
        const Float3x4& rhs = firstTransform;

        Float3x4 result;
        result(0,0) = lhs(0,0) * rhs(0,0) + lhs(0,1) * rhs(1,0) + lhs(0,2) * rhs(2,0);
        result(0,1) = lhs(0,0) * rhs(0,1) + lhs(0,1) * rhs(1,1) + lhs(0,2) * rhs(2,1);
        result(0,2) = lhs(0,0) * rhs(0,2) + lhs(0,1) * rhs(1,2) + lhs(0,2) * rhs(2,2);
        result(0,3) = lhs(0,0) * rhs(0,3) + lhs(0,1) * rhs(1,3) + lhs(0,2) * rhs(2,3) + lhs(0,3);
        
        result(1,0) = lhs(1,0) * rhs(0,0) + lhs(1,1) * rhs(1,0) + lhs(1,2) * rhs(2,0);
        result(1,1) = lhs(1,0) * rhs(0,1) + lhs(1,1) * rhs(1,1) + lhs(1,2) * rhs(2,1);
        result(1,2) = lhs(1,0) * rhs(0,2) + lhs(1,1) * rhs(1,2) + lhs(1,2) * rhs(2,2);
        result(1,3) = lhs(1,0) * rhs(0,3) + lhs(1,1) * rhs(1,3) + lhs(1,2) * rhs(2,3) + lhs(1,3);
        
        result(2,0) = lhs(2,0) * rhs(0,0) + lhs(2,1) * rhs(1,0) + lhs(2,2) * rhs(2,0);
        result(2,1) = lhs(2,0) * rhs(0,1) + lhs(2,1) * rhs(1,1) + lhs(2,2) * rhs(2,1);
        result(2,2) = lhs(2,0) * rhs(0,2) + lhs(2,1) * rhs(1,2) + lhs(2,2) * rhs(2,2);
        result(2,3) = lhs(2,0) * rhs(0,3) + lhs(2,1) * rhs(1,3) + lhs(2,2) * rhs(2,3) + lhs(2,3);
        return result;
    }

    void Combine_IntoRHS(const Float3& translate, Float4x4& transform)
    {
        Float4x4& lhs = transform;
        // const float rhs00 = 1.f, rhs11 = 1.f, rhs22 = 1.f;
        const float rhs33 = 1.f;
        const float rhs03 = translate[0], rhs13 = translate[1], rhs23 = translate[2];

        // lhs(0,0) = lhs(0,0) * rhs00;
        // lhs(0,1) =                      lhs(0,1) * rhs11;
        // lhs(0,2) =                                          lhs(0,2) * rhs22;
        lhs(0,3) = lhs(0,0) * rhs03 +   lhs(0,1) * rhs13 +  lhs(0,2) * rhs23 +      lhs(0,3) * rhs33;

        // lhs(1,0) = lhs(1,0) * rhs00;
        // lhs(1,1) =                      lhs(1,1) * rhs11;
        // lhs(1,2) =                                          lhs(1,2) * rhs22;
        lhs(1,3) = lhs(1,0) * rhs03 +   lhs(1,1) * rhs13 +  lhs(1,2) * rhs23 +      lhs(1,3) * rhs33;

        // lhs(2,0) = lhs(2,0) * rhs00;
        // lhs(2,1) =                      lhs(2,1) * rhs11;
        // lhs(2,2) =                                          lhs(2,2) * rhs22;
        lhs(2,3) = lhs(2,0) * rhs03 +   lhs(2,1) * rhs13 +  lhs(2,2) * rhs23 +      lhs(2,3) * rhs33;

        // lhs(3,0) = lhs(3,0) * rhs00;
        // lhs(3,1) =                      lhs(3,1) * rhs11;
        // lhs(3,2) =                                          lhs(3,2) * rhs22;
        lhs(3,3) = lhs(3,0) * rhs03 +   lhs(3,1) * rhs13 +  lhs(3,2) * rhs23 +      lhs(3,3) * rhs33;
    }

    void Combine_IntoLHS(Float4x4& transform, const Float3& translate)
    {
        Float4x4& rhs = transform;
        // const float lhs00 = 1.f, lhs11 = 1.f, lhs22 = 1.f;
        // const float lhs33 = 1.f;
        const float lhs03 = translate[0], lhs13 = translate[1], lhs23 = translate[2];

        rhs(0,0) = rhs(0,0) + lhs03 * rhs(3,0);
        rhs(0,1) = rhs(0,1) + lhs03 * rhs(3,1);
        rhs(0,2) = rhs(0,2) + lhs03 * rhs(3,2);
        rhs(0,3) = rhs(0,3) + lhs03 * rhs(3,3);
        
        rhs(1,0) = rhs(1,0) + lhs13 * rhs(3,0);
        rhs(1,1) = rhs(1,1) + lhs13 * rhs(3,1);
        rhs(1,2) = rhs(1,2) + lhs13 * rhs(3,2);
        rhs(1,3) = rhs(1,3) + lhs13 * rhs(3,3);
        
        rhs(2,0) = rhs(2,0) + lhs23 * rhs(3,0);
        rhs(2,1) = rhs(2,1) + lhs23 * rhs(3,1);
        rhs(2,2) = rhs(2,2) + lhs23 * rhs(3,2);
        rhs(2,3) = rhs(2,3) + lhs23 * rhs(3,3);
    }

    void Combine_IntoRHS(const UniformScale& scale, Float4x4& transform)
    {
        transform(0,0) *= scale._scale; transform(0,1) *= scale._scale; transform(0,2) *= scale._scale;
        transform(1,0) *= scale._scale; transform(1,1) *= scale._scale; transform(1,2) *= scale._scale;
        transform(2,0) *= scale._scale; transform(2,1) *= scale._scale; transform(2,2) *= scale._scale;
    }

    void Combine_IntoRHS(const ArbitraryScale& scale, Float4x4& transform)
    {
        auto& lhs = transform;
		const auto& rhs = scale._scale;
        lhs(0,0) *= rhs[0]; lhs(1,0) *= rhs[0]; lhs(2,0) *= rhs[0];
        lhs(0,1) *= rhs[1]; lhs(1,1) *= rhs[1]; lhs(2,1) *= rhs[1];
        lhs(0,2) *= rhs[2]; lhs(1,2) *= rhs[2]; lhs(2,2) *= rhs[2];
    }

    void Combine_IntoRHS(RotationX rotation, Float4x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs11 = cosine, rhs12 = -sine;
        const float rhs21 = sine,   rhs22 =  cosine;
        // const float rhs00 = 1.f,    rhs33 = 1.f;
        Float4x4& lhs          = transform;

        // lhs(0,0) = lhs(0,0);
        const float lhs01 = lhs(0,1);
        lhs(0,1) = lhs01 * rhs11 + lhs(0,2) * rhs21;
        lhs(0,2) = lhs01 * rhs12 + lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3);

        // lhs(1,0) = lhs(1,0);
        const float lhs11 = lhs(1,1);
        lhs(1,1) = lhs11 * rhs11 + lhs(1,2) * rhs21;
        lhs(1,2) = lhs11 * rhs12 + lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3);

        // lhs(2,0) = lhs(2,0);
        const float lhs21 = lhs(2,1);
        lhs(2,1) = lhs21 * rhs11 + lhs(2,2) * rhs21;
        lhs(2,2) = lhs21 * rhs12 + lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3);

        // lhs(3,0) = lhs(3,0) * rhs33;
        // lhs(3,1) = lhs(3,1) * rhs33;
        // lhs(3,2) = lhs(3,2) * rhs33;
        // lhs(3,3) = lhs(3,3) * rhs33;
    }

    void Combine_IntoRHS(RotationY rotation, Float4x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs00 = cosine, rhs02 = sine;
        const float rhs20 = -sine,  rhs22 = cosine;
        // const float rhs11 = 1.f,    rhs33 = 1.f;
        Float4x4& lhs          = transform;

        const float lhs00 = lhs(0,0);
        lhs(0,0) = lhs00 * rhs00 + lhs(0,2) * rhs20;
        // lhs(0,1) = lhs(0,1);
        lhs(0,2) = lhs00 * rhs02 + lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3);

        const float lhs10 = lhs(1,0);
        lhs(1,0) = lhs10 * rhs00 + lhs(1,2) * rhs20;
        // lhs(1,1) = lhs(1,1);
        lhs(1,2) = lhs10 * rhs02 + lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3);

        const float lhs20 = lhs(2,0);
        lhs(2,0) = lhs20 * rhs00 + lhs(2,2) * rhs20;
        // lhs(2,1) = lhs(2,1);
        lhs(2,2) = lhs20 * rhs02 + lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3);

        // lhs(3,0) = lhs(3,0);
        // lhs(3,1) = lhs(3,1);
        // lhs(3,2) = lhs(3,2);
        // lhs(3,3) = lhs(3,3);
    }

    void Combine_IntoRHS(RotationZ rotation, Float4x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs00 = cosine, rhs01 = -sine;
        const float rhs10 = sine,   rhs11 =  cosine;
        // const float rhs22 = 1.f,    rhs33 = 1.f;
        Float4x4& lhs          = transform;

        const float lhs00 = lhs(0,0);
        lhs(0,0) = lhs00 * rhs00 + lhs(0,1) * rhs10;
        lhs(0,1) = lhs00 * rhs01 + lhs(0,1) * rhs11;
        // lhs(0,2) = lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3) * rhs33;

        const float lhs10 = lhs(1,0);
        lhs(1,0) = lhs10 * rhs00 + lhs(1,1) * rhs10;
        lhs(1,1) = lhs10 * rhs01 + lhs(1,1) * rhs11;
        // lhs(1,2) = lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3) * rhs33;

        const float lhs20 = lhs(2,0);
        lhs(2,0) = lhs20 * rhs00 + lhs(2,1) * rhs10;
        lhs(2,1) = lhs20 * rhs01 + lhs(2,1) * rhs11;
        // lhs(2,2) = lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3) * rhs33;

        // lhs(3,0) = lhs(3,0) * rhs33;
        // lhs(3,1) = lhs(3,1) * rhs33;
        // lhs(3,2) = lhs(3,2) * rhs33;
        // lhs(3,3) = lhs(3,3) * rhs33;
    }

    void Combine_IntoLHS(Float4x4& transform, RotationX rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs11 = cosine, lhs12 = -sine;
        const float lhs21 = sine,   lhs22 =  cosine;
        // const float lhs00 = 1.f,    lhs33 = 1.f;
        Float4x4& rhs          = transform;

        // rhs(0,0) = rhs(0,0);
        // rhs(0,1) = rhs(0,1);
        // rhs(0,2) = rhs(0,2);
        // rhs(0,3) = rhs(0,3);
        
        const float rhs10 = rhs(1,0), rhs11 = rhs(1,1), rhs12 = rhs(1,2), rhs13 = rhs(1,3);
        rhs(1,0) = lhs11 * rhs10 + lhs12 * rhs(2,0);
        rhs(1,1) = lhs11 * rhs11 + lhs12 * rhs(2,1);
        rhs(1,2) = lhs11 * rhs12 + lhs12 * rhs(2,2);
        rhs(1,3) = lhs11 * rhs13 + lhs12 * rhs(2,3);
        
        rhs(2,0) = lhs21 * rhs10 + lhs22 * rhs(2,0);
        rhs(2,1) = lhs21 * rhs11 + lhs22 * rhs(2,1);
        rhs(2,2) = lhs21 * rhs12 + lhs22 * rhs(2,2);
        rhs(2,3) = lhs21 * rhs13 + lhs22 * rhs(2,3);
        
        // rhs(3,0) = rhs(3,0);
        // rhs(3,1) = rhs(3,1);
        // rhs(3,2) = rhs(3,2);
        // rhs(3,3) = rhs(3,3);
    }

    void Combine_IntoLHS(Float4x4& transform, RotationY rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs00 = cosine, lhs02 = sine;
        const float lhs20 = -sine,  lhs22 = cosine;
        // const float lhs11 = 1.f,    lhs33 = 1.f;
        Float4x4& rhs = transform;

        const float rhs00 = rhs(0,0), rhs01 = rhs(0,1), rhs02 = rhs(0,2), rhs03 = rhs(0,3);
        rhs(0,0) = lhs00 * rhs00 + lhs02 * rhs(2,0);
        rhs(0,1) = lhs00 * rhs01 + lhs02 * rhs(2,1);
        rhs(0,2) = lhs00 * rhs02 + lhs02 * rhs(2,2);
        rhs(0,3) = lhs00 * rhs03 + lhs02 * rhs(2,3);
        
        // rhs(1,0) = rhs(1,0);
        // rhs(1,1) = rhs(1,1);
        // rhs(1,2) = rhs(1,2);
        // rhs(1,3) = rhs(1,3);
        
        rhs(2,0) = lhs20 * rhs00 + lhs22 * rhs(2,0);
        rhs(2,1) = lhs20 * rhs01 + lhs22 * rhs(2,1);
        rhs(2,2) = lhs20 * rhs02 + lhs22 * rhs(2,2);
        rhs(2,3) = lhs20 * rhs03 + lhs22 * rhs(2,3);
        
        // rhs(3,0) = rhs(3,0);
        // rhs(3,1) = rhs(3,1);
        // rhs(3,2) = rhs(3,2);
        // rhs(3,3) = rhs(3,3);
    }

    void Combine_IntoLHS(Float4x4& transform, RotationZ rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs00 = cosine, lhs01 = -sine;
        const float lhs10 = sine,   lhs11 =  cosine;
        // const float lhs22 = 1.f,    lhs33 = 1.f;
        Float4x4& rhs          = transform;

        const float rhs00 = rhs(0,0), rhs01 = rhs(0,1), rhs02 = rhs(0,2), rhs03 = rhs(0,3);
        rhs(0,0) = lhs00 * rhs00 + lhs01 * rhs(1,0);
        rhs(0,1) = lhs00 * rhs01 + lhs01 * rhs(1,1);
        rhs(0,2) = lhs00 * rhs02 + lhs01 * rhs(1,2);
        rhs(0,3) = lhs00 * rhs03 + lhs01 * rhs(1,3);
        
        rhs(1,0) = lhs10 * rhs00 + lhs11 * rhs(1,0);
        rhs(1,1) = lhs10 * rhs01 + lhs11 * rhs(1,1);
        rhs(1,2) = lhs10 * rhs02 + lhs11 * rhs(1,2);
        rhs(1,3) = lhs10 * rhs03 + lhs11 * rhs(1,3);
        
        rhs(2,0) = rhs(2,0);
        rhs(2,1) = rhs(2,1);
        rhs(2,2) = rhs(2,2);
        rhs(2,3) = rhs(2,3);
        
        rhs(3,0) = rhs(3,0);
        rhs(3,1) = rhs(3,1);
        rhs(3,2) = rhs(3,2);
        rhs(3,3) = rhs(3,3);
    }

	void            Combine_IntoRHS(ArbitraryRotation rotation, Float4x4& transform)
	{
		// note -- inefficient implementation!
		transform = Combine(MakeRotationMatrix(rotation._axis, rotation._angle), transform);
	}
	
	void            Combine_IntoLHS(Float4x4& transform, ArbitraryRotation rotation)
	{
		// note -- inefficient implementation!
		transform = Combine(transform, MakeRotationMatrix(rotation._axis, rotation._angle));
	}

	void            Combine_IntoRHS(Quaternion rotation, Float4x4& transform)
	{
		// note -- inefficient implementation!
		// When using quaternions frequently, we're probably better off avoiding
		// matrix form until the last moment. That is, we should combine local to parent
		// transforms using a quaternion/translation/scale object (ie, ScaleRotationTranslationQ)
		transform = Combine(AsFloat3x3(rotation), transform);
	}

	void            Combine_IntoLHS(Float4x4& transform, Quaternion rotation)
	{
		// note -- inefficient implementation!
		transform = Combine(transform, AsFloat3x3(rotation));
	}

    void Combine_IntoLHS(Float4x4& transform, const UniformScale& scale)
    {
        transform(0,0) *= scale._scale; transform(0,1) *= scale._scale; transform(0,2) *= scale._scale; transform(0,3) *= scale._scale;
        transform(1,0) *= scale._scale; transform(1,1) *= scale._scale; transform(1,2) *= scale._scale; transform(1,3) *= scale._scale;
        transform(2,0) *= scale._scale; transform(2,1) *= scale._scale; transform(2,2) *= scale._scale; transform(2,3) *= scale._scale;
    }

    void Combine_IntoLHS(Float4x4& transform, const ArbitraryScale& scale)
    {
        transform(0,0) *= scale._scale[0]; transform(0,1) *= scale._scale[0]; transform(0,2) *= scale._scale[0]; transform(0,3) *= scale._scale[0];
        transform(1,0) *= scale._scale[1]; transform(1,1) *= scale._scale[1]; transform(1,2) *= scale._scale[1]; transform(1,3) *= scale._scale[1];
        transform(2,0) *= scale._scale[2]; transform(2,1) *= scale._scale[2]; transform(2,2) *= scale._scale[2]; transform(2,3) *= scale._scale[2];
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void Combine_IntoRHS(RotationX rotation, Float3x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs11 = cosine, rhs12 = -sine;
        const float rhs21 = sine,   rhs22 =  cosine;
        // const float rhs00 = 1.f,    rhs33 = 1.f;
        auto& lhs          = transform;

        // lhs(0,0) = lhs(0,0);
        const float lhs01 = lhs(0,1);
        lhs(0,1) = lhs01 * rhs11 + lhs(0,2) * rhs21;
        lhs(0,2) = lhs01 * rhs12 + lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3);

        // lhs(1,0) = lhs(1,0);
        const float lhs11 = lhs(1,1);
        lhs(1,1) = lhs11 * rhs11 + lhs(1,2) * rhs21;
        lhs(1,2) = lhs11 * rhs12 + lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3);

        // lhs(2,0) = lhs(2,0);
        const float lhs21 = lhs(2,1);
        lhs(2,1) = lhs21 * rhs11 + lhs(2,2) * rhs21;
        lhs(2,2) = lhs21 * rhs12 + lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3);

        // lhs(3,0) = lhs(3,0) * rhs33;
        // lhs(3,1) = lhs(3,1) * rhs33;
        // lhs(3,2) = lhs(3,2) * rhs33;
        // lhs(3,3) = lhs(3,3) * rhs33;
    }

    void Combine_IntoRHS(RotationY rotation, Float3x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs00 = cosine, rhs02 = sine;
        const float rhs20 = -sine,  rhs22 = cosine;
        // const float rhs11 = 1.f,    rhs33 = 1.f;
        auto& lhs          = transform;

        const float lhs00 = lhs(0,0);
        lhs(0,0) = lhs00 * rhs00 + lhs(0,2) * rhs20;
        // lhs(0,1) = lhs(0,1);
        lhs(0,2) = lhs00 * rhs02 + lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3);

        const float lhs10 = lhs(1,0);
        lhs(1,0) = lhs10 * rhs00 + lhs(1,2) * rhs20;
        // lhs(1,1) = lhs(1,1);
        lhs(1,2) = lhs10 * rhs02 + lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3);

        const float lhs20 = lhs(2,0);
        lhs(2,0) = lhs20 * rhs00 + lhs(2,2) * rhs20;
        // lhs(2,1) = lhs(2,1);
        lhs(2,2) = lhs20 * rhs02 + lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3);

        // lhs(3,0) = lhs(3,0);
        // lhs(3,1) = lhs(3,1);
        // lhs(3,2) = lhs(3,2);
        // lhs(3,3) = lhs(3,3);
    }

    void Combine_IntoRHS(RotationZ rotation, Float3x4& transform)
    {
            /*
                Following the OpenGL standard for rotation around an axis
                (also used by Collada).

                From the OpenGL Red Book:

                Multiplies the current matrix by a matrix that rotates an object (or the local coordinate system) in a 
                counterclockwise direction about the ray from the origin through the point (x, y, z).

                Holds true for right hand coordinate systems
            */
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

            //      Almost all values in rhs are zero. So we can simplify the math.
            //      (try to minimize how many elements we need to copy lhs into temporaries)
        const float rhs00 = cosine, rhs01 = -sine;
        const float rhs10 = sine,   rhs11 =  cosine;
        // const float rhs22 = 1.f,    rhs33 = 1.f;
        auto& lhs          = transform;

        const float lhs00 = lhs(0,0);
        lhs(0,0) = lhs00 * rhs00 + lhs(0,1) * rhs10;
        lhs(0,1) = lhs00 * rhs01 + lhs(0,1) * rhs11;
        // lhs(0,2) = lhs(0,2) * rhs22;
        // lhs(0,3) = lhs(0,3) * rhs33;

        const float lhs10 = lhs(1,0);
        lhs(1,0) = lhs10 * rhs00 + lhs(1,1) * rhs10;
        lhs(1,1) = lhs10 * rhs01 + lhs(1,1) * rhs11;
        // lhs(1,2) = lhs(1,2) * rhs22;
        // lhs(1,3) = lhs(1,3) * rhs33;

        const float lhs20 = lhs(2,0);
        lhs(2,0) = lhs20 * rhs00 + lhs(2,1) * rhs10;
        lhs(2,1) = lhs20 * rhs01 + lhs(2,1) * rhs11;
        // lhs(2,2) = lhs(2,2) * rhs22;
        // lhs(2,3) = lhs(2,3) * rhs33;

        // lhs(3,0) = lhs(3,0) * rhs33;
        // lhs(3,1) = lhs(3,1) * rhs33;
        // lhs(3,2) = lhs(3,2) * rhs33;
        // lhs(3,3) = lhs(3,3) * rhs33;
    }

    void Combine_IntoLHS(Float3x4& transform, RotationX rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs11 = cosine, lhs12 = -sine;
        const float lhs21 = sine,   lhs22 =  cosine;
        // const float lhs00 = 1.f,    lhs33 = 1.f;
        auto& rhs          = transform;

        // rhs(0,0) = rhs(0,0);
        // rhs(0,1) = rhs(0,1);
        // rhs(0,2) = rhs(0,2);
        // rhs(0,3) = rhs(0,3);
        
        const float rhs10 = rhs(1,0), rhs11 = rhs(1,1), rhs12 = rhs(1,2), rhs13 = rhs(1,3);
        rhs(1,0) = lhs11 * rhs10 + lhs12 * rhs(2,0);
        rhs(1,1) = lhs11 * rhs11 + lhs12 * rhs(2,1);
        rhs(1,2) = lhs11 * rhs12 + lhs12 * rhs(2,2);
        rhs(1,3) = lhs11 * rhs13 + lhs12 * rhs(2,3);
        
        rhs(2,0) = lhs21 * rhs10 + lhs22 * rhs(2,0);
        rhs(2,1) = lhs21 * rhs11 + lhs22 * rhs(2,1);
        rhs(2,2) = lhs21 * rhs12 + lhs22 * rhs(2,2);
        rhs(2,3) = lhs21 * rhs13 + lhs22 * rhs(2,3);
        
        // rhs(3,0) = rhs(3,0);
        // rhs(3,1) = rhs(3,1);
        // rhs(3,2) = rhs(3,2);
        // rhs(3,3) = rhs(3,3);
    }

    void Combine_IntoLHS(Float3x4& transform, RotationY rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs00 = cosine, lhs02 = sine;
        const float lhs20 = -sine,  lhs22 = cosine;
        // const float lhs11 = 1.f,    lhs33 = 1.f;
        auto& rhs = transform;

        const float rhs00 = rhs(0,0), rhs01 = rhs(0,1), rhs02 = rhs(0,2), rhs03 = rhs(0,3);
        rhs(0,0) = lhs00 * rhs00 + lhs02 * rhs(2,0);
        rhs(0,1) = lhs00 * rhs01 + lhs02 * rhs(2,1);
        rhs(0,2) = lhs00 * rhs02 + lhs02 * rhs(2,2);
        rhs(0,3) = lhs00 * rhs03 + lhs02 * rhs(2,3);
        
        // rhs(1,0) = rhs(1,0);
        // rhs(1,1) = rhs(1,1);
        // rhs(1,2) = rhs(1,2);
        // rhs(1,3) = rhs(1,3);
        
        rhs(2,0) = lhs20 * rhs00 + lhs22 * rhs(2,0);
        rhs(2,1) = lhs20 * rhs01 + lhs22 * rhs(2,1);
        rhs(2,2) = lhs20 * rhs02 + lhs22 * rhs(2,2);
        rhs(2,3) = lhs20 * rhs03 + lhs22 * rhs(2,3);
        
        // rhs(3,0) = rhs(3,0);
        // rhs(3,1) = rhs(3,1);
        // rhs(3,2) = rhs(3,2);
        // rhs(3,3) = rhs(3,3);
    }

    void Combine_IntoLHS(Float3x4& transform, RotationZ rotation)
    {
        float sine, cosine;
        std::tie(sine, cosine) = XlSinCos(rotation._angle);

        const float lhs00 = cosine, lhs01 = -sine;
        const float lhs10 = sine,   lhs11 =  cosine;
        // const float lhs22 = 1.f,    lhs33 = 1.f;
        auto& rhs          = transform;

        const float rhs00 = rhs(0,0), rhs01 = rhs(0,1), rhs02 = rhs(0,2), rhs03 = rhs(0,3);
        rhs(0,0) = lhs00 * rhs00 + lhs01 * rhs(1,0);
        rhs(0,1) = lhs00 * rhs01 + lhs01 * rhs(1,1);
        rhs(0,2) = lhs00 * rhs02 + lhs01 * rhs(1,2);
        rhs(0,3) = lhs00 * rhs03 + lhs01 * rhs(1,3);
        
        rhs(1,0) = lhs10 * rhs00 + lhs11 * rhs(1,0);
        rhs(1,1) = lhs10 * rhs01 + lhs11 * rhs(1,1);
        rhs(1,2) = lhs10 * rhs02 + lhs11 * rhs(1,2);
        rhs(1,3) = lhs10 * rhs03 + lhs11 * rhs(1,3);
        
        rhs(2,0) = rhs(2,0);
        rhs(2,1) = rhs(2,1);
        rhs(2,2) = rhs(2,2);
        rhs(2,3) = rhs(2,3);
        
        rhs(3,0) = rhs(3,0);
        rhs(3,1) = rhs(3,1);
        rhs(3,2) = rhs(3,2);
        rhs(3,3) = rhs(3,3);
    }

	void            Combine_IntoRHS(ArbitraryRotation rotation, Float3x4& transform)
	{
		// note -- inefficient implementation!
		transform = Combine(MakeRotationMatrix(rotation._axis, rotation._angle), transform);
	}
	
	void            Combine_IntoLHS(Float3x4& transform, ArbitraryRotation rotation)
	{
		// note -- inefficient implementation!
		transform = Combine(transform, MakeRotationMatrix(rotation._axis, rotation._angle));
	}

	void            Combine_IntoRHS(Quaternion rotation, Float3x4& transform)
	{
		// note -- inefficient implementation!
		transform = Combine(AsFloat3x3(rotation), transform);
	}

	void            Combine_IntoLHS(Float3x4& transform, Quaternion rotation)
	{
		// note -- inefficient implementation!
		transform = Combine(transform, AsFloat3x3(rotation));
	}

    void Combine_IntoLHS(Float3x4& transform, const UniformScale& scale)
    {
        transform(0,0) *= scale._scale; transform(0,1) *= scale._scale; transform(0,2) *= scale._scale; transform(0,3) *= scale._scale;
        transform(1,0) *= scale._scale; transform(1,1) *= scale._scale; transform(1,2) *= scale._scale; transform(1,3) *= scale._scale;
        transform(2,0) *= scale._scale; transform(2,1) *= scale._scale; transform(2,2) *= scale._scale; transform(2,3) *= scale._scale;
    }

    void Combine_IntoLHS(Float3x4& transform, const ArbitraryScale& scale)
    {
        transform(0,0) *= scale._scale[0]; transform(0,1) *= scale._scale[0]; transform(0,2) *= scale._scale[0]; transform(0,3) *= scale._scale[0];
        transform(1,0) *= scale._scale[1]; transform(1,1) *= scale._scale[1]; transform(1,2) *= scale._scale[1]; transform(1,3) *= scale._scale[1];
        transform(2,0) *= scale._scale[2]; transform(2,1) *= scale._scale[2]; transform(2,2) *= scale._scale[2]; transform(2,3) *= scale._scale[2];
    }

    void Combine_IntoRHS(const Float3& translate, Float3x4& transform)
    {
        auto& lhs = transform;
        // const float rhs00 = 1.f, rhs11 = 1.f, rhs22 = 1.f;
        const float rhs33 = 1.f;
        const float rhs03 = translate[0], rhs13 = translate[1], rhs23 = translate[2];

        // lhs(0,0) = lhs(0,0) * rhs00;
        // lhs(0,1) =                      lhs(0,1) * rhs11;
        // lhs(0,2) =                                          lhs(0,2) * rhs22;
        lhs(0,3) = lhs(0,0) * rhs03 +   lhs(0,1) * rhs13 +  lhs(0,2) * rhs23 +      lhs(0,3) * rhs33;

        // lhs(1,0) = lhs(1,0) * rhs00;
        // lhs(1,1) =                      lhs(1,1) * rhs11;
        // lhs(1,2) =                                          lhs(1,2) * rhs22;
        lhs(1,3) = lhs(1,0) * rhs03 +   lhs(1,1) * rhs13 +  lhs(1,2) * rhs23 +      lhs(1,3) * rhs33;

        // lhs(2,0) = lhs(2,0) * rhs00;
        // lhs(2,1) =                      lhs(2,1) * rhs11;
        // lhs(2,2) =                                          lhs(2,2) * rhs22;
        lhs(2,3) = lhs(2,0) * rhs03 +   lhs(2,1) * rhs13 +  lhs(2,2) * rhs23 +      lhs(2,3) * rhs33;

        // lhs(3,0) = lhs(3,0) * rhs00;
        // lhs(3,1) =                      lhs(3,1) * rhs11;
        // lhs(3,2) =                                          lhs(3,2) * rhs22;
        lhs(3,3) = lhs(3,0) * rhs03 +   lhs(3,1) * rhs13 +  lhs(3,2) * rhs23 +      lhs(3,3) * rhs33;
    }

    void Combine_IntoLHS(Float3x4& transform, const Float3& translate)
    {
        auto& rhs = transform;
        // const float lhs00 = 1.f, lhs11 = 1.f, lhs22 = 1.f;
        // const float lhs33 = 1.f;
        const float lhs03 = translate[0], lhs13 = translate[1], lhs23 = translate[2];

        rhs(0,0) = rhs(0,0) + lhs03 * rhs(3,0);
        rhs(0,1) = rhs(0,1) + lhs03 * rhs(3,1);
        rhs(0,2) = rhs(0,2) + lhs03 * rhs(3,2);
        rhs(0,3) = rhs(0,3) + lhs03 * rhs(3,3);
        
        rhs(1,0) = rhs(1,0) + lhs13 * rhs(3,0);
        rhs(1,1) = rhs(1,1) + lhs13 * rhs(3,1);
        rhs(1,2) = rhs(1,2) + lhs13 * rhs(3,2);
        rhs(1,3) = rhs(1,3) + lhs13 * rhs(3,3);
        
        rhs(2,0) = rhs(2,0) + lhs23 * rhs(3,0);
        rhs(2,1) = rhs(2,1) + lhs23 * rhs(3,1);
        rhs(2,2) = rhs(2,2) + lhs23 * rhs(3,2);
        rhs(2,3) = rhs(2,3) + lhs23 * rhs(3,3);
    }

    void Combine_IntoRHS(const UniformScale& scale, Float3x4& transform)
    {
        transform(0,0) *= scale._scale; transform(0,1) *= scale._scale; transform(0,2) *= scale._scale;
        transform(1,0) *= scale._scale; transform(1,1) *= scale._scale; transform(1,2) *= scale._scale;
        transform(2,0) *= scale._scale; transform(2,1) *= scale._scale; transform(2,2) *= scale._scale;
    }

    void Combine_IntoRHS(const ArbitraryScale& scale, Float3x4& transform)
    {
        auto& lhs = transform;
		const auto& rhs = scale._scale;
        lhs(0,0) *= rhs[0]; lhs(1,0) *= rhs[0]; lhs(2,0) *= rhs[0];
        lhs(0,1) *= rhs[1]; lhs(1,1) *= rhs[1]; lhs(2,1) *= rhs[1];
        lhs(0,2) *= rhs[2]; lhs(1,2) *= rhs[2]; lhs(2,2) *= rhs[2];
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    Float3          TransformPoint(const Float3x4& transform, Float3 pt)
    {
        return transform * Expand(pt, 1.f);
    }

    Float3          TransformPoint(const Float4x4& transform, Float3 pt)
    {
        return Truncate(transform * Expand(pt, 1.f));
    }

    Float3          TransformDirectionVector(const Float3x3& transform, Float3 pt)
    {
        return transform * pt;
    }

    Float3          TransformDirectionVector(const Float3x4& transform, Float3 pt)
    {
        return transform * Expand(pt, 0.f);
    }

    Float3          TransformDirectionVector(const Float4x4& transform, Float3 pt)
    {
        return Truncate(transform * Expand(pt, 0.f));
    }

    Float3          TransformPointByOrthonormalInverse(const Float3x4& transform, Float3 pt)
    {
        assert(IsOrthonormal(Truncate3x3(transform)));
        float t[3];
        t[0] = transform(0,0) * -transform(0,3) + transform(1,0) * -transform(1,3) + transform(2,0) * -transform(2,3);
        t[1] = transform(0,1) * -transform(0,3) + transform(1,1) * -transform(1,3) + transform(2,1) * -transform(2,3);
        t[2] = transform(0,2) * -transform(0,3) + transform(1,2) * -transform(1,3) + transform(2,2) * -transform(2,3);

        Float3 result;
        result[0] = transform(0,0) * pt[0] + transform(1,0) * pt[1] + transform(2,0) * pt[2] + t[0];
        result[1] = transform(0,1) * pt[0] + transform(1,1) * pt[1] + transform(2,1) * pt[2] + t[1];
        result[2] = transform(0,2) * pt[0] + transform(1,2) * pt[1] + transform(2,2) * pt[2] + t[2];
        return result;
    }

    Float3          TransformPointByOrthonormalInverse(const Float4x4& transform, Float3 pt)
    {
        assert(IsOrthonormal(Truncate3x3(transform)));
        float t[3];
        t[0] = transform(0,0) * -transform(0,3) + transform(1,0) * -transform(1,3) + transform(2,0) * -transform(2,3);
        t[1] = transform(0,1) * -transform(0,3) + transform(1,1) * -transform(1,3) + transform(2,1) * -transform(2,3);
        t[2] = transform(0,2) * -transform(0,3) + transform(1,2) * -transform(1,3) + transform(2,2) * -transform(2,3);

        Float3 result;
        result[0] = transform(0,0) * pt[0] + transform(1,0) * pt[1] + transform(2,0) * pt[2] + t[0];
        result[1] = transform(0,1) * pt[0] + transform(1,1) * pt[1] + transform(2,1) * pt[2] + t[1];
        result[2] = transform(0,2) * pt[0] + transform(1,2) * pt[1] + transform(2,2) * pt[2] + t[2];
        return result;
    }


    bool IsOrthonormal(const Float3x3& rotationMatrix, float tolerance)
    {
        Float3 A(rotationMatrix(0,0), rotationMatrix(0,1), rotationMatrix(0,2));
        Float3 B(rotationMatrix(1,0), rotationMatrix(1,1), rotationMatrix(1,2));
        Float3 C(rotationMatrix(2,0), rotationMatrix(2,1), rotationMatrix(2,2));
        return  Equivalent(Dot(A,B), 0.f, tolerance)
            &&  Equivalent(Dot(A,C), 0.f, tolerance)
            &&  Equivalent(Dot(B,C), 0.f, tolerance)
            &&  Equivalent(MagnitudeSquared(A), 1.f, tolerance)
            &&  Equivalent(MagnitudeSquared(B), 1.f, tolerance)
            &&  Equivalent(MagnitudeSquared(C), 1.f, tolerance)
            ;
    }

    Float4x4 InvertOrthonormalTransform(const Float4x4& input)
    {
            //
            //      Given a standard orthonormal transformation matrix, we can calculate
            //      the inverse quickly...
            //

        float t[3];
        t[0] = input(0,0) * -input(0,3) + input(1,0) * -input(1,3) + input(2,0) * -input(2,3);
        t[1] = input(0,1) * -input(0,3) + input(1,1) * -input(1,3) + input(2,1) * -input(2,3);
        t[2] = input(0,2) * -input(0,3) + input(1,2) * -input(1,3) + input(2,2) * -input(2,3);
        Float4x4 result;
        result(0,0) = input(0,0);   result(0,1) = input(1,0);   result(0,2) = input(2,0);   result(0,3) = t[0];
        result(1,0) = input(0,1);   result(1,1) = input(1,1);   result(1,2) = input(2,1);   result(1,3) = t[1];
        result(2,0) = input(0,2);   result(2,1) = input(1,2);   result(2,2) = input(2,2);   result(2,3) = t[2];
        result(3,0) = 0.f; result(3,1) = 0.f; result(3,2) = 0.f; result(3,3) = 1.f;
        return result;
    }

    Float3x4 InvertOrthonormalTransform(const Float3x4& input)
    {
        float t[3];
        t[0] = input(0,0) * -input(0,3) + input(1,0) * -input(1,3) + input(2,0) * -input(2,3);
        t[1] = input(0,1) * -input(0,3) + input(1,1) * -input(1,3) + input(2,1) * -input(2,3);
        t[2] = input(0,2) * -input(0,3) + input(1,2) * -input(1,3) + input(2,2) * -input(2,3);
        Float3x4 result;
        result(0,0) = input(0,0);   result(0,1) = input(1,0);   result(0,2) = input(2,0);   result(0,3) = t[0];
        result(1,0) = input(0,1);   result(1,1) = input(1,1);   result(1,2) = input(2,1);   result(1,3) = t[1];
        result(2,0) = input(0,2);   result(2,1) = input(1,2);   result(2,2) = input(2,2);   result(2,3) = t[2];
        return result;
    }

    Float2x3 InvertOrthonormalTransform(const Float2x3& input)
    {
        float t[2];
        t[0] = input(0,0) * -input(0,3) + input(1,0) * -input(1,3);
        t[1] = input(0,1) * -input(0,3) + input(1,1) * -input(1,3);
        Float2x3 result;
        result(0,0) = input(0,0);   result(0,1) = input(1,0);   result(0,2) = t[0];
        result(1,0) = input(0,1);   result(1,1) = input(1,1);   result(1,2) = t[1];
        return result;
    }

    Float4          TransformPlane(const Float4x4& transform, Float4 plane)
    {
        return  Transpose(Inverse(transform)) * plane;
    }
        ////////////////////////////////////////////////////////////////

    ArbitraryRotation::ArbitraryRotation(const Float3x3& rotationMatrix)
    {
        // Assuming the input is a orthonormal rotation matrix, let's extract
        // the axis/angle form.
        assert(IsOrthonormal(rotationMatrix));
        cml::matrix_to_axis_angle(rotationMatrix, _axis, _angle);
    }

        ////////////////////////////////////////////////////////////////
    
    static Float3x3 SqRootSymmetric(const Float3x3& usq)
    {
        // Find the squareroot of the input matrix
        // This only works correctly for symmetric matrices (nonsymmetic
        // matrices will have complex values in their square root)

        Eigen<float> kES(3);
		kES(0,0) = usq(0,0);
		kES(0,1) = usq(0,1);
		kES(0,2) = usq(0,2);
		kES(1,0) = usq(1,0);
		kES(1,1) = usq(1,1);
		kES(1,2) = usq(1,2);
		kES(2,0) = usq(2,0);
		kES(2,1) = usq(2,1);
		kES(2,2) = usq(2,2);
		kES.EigenStuff3();

        auto* eigenValues = kES.GetEigenvalues();
        Float3x3 Udash(
            XlSqrt(eigenValues[0]), 0.f, 0.f,
            0.f, XlSqrt(eigenValues[1]), 0.f,
            0.f, 0.f, XlSqrt(eigenValues[2]));
        auto Q = Truncate3x3(kES.GetEigenvectors());
        auto U = Transpose(Q) * Udash * Q;
        return U;
    }

    template<typename DestType> DestType Convert(const Float3x3& input);

    template<> 
        Float3x3 Convert(const Float3x3& input) { return input; }

    template<> 
        Quaternion Convert(const Float3x3& input)
        {
            Quaternion result;
            cml::quaternion_rotation_matrix(result, input);
            return result;
        }

    static bool AssumingSymmetricIsDiagonal(const Float3x3& input, float threshold)
    {
        // assuming the input is symmetric, is it diagonal?
        return  XlAbs(input(0,1)) < threshold
            &&  XlAbs(input(0,2)) < threshold
            &&  XlAbs(input(1,2)) < threshold;
    }

    template<typename RotationType>
        ScaleRotationTranslation<RotationType>::ScaleRotationTranslation(
            const Float4x4& copyFrom)
    {
        // Using RU decomposition to separate the rotation and scale part.
        // See reference here:
        // http://www.continuummechanics.org/cm/polardecomposition.html
        // & http://callumhay.blogspot.com/2010/10/decomposing-affine-transforms.html
        //
        // Since RU decomposition is quite expensive, we will check for 
        // a simple case when there is no skew. When there is no skew,
        // the calculations collapse to something much simplier.
        //
        // However note that this method calculates the eigen vectors for
        // one matrix, and then inverts another matrix. It seems like the 
        // quantity of calculations could introduce some floating point creep.
        // It might be better if we could find a way to calculate R without the 
        // matrix invert.

        Float3x3 F = Truncate3x3(copyFrom);
        auto usq = LeftMultiplyByTranspose(F);      // usq = Transpose(F) * F;

            // Here, usq should always be symmetric
            // If it is diagonal, we can simply the math greatly
            // (taking the squareroot becomes trivial, as does building the inverse)

        Float3x3 rotPart;
        const float diagThreshold = 1e-4f;  // we can give a little leaway here
        if (AssumingSymmetricIsDiagonal(usq, diagThreshold)) {
                // To take the square root of a diagonal matrix, we just have to
                // take the square root of the diagonal elements.
                // Since the diagonal parts of usq are the dot products of the 
                // columns of F, this means that calculating _scale is the same as
                // 3 vector magnitude calculations -- which is perfectly logical!
                //
                // And to make the inverse of a diagonal matrix, we just have to
                // take the reciprocals of the diagonal elements. So, we end up
                // normalizing the columns of the rotation matrix.
                //
                // So this case factors out into a much simplier result --
            _scale = Float3(XlSqrt(usq(0,0)), XlSqrt(usq(1,1)), XlSqrt(usq(2,2)));
            rotPart = Float3x3(
                F(0,0)/_scale[0], F(0,1)/_scale[1], F(0,2)/_scale[2],
                F(1,0)/_scale[0], F(1,1)/_scale[1], F(1,2)/_scale[2],
                F(2,0)/_scale[0], F(2,1)/_scale[1], F(2,2)/_scale[2]);
        } else {
                // This path is much more complex... The matrix has some skew on it.
                // After we extract the skew, we just ignore it. But by separating
                // it from scale & rotation, it means we up with a well formed final
                // result.
            auto U = SqRootSymmetric(usq); // U is our decomposed scale part.
            rotPart = F * Inverse(U);
            _scale = Float3(U(0,0), U(1,1), U(2,2));
        }

            // If "rotPart" is equal to identity, apart from sign values, then 
            // we should move the sign into "scale"
            // This is required for common cases where we want to extract the
            // scale from a matrix that has no rotation.
        const float identityThreshold = 1e-4f;
        if (    Equivalent(rotPart(0,1), 0.f, identityThreshold)
            &&  Equivalent(rotPart(0,2), 0.f, identityThreshold)
            &&  Equivalent(rotPart(1,0), 0.f, identityThreshold)
            &&  Equivalent(rotPart(1,2), 0.f, identityThreshold)
            &&  Equivalent(rotPart(2,0), 0.f, identityThreshold)
            &&  Equivalent(rotPart(2,1), 0.f, identityThreshold)) {
            if (rotPart(0,0)<0.f) { _scale[0] *= -1.f; rotPart(0,0) *= -1.f; }
            if (rotPart(1,1)<0.f) { _scale[1] *= -1.f; rotPart(1,1) *= -1.f; }
            if (rotPart(2,2)<0.f) { _scale[2] *= -1.f; rotPart(2,2) *= -1.f; }
        }

        _rotation = Convert<RotationType>(rotPart);
        _translation = ExtractTranslation(copyFrom);
    }

    template<typename RotationType>
        ScaleRotationTranslation<RotationType>::ScaleRotationTranslation(
            const Float4x4& copyFrom, bool& goodDecomposition)
    {
        Float3x3 F = Truncate3x3(copyFrom);
        auto usq = LeftMultiplyByTranspose(F);

        Float3x3 rotPart;
        const float diagThreshold = 1e-4f;
        if (AssumingSymmetricIsDiagonal(usq, diagThreshold)) {
            _scale = Float3(XlSqrt(usq(0,0)), XlSqrt(usq(1,1)), XlSqrt(usq(2,2)));
            rotPart = Float3x3(
                F(0,0)/_scale[0], F(0,1)/_scale[1], F(0,2)/_scale[2],
                F(1,0)/_scale[0], F(1,1)/_scale[1], F(1,2)/_scale[2],
                F(2,0)/_scale[0], F(2,1)/_scale[1], F(2,2)/_scale[2]);
            goodDecomposition = true;
        } else {
            auto U = SqRootSymmetric(usq); // U is our decomposed scale part.
            rotPart = F * Inverse(U);
            _scale = Float3(U(0,0), U(1,1), U(2,2));

                // Do a final test for skew... Sometimes when we get here,
                // the skew is insignificant.
                // U should be symmetric, as well. We just want to see if
                // it is diagonal.
            const float skewThreshold = 1e-4f;
            goodDecomposition = AssumingSymmetricIsDiagonal(U, skewThreshold);
        }

        const float identityThreshold = 1e-4f;
        if (    Equivalent(rotPart(0,1), 0.f, identityThreshold)
            &&  Equivalent(rotPart(0,2), 0.f, identityThreshold)
            &&  Equivalent(rotPart(1,0), 0.f, identityThreshold)
            &&  Equivalent(rotPart(1,2), 0.f, identityThreshold)
            &&  Equivalent(rotPart(2,0), 0.f, identityThreshold)
            &&  Equivalent(rotPart(2,1), 0.f, identityThreshold)) {
            if (rotPart(0,0)<0.f) { _scale[0] *= -1.f; rotPart(0,0) *= -1.f; }
            if (rotPart(1,1)<0.f) { _scale[1] *= -1.f; rotPart(1,1) *= -1.f; }
            if (rotPart(2,2)<0.f) { _scale[2] *= -1.f; rotPart(2,2) *= -1.f; }
        }

        _rotation = Convert<RotationType>(rotPart);
        _translation = ExtractTranslation(copyFrom);
    }

    template<typename RotationType>
        ScaleRotationTranslation<RotationType>::ScaleRotationTranslation(const Float3x4& copyFrom)
    : ScaleRotationTranslation(AsFloat4x4(copyFrom))
    {}
    template<typename RotationType>
        ScaleRotationTranslation<RotationType>::ScaleRotationTranslation(const Float3x4& copyFrom, bool& goodDecomposition)
    : ScaleRotationTranslation(AsFloat4x4(copyFrom), goodDecomposition)
    {}

    template class ScaleRotationTranslation<Quaternion>;
    template class ScaleRotationTranslation<Float3x3>;

    ScaleRotationTranslationQ SphericalInterpolate(
        const ScaleRotationTranslationQ& lhs, const ScaleRotationTranslationQ& rhs, float alpha)
    {
        return ScaleRotationTranslationQ(
            LinearInterpolate(lhs._scale, rhs._scale, alpha),
            SphericalInterpolate(lhs._rotation, rhs._rotation, alpha),
            LinearInterpolate(lhs._translation, rhs._translation, alpha));
    }

    static float GetMedianElement(Float3 input)
    {
        Float3 absv(XlAbs(input[0]), XlAbs(input[1]), XlAbs(input[2]));
        if (absv[0] < absv[1]) {
            if (absv[2] < absv[0]) return input[0];
            if (absv[2] < absv[1]) return input[2];
            return input[1];
        } else {
            if (absv[2] > absv[0]) return input[0];
            if (absv[2] > absv[1]) return input[2];
            return input[1];
        }
    }

    float ExtractUniformScaleFast(const Float3x4& input)
    {
            // Get a uniform scale value from a given local to world matrix
            // If we assume no skew, then we can get the scale value by taking
            // the magnitude of the column vectors.
            //      (see ScaleRotationTranslation for more information)
            // If non-uniform, we will estimate with the median scale.
        Float3 scaleSq(
            input(0,0) * input(0,0) + input(1,0) * input(1,0) + input(2,0) * input(2,0),
            input(0,1) * input(0,1) + input(1,1) * input(1,1) + input(2,1) * input(2,1),
            input(0,2) * input(0,2) + input(1,2) * input(1,2) + input(2,2) * input(2,2));
        return XlSqrt(GetMedianElement(scaleSq));
    }

    Float3x3   AsFloat3x3(const Quaternion& input)
    {
        Float3x3 result;
        cml::matrix_rotation_quaternion(result, input);
        return result;
    }

    Float4x4    AsFloat4x4(const ScaleTranslation& input)
    {
        Float3 s = input._scale;
        Float4x4 result(
            s[0], 0.f, 0.f, input._translation[0],
            0.f, s[1], 0.f, input._translation[1],
            0.f, 0.f, s[2], input._translation[2],
            0.f, 0.f, 0.f, 1.f);
        return result;
    }

    Float4x4    AsFloat4x4(const UniformScaleYRotTranslation& input)
    {
        float sinTheta, cosTheta;
        std::tie(sinTheta, cosTheta) = XlSinCos(input._yRotation);
        return MakeFloat4x4(
            input._scale * cosTheta, 0.0f, input._scale * sinTheta, input._translation[0],
            0.0f, input._scale, 0.0f, input._translation[1],
            input._scale * -sinTheta, 0.0f, input._scale * cosTheta, input._translation[2],
            0.0f, 0.0f, 0.0f, 1.0f);
    }

    Float4x4   AsFloat4x4(const ScaleRotationTranslationQ& input)
    {
            //
            //          Convert from our separate rotation/scale/translation representation to
            //          a general 4x4 matrix
            //
        Float3x3 rotationPart = AsFloat3x3(input._rotation);
        const Float3& s = input._scale;
        return Float4x4(
            rotationPart(0,0) * s[0], rotationPart(0,1) * s[1], rotationPart(0,2) * s[2], input._translation[0],
            rotationPart(1,0) * s[0], rotationPart(1,1) * s[1], rotationPart(1,2) * s[2], input._translation[1],
            rotationPart(2,0) * s[0], rotationPart(2,1) * s[1], rotationPart(2,2) * s[2], input._translation[2],
            0.f, 0.f, 0.f, 1.f);
    }

    Float4x4   AsFloat4x4(const ScaleRotationTranslationM& input)
    {
        const Float3x3& rotationPart = input._rotation;
        const Float3& s = input._scale;
        return Float4x4(
            rotationPart(0,0) * s[0], rotationPart(0,1) * s[1], rotationPart(0,2) * s[2], input._translation[0],
            rotationPart(1,0) * s[0], rotationPart(1,1) * s[1], rotationPart(1,2) * s[2], input._translation[1],
            rotationPart(2,0) * s[0], rotationPart(2,1) * s[1], rotationPart(2,2) * s[2], input._translation[2],
            0.f, 0.f, 0.f, 1.f);
    }

	Float3x4   AsFloat3x4(const ScaleRotationTranslationQ& input)
    {
            //
            //          Convert from our separate rotation/scale/translation representation to
            //          a general 4x4 matrix
            //
        Float3x3 rotationPart = AsFloat3x3(input._rotation);
        const Float3& s = input._scale;
        return MakeFloat3x4(
            rotationPart(0,0) * s[0], rotationPart(0,1) * s[1], rotationPart(0,2) * s[2], input._translation[0],
            rotationPart(1,0) * s[0], rotationPart(1,1) * s[1], rotationPart(1,2) * s[2], input._translation[1],
            rotationPart(2,0) * s[0], rotationPart(2,1) * s[1], rotationPart(2,2) * s[2], input._translation[2]);
    }

    Float3x4   AsFloat3x4(const ScaleRotationTranslationM& input)
    {
        const Float3x3& rotationPart = input._rotation;
        const Float3& s = input._scale;
        return MakeFloat3x4(
            rotationPart(0,0) * s[0], rotationPart(0,1) * s[1], rotationPart(0,2) * s[2], input._translation[0],
            rotationPart(1,0) * s[0], rotationPart(1,1) * s[1], rotationPart(1,2) * s[2], input._translation[1],
            rotationPart(2,0) * s[0], rotationPart(2,1) * s[1], rotationPart(2,2) * s[2], input._translation[2]);
    }

    Float4x4   AsFloat4x4(const UniformScale& input)
    {
        return Float4x4(
            input._scale, 0.f, 0.f, 0.f,
            0.f, input._scale, 0.f, 0.f,
            0.f, 0.f, input._scale, 0.f,
            0.f, 0.f, 0.f, 1.f);
    }

    Float4x4    AsFloat4x4(const Float3& translation)
    {
        return Float4x4(
            1.f, 0.f, 0.f, translation[0],
            0.f, 1.f, 0.f, translation[1],
            0.f, 0.f, 1.f, translation[2],
            0.f, 0.f, 0.f, 1.f);
    }

    Float4x4    AsFloat4x4(const RotationX& input)
    {
        auto result = Identity<Float4x4>();
        Combine_IntoLHS(result, input);
        return result;
    }

    Float4x4    AsFloat4x4(const RotationY& input)
    {
        auto result = Identity<Float4x4>();
        Combine_IntoLHS(result, input);
        return result;
    }

    Float4x4    AsFloat4x4(const RotationZ& input)
    {
        auto result = Identity<Float4x4>();
        Combine_IntoLHS(result, input);
        return result;
    }

    Float4x4    AsFloat4x4(const ArbitraryRotation& input)
    {
        return AsFloat4x4(MakeRotationMatrix(input._axis, input._angle));
    }

    Float4x4    AsFloat4x4(const ArbitraryScale& input)
    {
        return Float4x4(
            input._scale[0], 0.f, 0.f, 0.f,
            0.f, input._scale[1], 0.f, 0.f,
            0.f, 0.f, input._scale[2], 0.f,
            0.f, 0.f, 0.f, 1.f);
    }

    Float3x4    AsFloat3x4(const Float3& translation)
    {
        return MakeFloat3x4(
            1.f, 0.f, 0.f, translation[0],
            0.f, 1.f, 0.f, translation[1],
            0.f, 0.f, 1.f, translation[2]);
    }

	Float4x4    AsFloat4x4(const Quaternion& input)
	{
			// todo -- better implementation possible
		return AsFloat4x4(AsFloat3x3(input));
	}

	Float4x4    AsFloat4x4(const Float3x3& rotationMatrix)
	{
		return Float4x4(
			rotationMatrix(0, 0), rotationMatrix(0, 1), rotationMatrix(0, 2), 0.f,
			rotationMatrix(1, 0), rotationMatrix(1, 1), rotationMatrix(1, 2), 0.f,
			rotationMatrix(2, 0), rotationMatrix(2, 1), rotationMatrix(2, 2), 0.f,
			0.f, 0.f, 0.f, 1.f);
	}

    Float4x4    AsFloat4x4(const Float3x4& orthonormalTransform)
    {
        return Float4x4(
            orthonormalTransform(0,0), orthonormalTransform(0,1), orthonormalTransform(0,2), orthonormalTransform(0,3), 
            orthonormalTransform(1,0), orthonormalTransform(1,1), orthonormalTransform(1,2), orthonormalTransform(1,3), 
            orthonormalTransform(2,0), orthonormalTransform(2,1), orthonormalTransform(2,2), orthonormalTransform(2,3), 
            0.f, 0.f, 0.f, 1.f);
    }

    Float3x4    AsFloat3x4(const Float4x4& orthonormalTransform)
    {
            //  Make sure the last row is the same as the last row
            //  that we assume when using Float3x4 as a transformation
            //  matrix.
        assert(Equivalent(orthonormalTransform(3, 0), 0.f, 1e-6f));
        assert(Equivalent(orthonormalTransform(3, 1), 0.f, 1e-6f));
        assert(Equivalent(orthonormalTransform(3, 2), 0.f, 1e-6f));
        assert(Equivalent(orthonormalTransform(3, 3), 1.f, 1e-6f));
        return MakeFloat3x4(
            orthonormalTransform(0,0), orthonormalTransform(0,1), orthonormalTransform(0,2), orthonormalTransform(0,3), 
            orthonormalTransform(1,0), orthonormalTransform(1,1), orthonormalTransform(1,2), orthonormalTransform(1,3), 
            orthonormalTransform(2,0), orthonormalTransform(2,1), orthonormalTransform(2,2), orthonormalTransform(2,3));
    }

    Float4x4    AsFloat4x4(const Float2x3& input)
    {
        return Float4x4(
            input(0,0), input(0,1), 0.f, input(0,2),
            input(1,0), input(1,1), 0.f, input(1,2),
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f);
    }

    Float4x4 MakeCameraToWorld(const Float3& forward, const Float3& up, const Float3& right, const Float3& position)
    {
        return Float4x4(
            right[0], up[0], -forward[0], position[0],
            right[1], up[1], -forward[1], position[1],
            right[2], up[2], -forward[2], position[2],
            0.f, 0.f, 0.f, 1.f);
    }

    Float4x4 MakeCameraToWorld(const Float3& forward, const Float3& up, const Float3& position)
    {
        Float3 right = Cross(forward, up);
        if (XlAbs(MagnitudeSquared(right)) < 1e-10f) {
                // If forward and up are perpendicular, right will be zero
                // length (or close to zero).
                // We need to pick another up in this case
                //  -- ideally the caller would provide a better up vector
            right = Cross(forward, Float3(0.f, 1.f, 0.f));
            if (XlAbs(MagnitudeSquared(right)) < 1e-10f)
                right = Cross(forward, Float3(1.f, 0.f, 0.f));
        }

        Float3 adjustedUp       = Normalize(Cross(right, forward));
        Float3 adjustedRight    = Normalize(Cross(forward, adjustedUp));

        return MakeCameraToWorld(forward, adjustedUp, adjustedRight, position);
    }

	Float4x4 MakeObjectToWorld(const Float3& forward, const Float3& up, const Float3& right, const Float3& position)
	{
		return Float4x4(
			right[0], forward[0], up[0], position[0],
			right[1], forward[1], up[1], position[1],
			right[2], forward[2], up[2], position[2],
			0.f, 0.f, 0.f, 1.f);
	}

	Float4x4    MakeObjectToWorld(const Float3& forward, const Float3& up, const Float3& position)
	{
		Float3 right = Cross(forward, up);
		if (XlAbs(MagnitudeSquared(right)) < 1e-10f) {
			// If forward and up are perpendicular, right will be zero
			// length (or close to zero).
			// We need to pick another up in this case
			//  -- ideally the caller would provide a better up vector
			right = Cross(forward, Float3(0.f, 1.f, 0.f));
			if (XlAbs(MagnitudeSquared(right)) < 1e-10f)
				right = Cross(forward, Float3(1.f, 0.f, 0.f));
		}

		Float3 adjustedUp = Normalize(Cross(right, forward));
		Float3 adjustedRight = Normalize(Cross(forward, adjustedUp));

		return MakeObjectToWorld(forward, adjustedUp, adjustedRight, position);
	}

    signed ArbitraryRotation::IsRotationX() const
    {
            // This math only works correctly if the axis is of a reasonable
            // length (we're expecting unit length in most case)
            // If y and z are very close to zero, and the vector magnitude
            // is reasonable, then it must lay on the cardinal x axis somewhere.
        assert(MagnitudeSquared(_axis) > 0.25f);

        const float epsilon = 1e-5f;
        if (    !Equivalent(_axis[1], 0.f, epsilon)
            ||  !Equivalent(_axis[2], 0.f, epsilon))
            return 0;

        return std::signbit(_axis[0]) ? -1 : 1;
    }

    signed ArbitraryRotation::IsRotationY() const
    {
        assert(MagnitudeSquared(_axis) > 0.25f);

        const float epsilon = 1e-5f;
        if (    !Equivalent(_axis[0], 0.f, epsilon)
            ||  !Equivalent(_axis[2], 0.f, epsilon))
            return 0;

        return std::signbit(_axis[1]) ? -1 : 1;
    }

    signed ArbitraryRotation::IsRotationZ() const
    {
        assert(MagnitudeSquared(_axis) > 0.25f);

        const float epsilon = 1e-5f;
        if (    !Equivalent(_axis[0], 0.f, epsilon)
            ||  !Equivalent(_axis[1], 0.f, epsilon))
            return 0;

        return std::signbit(_axis[2]) ? -1 : 1;
    }

}
