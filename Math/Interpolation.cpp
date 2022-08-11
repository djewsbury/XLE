// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Interpolation.h"
#include "Transformations.h"

namespace XLEMath
{
    float BezierInterpolate(float P0, float C0, float C1, float P1, float s)
    {
        float sSq = s*s;
        float sCb = sSq*s;
        float complement = 1.f-s;
        float complement2 = complement*complement;
        float complement3 = complement2*complement;

            //  This the standard Bezier equation (as seen in textbooks everywhere)
        return	P0 * complement3
            +   3.f * C0 * s * complement2
            +   3.f * C1 * sSq * complement
            +   P1 * sCb;
    }

    Float3 BezierInterpolate(Float3 P0, Float3 C0, Float3 C1, Float3 P1, float alpha)
    {
        return Float3(
            BezierInterpolate(P0[0], C0[0], C1[0], P1[0], alpha),
            BezierInterpolate(P0[1], C0[1], C1[1], P1[1], alpha),
            BezierInterpolate(P0[2], C0[2], C1[2], P1[2], alpha) );
    }

    Float4 BezierInterpolate(const Float4& P0, const Float4& C0, const Float4& C1, const Float4& P1, float alpha)
    {
        return Float4(
            BezierInterpolate(P0[0], C0[0], C1[0], P1[0], alpha),
            BezierInterpolate(P0[1], C0[1], C1[1], P1[1], alpha),
            BezierInterpolate(P0[2], C0[2], C1[2], P1[2], alpha),
            BezierInterpolate(P0[3], C0[3], C1[3], P1[3], alpha));
    }

    Float4x4 BezierInterpolate(const Float4x4& P0, const Float4x4& C0, const Float4x4& C1, const Float4x4& P1, float alpha)
    {
        Float4x4 result;
        for (unsigned j=0; j<4; ++j)
            for (unsigned i=0; i<4; ++i)
                result(i,j) = BezierInterpolate(P0(i,j), C0(i,j), C1(i,j), P1(i,j), alpha);
        return result;
    }

	Quaternion  BezierInterpolate(const Quaternion& P0, const Quaternion& C0, const Quaternion& C1, const Quaternion& P1, float alpha)
	{
		return P0;		// (not implemented)
	}


    float SphericalInterpolate(float A, float B, float alpha)
    {
        return LinearInterpolate(A, B, alpha);
    }

    Float3 SphericalInterpolate(Float3 A, Float3 B, float alpha)
    {
        return LinearInterpolate(A, B, alpha);
    }

    Float4 SphericalInterpolate(const Float4& A, const Float4& B, float alpha)
    {
            //  Note -- the type of interpolation here depends on the meaning of the values
            //          Is it a rotation axis/angle? Or something else
        return LinearInterpolate(A, B, alpha);
    }
        
    Float4x4 SphericalInterpolate(const Float4x4& A, const Float4x4& B, float alpha)
    {
            //  We're assuming that this input matrix is an affine geometry transform. So we can convert it
            //  into a format that can be slerped!
        auto result = SphericalInterpolate(
            ScaleRotationTranslationQ(A), 
            ScaleRotationTranslationQ(B), alpha);
        return AsFloat4x4(result);
    }


        
    float SphericalBezierInterpolate(float P0, float C0, float C1, float P1, float alpha)
    {
            // (just do the non-spherical interpolate, for lazyness)
        return BezierInterpolate(P0, C0, C1, P1, alpha);
    }

    Float3 SphericalBezierInterpolate(Float3 P0, Float3 C0, Float3 C1, Float3 P1, float alpha)
    {
            // (just do the non-spherical interpolate, for lazyness)
        return BezierInterpolate(P0, C0, C1, P1, alpha);
    }

    Float4 SphericalBezierInterpolate(const Float4& P0, const Float4& C0, const Float4& C1, const Float4& P1, float alpha)
    {
            // (just do the non-spherical interpolate, for lazyness)
        return BezierInterpolate(P0, C0, C1, P1, alpha);
    }
        
    Float4x4 SphericalBezierInterpolate(const Float4x4& P0, const Float4x4& C0, const Float4x4& C1, const Float4x4& P1, float alpha)
    {
            // (just do the non-spherical interpolate, for lazyness)
        return BezierInterpolate(P0, C0, C1, P1, alpha);
    }

	Quaternion  SphericalBezierInterpolate(const Quaternion& P0, const Quaternion& C0, const Quaternion& C1, const Quaternion& P1, float alpha)
	{
			// (just do the non-spherical interpolate, for lazyness)
		return BezierInterpolate(P0, C0, C1, P1, alpha);
	}



	float HermiteInterpolate(float P0, float m0, float P1, float m1, float s)
    {
        float sSq = s*s;
        float complement = 1.0f-s;
        float complementSq = complement*complement;

            //  This is the hermite interpolation formula
			//  Note that if we wanted to interpolate the same spline segment
			//	multiple times, with different values for 's', then we could
			//	refactor this into the form:
			//		c3*s^3 + c2*s^2 + c1*s^1 + c0
        return	P0 * ((1.f+2.f*s) * complementSq)
            +   m0 * (s * complementSq)
            +   P1 * (sSq * (3.f-2.f*s))
            +   m1 * -(sSq * complement);
    }

	template<typename Type>
		typename std::remove_reference<Type>::type HermiteInterpolate(
			Type P0, Type m0, float m0Scale,
			Type P1, Type m1, float m1Scale,
			float t)
    {
        float tSq = t*t;
        float complement = 1.0f-t;
        float complementSq = complement*complement;

            //  This the standard Bezier equation (as seen in textbooks everywhere)
        return	P0 * ((1.f+2.f*t) * complementSq)
            +   m0 * ((t * complementSq) * m0Scale)
            +   P1 * (tSq * (3.f-2.f*t))
            +   m1 * ((tSq * (t - 1.f)) * m1Scale);
    }

	float       SphericalCatmullRomInterpolate(float P0n1, float P0, float P1, float P1p1, float P0n1t, float P1p1t, float alpha)
	{
		float m0 = (P1 - P0n1) / (1.f - P0n1t);
		float m1 = (P1p1 - P0) / (P1p1t - 0.f);
		return HermiteInterpolate(P0, m0, P1, m1, alpha);
	}

    Float3      SphericalCatmullRomInterpolate(Float3 P0n1, Float3 P0, Float3 P1, Float3 P1p1, float P0n1t, float P1p1t, float alpha)
	{
		return HermiteInterpolate<Float3>(
			P0, P1 - P0n1, 1.f / (1.f - P0n1t),
			P1, P1p1 - P0, 1.f / (P1p1t - 0.f),
			alpha);
	}

    Float4      SphericalCatmullRomInterpolate(const Float4& P0n1, const Float4& P0, const Float4& P1, const Float4& P1p1, float P0n1t, float P1p1t, float alpha)
	{
		return HermiteInterpolate<const Float4&>(
			P0, P1 - P0n1, 1.f / (1.f - P0n1t),
			P1, P1p1 - P0, 1.f / (P1p1t - 0.f),
			alpha);
	}

    Float4x4    SphericalCatmullRomInterpolate(const Float4x4& P0n1, const Float4x4& P0, const Float4x4& P1, const Float4x4& P1p1, float P0n1t, float P1p1t, float alpha)
	{
		return HermiteInterpolate<const Float4x4&>(
			P0, P1 - P0n1, 1.f / (1.f - P0n1t),
			P1, P1p1 - P0, 1.f / (P1p1t - 0.f),
			alpha);
	}

	Quaternion  SphericalCatmullRomInterpolate(const Quaternion& P0n1, const Quaternion& P0, const Quaternion& P1, const Quaternion& P1p1, float P0n1t, float P1p1t, float alpha)
	{
        // based on "Using Geometric Constructions to Interpolate Orientations with Quaternions" from Graphics Gems II
        // This is derived from Shoemake's work on scalars.
        // However there are multiple ways to approach this problem, and the results here may not be perfect from the
        // point of view of continuity and smoothness
        // also, here we're assuming that all of the keyframes are spaced evenly, which might not actually be true
		auto q10 = SphericalInterpolate(P0n1, P0, alpha+1);
        auto q11 = SphericalInterpolate(P0, P1, alpha);
        auto q12 = SphericalInterpolate(P1, P1p1, alpha-1);
        auto q20 = SphericalInterpolate(q10, q11, (alpha+1)/2);
        auto q21 = SphericalInterpolate(q11, q12, alpha/2);
        return SphericalInterpolate(q20, q21, alpha).normalize();
	}

    template<typename T>
        static T Bn(IteratorRange<const T*> controlPoints, IteratorRange<const uint16_t*> knots, unsigned n)
    {
        auto denom = knots[n+5] - knots[n+2];
        assert(denom != 0);
        float A = (knots[n+5] - knots[n+3]) / float(denom);
        float B = (knots[n+3] - knots[n+2]) / float(denom);
        return A * controlPoints[n+1] + B * controlPoints[n+2];
    }

    template<typename T>
        static T Cn(IteratorRange<const T*> controlPoints, IteratorRange<const uint16_t*> knots, unsigned n)
    {
        auto denom = knots[n+5] - knots[n+2];
        assert(denom != 0);
        float A = (knots[n+5] - knots[n+4]) / float(denom);
        float B = (knots[n+4] - knots[n+2]) / float(denom);
        return A * controlPoints[n+1] + B * controlPoints[n+2];
    }

    template<typename T>
        static T Vn(IteratorRange<const uint16_t*> knots, unsigned n, T cnm1, T bn)
    {
        auto denom = knots[n+4] - knots[n+2];
        assert(denom != 0);
        float A = (knots[n+4] - knots[n+3]) / float(denom);
        float B = (knots[n+3] - knots[n+2]) / float(denom);
        return A * cnm1 + B * bn;
    }

    static void CalculateBSplineBasisForNURBS(
        float output[],          // should be an array of degree + 1 elements
        IteratorRange<const uint16_t*> knots,
        unsigned i,
        float time,
        unsigned degree)
    {
        // based on implementation from https://github.com/pradeep-pyro/tinynurbs/blob/master/include/tinynurbs/core/basis.h, but this
        // algorithm can be found on wikipedia as well as other places
        assert(degree >= 1);
        float left[degree+1], right[degree+1];
        output[0] = 1.f;
        left[0] = right[1] = 0.f;

        for (unsigned j=1; j<=degree; ++j) {
            left[j] = time - knots[i+1-j];
            right[j] = knots[i+j] - time;
            float saved = 0.f;
            for (unsigned r=0; r<j; ++r) {
                float temp = output[r] / (right[r+1] + left[j-r]);
                output[r] = saved + right[r+1] * temp;
                saved = left[j-r] * temp;
            }
            output[j] = saved;
        }
    }

    Float3      CubicNURBSInterpolate(
        IteratorRange<const Float3*> controlPoints,
        IteratorRange<const uint16_t*> knots,
        float time)
    {
        const unsigned degree = 3;
        // Some background here:
        //      https://www.codeproject.com/Articles/996281/NURBS-curve-made-easy
        //      https://github.com/pradeep-pyro/tinynurbs (unfortunately though it's called "tiny" it's still a little too heavy to use practically here)
        //      & the wikipedia page
        auto i = std::upper_bound(knots.begin() + degree, knots.end(), (unsigned)time) - 1 - knots.begin();
        assert(i >= degree-1);

        float basis[degree+1];
        CalculateBSplineBasisForNURBS(basis, knots, i, time, degree);
        assert(Equivalent(basis[0]+basis[1]+basis[2]+basis[3], 1.f, 1e-4f));
        Float3 result = Zero<Float3>();
        for (unsigned c=0; c<degree+1; ++c) {
            auto ctrlIdx = int(i-degree+c);
            ctrlIdx = std::clamp(ctrlIdx+1, 0, int(controlPoints.size()-1));
            assert(ctrlIdx < controlPoints.size());
            result += basis[c] * controlPoints[i+degree+c];
        }
        return result;

#if 0
        // This method is inefficient, but convenient. We can generate equivalent bezier control points for the nurbs
        // curve by looking at the knots.
        auto bn = Bn<Float3>(controlPoints, knots, n);
        auto cn = Cn<Float3>(controlPoints, knots, n);
        Float3 cnm1 = cn;
        if (n!=0) cnm1 = Cn<Float3>(controlPoints, knots, n-1);
        auto bnp1 = Bn<Float3>(controlPoints, knots, n+1);
        auto vn = Vn<Float3>(knots, n, cnm1, bn);
        auto vnp1 = Vn<Float3>(knots, n+1, cn, bnp1);

        return BezierInterpolate(vn, bn, cn, vnp1, 0.5f);
#endif

    }

    Float4      CubicNURBSInterpolate(
        IteratorRange<const Float4*> controlPoints,
        IteratorRange<const uint16_t*> knots,
        float time)
    {
        const unsigned degree = 3;
        // Some background here:
        //      https://www.codeproject.com/Articles/996281/NURBS-curve-made-easy (though the derivations there might only be partially accurate)
        //      https://github.com/pradeep-pyro/tinynurbs (unfortunately though it's called "tiny" it's still a little too heavy to use practically here)
        //      & the wikipedia page
        auto i = std::upper_bound(knots.begin() + degree, knots.end(), (unsigned)time) - 1 - knots.begin();
        assert(i >= degree-1);
        
        float basis[degree+1];
        CalculateBSplineBasisForNURBS(basis, knots, i, time, degree);
        assert(Equivalent(basis[0]+basis[1]+basis[2]+basis[3], 1.f, 1e-4f));
        Float4 result = Zero<Float4>();
        for (unsigned c=0; c<degree+1; ++c) {
            auto ctrlIdx = int(i-degree+c);
            ctrlIdx = std::clamp(ctrlIdx+1, 0, int(controlPoints.size()-1));
            assert(ctrlIdx < controlPoints.size());
            result += basis[c] * controlPoints[ctrlIdx];
        }
        return result;

#if 0
        // This method is inefficient, but convenient. We can generate equivalent bezier control points for the nurbs
        // curve by looking at the knots.
        auto bn = Bn<Float4>(controlPoints, knots, n);
        auto cn = Cn<Float4>(controlPoints, knots, n);
        Float4 cnm1 = cn;
        if (n!=0) cnm1 = Cn<Float4>(controlPoints, knots, n-1);
        auto bnp1 = Bn<Float4>(controlPoints, knots, n+1);
        auto vn = Vn<Float4>(knots, n, cnm1, bn);
        auto vnp1 = Vn<Float4>(knots, n+1, cn, bnp1);

        return BezierInterpolate(vn, bn, cn, vnp1, 0.5f);
#endif

    }

}