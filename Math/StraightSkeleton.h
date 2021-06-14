// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Math/Vector.h"
#include "../Utility/IteratorUtils.h"
#include <vector>
#include <limits>

namespace XLEMath
{
	T1(Primitive) class StraightSkeleton
	{
	public:
		using VertexId = unsigned;
		enum class EdgeType { VertexPath, Wavefront };
		struct Edge { VertexId _head; VertexId _tail; EdgeType _type; };

		size_t _boundaryPointCount = 0;
		std::vector<Vector3T<Primitive>> _steinerVertices;
		std::vector<Edge> _edges;

		std::vector<std::vector<unsigned>> WavefrontAsVertexLoops();
	};

	T1(Primitive) StraightSkeleton<Primitive> CalculateStraightSkeleton(
		IteratorRange<const Vector2T<Primitive>*> vertices, 
		Primitive maxInset = std::numeric_limits<Primitive>::max());
}
