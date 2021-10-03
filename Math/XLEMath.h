// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/SelectConfiguration.h"
#include "../Core/Prefix.h"
#include <tuple>
#include <cmath>
#include <assert.h>
#include <algorithm>        // (for std::min/std::max)

#define MATHLIBRARY_CML         2
#define MATHLIBRARY_ACTIVE      MATHLIBRARY_CML

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
    #include <intrin.h>
#endif

// note -   namespace "::Math" causes problems with with .net "::Math"
//          it's a common name, so it might be used in other foreign libraries
//          as well. To avoid problems, let's prefer less generic names for root
//          namespaces.
namespace XLEMath
{
        //
        //      Useful constants. Some of these can be accessed from
        //      <math.h> by setting "_USE_MATH_DEFINES". But that just seems
        //      silly.
        //
    static const float gE               = 2.71828182845904523536f;
    static const float gLog2E           = 1.44269504088896340736f;
    static const float gLog10E          = 0.434294481903251827651f;
    static const float gLn2             = 0.693147180559945309417f;
    static const float gLn10            = 2.30258509299404568402f;
    static const float gPI              = 3.14159265358979323846f;
    static const float gHalfPI          = 1.57079632679489661923f;
    static const float gQuarterPI       = 0.785398163397448309616f;
    static const float gSqrt2           = 1.41421356237309504880f;
    static const float gReciprocalSqrt2 = 0.707106781186547524401f;
    static const float gSqrtHalf        = 0.707106781186547524401f;

        //
        //      Prefer Xl... functions over using the standard library math
        //      functions directly.
        //
    inline float XlSin(float radians)                   { return std::sin(radians); }
    inline float XlCos(float radians)                   { return std::cos(radians); }
    inline float XlTan(float radians)                   { return std::tan(radians); }
    inline float XlASin(float x)                        { return std::asin(x); }
    inline float XlACos(float x)                        { return std::acos(x); }
    inline float XlATan(float x)                        { return std::atan(x); }
    inline float XlATan2(float y, float x)              { return std::atan2(y, x); }
    inline float XlCotangent(float radians)             { return 1.f/std::tan(radians); }
    inline float XlFMod(float value, float modulo)      { return std::fmod(value, modulo); }
    inline float XlAbs(float value)                     { return std::abs(value); }
    inline float XlFloor(float value)                   { return std::floor(value); }
    inline float XlCeil(float value)                    { return std::ceil(value); }
    inline float XlExp(float value)                     { return std::exp(value); }
    inline float XlLog(float value)                     { return std::log(value); }

    T1(Primitive) inline Primitive XlSqrt(Primitive value)      { return std::sqrt(value); }
    T1(Primitive) inline Primitive XlRSqrt(Primitive value)     { return Primitive(1) / std::sqrt(value); }  // no standard reciprocal sqrt?

    T1(Primitive) inline bool XlRSqrt_Checked(Primitive* output, Primitive value)                   
    {
        assert(output);
            // this is used by Normalize_Checked to check for vectors
            // that are too small to be normalized correctly (and other
            // situations where floating point accuracy becomes questionable)
            // The epsilon value is a little arbitrary
        if (value > Primitive(-1e-15) && value < Primitive(1e-15)) return false;
        *output = 1.f / std::sqrt(value); 
        return true;
    }

    T1(Primitive) inline Primitive XlAbs(Primitive value)     { return std::abs(value); }

    inline std::tuple<float, float> XlSinCos(float angle)
    {
        return std::make_tuple(XlSin(angle), XlCos(angle));
    }

    inline float Deg2Rad(float input)               { return input / 180.f * gPI; }
    inline float Rad2Deg(float input)               { return input * 180.f / gPI; }

        //
        //      Useful general math functions:
        //
        //          Clamp(value, min, max)          --  returns a value clamped between the given limits
        //          Equivalent(A, B, tolerance)     --  returns true iff A and B are within "tolerance". 
        //                                              Useful for checking for equality between float types
        //          LinearInterpolate(A, B, alpha)  --  linearly interpolate between two values
        //                                              Like HLSL "lerp" built-in function
        //          Identity<Type>                  --  returns the identity of a given object
        //
        //      These functions are specialised for many different types. They come in handy for
        //      a wide range of math operations.
        //

    template<typename Type>
        inline bool Equivalent(Type a, Type b, Type tolerance) 
    {
        Type d = a-b;
        return d < tolerance && d > -tolerance;
    }

    template < typename T >
    T Clamp(T value, T minval, T maxval) {
        return std::max(std::min(value, maxval), minval);
    }

    inline float LinearInterpolate(float lhs, float rhs, float alpha)
    {
        return (rhs - lhs) * alpha + lhs;
    }

    inline int LinearInterpolate(int lhs, int rhs, float alpha)
    {
        return int((rhs - lhs) * alpha + .5f) + lhs;
    }

    inline int64_t LinearInterpolate(int64_t lhs, int64_t rhs, float alpha)
    {
        return int((rhs - lhs) * alpha + .5f) + lhs;
    }

    #if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
        inline float BranchlessMin(float a, float b)
        {
            _mm_store_ss( &a, _mm_min_ss(_mm_set_ss(a),_mm_set_ss(b)) );
            return a;
        }

        inline float BranchlessMax(float a, float b)
        {
            _mm_store_ss( &a, _mm_max_ss(_mm_set_ss(a),_mm_set_ss(b)) );
            return a;
        }

        inline float BranchlessClamp(float val, float minval, float maxval)
        {
            _mm_store_ss( &val, _mm_min_ss( _mm_max_ss(_mm_set_ss(val),_mm_set_ss(minval)), _mm_set_ss(maxval) ) );
            return val;
        }
    #else
        inline float BranchlessMin(float a, float b) { return std::min(a, b); }
        inline float BranchlessMax(float a, float b) { return std::max(a, b); }
        inline float BranchlessClamp(float val, float minval, float maxval) { return Clamp(val, minval, maxval); }
    #endif

    template<typename Type> const Type& Identity();
    template<typename Type> const Type& Zero();
}

using namespace XLEMath;

