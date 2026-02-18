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

    bool RayVsAABB(const std::pair<Float3, Float3>& worldSpaceRay, const Float3x4& aabbToWorld, const Float3& mins, const Float3& maxs)
    {
            //  Does this ray intersect the aabb? 
            //  transform the ray back into aabb space, and do tests against the edge planes of the bounding box
        assert(IsOrthonormal(Truncate3x3(aabbToWorld)));
        auto ray = std::make_pair(
            TransformPointByOrthonormalInverse(aabbToWorld, worldSpaceRay.first), 
            TransformPointByOrthonormalInverse(aabbToWorld, worldSpaceRay.second));
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

    template<typename ResultPrecision, typename InputPrimitive>
        std::optional<Vector3T<ResultPrecision>> TriplePlaneIntersection(const Vector4T<InputPrimitive>& p0, const Vector4T<InputPrimitive>& p1, const Vector4T<InputPrimitive>& p2)
    {
        // See also TriplePlaneIntersection in Brush.cpp
        Matrix3x3T<InputPrimitive> matrix {
            p0[0], p1[0], p2[0],
            p0[1], p1[1], p2[1],
            p0[2], p1[2], p2[2]};

        auto m00 = matrix(1,1)*matrix(2,2) - matrix(1,2)*matrix(2,1);
        auto m01 = matrix(1,2)*matrix(2,0) - matrix(1,0)*matrix(2,2);
        auto m02 = matrix(1,0)*matrix(2,1) - matrix(1,1)*matrix(2,0);

        auto m10 = matrix(0,2)*matrix(2,1) - matrix(0,1)*matrix(2,2);
        auto m11 = matrix(0,0)*matrix(2,2) - matrix(0,2)*matrix(2,0);
        auto m12 = matrix(0,1)*matrix(2,0) - matrix(0,0)*matrix(2,1);

		auto m20 = matrix(0,1)*matrix(1,2) - matrix(0,2)*matrix(1,1);
        auto m21 = matrix(0,2)*matrix(1,0) - matrix(0,0)*matrix(1,2);
        auto m22 = matrix(0,0)*matrix(1,1) - matrix(0,1)*matrix(1,0);

        auto det = matrix(0,0)*m00 + matrix(0,1)*m01 + matrix(0,2)*m02;

        if (std::abs(det) > InputPrimitive(1e-10)) {		// attempt to prevent low precision results
            matrix(0,0) = m00/det;  matrix(0,1) = m10/det;  matrix(0,2) = m20/det;
            matrix(1,0) = m01/det;  matrix(1,1) = m11/det;  matrix(1,2) = m21/det;
            matrix(2,0) = m02/det;  matrix(2,1) = m12/det;  matrix(2,2) = m22/det;
            return Vector3T<ResultPrecision>{-p0[3], -p1[3], -p2[3]} * matrix;
        } else
            return {};
    }

    template std::optional<Vector3T<float>> TriplePlaneIntersection(const Vector4T<float>& p0, const Vector4T<float>& p1, const Vector4T<float>& p2);
    template std::optional<Vector3T<double>> TriplePlaneIntersection(const Vector4T<float>& p0, const Vector4T<float>& p1, const Vector4T<float>& p2);
    template std::optional<Vector3T<double>> TriplePlaneIntersection(const Vector4T<double>& p0, const Vector4T<double>& p1, const Vector4T<double>& p2);

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
    template auto PlaneFit(const Vector3T<double> pts[], size_t ptCount ) -> Vector4T<double>;
    template auto PlaneFit(const Vector3T<double> & pt0, const Vector3T<double> & pt1, const Vector3T<double> & pt2 ) -> Vector4T<double>;
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
        // Vector3T<Primitive> normal = Cross( pt2 - pt1, pt0 - pt1 );
		Primitive w = -(Dot( pt0, normal ) + Dot( pt1, normal ) + Dot( pt2, normal )) / Primitive(3.);
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

        auto w = -(Dot( pt0, normal ) + Dot( pt1, normal ) + Dot( pt2, normal )) / Primitive(3.);
		*result = Expand( normal, w );
        return true;
	}
    
    template bool PlaneFit_Checked(Vector4T<float>* result, const Vector3T<float>& pt0, const Vector3T<float>& pt1, const Vector3T<float>& pt2);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Primitive>
		static bool PtInPolygon(IteratorRange<const Vector2T<Primitive>*> loop, Vector2T<Primitive> testPt)
	{
		// Note that the basic algorithm here doesn't support colinear lines in "loop" too well
		// since we're likely to get that case, we ideally want to prefer to use a "testPt" where Y
		// component is not a multiple of 0.5
		auto lastPt = *(loop.end()-1);
		unsigned intersectionCount = 0;
		for (auto pt:loop) {
			assert(pt[1] != testPt[1]);		// see note above, preferable to avoid this

			// imagine drawing a line in +X from testPt. Does it intersect this part of the loop?
			if (	std::min(pt[1], lastPt[1]) <= testPt[1]
				&& 	std::max(pt[1], lastPt[1]) >  testPt[1]) {

				Primitive A = (testPt[1] - pt[1]) / (lastPt[1] - pt[1]);
				Primitive xA = LinearInterpolate(pt[0], lastPt[0], A);
				intersectionCount += xA >= testPt[0];
			}
			lastPt = pt;
		}

		return intersectionCount & 1;		// odd = inside
	}

	T1(Primitive) std::optional<Vector3T<Primitive>> FindAngularCentroidXY(
		IteratorRange<const Vector3T<Primitive>*> positions,
		IteratorRange<const unsigned*> indices,
		Primitive colinearThreshold)
	{
		// Iteratively find a point within the polygon such that the new dividing line from each vertex to the 
		// point is balanced between the existing edges
		VLA_UNSAFE_FORCE(Vector2T<Primitive>, edgeTangents, indices.size());
		auto centroidPos = Zero<Vector2T<Primitive>>();
		for (unsigned c=0; c<indices.size(); ++c) {
			auto p0 = positions[indices[c]], p1 = positions[indices[(c+1)%indices.size()]];
			edgeTangents[c] = Normalize(Truncate(p1)-Truncate(p0));
			centroidPos += Truncate(p0);
		}
		centroidPos /= Primitive(indices.size());

		unsigned iterationCount = 6;
		while (iterationCount--) {
			auto movement = Zero<Vector2T<Primitive>>();

			for (unsigned c=0; c<indices.size(); ++c) {
				auto em1 = (c+indices.size()-1)%indices.size();
				auto e0 = c;

				Vector2T<Primitive> a;
				if (!Normalize_Checked(&a, Vector2T<Primitive>(edgeTangents[ e0]-edgeTangents[em1])))
					a = {edgeTangents[ e0][1], -edgeTangents[ e0][0]};
				Vector2T<Primitive> m = {a[1], -a[0]};
				movement -= (m * Dot(m, centroidPos-Truncate(positions[indices[e0]])));
			}

			movement /= Primitive(indices.size());
			centroidPos += movement;
			if (MagnitudeSquared(movement) < colinearThreshold*colinearThreshold)
				break;
		}

		{
			VLA_UNSAFE_FORCE(Vector2T<Primitive>, winding, indices.size());
			for (unsigned c=0; c<indices.size(); ++c) winding[c] = Truncate(positions[indices[c]]);
			if (!PtInPolygon<Primitive>(MakeIteratorRange(winding, winding+indices.size()), centroidPos)) return {};
		}

		// also check if the centroid landed on an edge
		for (unsigned c=0; c<indices.size(); ++c) {
			auto em1 = (c+indices.size()-1)%indices.size();
			auto e0 = c;
			auto edge = Truncate(positions[indices[e0]])-Truncate(positions[indices[em1]]);
			auto edgeMagnitude = Magnitude(edge);
			auto a = Dot(centroidPos-Truncate(positions[indices[em1]]), edge)/(edgeMagnitude*edgeMagnitude);
			if (a < 0 || a > 1) continue;
			auto linePt = LinearInterpolate(Truncate(positions[indices[em1]]), Truncate(positions[indices[e0]]), a);
			auto distSq = MagnitudeSquared(linePt-centroidPos);
			if (distSq <= colinearThreshold*colinearThreshold) return {};		// fell on the edge
		}

		// good centroid, but we need to calculate the Z value
		Primitive centroidZ;
		{
			VLA_UNSAFE_FORCE(Vector3T<Primitive>, winding, indices.size());
			for (unsigned c=0; c<indices.size(); ++c) winding[c] = positions[indices[c]];
			auto plane = PlaneFit(winding, indices.size());
			// 0 = A * x + B * y + C * z + D
			// z = (A * x + B * y + D) / -C
			centroidZ = -(plane[0] * centroidPos[0] + plane[1] * centroidPos[1] + plane[3]) / plane[2];
		}

		return Vector3T<Primitive>{ centroidPos[0], centroidPos[1], centroidZ };
	}

    template std::optional<Vector3T<float>> FindAngularCentroidXY(IteratorRange<const Vector3T<float>*> positions, IteratorRange<const unsigned*> indices, float colinearThreshold);
    template std::optional<Vector3T<double>> FindAngularCentroidXY(IteratorRange<const Vector3T<double>*> positions, IteratorRange<const unsigned*> indices, double colinearThreshold);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
            UNREACHABLE();
            return 0;
        }
    }

	template<typename Primitive, bool useIndexToStaticPtPosition>
		std::pair<unsigned, unsigned> ClipIndexedBasedTriangle_Internal(
			unsigned positiveSideIndicesDst[],
			unsigned negativeSideIndicesDst[],
			std::vector<GeneratedPoint<Primitive>>& generatedPts,
			IteratorRange<const Vector3T<Primitive>*> staticPtPositions,
            IteratorRange<const unsigned*> indexToStaticPtPosition,
			unsigned sourceIndices[], Primitive clippingParam[],
			const Primitive coplanarThreshold)
	{
		// Clip the triangle against a single plane, at the point where clippingParam[] is
		// linearly interpolated as zero. We will keep both the sides of the clipping plane
		// Generates 0, 1 or 2 output triangles in both output arrays
		// Retains the vertex indices where possible, so we know how a vertex was generated
		unsigned clearlyOutside = unsigned(clippingParam[0] < -coplanarThreshold) | (unsigned(clippingParam[1] < -coplanarThreshold) << 1) | (unsigned(clippingParam[2] < -coplanarThreshold) << 2);
		unsigned coplanar = unsigned(clippingParam[0] < coplanarThreshold) | (unsigned(clippingParam[1] < coplanarThreshold) << 1) | (unsigned(clippingParam[2] < coplanarThreshold) << 2);
		coplanar &= ~clearlyOutside;

		if (clearlyOutside == 0) {
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = sourceIndices[2];
			return {1, 0};
		}

		if ((clearlyOutside|coplanar) == 7) {
			negativeSideIndicesDst[0] = sourceIndices[0]; negativeSideIndicesDst[1] = sourceIndices[1]; negativeSideIndicesDst[2] = sourceIndices[2];
			return {0, 1};
		}

		auto GetVertexPosition = [&](unsigned idx) -> Vector3T<Primitive> {
			const auto highBit = 1u<<31u;
			if (idx & highBit) return generatedPts[idx & ~highBit]._position;
            if constexpr (useIndexToStaticPtPosition) {
                return staticPtPositions[indexToStaticPtPosition[idx]];
            } else
			    return staticPtPositions[idx];
		};

		Vector3T<Primitive> sourcePositions[3] {
			GetVertexPosition(sourceIndices[0]),
			GetVertexPosition(sourceIndices[1]),
			GetVertexPosition(sourceIndices[2])
		};

		auto GeneratePoint = [&](unsigned a, unsigned b, Primitive alpha) -> unsigned {
			GeneratedPoint<Primitive> genPt;
			genPt._position = LinearInterpolate(sourcePositions[a], sourcePositions[b], alpha);
			genPt._lhsIdx = sourceIndices[a];
			genPt._rhsIdx = sourceIndices[b];
			genPt._alpha = alpha;
			generatedPts.push_back(genPt);
			const auto highBit = 1u<<31u;
			return unsigned(generatedPts.size()-1) | highBit;
		};

		unsigned A, B;
		switch (clearlyOutside | (coplanar<<3u))
		{
		case 1: // just [0] clipped
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			B = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			positiveSideIndicesDst[0] = A; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = sourceIndices[2];
			positiveSideIndicesDst[3] = A; positiveSideIndicesDst[4] = sourceIndices[2]; positiveSideIndicesDst[5] = B;
			negativeSideIndicesDst[0] = sourceIndices[0]; negativeSideIndicesDst[1] = A; negativeSideIndicesDst[2] = B;
			return {2, 1};

		case 2: // just [1] clipped
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			B = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = A; positiveSideIndicesDst[2] = sourceIndices[2];
			positiveSideIndicesDst[3] = sourceIndices[2]; positiveSideIndicesDst[4] = A; positiveSideIndicesDst[5] = B;
			negativeSideIndicesDst[0] = sourceIndices[1]; negativeSideIndicesDst[1] = B; negativeSideIndicesDst[2] = A;
			return {2, 1};

		case 4: // just [2] clipped
			A = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			B = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = B;
			positiveSideIndicesDst[3] = B; positiveSideIndicesDst[4] = sourceIndices[1]; positiveSideIndicesDst[5] = A;
			negativeSideIndicesDst[0] = sourceIndices[2]; negativeSideIndicesDst[1] = B; negativeSideIndicesDst[2] = A;
			return {2, 1};

		case 3: // [0] & [1] clipped
			A = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			B = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			positiveSideIndicesDst[0] = A; positiveSideIndicesDst[1] = B; positiveSideIndicesDst[2] = sourceIndices[2];
			negativeSideIndicesDst[0] = sourceIndices[0]; negativeSideIndicesDst[1] = sourceIndices[1]; negativeSideIndicesDst[2] = A;
			negativeSideIndicesDst[3] = A; negativeSideIndicesDst[4] = sourceIndices[1]; negativeSideIndicesDst[5] = B;
			return {1, 2};

		case 5: // [0] & [2] clipped
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			B = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			positiveSideIndicesDst[0] = A; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = B;
			negativeSideIndicesDst[0] = sourceIndices[2]; negativeSideIndicesDst[1] = sourceIndices[0]; negativeSideIndicesDst[2] = B;
			negativeSideIndicesDst[3] = B; negativeSideIndicesDst[4] = sourceIndices[0]; negativeSideIndicesDst[5] = A;
			return {1, 2};

		case 6: // [1] & [2] clipped
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			B = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = A; positiveSideIndicesDst[2] = B;
			negativeSideIndicesDst[0] = sourceIndices[1]; negativeSideIndicesDst[1] = sourceIndices[2]; negativeSideIndicesDst[2] = A;
			negativeSideIndicesDst[3] = A; negativeSideIndicesDst[4] = sourceIndices[2]; negativeSideIndicesDst[5] = B;
			return {1, 2};

		// cases with some coplanar vertices...

		case 1u | (2u<<3u): // just [0] clipped, [1] coplanar
			B = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			positiveSideIndicesDst[0] = B; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = sourceIndices[2];
			negativeSideIndicesDst[0] = sourceIndices[0]; negativeSideIndicesDst[1] = sourceIndices[1]; negativeSideIndicesDst[2] = B;
			return {1, 1};

		case 1u | (4u<<3u): // just [0] clipped, [2] coplanar
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			positiveSideIndicesDst[0] = A; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = sourceIndices[2];
			negativeSideIndicesDst[0] = sourceIndices[0]; negativeSideIndicesDst[1] = A; negativeSideIndicesDst[2] = sourceIndices[2];
			return {1, 1};

		case 2u | (1u<<3u): // just [1] clipped, [0] coplanar
			B = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = B; positiveSideIndicesDst[2] = sourceIndices[2];
			negativeSideIndicesDst[0] = sourceIndices[1]; negativeSideIndicesDst[1] = B; negativeSideIndicesDst[2] = sourceIndices[0];
			return {1, 1};

		case 2u | (4u<<3u): // just [1] clipped, [2] coplanar
			A = GeneratePoint(0, 1, clippingParam[0] / (clippingParam[0] - clippingParam[1]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = A; positiveSideIndicesDst[2] = sourceIndices[2];
			negativeSideIndicesDst[0] = sourceIndices[1]; negativeSideIndicesDst[1] = sourceIndices[2]; negativeSideIndicesDst[2] = A;
			return {1, 1};

		case 4u | (1u<<3u): // just [2] clipped, [0] coplanar
			A = GeneratePoint(1, 2, clippingParam[1] / (clippingParam[1] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = A;
			negativeSideIndicesDst[0] = sourceIndices[2]; negativeSideIndicesDst[1] = sourceIndices[0]; negativeSideIndicesDst[2] = A;
			return {1, 1};

		case 4u | (2u<<3u): // just [2] clipped, [1] coplanar
			B = GeneratePoint(0, 2, clippingParam[0] / (clippingParam[0] - clippingParam[2]));
			positiveSideIndicesDst[0] = sourceIndices[0]; positiveSideIndicesDst[1] = sourceIndices[1]; positiveSideIndicesDst[2] = B;
			negativeSideIndicesDst[0] = sourceIndices[2]; negativeSideIndicesDst[1] = B; negativeSideIndicesDst[2] = sourceIndices[1];
			return {1, 1};

		default:
			UNREACHABLE();
			return {0, 0};
		}
	}

    template<typename Primitive>
		std::pair<unsigned, unsigned> ClipIndexedBasedTriangle(
			unsigned positiveSideIndicesDst[],
			unsigned negativeSideIndicesDst[],
			std::vector<GeneratedPoint<Primitive>>& generatedPts,
			IteratorRange<const Vector3T<Primitive>*> staticPtPositions,
            IteratorRange<const unsigned*> indexToStaticPtPosition,
			unsigned sourceIndices[], Primitive clippingParam[],
			const Primitive coplanarThreshold)
	{
        if (!indexToStaticPtPosition.empty()) {
            return ClipIndexedBasedTriangle_Internal<Primitive, true>(positiveSideIndicesDst, negativeSideIndicesDst, generatedPts, staticPtPositions, indexToStaticPtPosition, sourceIndices, clippingParam, coplanarThreshold);
        } else {
            return ClipIndexedBasedTriangle_Internal<Primitive, false>(positiveSideIndicesDst, negativeSideIndicesDst, generatedPts, staticPtPositions, indexToStaticPtPosition, sourceIndices, clippingParam, coplanarThreshold);
        } 
    }

    template<typename Primitive>
        unsigned PlaneAABBIntersection(Vector3T<Primitive> dst[], Vector4T<Primitive> planeEquation, Vector3T<Primitive> aabbMins, Vector3T<Primitive> aabbMaxs)
    {
        // Following the idea here: https://www.asawicki.info/news_1428_finding_polygon_of_plane-aabb_intersection
        // we're going to do this is a pretty simple way: we just find the intersection with each edge of the AABB
        // and then (since the resulting polygon is always convex), we can just sort to put it in winding order
        unsigned vertexCount = 0;

        auto testEdge = [&](Vector3T<Primitive> start, Vector3T<Primitive> end, Primitive startDistance, Primitive endDistance) {
            if ((startDistance < 0) != (endDistance < 0)) {
                assert(vertexCount < 6);
                dst[vertexCount++] = LinearInterpolate(start, end, startDistance / (startDistance - endDistance));
            }
        };

        // 3 edges starting from aabbMins

        Vector3T<Primitive> start = aabbMins;
        auto startDistance = SignedDistance(start, planeEquation);
        Vector3T<Primitive> end = aabbMins;

        end[0] = aabbMaxs[0];
        testEdge(start, end, startDistance, SignedDistance(end, planeEquation));

        end[0] = aabbMins[0]; end[1] = aabbMaxs[1];
        testEdge(start, end, startDistance, SignedDistance(end, planeEquation));

        end[1] = aabbMins[1]; end[2] = aabbMaxs[2];
        testEdge(start, end, startDistance, SignedDistance(end, planeEquation));

        // 3 edges starting from aabbMaxs

        start = aabbMaxs;
        startDistance = SignedDistance(start, planeEquation);
        end = aabbMaxs;

        end[0] = aabbMins[0];
        testEdge(start, end, startDistance, SignedDistance(end, planeEquation));

        end[0] = aabbMaxs[0]; end[1] = aabbMins[1];
        testEdge(start, end, startDistance, SignedDistance(end, planeEquation));

        end[1] = aabbMaxs[1]; end[2] = aabbMins[2];
        auto endDistance = SignedDistance(end, planeEquation);
        testEdge(start, end, startDistance, endDistance);

        // remaining 6 edges walking around a circumference
        // we only need to set 1 vertex every time, because we always reuse a vertex from the previous test

        start = Vector3T<Primitive> { aabbMins[0], aabbMaxs[1], aabbMins[2] };       // 0
        startDistance = SignedDistance(start, planeEquation);
        testEdge(start, end, startDistance, endDistance);

        start = Vector3T<Primitive> { aabbMaxs[0], aabbMins[1], aabbMins[2] };       // 2
        startDistance = SignedDistance(start, planeEquation);
        testEdge(end, start, endDistance, startDistance);

        end = Vector3T<Primitive> { aabbMaxs[0], aabbMins[1], aabbMaxs[2] };         // 3
        endDistance = SignedDistance(end, planeEquation);
        testEdge(start, end, startDistance, endDistance);

        start = Vector3T<Primitive> { aabbMins[0], aabbMins[1], aabbMaxs[2] };       // 4
        startDistance = SignedDistance(start, planeEquation);
        testEdge(end, start, endDistance, startDistance);

        end = Vector3T<Primitive> { aabbMins[0], aabbMaxs[1], aabbMaxs[2] };         // 5
        endDistance = SignedDistance(end, planeEquation);
        testEdge(start, end, startDistance, endDistance);

        start = Vector3T<Primitive> { aabbMins[0], aabbMaxs[1], aabbMins[2] };       // 0
        startDistance = SignedDistance(start, planeEquation);
        testEdge(end, start, endDistance, startDistance);

        if (!vertexCount) return 0;

		const float equivalenceThreshold = Primitive(1e-6);
        auto anchor = dst[0];
        std::sort(
            dst, &dst[vertexCount],
            [planeEquation, anchor, equivalenceThreshold](const auto& lhs, const auto& rhs) {
				if (std::abs(lhs[0]-rhs[0])<equivalenceThreshold && std::abs(lhs[1]-rhs[1])<equivalenceThreshold && std::abs(lhs[2]-rhs[2])<equivalenceThreshold)
					return false;

                auto c = Cross(lhs - anchor, rhs - anchor);
				assert(std::isfinite(c[0]) && !std::isnan(c[0]));
                return Dot(c, Truncate(planeEquation)) < 0;
            });

		// filter out sequential equivalents (they should be made sequential by the sorting above)
		auto w = dst+1;
		for (auto i=dst+1; i!=dst+vertexCount; i++) {
			if (std::abs((*(i-1))[0]-(*i)[0])<equivalenceThreshold && std::abs((*(i-1))[1]-(*i)[1])<equivalenceThreshold && std::abs((*(i-1))[2]-(*i)[2])<equivalenceThreshold)
				continue;
			*w++ = *i;
		}

        return w-dst;
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

    void ConvexPolygonToTriangles(
		IteratorRange<unsigned*> triListOutput,
		unsigned polygonLoopCount)
	{
		assert(triListOutput.size() == (polygonLoopCount-2)*3);
		for (unsigned tri=0; tri<polygonLoopCount-2; ++tri) {
			if (!(tri&1)) {
				triListOutput[tri*3+0] = tri/2;
				triListOutput[tri*3+1] = tri/2+1;
				triListOutput[tri*3+2] = polygonLoopCount-tri/2-1;
			} else {
				triListOutput[tri*3+0] = polygonLoopCount-tri/2-1;
				triListOutput[tri*3+1] = tri/2+1;
				triListOutput[tri*3+2] = polygonLoopCount-tri/2-2;
			}
		}
	}

    template
        std::pair<unsigned, unsigned> ClipIndexedBasedTriangle(
			unsigned[], unsigned[],
			std::vector<GeneratedPoint<float>>&,
			IteratorRange<const Vector3T<float>*>, IteratorRange<const unsigned*>,
			unsigned[], float[], float);

    template
        std::pair<unsigned, unsigned> ClipIndexedBasedTriangle(
			unsigned[], unsigned[],
			std::vector<GeneratedPoint<double>>&,
			IteratorRange<const Vector3T<double>*>, IteratorRange<const unsigned*>,
			unsigned[], double[], double);

    template unsigned PlaneAABBIntersection(Vector3T<float>[], Vector4T<float>, Vector3T<float>, Vector3T<float>);
    template unsigned PlaneAABBIntersection(Vector3T<double>[], Vector4T<double>, Vector3T<double>, Vector3T<double>);
}
