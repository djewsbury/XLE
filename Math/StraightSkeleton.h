// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <limits>
#include <memory>

namespace XLEMath
{
	T1(Primitive) class StraightSkeleton
	{
	public:
		enum class EdgeType { VertexPath, Wavefront };
		struct Edge { unsigned _head; unsigned _tail; EdgeType _type; };

		size_t _boundaryPointCount = 0;
		std::vector<Vector3T<Primitive>> _steinerVertices;
		std::vector<Edge> _edges;

		std::vector<std::vector<unsigned>> WavefrontAsVertexLoops();
	};

	T1(Primitive) class StraightSkeletonGraph;

	T1(Primitive) class StraightSkeletonCalculator
	{
	public:
		void AddLoop(IteratorRange<const Vector2T<Primitive>*> vertices);
		StraightSkeleton<Primitive> Calculate(Primitive maxInset = std::numeric_limits<Primitive>::max());

		StraightSkeletonCalculator();
		~StraightSkeletonCalculator();
	private:
		std::unique_ptr<StraightSkeletonGraph<Primitive>> _graph;
	};

	/// <summary>Test if a given polygon is valid for generating a straight skeleton</summary>
	/// Some polygon inputs will not generate a valid straight skeleton. This include cases such as
	///  * input edges cross over other edges
	///	 * colinear adjacent edges
	///  * duplicated vertices
	/// This will check for cases like this and will return false if the loop is not valid for
	/// straight skeleton
	T1(Primitive) bool ValidatePolygonLoop(IteratorRange<const Vector2T<Primitive>*> vertices);
}
