// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Vector.h"
#include "Geometry.h"
#include <vector>

namespace XLEMath
{
	template<typename Primitive>
		class ConvexPolyhedraSet
	{
	public:
		using VertexIndex = unsigned;
		using FaceIndex = unsigned;
		using PolyhedronIndex = unsigned;
		static constexpr FaceIndex s_FaceIndex_Invalid = ~0u;
		static constexpr PolyhedronIndex s_PolyhedronIndex_Invalid = ~0u;
		using Vector3 = Vector3T<Primitive>;
		using Vector4 = Vector4T<Primitive>;
		std::vector<Vector3> _vertices;

		struct Face
		{
			Vector4 _plane;
			std::vector<VertexIndex> _polygonVertices;
		};

		struct Polyhedron
		{
			std::vector<FaceIndex> _faces;
			std::pair<Vector3, Vector3> _aabb;
		};

		std::vector<Face> _faces;
		std::vector<Polyhedron> _polyhedra;

		PolyhedronIndex AddAxiallyAlignedBox(Vector3 mins, Vector3 maxs);
		FaceIndex AddFace(Vector3 A, Vector3 B, Vector3 C);
		FaceIndex AddFace(IteratorRange<const Vector3*> vertices, Vector4 plane);

		struct SplittingParams
		{
			Primitive _coplanarThreshold = Primitive(1e-3);
			Primitive _strictPositionEquivalenceThreshold = Primitive(1e-5);
		};

		struct SplitPolyhedronResult
		{
			PolyhedronIndex _positiveSide, _negativeSide;
			FaceIndex _intersection;
		};
		SplitPolyhedronResult SplitPolyhedron(PolyhedronIndex srcPolyhedron, Vector4 splittingPlane, const SplittingParams& params);

		struct SplitFaceResult
		{
			FaceIndex _positiveSide, _negativeSide;
		};
		SplitFaceResult SplitFace(
			FaceIndex srcFace, Vector4 splittingPlane,
			const SplittingParams& params);

		// return -1 for inside, 0 for on (or near) the edge, and 1 for outside
		int VolumeTest(PolyhedronIndex polyhedron, Vector3 testPt);

		Primitive FaceArea(FaceIndex srcFace);

	private:
		struct SplitFaceInternalResult
		{
			enum Type { AllPositive, AllNegative, Split };
			Type _type;
			Face _positiveSide, _negativeSide;
		};
		SplitFaceInternalResult SplitFace_Internal(
			std::vector<unsigned>& coplanarVertices,
			std::vector<Vector3>& newVertices,
			const Face& srcFace, Vector4 splittingPlane,
			const SplittingParams& params);
		Primitive FaceArea_Internal(const Face& f);

		FaceIndex FindPolyhedronIntersection(std::vector<Vector3>& newVertices, PolyhedronIndex srcPolyhedron, Vector4 splittingPlane, const SplittingParams& params);
		std::vector<unsigned> OrderVerticesForWinding(std::vector<unsigned>&& inputVertices, Vector4 facePlane, const SplittingParams& params);
	};

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::SplitPolyhedron(unsigned srcPolyhedron, Vector4 splittingPlane, const SplittingParams& params) -> SplitPolyhedronResult
	{
		// Divide the given polyhedron using the splitting plane.
		// Return two new polyhedra for the inside and outside parts of the polyhedra
		// If the original polyhedra is entirely on one side of the plane, return no new polyhedra are created,
		// instead the original index is returned in one of the resultant values (the other will be s_PolyhedronIndex_Invalid)
		assert(srcPolyhedron < _polyhedra.size());

		std::vector<Vector3> newVertices;
		std::vector<unsigned> coplanarVertices;

		// 
		Polyhedron positiveSidePolyhedron;
		Polyhedron negativeSidePolyhedron;
		auto& p = _polyhedra[srcPolyhedron];
		positiveSidePolyhedron._faces.reserve(p._faces.size());
		negativeSidePolyhedron._faces.reserve(p._faces.size());
		for (unsigned c=0; c<p._faces.size(); ++c) {
			auto split = SplitFace_Internal(coplanarVertices, newVertices, _faces[p._faces[c]], splittingPlane, params);
			switch (split._type) {
			case SplitFaceInternalResult::AllNegative: negativeSidePolyhedron._faces.push_back(p._faces[c]); break;
			case SplitFaceInternalResult::AllPositive: positiveSidePolyhedron._faces.push_back(p._faces[c]); break;
			default:
				assert(split._type == SplitFaceInternalResult::Split);
				if (!split._positiveSide._polygonVertices.empty()) {
					_faces.emplace_back(std::move(split._positiveSide));
					positiveSidePolyhedron._faces.push_back(FaceIndex(_faces.size()-1));
				}
				if (!split._negativeSide._polygonVertices.empty()) {
					_faces.emplace_back(std::move(split._negativeSide));
					negativeSidePolyhedron._faces.push_back(FaceIndex(_faces.size()-1));
				}
				break;
			}
		}

		SplitPolyhedronResult result;
		result._positiveSide = result._negativeSide = s_PolyhedronIndex_Invalid;
		result._intersection = ~0u;

		constexpr bool useNewIntersectionCalculation = true;
		if (useNewIntersectionCalculation)
			result._intersection = FindPolyhedronIntersection(newVertices, srcPolyhedron, splittingPlane, params);

		// All vertices in newVertices are on the plane, and so therefor must be part of the plane intersection
		// Furthermore, any vertices from the original shape that are exactly on the planar are also part of that
		// intersection. Since the intersection is again a convex polygon, we can safely collect all of the vertices
		// and sort them into winding order
		_vertices.insert(_vertices.end(), newVertices.begin(), newVertices.end());
		#if defined(_DEBUG)
			for (auto v:newVertices)
				assert(std::abs(SignedDistance(v, splittingPlane)) < params._coplanarThreshold);
			for (auto v:coplanarVertices)
				assert(std::abs(SignedDistance(_vertices[v], splittingPlane)) < params._coplanarThreshold);

			// ensure that we haven't changed the total area
			Primitive oldArea = 0, positiveSideArea = 0, negativeSideArea = 0;
			for (auto f:p._faces) oldArea += FaceArea(f);
			for (auto f:positiveSidePolyhedron._faces) positiveSideArea += FaceArea(f);
			for (auto f:negativeSidePolyhedron._faces) negativeSideArea += FaceArea(f);
			assert(Equivalent(positiveSideArea+negativeSideArea, oldArea, Primitive(1e-3)));		// we need some room for changes here, because snapping degenerates will change the area
		#endif

		if (!useNewIntersectionCalculation) {
			// remove duplicates in coplanarVertices
			for (unsigned v=_vertices.size() - newVertices.size(); v!=_vertices.size(); ++v)
				coplanarVertices.push_back(v);		// move in these indices

			// ensure no vertices are too close to each other
			for (unsigned q=0; q<coplanarVertices.size(); ++q)
				for (unsigned i=q+1; i<coplanarVertices.size(); ++i)
					if (coplanarVertices[q] != coplanarVertices[i] && Equivalent(_vertices[coplanarVertices[q]], _vertices[coplanarVertices[i]], params._strictPositionEquivalenceThreshold))
						coplanarVertices[i] = coplanarVertices[q];

			std::sort(coplanarVertices.begin(), coplanarVertices.end());
			coplanarVertices.erase(std::unique(coplanarVertices.begin(), coplanarVertices.end()), coplanarVertices.end());

			if (coplanarVertices.size() >= 3) {
				Face intersectionFace;
				intersectionFace._polygonVertices = OrderVerticesForWinding(std::move(coplanarVertices), splittingPlane, params);
				intersectionFace._plane = splittingPlane;
				if (intersectionFace._polygonVertices.size() >= 3) {
					_faces.push_back(intersectionFace);
					result._intersection = unsigned(_faces.size()-1);
				}
			}
		}

		// exit early if we're entirely on one side of the clipping plane
		if (negativeSidePolyhedron._faces.empty()) {
			assert(result._intersection == ~0u);
			result._positiveSide = srcPolyhedron;
			return result;
		} else if (positiveSidePolyhedron._faces.empty()) {
			assert(result._intersection == ~0u);
			result._negativeSide = srcPolyhedron;
			return result;
		}

		if (result._intersection != ~0u) {
			if (!positiveSidePolyhedron._faces.empty()) {
				auto flippedFace = _faces[result._intersection];
				flippedFace._plane = -flippedFace._plane;
				_faces.push_back(flippedFace);
				positiveSidePolyhedron._faces.push_back(unsigned(_faces.size()-1));
			}

			if (!negativeSidePolyhedron._faces.empty())
				negativeSidePolyhedron._faces.push_back(result._intersection);
		}

		positiveSidePolyhedron._aabb = { Vector3{std::numeric_limits<Primitive>::max(), std::numeric_limits<Primitive>::max(), std::numeric_limits<Primitive>::max()}, Vector3{-std::numeric_limits<Primitive>::max(), -std::numeric_limits<Primitive>::max(), -std::numeric_limits<Primitive>::max()} };
		for (auto& f:positiveSidePolyhedron._faces)
			for (auto v:_faces[f]._polygonVertices) {
				auto p = _vertices[v];
				positiveSidePolyhedron._aabb.first[0] = std::min(positiveSidePolyhedron._aabb.first[0], p[0]);
				positiveSidePolyhedron._aabb.first[1] = std::min(positiveSidePolyhedron._aabb.first[1], p[1]);
				positiveSidePolyhedron._aabb.first[2] = std::min(positiveSidePolyhedron._aabb.first[2], p[2]);
				positiveSidePolyhedron._aabb.second[0] = std::max(positiveSidePolyhedron._aabb.second[0], p[0]);
				positiveSidePolyhedron._aabb.second[1] = std::max(positiveSidePolyhedron._aabb.second[1], p[1]);
				positiveSidePolyhedron._aabb.second[2] = std::max(positiveSidePolyhedron._aabb.second[2], p[2]);
			}
		negativeSidePolyhedron._aabb = { Vector3{std::numeric_limits<Primitive>::max(), std::numeric_limits<Primitive>::max(), std::numeric_limits<Primitive>::max()}, Vector3{-std::numeric_limits<Primitive>::max(), -std::numeric_limits<Primitive>::max(), -std::numeric_limits<Primitive>::max()} };
		for (auto& f:negativeSidePolyhedron._faces)
			for (auto v:_faces[f]._polygonVertices) {
				auto p = _vertices[v];
				negativeSidePolyhedron._aabb.first[0] = std::min(negativeSidePolyhedron._aabb.first[0], p[0]);
				negativeSidePolyhedron._aabb.first[1] = std::min(negativeSidePolyhedron._aabb.first[1], p[1]);
				negativeSidePolyhedron._aabb.first[2] = std::min(negativeSidePolyhedron._aabb.first[2], p[2]);
				negativeSidePolyhedron._aabb.second[0] = std::max(negativeSidePolyhedron._aabb.second[0], p[0]);
				negativeSidePolyhedron._aabb.second[1] = std::max(negativeSidePolyhedron._aabb.second[1], p[1]);
				negativeSidePolyhedron._aabb.second[2] = std::max(negativeSidePolyhedron._aabb.second[2], p[2]);
			}

		if (!positiveSidePolyhedron._faces.empty()) {
			_polyhedra.emplace_back(std::move(positiveSidePolyhedron));
			result._positiveSide = unsigned(_polyhedra.size()-1);
		}

		if (!negativeSidePolyhedron._faces.empty()) {
			_polyhedra.emplace_back(std::move(negativeSidePolyhedron));
			result._negativeSide = unsigned(_polyhedra.size()-1);
		}
		return result;
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::SplitFace_Internal(
			std::vector<unsigned>& coplanarVertices,
			std::vector<Vector3>& newVertices,
			const Face& f, Vector4 splittingPlane,
			const SplittingParams& params) -> SplitFaceInternalResult
	{
		// expecting face is not coplanar with the splitting plane

		auto GetVertexPosition = [&](unsigned idx) {
			if (idx < _vertices.size()) return _vertices[idx];
			return newVertices[idx - _vertices.size()];
		};

		VLA(Primitive, splittingCoefficientBuffer, f._polygonVertices.size());
		Primitive minCoefficient = std::numeric_limits<Primitive>::max(), maxCoefficient = -std::numeric_limits<Primitive>::max();
		for (unsigned v=0; v<f._polygonVertices.size(); ++v) {
			auto coeff = SignedDistance_Accurate(GetVertexPosition(f._polygonVertices[v]), splittingPlane);
			splittingCoefficientBuffer[v] = coeff;
			minCoefficient = std::min(minCoefficient, coeff);
			maxCoefficient = std::max(maxCoefficient, coeff);
		}

		#if defined(_DEBUG)
			// Convexity check. We can actually correctly clip some nonconvex shapes... So we can instead choose
			// to check for convexity related issues by looking at "changeSideCount" later
			const bool strictlyRequireConvexInputs = false;
			if (strictlyRequireConvexInputs) {
				unsigned sideA = 0, sideB = 0;
				for (unsigned c=0; c<f._polygonVertices.size(); ++c) {
					auto A = GetVertexPosition(f._polygonVertices[c]);
					auto B = GetVertexPosition(f._polygonVertices[(c+1)%f._polygonVertices.size()]);
					auto C = GetVertexPosition(f._polygonVertices[(c+2)%f._polygonVertices.size()]);
					auto dir = Truncate(PlaneFit_Accurate(A, B, C));
					auto d = Dot_Accurate(dir, Truncate(f._plane));
					if (d.first >= -d.second) ++sideA;
					else ++sideB;
				}
				assert((sideA != 0) ^ (sideB != 0));
			}
		#endif

		if (minCoefficient >= -params._coplanarThreshold)
			return { SplitFaceInternalResult::AllPositive };		// we're entirely on the positive side

		if (maxCoefficient <= params._coplanarThreshold)
			return { SplitFaceInternalResult::AllNegative };		// we're entirely on the negative side

		// the face polygons must be convex, so the clipping process becomes much easier
		unsigned newVertexOffset = unsigned(_vertices.size());
		Face positiveFace, negativeFace;
		positiveFace._plane = negativeFace._plane = f._plane;
		unsigned lastVertexIndex = unsigned(f._polygonVertices.size()-1);
		Primitive lastCoefficient = splittingCoefficientBuffer[lastVertexIndex];
		auto lastPosition = GetVertexPosition(f._polygonVertices[lastVertexIndex]);
		unsigned changeSideCount = 0;
		for (unsigned vertexIndex=0; vertexIndex < f._polygonVertices.size(); ++vertexIndex) {
			auto coeff = splittingCoefficientBuffer[vertexIndex];
			auto position = GetVertexPosition(f._polygonVertices[vertexIndex]);
			if (std::abs(coeff) < params._coplanarThreshold)
				coplanarVertices.push_back(f._polygonVertices[vertexIndex]);
			if (lastCoefficient < -params._coplanarThreshold) {
				if (coeff < params._coplanarThreshold) {
					negativeFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
				} else {
					// previous point was negative, but we've gone positive
					// generate clipping point, and it should go into both positive and negative sides
					auto clippedPosition = LinearInterpolate_Accurate(lastPosition, position, lastCoefficient / (lastCoefficient - coeff));
					auto d = SignedDistance_Accurate(clippedPosition, splittingPlane);
					assert(std::abs(d) < params._coplanarThreshold);
					unsigned v=0;
					for (; v<newVertices.size(); ++v)
						if (Equivalent(newVertices[v], clippedPosition, params._strictPositionEquivalenceThreshold))
							break;
					if (v == newVertices.size()) newVertices.push_back(clippedPosition);
					negativeFace._polygonVertices.push_back(newVertexOffset+v);
					positiveFace._polygonVertices.push_back(newVertexOffset+v);
					positiveFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
					++changeSideCount;
				}
			} else if (lastCoefficient > params._coplanarThreshold) {
				if (coeff < -params._coplanarThreshold) {
					// previous point was positive, but we've gone negative
					auto clippedPosition = LinearInterpolate_Accurate(lastPosition, position, lastCoefficient / (lastCoefficient - coeff));
					auto d = SignedDistance_Accurate(clippedPosition, splittingPlane);
					assert(std::abs(d) < params._coplanarThreshold);
					unsigned v=0;
					for (; v<newVertices.size(); ++v)
						if (Equivalent(newVertices[v], clippedPosition, params._strictPositionEquivalenceThreshold))
							break;
					if (v == newVertices.size()) newVertices.push_back(clippedPosition);
					positiveFace._polygonVertices.push_back(newVertexOffset+v);
					negativeFace._polygonVertices.push_back(newVertexOffset+v);
					negativeFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
					++changeSideCount;
				} else {
					positiveFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
				}
			} else {
				// previous point was right on the boundary. However, it was (or will be) inserted only to one side
				// we need to track back until we find the last vertex that was on a particular side
				auto testVertexIndex = (vertexIndex + f._polygonVertices.size() - 2) % f._polygonVertices.size();
				for (;;) {
					if (std::abs(splittingCoefficientBuffer[testVertexIndex]) > params._coplanarThreshold)
						break;
					assert(testVertexIndex != vertexIndex);	// since we're assuming that face is not coplanar with the face, we should never get here (however, it's possible testVertexIndex is the only vertex on a particular side)
					testVertexIndex = (testVertexIndex + f._polygonVertices.size() - 1) % f._polygonVertices.size();
				}
				if (splittingCoefficientBuffer[testVertexIndex] < -params._coplanarThreshold) {
					if (coeff < params._coplanarThreshold) {
						negativeFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
					} else {
						// we've gone positive. Don't need to clip, just duplicate the previous point
						positiveFace._polygonVertices.push_back(f._polygonVertices[lastVertexIndex]);
						positiveFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
						++changeSideCount;
					}
				} else {
					assert(splittingCoefficientBuffer[testVertexIndex] > params._coplanarThreshold);
					if (coeff < -params._coplanarThreshold) {
						// we've gone negative. Don't need to clip, just duplicate the previous point
						negativeFace._polygonVertices.push_back(f._polygonVertices[lastVertexIndex]);
						negativeFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
						++changeSideCount;
					} else {
						positiveFace._polygonVertices.push_back(f._polygonVertices[vertexIndex]);
					}
				}
			}

			lastVertexIndex = vertexIndex;
			lastCoefficient = coeff;
			lastPosition = position;
		}

		assert(changeSideCount <= 2);

		// We can potentially generate degenerate triangles if multiple intersection tests find points within params._strictPositionEquivalenceThreshold
		// This is rare, but can we don't want to return degenerates, so we should check for it
		constexpr bool sanitizeOutput = true;
		bool sanitized = false;
		if constexpr (sanitizeOutput) {
			// duplicates should be sequential due to convexity, so we can use std::unique (just check the wrap around case)
			auto i = std::unique(negativeFace._polygonVertices.begin(), negativeFace._polygonVertices.end());
			sanitized |= i != negativeFace._polygonVertices.end();
			negativeFace._polygonVertices.erase(i, negativeFace._polygonVertices.end());
			while (!negativeFace._polygonVertices.empty() && negativeFace._polygonVertices.back() == negativeFace._polygonVertices.front()) {
				negativeFace._polygonVertices.pop_back();
				sanitized = true;
			}
			if (negativeFace._polygonVertices.size() < 3)
				negativeFace._polygonVertices.clear();

			i = std::unique(positiveFace._polygonVertices.begin(), positiveFace._polygonVertices.end());
			sanitized |= i != positiveFace._polygonVertices.end();
			positiveFace._polygonVertices.erase(i, positiveFace._polygonVertices.end());
			while (!positiveFace._polygonVertices.empty() && positiveFace._polygonVertices.back() == positiveFace._polygonVertices.front()) {
				positiveFace._polygonVertices.pop_back();
				sanitized = true;
			}
			if (positiveFace._polygonVertices.size() < 3)
				positiveFace._polygonVertices.clear();

			#if defined(_DEBUG)
				for (auto i=negativeFace._polygonVertices.begin(); i!=negativeFace._polygonVertices.end(); ++i)
					for (auto i2=i+1; i2!=negativeFace._polygonVertices.end(); ++i2)
						assert(*i2 != *i);
				for (auto i=positiveFace._polygonVertices.begin(); i!=positiveFace._polygonVertices.end(); ++i)
					for (auto i2=i+1; i2!=positiveFace._polygonVertices.end(); ++i2)
						assert(*i2 != *i);
			#endif
		}

		#if defined(_DEBUG)
			if (strictlyRequireConvexInputs) {
				if (!negativeFace._polygonVertices.empty()) {
					unsigned sideA = 0, sideB = 0;
					for (unsigned c=0; c<negativeFace._polygonVertices.size(); ++c) {
						auto A = GetVertexPosition(negativeFace._polygonVertices[c]);
						auto B = GetVertexPosition(negativeFace._polygonVertices[(c+1)%negativeFace._polygonVertices.size()]);
						auto C = GetVertexPosition(negativeFace._polygonVertices[(c+2)%negativeFace._polygonVertices.size()]);
						auto dir = Cross_Accurate<Primitive>(B - A, C - A);
						auto d = Dot_Accurate(dir, Truncate(f._plane));
						if (d.first >= -d.second) ++sideA;
						else ++sideB;
					}
					assert((sideA != 0) ^ (sideB != 0));
				}

				if (!positiveFace._polygonVertices.empty()) {
					unsigned sideA = 0, sideB = 0;
					for (unsigned c=0; c<positiveFace._polygonVertices.size(); ++c) {
						auto A = GetVertexPosition(positiveFace._polygonVertices[c]);
						auto B = GetVertexPosition(positiveFace._polygonVertices[(c+1)%positiveFace._polygonVertices.size()]);
						auto C = GetVertexPosition(positiveFace._polygonVertices[(c+2)%positiveFace._polygonVertices.size()]);
						auto dir = Cross_Accurate<Primitive>(B - A, C - A);
						auto d = Dot_Accurate(dir, Truncate(f._plane));
						if (d.first >= -d.second) ++sideA;
						else ++sideB;
					}
					assert((sideA != 0) ^ (sideB != 0));
				}
			}
		#endif

		if (negativeFace._polygonVertices.empty()) { // entirely on the positive side
			assert(positiveFace._polygonVertices.size() == f._polygonVertices.size());
			if (sanitized) {
				if (positiveFace._polygonVertices.empty())
					return {};		// input must have been entirely degenerate, nothing left
				return { SplitFaceInternalResult::Split, std::move(positiveFace), {} }; // we did some patchup, probably due to degenerate inputs
			}
			return { SplitFaceInternalResult::AllPositive };
		}
		if (positiveFace._polygonVertices.empty()) { // entirely on the negative side
			assert(negativeFace._polygonVertices.size() == f._polygonVertices.size());
			if (sanitized) {
				if (negativeFace._polygonVertices.empty())
					return {};		// input must have been entirely degenerate, nothing left
				return { SplitFaceInternalResult::Split, {}, std::move(negativeFace) };
			}
			return { SplitFaceInternalResult::AllNegative };
		}

		return { SplitFaceInternalResult::Split, std::move(positiveFace), std::move(negativeFace) };
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::SplitFace(FaceIndex srcFace, Vector4 splittingPlane, const SplittingParams& params) -> SplitFaceResult
	{
		std::vector<unsigned> coplanarVertices;
		std::vector<Vector3> newVertices;
		auto internalRes = SplitFace_Internal(coplanarVertices, newVertices, _faces[srcFace], splittingPlane, params);
		SplitFaceResult res { s_FaceIndex_Invalid, s_FaceIndex_Invalid };
		switch (internalRes._type) {
		case SplitFaceInternalResult::AllNegative: res._negativeSide = srcFace; break;
		case SplitFaceInternalResult::AllPositive: res._positiveSide = srcFace; break;
		default:
			assert(internalRes._type == SplitFaceInternalResult::Split);
			if (!internalRes._positiveSide._polygonVertices.empty()) {
				_faces.emplace_back(std::move(internalRes._positiveSide));
				res._positiveSide = unsigned(_faces.size()-1);
			}
			if (!internalRes._negativeSide._polygonVertices.empty()) {
				_faces.emplace_back(std::move(internalRes._negativeSide));
				res._negativeSide = unsigned(_faces.size()-1);
			}
			break;
		}
		_vertices.insert(_vertices.end(), newVertices.begin(), newVertices.end());

		#if defined(_DEBUG)		// tend to get floating point creep errors that trigger this
			Primitive oldArea = FaceArea(srcFace);
			Primitive newArea = 0;
			if (res._negativeSide != s_FaceIndex_Invalid) newArea += FaceArea(res._negativeSide);
			if (res._positiveSide != s_FaceIndex_Invalid) newArea += FaceArea(res._positiveSide);
			assert(Equivalent(oldArea, newArea, Primitive(1e-3)));

			if (res._negativeSide != s_FaceIndex_Invalid)
				for (auto v:_faces[res._negativeSide]._polygonVertices) {
					auto d = SignedDistance_Accurate(_vertices[v], splittingPlane);
					assert(d < params._coplanarThreshold);
				}
			if (res._positiveSide != s_FaceIndex_Invalid)
				for (auto v:_faces[res._positiveSide]._polygonVertices) {
					auto d = SignedDistance_Accurate(_vertices[v], splittingPlane);
					assert(d > -params._coplanarThreshold);
				}
		#endif
		return res;
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::FindPolyhedronIntersection(std::vector<Vector3>& newVertices, unsigned srcPolyhedron, Vector4 splittingPlane, const SplittingParams& params) -> FaceIndex
	{
		Vector3 outerVertices[6];
		Vector3 q = _polyhedra[srcPolyhedron]._aabb.second - _polyhedra[srcPolyhedron]._aabb.first;
		auto ptCount = PlaneAABBIntersection<Primitive>(outerVertices, splittingPlane, _polyhedra[srcPolyhedron]._aabb.first - Primitive(0.05) * q, _polyhedra[srcPolyhedron]._aabb.second + Primitive(0.05) * q);

		Face workingFace;
		workingFace._plane = splittingPlane;
		for (unsigned c=0; c<ptCount; ++c)
			workingFace._polygonVertices.push_back(_vertices.size() + newVertices.size() + c);
		auto originalNewVerticesSize = newVertices.size();
		newVertices.insert(newVertices.end(), outerVertices, outerVertices+ptCount);

		std::vector<unsigned> coplanarVertices;
		auto& p = _polyhedra[srcPolyhedron];
		for (unsigned c=0; c<p._faces.size() && workingFace._polygonVertices.size() >= 3; ++c) {
			auto split = SplitFace_Internal(coplanarVertices, newVertices, workingFace, _faces[p._faces[c]]._plane, params);
			switch (split._type) {
			case SplitFaceInternalResult::AllNegative: break;
			case SplitFaceInternalResult::AllPositive: workingFace = {}; break;
			default:
				assert(split._type == SplitFaceInternalResult::Split);
				workingFace = std::move(split._negativeSide);
				break;
			}
		}

		if (workingFace._polygonVertices.size() < 3) {
			newVertices.erase(newVertices.begin() + originalNewVerticesSize, newVertices.end());
			return s_FaceIndex_Invalid;
		}

		{
			// Filter out vertices that we added to newVertices, but no longer required for the final polygon. This avoids
			// polluting "newVertices" too much
			VLA(unsigned, remappedVertexIndices, newVertices.size()-originalNewVerticesSize);
			std::fill(remappedVertexIndices, remappedVertexIndices+newVertices.size()-originalNewVerticesSize, ~0u);
			unsigned iterator = 0;
			for (auto& i:workingFace._polygonVertices)
				if (i >= _vertices.size() + originalNewVerticesSize) {
					if (remappedVertexIndices[i-originalNewVerticesSize-_vertices.size()] == ~0u)
						remappedVertexIndices[i-originalNewVerticesSize-_vertices.size()] = iterator++;
					i = _vertices.size() + originalNewVerticesSize + remappedVertexIndices[i-originalNewVerticesSize-_vertices.size()];
				}

			VLA_UNSAFE_FORCE(Vector3, temp, iterator);
			for (unsigned c=0; c<newVertices.size()-originalNewVerticesSize; ++c)
				if (remappedVertexIndices[c] != ~0u)
					temp[remappedVertexIndices[c]] = newVertices[originalNewVerticesSize+c];

			newVertices.erase(newVertices.begin() + originalNewVerticesSize + iterator, newVertices.end());
			std::copy(temp, temp+iterator, newVertices.begin() + originalNewVerticesSize);
		}

		_faces.push_back(workingFace);
		return FaceIndex(_faces.size()-1);
	}

	template<typename Primitive>
		std::vector<unsigned> ConvexPolyhedraSet<Primitive>::OrderVerticesForWinding(std::vector<unsigned>&& inputVertices, Vector4 facePlane, const SplittingParams& params)
	{
		assert(inputVertices.size() >= 3);
		// Look for a winding order, assuming that vertices border a convex polygon
		bool fallbackToOldApproach = false;
		std::vector<unsigned> polygonVertices;
		polygonVertices.push_back(inputVertices.back());
		inputVertices.pop_back();
		while (!inputVertices.empty()) {
			auto lastAdded = polygonVertices.back();
			// search for the correct next vertex. This may have issues if there are near-colinear vertices
			std::vector<unsigned> candidateNextVertices;
			for (unsigned v=0; v<inputVertices.size(); ++v) {
				auto plane = PlaneFit_AccurateNoNormalize<Primitive>(_vertices[lastAdded], _vertices[inputVertices[v]], _vertices[lastAdded]+Truncate(facePlane));
				bool foundNegativePt = false;
				for (unsigned v2=0; v2<inputVertices.size() && !foundNegativePt; ++v2) {
					if (v2 == v) continue;
					foundNegativePt |= SignedDistance_Accurate(_vertices[inputVertices[v2]], plane) < -params._coplanarThreshold;
				}
				for (unsigned v2=0; v2<polygonVertices.size()-1 && !foundNegativePt; ++v2)		// (don't need to check the last added vertex)
					foundNegativePt |= SignedDistance_Accurate(_vertices[polygonVertices[v2]], plane) < -params._coplanarThreshold;

				if (!foundNegativePt)
					candidateNextVertices.push_back(v);
			}

			if (candidateNextVertices.empty()) {
				fallbackToOldApproach = true;
				break;
			}

			if (candidateNextVertices.size() == 1) {
				polygonVertices.push_back(inputVertices[candidateNextVertices.front()]);
				inputVertices.erase(inputVertices.begin() + candidateNextVertices.front());
			} else {
				// if there are multiple options (probably colinear vertices), sort by edge length
				std::sort(
					candidateNextVertices.begin(), candidateNextVertices.end(),
					[&](auto lhs, auto rhs) {
						auto a = _vertices[lastAdded];
						return MagnitudeSquared(_vertices[inputVertices[lhs]] - a) < MagnitudeSquared(_vertices[inputVertices[rhs]] - a);
					});

				for (auto v:candidateNextVertices)
					polygonVertices.push_back(inputVertices[v]);

				std::sort(candidateNextVertices.begin(), candidateNextVertices.end());
				for (auto i=candidateNextVertices.rbegin(); i!=candidateNextVertices.rend(); ++i)
					inputVertices.erase(inputVertices.begin() + *i);
			}
		}

		if (fallbackToOldApproach) {
			// There may be some concavity. But the vertices back in inputVertices again, and try another approach
			inputVertices.insert(inputVertices.end(), polygonVertices.begin(), polygonVertices.end());

			Vector3 anchor = _vertices[inputVertices[0]];
			for (unsigned v=1; v<inputVertices.size(); ++v)
				anchor += Primitive(.1) * (_vertices[inputVertices[v]] - anchor);

			std::sort(
				inputVertices.begin(), inputVertices.end(),
				[facePlane, anchor, this](auto lhs, auto rhs) {
					auto c = Cross_Accurate<Primitive>(_vertices[lhs] - anchor, _vertices[rhs] - anchor);
					auto d = Dot_Accurate(c, Truncate(facePlane));
					return d.first < -d.second;
				});
			polygonVertices = std::move(inputVertices);
		}

		return polygonVertices;
	}

	template<typename Primitive>
		Primitive ConvexPolyhedraSet<Primitive>::FaceArea(FaceIndex srcFace)
	{
		return FaceArea_Internal(_faces[srcFace]);
	}

	template<typename Primitive>
		Primitive ConvexPolyhedraSet<Primitive>::FaceArea_Internal(const Face& f)
	{
		// convert the polygon into tristrip order, and sum the order of the triangles
		// this is only correct because everything is convex
		assert(f._polygonVertices.size() > 2);
		VLA(unsigned, triStripOrder, f._polygonVertices.size());
		for (unsigned c=0; c<f._polygonVertices.size(); ++c) {
			if ((c&1) == 0) {
				triStripOrder[c] = f._polygonVertices[(f._polygonVertices.size() - c/2) % f._polygonVertices.size()];
			} else {
				triStripOrder[c] = f._polygonVertices[c/2 + 1];
			}
		}

		Primitive result = 0;
		for (unsigned c=0; c<f._polygonVertices.size() - 2; ++c) {
			auto A = _vertices[triStripOrder[c]];
			auto B = _vertices[triStripOrder[c+1]];
			auto C = _vertices[triStripOrder[c+2]];
			result += TriangleArea_Accurate(A, B, C);
		}
		return result;
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::AddAxiallyAlignedBox(Vector3 mins, Vector3 maxs) -> PolyhedronIndex
	{
		assert(mins[0] < maxs[0]); assert(mins[1] < maxs[1]); assert(mins[2] < maxs[2]);

		// Generate face for each of the aabb faces
		// positive side of face planes will be pointing outwards
		Polyhedron polyhedron;
		polyhedron._aabb = { mins, maxs };

		Vector3 corners[] {
			Vector3 { mins[0], mins[1], mins[2] },
			Vector3 { maxs[0], mins[1], mins[2] },
			Vector3 { maxs[0], maxs[1], mins[2] },
			Vector3 { mins[0], maxs[1], mins[2] },
			Vector3 { mins[0], mins[1], maxs[2] },
			Vector3 { maxs[0], mins[1], maxs[2] },
			Vector3 { maxs[0], maxs[1], maxs[2] },
			Vector3 { mins[0], maxs[1], maxs[2] }
		};

		auto voff = (unsigned)_vertices.size();
		auto foff = (unsigned)_faces.size();
		_vertices.insert(_vertices.end(), corners, corners+8);

		Face f0;
		f0._polygonVertices = { 0, 1, 5, 4 };
		f0._plane = Vector4 { 0.f, -1.f, 0.f, mins[1] };
		for (auto& v:f0._polygonVertices) v += voff;
		_faces.push_back(f0);

		Face f1;
		f1._polygonVertices = { 1, 2, 6, 5 };
		f1._plane = Vector4 { 1.f, 0.f, 0.f, -maxs[0] };
		for (auto& v:f1._polygonVertices) v += voff;
		_faces.push_back(f1);

		Face f2;
		f2._polygonVertices = { 2, 3, 7, 6 };
		f2._plane = Vector4 { 0.f, 1.f, 0.f, -maxs[1] };
		for (auto& v:f2._polygonVertices) v += voff;
		_faces.push_back(f2);

		Face f3;
		f3._polygonVertices = { 3, 0, 4, 7 };
		f3._plane = Vector4 { -1.f, 0.f, 0.f, mins[0] };
		for (auto& v:f3._polygonVertices) v += voff;
		_faces.push_back(f3);

		Face f4;
		f4._polygonVertices = { 0, 3, 2, 1 };
		f4._plane = Vector4 { 0.f, 0.f, -1.f, mins[2] };
		for (auto& v:f4._polygonVertices) v += voff;
		_faces.push_back(f4);

		Face f5;
		f5._polygonVertices = { 4, 5, 6, 7 };
		f5._plane = Vector4 { 0.f, 0.f, 1.f, -maxs[2] };
		for (auto& v:f5._polygonVertices) v += voff;
		_faces.push_back(f5);

		for (unsigned c=0; c<6; ++c)
			polyhedron._faces.push_back(foff+c);
		
		_polyhedra.emplace_back(std::move(polyhedron));
		return (unsigned)_polyhedra.size()-1;
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::AddFace(Vector3 A, Vector3 B, Vector3 C) -> FaceIndex
	{
		Face f;
		f._plane = PlaneFit(A, B, C);
		_vertices.push_back(A); _vertices.push_back(B); _vertices.push_back(C);
		f._polygonVertices.push_back(unsigned(_vertices.size()-3));
		f._polygonVertices.push_back(unsigned(_vertices.size()-2));
		f._polygonVertices.push_back(unsigned(_vertices.size()-1));
		_faces.push_back(f);
		return unsigned(_faces.size()-1);
	}

	template<typename Primitive>
		auto ConvexPolyhedraSet<Primitive>::AddFace(IteratorRange<const Vector3*> vertices, Vector4 plane) -> FaceIndex
	{
		Face f;
		f._plane = plane;
		f._polygonVertices.reserve(vertices.size());
		for (unsigned c=0; c<vertices.size(); ++c) {
			_vertices.push_back(vertices[c]);
			f._polygonVertices.push_back(unsigned(_vertices.size()-1));
		}
		_faces.push_back(f);
		return unsigned(_faces.size()-1);
	}

	template<typename Primitive>
		int ConvexPolyhedraSet<Primitive>::VolumeTest(PolyhedronIndex polyhedron, Vector3 testPt)
	{
		// The positive side of each plane is outside of the object. Since the polyhedra are
		// all convex, if we are on the outside of any plane, we are outside of the volume
		Primitive maxSignedDistance = -std::numeric_limits<Primitive>::max();
		for (auto f:_polyhedra[polyhedron]._faces) {
			auto sd = SignedDistance(testPt, _faces[f]._plane);
			maxSignedDistance = std::max(maxSignedDistance, sd);
		}
		const Primitive coplanarThreshold = Primitive(1e-3);
		if (maxSignedDistance < -coplanarThreshold)
			return -1;
		if (maxSignedDistance < coplanarThreshold)
			return 0;
		return 1;
	}
}
