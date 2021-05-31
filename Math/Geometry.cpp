// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Geometry.h"
#define HAS_EIGEN_LIBRARY
#if defined(HAS_EIGEN_LIBRARY)
    #include "EigenVector.h"
#endif
#include "Transformations.h"
#include "../Core/Prefix.h"
#include <assert.h>
#include <cfloat>

namespace XLEMath
{

    Float3 CartesianToSpherical(Float3 direction)
    {
        Float3 result;
        float rDist = XlRSqrt(MagnitudeSquared(direction));
        result[0] = XlACos(direction[2] * rDist);
        result[1] = XlATan2(direction[1], direction[0]);
        result[2] = 1.0f / rDist;
        return result;
    }

    Float3 SphericalToCartesian(Float3 spherical)
    {
        return Float3(
            spherical[2] * XlSin(spherical[0]) * XlCos(spherical[1]),
            spherical[2] * XlSin(spherical[0]) * XlSin(spherical[1]),
            spherical[2] * XlCos(spherical[0]));
    }

	bool ShortestSegmentBetweenLines(
		float& mua, float& mub,
		const std::pair<Float3, Float3>& rayA,
		const std::pair<Float3, Float3>& rayB)
	{
		/*
				The shortest line that connects to lines (p1->p2 and p3->p4) will
				be perpendicular to each. As a result,
					(pa-pb) dot (p2-p1) = 0
					(pa-pb) dot (p4-p3) = 0
				where pa->pb is the shortest line described. This gives us a two equations
				that can be solved with some simple algebra for the answer.

				This was originally based on an implementation from Paul Bourke.
					http://local.wasp.uwa.edu.au/~pbourke/geometry/lineline3d/
					(no longer available there)
		*/

		const float epsilon = 0.0001f;

		const Float3& p1 = rayA.first;
		const Float3& p2 = rayA.second;
		const Float3& p3 = rayB.first;
		const Float3& p4 = rayB.second;

		auto p13 = p1-p3;
		auto p43 = p4-p3;
		auto p21 = p2-p1;

			/* early out if either line is zero length (or too close for accuracy) */
		if (Dot(p43,p43) < epsilon || Dot(p21,p21) < epsilon)
			return false;

		auto d1343 = Dot(p13, p43);
		auto d4321 = Dot(p43, p21);
		auto d1321 = Dot(p13, p21);
		auto d4343 = Dot(p43, p43);
		auto d2121 = Dot(p21, p21);

		float denom = d2121 * d4343 - d4321 * d4321;
        if (std::abs(denom) < epsilon) return false;

		float numer = d1343 * d4321 - d1321 * d4343;
		mua = numer / denom;
		mub = (d1343 + d4321 * mua) / d4343;

		return true;
	}

	bool DistanceToSphereIntersection(
		float& distance,
		Float3 rayStart, Float3 rayDirection, float sphereRadiusSq)
	{
		/*	Find the std::distance, along the ray that begins at 'rayStart' and continues in unit direction 'direction', to the
			first intersection with the sphere centered at the origin and with radius 'radius'.
			Returns 0 if there is no intersection */
		auto d = Dot(-rayStart, rayDirection);
		const Float3 closestPoint = rayStart + d * rayDirection;
		const auto closestDistanceSq = MagnitudeSquared(closestPoint);
		if (closestDistanceSq > sphereRadiusSq)
			return false;
		auto a = XlSqrt(sphereRadiusSq - closestDistanceSq);
		distance = std::min(d-a, d+a);
		return true;
	}

    bool RayVsSphere(Float3 rayStart, Float3 rayEnd, float sphereRadiusSq)
    {
        Float3 rayDirection = rayEnd - rayStart;
        float rayLength = Magnitude(rayDirection);
        Float3 unitRayDirection = rayDirection / rayLength;
        auto d = Dot(-rayStart, unitRayDirection);
        d = Clamp(d, 0.f, rayLength);
		const Float3 closestPoint = rayStart + d * unitRayDirection;
		const auto closestDistanceSq = MagnitudeSquared(closestPoint);
		return closestDistanceSq <= sphereRadiusSq;
    }

    bool RayVsAABB(const std::pair<Float3, Float3>& worldSpaceRay, const Float4x4& aabbToWorld, const Float3& mins, const Float3& maxs)
    {
            //  Does this ray intersect the aabb? 
            //  transform the ray back into aabb space, and do tests against the edge planes of the bounding box

        auto worldToAabb = Inverse(aabbToWorld);     // maybe not be orthonormal input. This is used for terrain, which has scale on aabbToWorld
        auto ray = std::make_pair(
            TransformPoint(worldToAabb, worldSpaceRay.first), 
            TransformPoint(worldToAabb, worldSpaceRay.second));
        return RayVsAABB(ray, mins, maxs);
    }

    bool    RayVsAABB(const std::pair<Float3, Float3>& ray, const Float3& mins, const Float3& maxs)
    {
            // if both points are rejected by the same plane, then it's an early out
        unsigned inside = 0;
        for (unsigned c=0; c<3; ++c) {
            if (    (ray.first[c] < mins[c] && ray.second[c] < mins[c])
                ||  (ray.first[c] > maxs[c] && ray.second[c] > maxs[c]))
                return false;

            inside |= unsigned(ray.first[c] >= mins[c] && ray.first[c] <= maxs[c] && ray.second[c] >= mins[c] && ray.second[c] <= maxs[c]) << c;
        }

            // if completely inside, let's consider it an intersection
        if (inside == 7)
            return true;

            //  there's a potential intersection. Find the planes that the ray crosses, and find the intersection 
            //  point. If the intersection point is inside the aabb, then we have an intersection
        for (unsigned c=0; c<3; ++c) {

            {
                float a =  ray.first[c] - mins[c];
                float b = ray.second[c] - mins[c];
                if ((a<0) != (b<0)) {
                    float alpha = a / (a-b);
                    Float3 intersection = LinearInterpolate(ray.first, ray.second, alpha);
                        // don't test element "c", because we might get floating point creep
                    if (    intersection[(c+1)%3] >= mins[(c+1)%3] && intersection[(c+1)%3] <= maxs[(c+1)%3]
                        &&  intersection[(c+2)%3] >= mins[(c+2)%3] && intersection[(c+2)%3] <= maxs[(c+2)%3])
                        return true;
                }
            }

            {
                float a =  ray.first[c] - maxs[c];
                float b = ray.second[c] - maxs[c];
                if ((a<0) != (b<0)) {
                    float alpha = a / (a-b);
                    Float3 intersection = LinearInterpolate(ray.first, ray.second, alpha);
                    if (    intersection[(c+1)%3] >= mins[(c+1)%3] && intersection[(c+1)%3] <= maxs[(c+1)%3]
                        &&  intersection[(c+2)%3] >= mins[(c+2)%3] && intersection[(c+2)%3] <= maxs[(c+2)%3])
                        return true;
                }
            }

        }

        return false;
    }

    bool    Ray2DVsAABB(const std::pair<Float2, Float2>& localSpaceRay, const Float2& mins, const Float2& maxs)
    {
        // Based on a simple implementation from https://stackoverflow.com/questions/5514366/how-to-know-if-a-line-intersects-a-rectangle
        // Find min and max X for the segment
        auto minX = std::min(localSpaceRay.first[0], localSpaceRay.second[0]);
        auto maxX = std::max(localSpaceRay.first[0], localSpaceRay.second[0]);

        // Find the intersection of the segment's and rectangle's x-projections
        if (maxX > maxs[0]) maxX = maxs[0];
        if (minX < mins[0]) minX = mins[0];

        if (minX > maxX) // If their projections do not intersect return false
            return false;

        // Find corresponding min and max Y for min and max X we found before
        auto minY = localSpaceRay.first[1];
        auto maxY = localSpaceRay.second[1];

        auto dx = localSpaceRay.second[0] - localSpaceRay.first[0];

        if (std::abs(dx) > 0.0000001f) {
            auto a = (localSpaceRay.second[1] - localSpaceRay.first[1])/dx;
            auto b = localSpaceRay.first[1] - a*localSpaceRay.first[0];
            minY = a*minX + b;
            maxY = a*maxX + b;
        }

        if (minY > maxY)
            std::swap(minY, maxY);

        // Find the intersection of the segment's and rectangle's y-projections
        if (maxY > maxs[1]) maxY = maxs[1];
        if (minY < mins[1]) minY = mins[1];

        if (minY > maxY) // If Y-projections do not intersect return false
            return false;

        return true;
    }

    std::pair<Float3, Float3> TransformBoundingBox(const Float3x4& transformation, std::pair<Float3, Float3> boundingBox)
    {
        Float3 corners[] = 
        {
            Float3(  boundingBox.first[0], boundingBox.first[1],  boundingBox.first[2] ),
            Float3( boundingBox.second[0], boundingBox.first[1],  boundingBox.first[2] ),
            Float3(  boundingBox.first[0], boundingBox.second[1], boundingBox.first[2] ),
            Float3( boundingBox.second[0], boundingBox.second[1], boundingBox.first[2] ),

            Float3(  boundingBox.first[0], boundingBox.first[1],  boundingBox.second[2] ),
            Float3( boundingBox.second[0], boundingBox.first[1],  boundingBox.second[2] ),
            Float3(  boundingBox.first[0], boundingBox.second[1], boundingBox.second[2] ),
            Float3( boundingBox.second[0], boundingBox.second[1], boundingBox.second[2] )
        };

        for (unsigned c=0; c<dimof(corners); ++c) {
            corners[c] = TransformPoint(transformation, corners[c]);
        }

        Float3 mins(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), maxs(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (unsigned c=0; c<dimof(corners); ++c) {
            mins[0] = std::min(mins[0], corners[c][0]);
            mins[1] = std::min(mins[1], corners[c][1]);
            mins[2] = std::min(mins[2], corners[c][2]);

            maxs[0] = std::max(maxs[0], corners[c][0]);
            maxs[1] = std::max(maxs[1], corners[c][1]);
            maxs[2] = std::max(maxs[2], corners[c][2]);
        }

        return std::make_pair(mins, maxs);
    }

#if defined(HAS_EIGEN_LIBRARY)
    T1(PrimitiveType)
		Vector4T<PrimitiveType> PlaneFit(const Vector3T<PrimitiveType> pts[], size_t ptCount)
	{
			/*
					Given a set of points in 3 space, find a plane that best matches them, using least
					squares regression.

					The algorithm and base implementation here is from Geometric Tools, LLC
					Copyright (c) 1998-2010
					Distributed under the Boost Software License, Version 1.0.
					http://www.boost.org/LICENSE_1_0.txt
					http://www.geometrictools.com/License/Boost/LICENSE_1_0.txt

					see http://www.geometrictools.com/Documentation/LeastSquaresFitting.pdf for a
					description. Note that this is the orthogonal regression version. There is also
					a version for dealing with points of the form (x,y, f(x,y)) -- this is less
					general (as it seeks to minimize delta z), but may provide suitable results in
					most cases.
			*/


		// compute the mean of the points
		auto kOrigin = Zero<Vector3T<PrimitiveType>>();
		for (size_t i = 0; i < ptCount; i++)
			kOrigin += pts[i];
		PrimitiveType reciprocalCount = ((PrimitiveType)1.0)/ptCount;
		kOrigin *= reciprocalCount;

		// compute sums of products
		PrimitiveType fSumXX = (PrimitiveType)0.0, fSumXY = (PrimitiveType)0.0, fSumXZ = (PrimitiveType)0.0;
		PrimitiveType fSumYY = (PrimitiveType)0.0, fSumYZ = (PrimitiveType)0.0, fSumZZ = (PrimitiveType)0.0;
		for (size_t i = 0; i < ptCount; i++) 
		{
			auto kDiff = pts[i] - kOrigin;
			fSumXX += kDiff[0]*kDiff[0];
			fSumXY += kDiff[0]*kDiff[1];
			fSumXZ += kDiff[0]*kDiff[2];
			fSumYY += kDiff[1]*kDiff[1];
			fSumYZ += kDiff[1]*kDiff[2];
			fSumZZ += kDiff[2]*kDiff[2];
		}

		fSumXX *= reciprocalCount;
		fSumXY *= reciprocalCount;
		fSumXZ *= reciprocalCount;
		fSumYY *= reciprocalCount;
		fSumYZ *= reciprocalCount;
		fSumZZ *= reciprocalCount;

		// setup the eigensolver
		Eigen<PrimitiveType> kES(3);
		kES(0,0) = fSumXX;
		kES(0,1) = fSumXY;
		kES(0,2) = fSumXZ;
		kES(1,0) = fSumXY;
		kES(1,1) = fSumYY;
		kES(1,2) = fSumYZ;
		kES(2,0) = fSumXZ;
		kES(2,1) = fSumYZ;
		kES(2,2) = fSumZZ;

		// compute eigenstuff, smallest eigenvalue is in last position
		kES.DecrSortEigenStuff3();

		// get plane normal
		Vector3T<PrimitiveType> kNormal;
		kES.GetEigenvector(2,kNormal);

		// the minimum energy
		return Expand( kNormal, -Dot( kNormal, kOrigin ) );
	}
    
    template auto PlaneFit(const Vector3T<float> pts[], size_t ptCount ) -> Vector4T<float>;
    template auto PlaneFit(const Vector3T<float> & pt0, const Vector3T<float> & pt1, const Vector3T<float> & pt2 ) -> Vector4T<float>;
#endif

	T1(Primitive) Vector4T<Primitive> PlaneFit(
        const Vector3T<Primitive>& pt0,
		const Vector3T<Primitive>& pt1,
		const Vector3T<Primitive>& pt2)
	{
			/*
				Note -- this the most straightforward fashion to calculate a plane, but unfortunately it's inaccurate
						(particularly if the points are close together). There are better methods, but they require
						more complex math (see, for example, the Triangle library)

                The cross product is ordered so that if we pass in 3 points in counter clockwise winding from our
                perspective, then we should get a vector pointing towards us (ie, inline with CCW being front facing)
			*/
		auto normal = Normalize( Cross( pt2 - pt1, pt0 - pt1 ) );
		Primitive w = (-Dot( pt0, normal ) - Dot( pt1, normal ) - Dot( pt2, normal )) * Primitive(1./3.);
		return Expand( normal, w );
	}

    T1(Primitive) bool PlaneFit_Checked(
        Vector4T<Primitive>* result,
        const Vector3T<Primitive>& pt0,
		const Vector3T<Primitive>& pt1,
		const Vector3T<Primitive>& pt2)
	{
        assert(result);
		Vector3T<Primitive> normal;
        if (!Normalize_Checked(&normal, Vector3T<Primitive>(Cross(pt2 - pt1, pt0 - pt1))))
            return false;

        auto w = (-Dot( pt0, normal ) - Dot( pt1, normal ) - Dot( pt2, normal )) * Primitive(1./3.);
		*result = Expand( normal, w );
        return true;
	}
    
    template bool PlaneFit_Checked(Vector4T<float>* result, const Vector3T<float>& pt0, const Vector3T<float>& pt1, const Vector3T<float>& pt2);

    unsigned ClipTriangle(Float3 dst[], const Float3 source[], float clippingParam[])
    {
        // Clip the triangle against a single plane, at the point where clippingParam[] is
        // linearly interpolated as zero. We will keep the positive part of clippingParam[]
        // Generates 0, 1 or 2 output triangles
        bool c[] { clippingParam[0] < 0.0f, clippingParam[1] < 0.0f, clippingParam[2] < 0.0f };
        unsigned mode = unsigned(c[0]) | (unsigned(c[1]) << 1) | (unsigned(c[2]) << 2);
        Float3 A, B;
        switch (mode)
        {
        case 0: dst[0] = source[0]; dst[1] = source[1]; dst[2] = source[2]; return 1;
        case 7: return 0;

        case 1: // just [0] clipped
            A = LinearInterpolate(source[0], source[1], clippingParam[0] / (clippingParam[0] - clippingParam[1]));
            B = LinearInterpolate(source[0], source[2], clippingParam[0] / (clippingParam[0] - clippingParam[2]));
            dst[0] = A; dst[1] = source[1]; dst[2] = source[2];
            dst[3] = A; dst[4] = source[2]; dst[5] = B;
            return 2;

        case 2: // just [1] clipped
            A = LinearInterpolate(source[0], source[1], clippingParam[0] / (clippingParam[0] - clippingParam[1]));
            B = LinearInterpolate(source[1], source[2], clippingParam[1] / (clippingParam[1] - clippingParam[2]));
            dst[0] = source[0]; dst[1] = A; dst[2] = source[2];
            dst[3] = source[2]; dst[4] = A; dst[5] = B;
            return 2;

        case 4: // just [2] clipped
            A = LinearInterpolate(source[1], source[2], clippingParam[1] / (clippingParam[1] - clippingParam[2]));
            B = LinearInterpolate(source[0], source[2], clippingParam[0] / (clippingParam[0] - clippingParam[2]));
            dst[0] = source[0]; dst[1] = source[1]; dst[2] = B;
            dst[3] = B; dst[4] = source[1]; dst[5] = A;
            return 2;

        case 3: // [0] & [1] clipped
            A = LinearInterpolate(source[0], source[2], clippingParam[0] / (clippingParam[0] - clippingParam[2]));
            B = LinearInterpolate(source[1], source[2], clippingParam[1] / (clippingParam[1] - clippingParam[2]));
            dst[0] = A; dst[1] = B; dst[2] = source[2];
            return 1;

        case 5: // [0] & [2] clipped
            A = LinearInterpolate(source[0], source[1], clippingParam[0] / (clippingParam[0] - clippingParam[1]));
            B = LinearInterpolate(source[1], source[2], clippingParam[1] / (clippingParam[1] - clippingParam[2]));
            dst[0] = A; dst[1] = source[1]; dst[2] = B;
            return 1;

        case 6: // [1] & [2] clipped
            A = LinearInterpolate(source[0], source[1], clippingParam[0] / (clippingParam[0] - clippingParam[1]));
            B = LinearInterpolate(source[0], source[2], clippingParam[0] / (clippingParam[0] - clippingParam[2]));
            dst[0] = source[0]; dst[1] = A; dst[2] = B;
            return 1;

        default:
            assert(0);
            return 0;
        }
    }

    int TriangleSign(Float2 p1, Float2 p2, Float2 p3) {
        float w = (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1]);
        const static float EPSILON = 0.00001f;
        if (w > EPSILON) {
            return 1;
        } else if (w < -EPSILON) {
            return -1;
        }
        return 0;
    }

    bool PointInTriangle(Float2 pt, Float2 v0, Float2 v1, Float2 v2) {
        bool b1 = TriangleSign(pt, v0, v1) == -1;
        bool b2 = TriangleSign(pt, v1, v2) == -1;
        bool b3 = TriangleSign(pt, v2, v0) == -1;
        
        return ((b1 == b2) && (b2 == b3));
    }
}
