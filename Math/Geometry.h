// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "../Utility/IteratorUtils.h"       // for IteratorRange
#include "../Core/Prefix.h"
#include <utility>

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__) 
    #include <immintrin.h>			// MSVC & clang intrinsic
    #define HAS_SSE_INSTRUCTIONS
#endif

namespace XLEMath
{
    template<typename Primitive>
        Primitive SignedDistance(const Vector3T<Primitive>& pt, const Vector4T<Primitive>& plane);
    template<typename Primitive>
        Primitive RayVsPlane(const Vector3T<Primitive>& rayStart, const Vector3T<Primitive>& rayEnd, const Vector4T<Primitive>& plane);

        /// <summary>Tests a ray against an AABB</summary>
        /// <param name="worldSpaceRay">The ray in world space. Start and end position</param>
    bool    RayVsAABB(const std::pair<Float3, Float3>& worldSpaceRay, const Float3x4& aabbToWorld, const Float3& mins, const Float3& maxs);
    bool    RayVsAABB(const std::pair<Float3, Float3>& localSpaceRay, const Float3& mins, const Float3& maxs);

    bool    Ray2DVsAABB(const std::pair<Float2, Float2>& localSpaceRay, const Float2& mins, const Float2& maxs);

    std::pair<Float3, Float3> TransformBoundingBox(const Float3x4& transformation, std::pair<Float3, Float3> boundingBox);

		/*
			Returns the parameters of the standard plane equation, eg:
				0 = A * x + B * y + C * z + D
			( so the result is a Vector4( A, B, C, D ) )
		*/
	T1(Primitive) auto PlaneFit(const Vector3T<Primitive> pts[], size_t ptCount ) -> Vector4T<Primitive>;
	T1(Primitive) auto PlaneFit(const Vector3T<Primitive> & pt0,
								const Vector3T<Primitive> & pt1,
								const Vector3T<Primitive> & pt2 ) -> Vector4T<Primitive>;
    T1(Primitive) bool PlaneFit_Checked(Vector4T<Primitive>* result,
                                        const Vector3T<Primitive>& pt0,
		                                const Vector3T<Primitive>& pt1,
		                                const Vector3T<Primitive>& pt2);

    /// <summary>Conversion from cartesian to spherical polar coordinates</summary>
    /// Returns a 3 component vector with:
    ///     [0] = theta
    ///     [1] = phi
    ///     [2] = distance
    ///
    /// See description in wikipedia:
    /// http://en.wikipedia.org/wiki/Spherical_coordinate_system
    Float3 CartesianToSpherical(Float3 direction);
    Float3 SphericalToCartesian(Float3 spherical);

	/// <summary>LineLine3D algorithm for finding the shortest line segment that joints two lines</summary>
	bool ShortestSegmentBetweenLines(
		float& muA, float& muB,
		const std::pair<Float3, Float3>& rayA,
		const std::pair<Float3, Float3>& rayB);

	/// <summary>Finds the distance along a ray to the intersection with a sphere at the origin</summary>
	/// Note that it takes "radiusSq" not radius!
	bool DistanceToSphereIntersection(
		float& distance,
		Float3 rayStart, Float3 rayDirection, float sphereRadiusSq);

    /// Returns true iff the given (finite length) ray intersects a sphere at the origin with the given radius squared
    bool RayVsSphere(Float3 rayStart, Float3 rayEnd, float sphereRadiusSq);

    unsigned ClipTriangle(Float3 dst[], const Float3 source[], float clippingParam[]);

	template<typename Primitive>
		struct GeneratedPoint
	{
		Vector3T<Primitive> _position;
		unsigned _lhsIdx, _rhsIdx;
		Primitive _alpha;
	};

    template<typename Primitive>
		std::pair<unsigned, unsigned> ClipIndexedBasedTriangle(
			unsigned insideIndicesDst[],
			unsigned outsideIndicesDst[],
			std::vector<GeneratedPoint<Primitive>>& generatedPts,
			IteratorRange<const Vector3T<Primitive>*> staticPtPositions,
			unsigned sourceIndices[], Primitive clippingParam[],
			const Primitive coplanarThreshold);

    /// <summary>Finds the intersection between a plane and a given plane</summary>
    /// Returns the number of vertices in the resultant intersection polygon, and the points of that polygon in 'dst'
    /// 'dst' should point to an array at least 6 vectors long
    template<typename Primitive>
        unsigned PlaneAABBIntersection(Vector3T<Primitive> dst[], Vector4T<Primitive> planeEquation, Vector3T<Primitive> aabbMins, Vector3T<Primitive> aabbMaxs);

    int TriangleSign(Float2 p1, Float2 p2, Float2 p3);
    bool PointInTriangle(Float2 pt, Float2 v0, Float2 v1, Float2 v2);

    template<typename Primitive>
        inline Primitive SignedDistance(const Vector3T<Primitive>& pt, const Vector4T<Primitive>& plane)
    {
        return Dot(pt, Truncate(plane)) + plane[3];
    }

    template<typename Primitive>
        inline Primitive RayVsPlane(const Vector3T<Primitive>& rayStart, const Vector3T<Primitive>& rayEnd, const Vector4T<Primitive>& plane)
    {
        auto a = SignedDistance(rayStart, plane);
        auto b = SignedDistance(rayEnd, plane);
        return a / (a - b);
    }

	inline void AddToBoundingBox(std::pair<Float3, Float3>& boundingBox, const Float3& position)
    {
        boundingBox.first[0]    = std::min(position[0], boundingBox.first[0]);
        boundingBox.first[1]    = std::min(position[1], boundingBox.first[1]);
        boundingBox.first[2]    = std::min(position[2], boundingBox.first[2]);
        boundingBox.second[0]   = std::max(position[0], boundingBox.second[0]);
        boundingBox.second[1]   = std::max(position[1], boundingBox.second[1]);
        boundingBox.second[2]   = std::max(position[2], boundingBox.second[2]);
    }

    inline std::pair<Float3, Float3>       InvalidBoundingBox()
    {
        const Float3 mins(      std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max(),
                                std::numeric_limits<Float3::value_type>::max());
        const Float3 maxs(      -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max(),
                                -std::numeric_limits<Float3::value_type>::max());
        return std::make_pair(mins, maxs);
    }

        ////////////////////////////////////////////////////////////////////////////////////////////////
            //      I N C R E A S E D   P R E C I S I O N   C A L C U L A T I O N S			//
        ////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Primitive>
		static std::pair<Primitive, Primitive> TwoProductFMA(Primitive a, Primitive b)
	{
		// Note that we have to be a little careful of compiler optimization here, since we're
		// generating equations that should result in 0 (assuming infinite precision)
		Primitive x = a * b;
		#if (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)
			Primitive y = __builtin_fma(a, b, -x);
		#else
			Primitive y = std::fma(a, b, -x);
		#endif
		return { x, y };
	}

	template<typename Primitive>
		static std::pair<Primitive, Primitive> TwoSum(Primitive a, Primitive b)
	{
		// Note that we have to be a little careful of compiler optimization here, since we're
		// generating equations that should result in 0 (assuming infinite precision)
		Primitive x = a + b;
		Primitive z = x - a;
		Primitive y = (a - (x - z)) + (b - z);		// note order of operations is significant
		return { x, y };
	}

	template<typename Primitive>
		static std::pair<Primitive, Primitive> Dot_Accurate(const Vector3T<Primitive>& lhs, const Vector3T<Primitive>& rhs)
	{
		// Using Ogita, Rump & Osihi's method for higher precision dot product calculation
		// 	https://www.researchgate.net/publication/220411325_Accurate_Sum_and_Dot_Product
		// See algorithm 5.3
		//
		// Note that we have to be a little careful of compiler optimization here, since we're
		// generating equations that should result in 0 (assuming infinite precision)

		Primitive p, s;
		std::tie(p, s) = TwoProductFMA(lhs[0], rhs[0]);

		Primitive h, r, q;
		std::tie(h, r) = TwoProductFMA(lhs[1], rhs[1]);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;		// addition order is important, we want to add q+r first

		std::tie(h, r) = TwoProductFMA(lhs[2], rhs[2]);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;		// addition order is important, we want to add q+r first

		// 's' is the extra precision we get through this method
		return { p,  s };
	}

	template<typename Primitive>
		static std::pair<Primitive, Primitive> Dot_Accurate(const Vector2T<Primitive>& lhs, const Vector2T<Primitive>& rhs)
	{
		Primitive p, s;
		std::tie(p, s) = TwoProductFMA(lhs[0], rhs[0]);

		Primitive h, r, q;
		std::tie(h, r) = TwoProductFMA(lhs[1], rhs[1]);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;		// addition order is important, we want to add q+r first

		// 's' is the extra precision we get through this method
		return { p,  s };
	}

	template<typename Primitive>
		static Vector3T<Primitive> Cross_Accurate(const Vector3T<Primitive>& lhs, const Vector3T<Primitive>& rhs)
	{
		// Let's follow the logic in Dot_Accurate and create an equivalent algorithm
		// assuming the subtraction is well behaved, we get a little more accuracy using TwoProductFMA
		Primitive a, b, c, d;
		Vector3T<Primitive> result;
		std::tie(a, b) = TwoProductFMA(lhs[1], rhs[2]);
		std::tie(c, d) = TwoProductFMA(lhs[2], rhs[1]);
		result[0] = (a - c) + (b - d);

		std::tie(a, b) = TwoProductFMA(lhs[2], rhs[0]);
		std::tie(c, d) = TwoProductFMA(lhs[0], rhs[2]);
		result[1] = (a - c) + (b - d);

		std::tie(a, b) = TwoProductFMA(lhs[0], rhs[1]);
		std::tie(c, d) = TwoProductFMA(lhs[1], rhs[0]);
		result[2] = (a - c) + (b - d);
		return result;
	}

	template<typename Primitive>
		static Vector4T<Primitive> PlaneFit_Accurate(const Vector3T<Primitive>& pt0, const Vector3T<Primitive>& pt1, const Vector3T<Primitive>& pt2)
	{
		// Let's follow the logic in Dot_Accurate and create an equivalent algorithm
		// though I haven't proven this is more accurate
		Vector3T<Primitive> normal;
		Primitive l0 = MagnitudeSquared(pt1 - pt0), l1 = MagnitudeSquared(pt2 - pt1), l2 = MagnitudeSquared(pt0 - pt2);
		if (l0 < l1) {
			if (l0 < l2) {
				normal = Cross_Accurate<Primitive>( pt0 - pt2, pt1 - pt2 );	// shortest segment is 0
			} else 
				normal = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2
		} else if (l1 < l2) {
			normal = Cross_Accurate<Primitive>( pt1 - pt0, pt2 - pt0 );	// shortest segment is 1
		} else
			normal = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2

		normal = Normalize(normal);

		Primitive p, h, r, s, q;
		std::tie(p, s) = Dot_Accurate(pt0, normal);

		std::tie(h, r) = Dot_Accurate(pt1, normal);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;

		std::tie(h, r) = Dot_Accurate(pt2, normal);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;

		Primitive w = (p+s) / Primitive(-3);
		return Expand( normal, w );
	}

	template<typename Primitive>
		static Vector4T<Primitive> PlaneFit_AccurateNoNormalize(const Vector3T<Primitive>& pt0, const Vector3T<Primitive>& pt1, const Vector3T<Primitive>& pt2)
	{
		// Let's follow the logic in Dot_Accurate and create an equivalent algorithm
		// though I haven't proven this is more accurate
		Vector3T<Primitive> normal;
		Primitive l0 = MagnitudeSquared(pt1 - pt0), l1 = MagnitudeSquared(pt2 - pt1), l2 = MagnitudeSquared(pt0 - pt2);
		if (l0 < l1) {
			if (l0 < l2) {
				normal = Cross_Accurate<Primitive>( pt0 - pt2, pt1 - pt2 );	// shortest segment is 0
			} else 
				normal = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2
		} else if (l1 < l2) {
			normal = Cross_Accurate<Primitive>( pt1 - pt0, pt2 - pt0 );	// shortest segment is 1
		} else
			normal = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2
		
		Primitive p, h, r, s, q;
		std::tie(p, s) = Dot_Accurate(pt0, normal);

		std::tie(h, r) = Dot_Accurate(pt1, normal);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;

		std::tie(h, r) = Dot_Accurate(pt2, normal);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;

		Primitive w = (p+s) / Primitive(-3);
		return Expand( normal, w );
	}

	template<typename Primitive>
		static Primitive SignedDistance_Accurate(const Vector3T<Primitive>& pt, const Vector4T<Primitive>& plane)
	{
		// See Dot_Accurate for more details
		Primitive p, s, q;
		std::tie(p, s) = Dot_Accurate(pt, Truncate(plane));
		std::tie(p, q) = TwoSum(p, plane[3]);
		return p + (s + q);
	}

	static Float3 Normalize_Accurate(Float3 input)
	{
		auto magSq = Dot_Accurate(input, input);
		#if defined(HAS_SSE_INSTRUCTIONS)
			// making use of SSE rsqrt (which is SIMD, but we'll only use one out of 4 results of the calculation)
			magSq.first += magSq.second;
			__m128 mm128 = _mm_load1_ps(&magSq.first);
			mm128 = _mm_rsqrt_ss(mm128);
			_mm_store_ss(&magSq.first, mm128);
			return input * magSq.first;
		#else
			return input / std::sqrt(magSq.first + magSq.second);
		#endif
	}

	static Vector3T<double> Normalize_Accurate(Vector3T<double> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return input / std::sqrt(magSq.first + magSq.second);
	}

	template<typename Primitive>
		static Primitive Magnitude_Accurate(Vector3T<Primitive> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return std::sqrt(magSq.first + magSq.second);
	}

	template<typename Primitive>
		static Primitive MagnitudeSquared_Accurate(Vector3T<Primitive> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return magSq.first + magSq.second;
	}

	static Float2 Normalize_Accurate(Float2 input)
	{
		auto magSq = Dot_Accurate(input, input);
		#if defined(HAS_SSE_INSTRUCTIONS)
			// making use of SSE rsqrt (which is SIMD, but we'll only use one out of 4 results of the calculation)
			magSq.first += magSq.second;
			__m128 mm128 = _mm_load1_ps(&magSq.first);
			mm128 = _mm_rsqrt_ss(mm128);
			_mm_store_ss(&magSq.first, mm128);
			return input * magSq.first;
		#else
			return input / std::sqrt(magSq.first + magSq.second);
		#endif
	}

	static Vector2T<double> Normalize_Accurate(Vector2T<double> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return input / std::sqrt(magSq.first + magSq.second);
	}

	template<typename Primitive>
		static Primitive Magnitude_Accurate(Vector2T<Primitive> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return std::sqrt(magSq.first + magSq.second);
	}

	template<typename Primitive>
		static Primitive MagnitudeSquared_Accurate(Vector2T<Primitive> input)
	{
		auto magSq = Dot_Accurate(input, input);
		return magSq.first + magSq.second;
	}

	template<typename Primitive>
		static Vector3T<Primitive> LinearInterpolate_Accurate(const Vector3T<Primitive>& lhs, const Vector3T<Primitive>& rhs, Primitive alpha)
	{
		// using the same principle as Dot_Accurate, let's improve this interpolate function
		// this is a good candidate for SIMD instructions
		// return lhs * (1-alpha) + rhs * a;

		Vector3T<Primitive> result;

		Primitive p, s;
		Primitive h, r, q;
		std::tie(p, s) = TwoProductFMA(lhs[0], 1-alpha);
		std::tie(h, r) = TwoProductFMA(rhs[0], alpha);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;
		result[0] = p + s;

		std::tie(p, s) = TwoProductFMA(lhs[1], 1-alpha);
		std::tie(h, r) = TwoProductFMA(rhs[1], alpha);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;
		result[1] = p + s;

		std::tie(p, s) = TwoProductFMA(lhs[2], 1-alpha);
		std::tie(h, r) = TwoProductFMA(rhs[2], alpha);
		std::tie(p, q) = TwoSum(p, h);
		s += q + r;
		result[2] = p + s;

		return result;
	}

	template<typename Primitive>
		static Primitive TriangleArea_Accurate(const Vector3T<Primitive>& pt0, const Vector3T<Primitive>& pt1, const Vector3T<Primitive>& pt2)
	{
		Primitive l0 = MagnitudeSquared(pt1 - pt0), l1 = MagnitudeSquared(pt2 - pt1), l2 = MagnitudeSquared(pt0 - pt2);
		Vector3T<Primitive> cross;
		if (l0 < l1) {
			if (l0 < l2) {
				cross = Cross_Accurate<Primitive>( pt0 - pt2, pt1 - pt2 );	// shortest segment is 0
			} else 
				cross = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2
		} else if (l1 < l2) {
			cross = Cross_Accurate<Primitive>( pt1 - pt0, pt2 - pt0 );	// shortest segment is 1
		} else
			cross = Cross_Accurate<Primitive>( pt2 - pt1, pt0 - pt1 );	// shortest segment is 2
		return Magnitude_Accurate(cross) / Primitive(2);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

        /// <summary>Iterator through a grid, finding that edges that intersect with a line segment</summary>
        /// The callback "opr" will be called for each grid edge that intersects with given line segment.
        /// Here, the grid is assumed to be made up of 1x1 elements on integer boundaries.
        /// The ray must start and end on integer boundaries. All of the math is done using integer math,
        /// with an algorithm similiar to Bresenham's 
    template<typename Operator>
        void GridEdgeIterator(Int2 start, Int2 end, Operator& opr)
        {
            Int2 s = start;
            Int2 e = end;

		    int w = e[0] - s[0];
		    int h = e[1] - s[1];

            int ystep = (h<0)?-1:1;
            h = XlAbs(h); 
            int xstep = (w<0)?-1:1;
            w = XlAbs(w); 
            int ddy = 2 * h;  // We may not need to double this (because we're starting from the corner of the pixel)
            int ddx = 2 * w; 

            int errorprev = 0, error = 0; // (start from corner. we don't want to start in the middle of the grid element)
            int x = s[0], y = s[1];
            if (ddx >= ddy) {
                for (int i=0; i<w; ++i) {
                    x += xstep; 
                    error += ddy; 
                    
                    Int2 e0, e1;
                    float edgeAlpha;

                    if (error >= ddx) {

                        y += ystep; 
                        error -= ddx; 

                            //  The cases for what happens here. Each case defines different edges
                            //  we need to check
                        if (error != 0) {
                            e0 = Int2(x, y); e1 = Int2(x, y+ystep);
                            edgeAlpha = error / float(ddx);

                            Int2 e0b(x-xstep, y), e1b(x, y);
                            int tri0 = ddx - errorprev;
                            int tri1 = error;
                            opr(e0b, e1b, tri0 / float(tri0+tri1));
                        } else {
                                // passes directly though the corner. Easiest case.
                            e0 = e1 = Int2(x, y);
                            edgeAlpha = 0.f;
                        }

                    } else {
                            // simple -- y isn't changing, just moving to the next "x" grid
                        e0 = Int2(x, y); e1 = Int2(x, y+ystep);
                        edgeAlpha = error / float(ddx);
                    }

                    opr(e0, e1, edgeAlpha);
                    errorprev = error; 
                }
            } else {
                for (int i=0; i<h; ++i) {
                    y += ystep;
                    error += ddx;

                    Int2 e0, e1;
                    float edgeAlpha;

                    if (error >= ddy) {

                        x += xstep; 
                        error -= ddy; 

                            //  The cases for what happens here. Each case defines different edges
                            //  we need to check
                        if (error != 0) {
                            e0 = Int2(x, y); e1 = Int2(x+xstep, y);
                            edgeAlpha = error / float(ddy);

                            Int2 e0b(x, y-ystep), e1b(x, y);
                            int tri0 = ddy - errorprev;
                            int tri1 = error;
                            opr(e0b, e1b, tri0 / float(tri0+tri1));
                        } else {
                                // passes directly though the corner. Easiest case.
                            e0 = e1 = Int2(x, y);
                            edgeAlpha = 0.f;
                        }

                    } else {
                            // simple -- y isn't changing, just moving to the next "x" grid
                        e0 = Int2(x, y); e1 = Int2(x+xstep, y);
                        edgeAlpha = error / float(ddy);
                    }

                    opr(e0, e1, edgeAlpha);
                    errorprev = error; 
                }
            }
        }

    inline float GridEdgeCeil(float input)
    {
            // The input number is always positive (and never nan/infinite and never
            // near the limit of floating point precision)
            // std::trunc may have a simplier implementation than std::ceil,
            // meaning that using trunc may give us better performance
        assert(input >= 0.f);
        return std::trunc(input) + 1.f;
    }

        /// <summary>Iterator through a grid, finding that edges that intersect with a line segment</summary>
        /// This is a floating point version of GridEdgeIterator. In this version, start and end can be 
        /// non integers (but edges are still found in integer values).
        /// "GridEdgeIterator" uses integer-only math. 
    template<typename Operator>
        void GridEdgeIterator2(Float2 start, Float2 end, Operator& opr)
        {
            float dx = end[0] - start[0];
            float dy = end[1] - start[1];

            float xsign = (dx < 0.f) ? -1.f : 1.f;
            float ysign = (dy < 0.f) ? -1.f : 1.f;
                
            dx = XlAbs(dx); dy = XlAbs(dy);
                // x and y values must be kept positive (because of the implementation of GridEdgeCeil)... So offset everything here
            const float xoffset = 10.f - std::min(xsign * start[0], xsign * end[0]), 
                        yoffset = 10.f - std::min(ysign * start[1], ysign * end[1]);
            float x = xsign * start[0] + xoffset, y = ysign * start[1] + yoffset;

            // const float epsilon = 1e-2f;    // hack! ceil(x) will sometimes return x... We need to prevent this!
            if (dx >= dy) {
                float r = dy / dx;
                float endx = xsign * end[0] + xoffset;
                for (;;) {
                    // float ceilx = XlCeil(x + epsilon), ceily = XlCeil(y + epsilon);
                    float ceilx = GridEdgeCeil(x), ceily = GridEdgeCeil(y);
                    float sx = ceilx - x;
                    float sy = ceily - y;
                    if (sy < sx * r) {
                        x += sy / r;
                        y += sy;
                        if (x > endx) break;
                        opr(    Float2(xsign*((ceilx - 1.f) - xoffset), ysign*(y - yoffset)), 
                                Float2(xsign*(ceilx - xoffset),         ysign*(y - yoffset)), 
                                x - (ceilx - 1.f));
                    } else {
                        x += sx;
                        y += sx * r;
                        if (x > endx) break;
                        opr(    Float2(xsign*(x - xoffset),             ysign*((ceily - 1.f) - yoffset)), 
                                Float2(xsign*(x - xoffset),             ysign*(ceily - yoffset)), 
                                y - (ceily - 1.f));
                    }
                }
            } else {
                float r = dx / dy;
                float endy = ysign * end[1] + yoffset;
                for (;;) {
                    // float ceilx = XlCeil(x + epsilon), ceily = XlCeil(y + epsilon);
                    float ceilx = GridEdgeCeil(x), ceily = GridEdgeCeil(y);
                    float sx = ceilx - x;
                    float sy = ceily - y;
                    if (sx < sy * r) {
                        x += sx;
                        y += sx / r;
                        if (y > endy) break;
                        opr(    Float2(xsign*(x - xoffset),             ysign*((ceily - 1.f) - yoffset)), 
                                Float2(xsign*(x - xoffset),             ysign*(ceily - yoffset)), 
                                y - (ceily - 1.f));
                    } else {
                        x += sy * r;
                        y += sy;
                        if (y > endy) break;
                        opr(    Float2(xsign*((ceilx - 1.f) - xoffset), ysign*(y - yoffset)), 
                                Float2(xsign*(ceilx - xoffset),         ysign*(y - yoffset)), 
                                x - (ceilx - 1.f));
                    }
                }
            }
        }
}


