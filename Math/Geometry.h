// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"
#include "Matrix.h"
#include "../Core/Prefix.h"
#include <utility>

namespace XLEMath
{
    float   SignedDistance(const Float3& pt, const Float4& plane);
    float   RayVsPlane(const Float3& rayStart, const Float3& rayEnd, const Float4& plane);

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
    
    
    int TriangleSign(Float2 p1, Float2 p2, Float2 p3);
    bool PointInTriangle(Float2 pt, Float2 v0, Float2 v1, Float2 v2);

        ////////////////////////////////////////////////////////////////////////////////////////////////
            //      I N L I N E   I M P L E M E N T A T I O N S
        ////////////////////////////////////////////////////////////////////////////////////////////////

    inline float SignedDistance(const Float3& pt, const Float4& plane)
    {
        return Dot(pt, Truncate(plane)) + plane[3];
    }

    inline float RayVsPlane(const Float3& rayStart, const Float3& rayEnd, const Float4& plane)
    {
        float a = SignedDistance(rayStart, plane);
        float b = SignedDistance(rayEnd, plane);
        return a / (a - b);
    }

    inline int XlSign(int input)
    {
        // std::signbit would be useful here!
        return (input<0)?-1:1;
    }

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

            int ystep = XlSign(h); 
            h = XlAbs(h); 
            int xstep = XlSign(w); 
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


