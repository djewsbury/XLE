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
    static constexpr float gE               = 2.71828182845904523536f;
    static constexpr float gLog2E           = 1.44269504088896340736f;
    static constexpr float gLog10E          = 0.434294481903251827651f;
    static constexpr float gLn2             = 0.693147180559945309417f;
    static constexpr float gLn10            = 2.30258509299404568402f;
    static constexpr float gPI              = 3.14159265358979323846f;
    static constexpr float gHalfPI          = 1.57079632679489661923f;
    static constexpr float gQuarterPI       = 0.785398163397448309616f;
    static constexpr float gSqrt2           = 1.41421356237309504880f;
    static constexpr float gReciprocalSqrt2 = 0.707106781186547524401f;
    static constexpr float gSqrtHalf        = 0.707106781186547524401f;

        //
        //      Prefer Xl... functions over using the standard library math
        //      functions directly.
        //
    constexpr float XlSin(float radians)                   { return std::sin(radians); }
    constexpr float XlCos(float radians)                   { return std::cos(radians); }
    constexpr float XlTan(float radians)                   { return std::tan(radians); }
    constexpr float XlASin(float x)                        { return std::asin(x); }
    constexpr float XlACos(float x)                        { return std::acos(x); }
    constexpr float XlATan(float x)                        { return std::atan(x); }
    constexpr float XlATan2(float y, float x)              { return std::atan2(y, x); }
    constexpr float XlCotangent(float radians)             { return 1.f/std::tan(radians); }
    constexpr float XlFMod(float value, float modulo)      { return std::fmod(value, modulo); }
    constexpr float XlAbs(float value)                     { return std::abs(value); }
    constexpr float XlFloor(float value)                   { return std::floor(value); }
    constexpr float XlCeil(float value)                    { return std::ceil(value); }
    constexpr float XlExp(float value)                     { return std::exp(value); }
    constexpr float XlLog(float value)                     { return std::log(value); }

    T1(Primitive) constexpr Primitive XlSqrt(Primitive value)      { return std::sqrt(value); }
    T1(Primitive) constexpr Primitive XlRSqrt(Primitive value)     { return Primitive(1) / std::sqrt(value); }  // no standard reciprocal sqrt?

    T1(Primitive) constexpr bool XlRSqrt_Checked(Primitive* output, Primitive value)                   
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

    T1(Primitive) constexpr Primitive XlAbs(Primitive value)     { return std::abs(value); }

    constexpr std::tuple<float, float> XlSinCos(float angle)
    {
        return std::make_tuple(XlSin(angle), XlCos(angle));
    }

    constexpr float Deg2Rad(float input)               { return input / 180.f * gPI; }
    constexpr float Rad2Deg(float input)               { return input * 180.f / gPI; }

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
        constexpr bool Equivalent(Type a, Type b, Type tolerance) 
    {
        Type d = a-b;
        return d < tolerance && d > -tolerance;
    }

    constexpr bool AdaptiveEquivalent(float A, float B, float epsilon)
	{
		// from https://floating-point-gui.de/errors/comparison/
		// More robust way of doing these comparisons; with better support through the whole number line
		// We can also consider just looking at the bit pattern and checking the difference in integer form
		auto absA = std::abs(A);
		auto absB = std::abs(B);
		auto diff = std::abs(A - B);

		if (A == B) {
			return true;
		} else if (A == 0 || B == 0 || (absA + absB < std::numeric_limits<decltype(A)>::min())) {
			return diff < (epsilon * std::numeric_limits<decltype(A)>::min());
		} else { // use relative error
			return diff / (absA + absB) < epsilon;
		}	
	}

	constexpr bool AdaptiveEquivalent(double A, double B, double epsilon)
	{
		auto absA = std::abs(A);
		auto absB = std::abs(B);
		auto diff = std::abs(A - B);

		if (A == B) {
			return true;
		} else if (A == 0 || B == 0 || (absA + absB < std::numeric_limits<decltype(A)>::min())) {
			return diff < (epsilon * std::numeric_limits<decltype(A)>::min());
		} else { // use relative error
			return diff / (absA + absB) < epsilon;
		}	
	}

    template < typename T >
    constexpr T Clamp(T value, T minval, T maxval) {
        return std::max(std::min(value, maxval), minval);
    }

    constexpr float LinearInterpolate(float lhs, float rhs, float alpha)
    {
        return (rhs - lhs) * alpha + lhs;
    }

    constexpr double LinearInterpolate(double lhs, double rhs, double alpha)
    {
        return (rhs - lhs) * alpha + lhs;
    }

    constexpr int LinearInterpolate(int lhs, int rhs, float alpha)
    {
        return int((rhs - lhs) * alpha + .5f) + lhs;
    }

    constexpr int64_t LinearInterpolate(int64_t lhs, int64_t rhs, float alpha)
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
        constexpr float BranchlessMin(float a, float b) { return std::min(a, b); }
        constexpr float BranchlessMax(float a, float b) { return std::max(a, b); }
        constexpr float BranchlessClamp(float val, float minval, float maxval) { return Clamp(val, minval, maxval); }
    #endif

    template<typename Type> const Type& Identity();
    template<typename Type> const Type& Zero();
}

using namespace XLEMath;

