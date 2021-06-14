// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StraightSkeleton.h"
#include "StraightSkeleton_Internal.h"
#include "../Math/Geometry.h"
#include <stack>
#include <cmath>
#include <random>	// (for validation testing)

#if defined(_DEBUG)
	// #define EXTRA_VALIDATION
#endif

#pragma warning(disable:4505) // 'SceneEngine::StraightSkeleton::ReplaceVertex': unreferenced local function has been removed


namespace XLEMath
{
	static const unsigned BoundaryVertexFlag = 1u<<31u;

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) class Vertex
	{
	public:
		Vector2T<Primitive>	_position;
		Vector2T<Primitive>	_velocity;
		Primitive			_initialTime;
		unsigned			_skeletonVertexId;
	};

	class WavefrontEdge
	{
	public:
		unsigned	_head, _tail;
		unsigned	_leftFace, _rightFace;
	};

	class MotorcycleSegment
	{
	public:
		unsigned _head;
		unsigned _tail;		// (this is the fixed vertex)
		unsigned _leftFace, _rightFace;
	};

    T1(Primitive) std::ostream& operator<<(std::ostream& str, const Vertex<Primitive>& vert)
    {
        str << "(pos: " << vert._position << ", vel: " << vert._velocity << ", initialTime: " << vert._initialTime << ", skeletonVertexId: " << vert._skeletonVertexId << ")";
        return str;
    }

	T1(Primitive) auto PositionAtTime(const Vertex<Primitive>& v, Primitive time)
		-> typename std::enable_if<!std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		auto result = v._position + v._velocity * (time - v._initialTime);
		assert(IsFiniteNumber(result[0]) && IsFiniteNumber(result[1]));
		return result;
	}

	T1(Primitive) auto PositionAtTime(const Vertex<Primitive>& v, Primitive time)
		-> typename std::enable_if<std::is_integral<Primitive>::value, Vector2T<Primitive>>::type
	{
		return v._position + v._velocity * (time - v._initialTime) / static_velocityVectorScale;
	}

	T1(Primitive) static Vector3T<Primitive> ClampedPositionAtTime(const Vertex<Primitive>& v, Primitive time)
	{
		if (AdaptiveEquivalent(v._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()))
			return Expand(v._position, v._initialTime);
		return Expand(PositionAtTime(v, time), time);
	}

	T1(Primitive) static Primitive CalculateCollapseTime(const Vertex<Primitive>& v0, const Vertex<Primitive>& v1)
	{
		// hack -- if one side is frozen, we must collapse immediately
		if (AdaptiveEquivalent(v0._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return std::numeric_limits<Primitive>::max();
		if (AdaptiveEquivalent(v1._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) return std::numeric_limits<Primitive>::max();

		// At some point the trajectories of v0 & v1 may intersect
		// We need to pick out a specific time on the timeline, and find both v0 and v1 at that
		// time. 
		auto calcTime = std::max(v0._initialTime, v1._initialTime);
		auto p0 = PositionAtTime(v0, calcTime);
		auto p1 = PositionAtTime(v1, calcTime);
		return calcTime + CalculateCollapseTime(p0, v0._velocity, p1, v1._velocity);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) static unsigned AddSteinerVertex(StraightSkeleton<Primitive>& skeleton, const Vector3T<Primitive>& vertex)
	{
		assert(vertex[2] != Primitive(0));
		assert(IsFiniteNumber(vertex[0]) && IsFiniteNumber(vertex[1]) && IsFiniteNumber(vertex[2]));
		assert(vertex[0] != std::numeric_limits<Primitive>::max() && vertex[1] != std::numeric_limits<Primitive>::max() && vertex[2] != std::numeric_limits<Primitive>::max());
		auto existing = std::find_if(skeleton._steinerVertices.begin(), skeleton._steinerVertices.end(),
			[vertex](const Vector3T<Primitive>& v) { return AdaptiveEquivalent(v, vertex, GetEpsilon<Primitive>()); });
		if (existing != skeleton._steinerVertices.end()) {
			return (unsigned)std::distance(skeleton._steinerVertices.begin(), existing);
		}
		#if defined(_DEBUG)
			auto test = std::find_if(skeleton._steinerVertices.begin(), skeleton._steinerVertices.end(),
				[vertex](const Vector3T<Primitive>& v) { return AdaptiveEquivalent(Truncate(v), Truncate(vertex), GetEpsilon<Primitive>()); });
			assert(test == skeleton._steinerVertices.end());
		#endif
		auto result = (unsigned)skeleton._steinerVertices.size();
		skeleton._steinerVertices.push_back(vertex);
		return result;
	}

	T1(Primitive) static bool BuildCrashEvent_OldMethod(
		Vector3T<Primitive>& pointAndTime,
		Vertex<Primitive> edgeHead, Vertex<Primitive> edgeTail,
		Vertex<Primitive> motorcycle)
	{
		// Attempt to find a crash event between the given motor cycle and the given edge.
		// Since the edge segments are moving, the solution is a little complex
		// We can create a triangle between head, tail & the motorcycle head
		// If there is a collision, the triangle area will be zero at that point.
		// So we can search for a time when the triangle area is zero, and check to see
		// if a collision has actually occurred at that time.
		const auto calcTime = std::max(std::max(edgeHead._initialTime, edgeTail._initialTime), motorcycle._initialTime);
		auto p0 = PositionAtTime(edgeHead, calcTime);
		auto p1 = PositionAtTime(edgeTail, calcTime);
		auto v0 = edgeHead._velocity;
		auto v1 = edgeTail._velocity;

		auto p2 = PositionAtTime(motorcycle, calcTime);
		auto v2 = motorcycle._velocity;

		// 2 * signed triangle area = 
		//		(p1[0]-p0[0]) * (p2[1]-p0[1]) - (p2[0]-p0[0]) * (p1[1]-p0[1])
		//
		// A =	(p1[0]+t*v1[0]-p0[0]-t*v0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		// B =  (p2[0]+t*v2[0]-p0[0]-t*v0[0]) * (p1[1]+t*v1[1]-p0[1]-t*v0[1]);
		//
		// A =   (p1[0]-p0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		//	 + t*(v1[0]-v0[0]) * (p2[1]+t*v2[1]-p0[1]-t*v0[1])
		//
		// A =   (p1[0]-p0[0]) * (p2[1]-p0[1]+t*(v2[1]-v0[1]))
		//	 + t*(v1[0]-v0[0]) * (p2[1]-p0[1]+t*(v2[1]-v0[1]))
		//
		// A =   (p1[0]-p0[0])*(p2[1]-p0[1]) + t*(p1[0]-p0[0])*(v2[1]-v0[1])
		//	 + t*(v1[0]-v0[0])*(p2[1]-p0[1]) + t*t*(v1[0]-v0[0])*(v2[1]-v0[1])
		//
		// B =   (p2[0]-p0[0])*(p1[1]-p0[1]) + t*(p2[0]-p0[0])*(v1[1]-v0[1])
		//	 + t*(v2[0]-v0[0])*(p1[1]-p0[1]) + t*t*(v2[0]-v0[0])*(v1[1]-v0[1])
		//
		// 0 = t*t*a + t*b + c
		// c = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(p1[1]-p0[1])
		// b = (p1[0]-p0[0])*(v2[1]-v0[1]) + (v1[0]-v0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(v1[1]-v0[1]) - (v2[0]-v0[0])*(p1[1]-p0[1])
		// a = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v2[0]-v0[0])*(v1[1]-v0[1])

		auto a = (v1[0]-v0[0])*(v2[1]-v0[1]) - (v2[0]-v0[0])*(v1[1]-v0[1]);
		if (AdaptiveEquivalent(a, Primitive(0), GetEpsilon<Primitive>())) return false;
			
		auto c = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(p1[1]-p0[1]);
		auto b = (p1[0]-p0[0])*(v2[1]-v0[1]) + (v1[0]-v0[0])*(p2[1]-p0[1]) - (p2[0]-p0[0])*(v1[1]-v0[1]) - (v2[0]-v0[0])*(p1[1]-p0[1]);
			
		// x = (-b +/- sqrt(b*b - 4*a*c)) / 2*a
		auto K = b*b - Primitive(4)*a*c;
		if (K < Primitive(0)) return false;

		auto Q = std::sqrt(K);
		Primitive ts[] = {
			calcTime + Primitive((-b + Q) / (decltype(Q)(2)*a)),
			calcTime + Primitive((-b - Q) / (decltype(Q)(2)*a))
		};

		// Is there is a viable collision at either t0 or t1?
		// All 3 points should be on the same line at this point -- so we just need to check if
		// the motorcycle is between them (or intersecting a vertex)
		for (auto t:ts) {
			if (t <= std::max(edgeHead._initialTime, edgeTail._initialTime)) continue;	// don't need to check collisions that happen too late
			auto P0 = PositionAtTime(edgeHead, t);
			auto P1 = PositionAtTime(edgeTail, t);
			auto P2 = PositionAtTime(motorcycle, t);
			if ((Dot(P1-P0, P2-P0) > Primitive(0)) && (Dot(P0-P1, P2-P1) > Primitive(0))) {
				// good collision
				pointAndTime = Expand(P2, t);
				return true;
			} else if (AdaptiveEquivalent(P0, P2, GetEpsilon<Primitive>()) || AdaptiveEquivalent(P1, P2, GetEpsilon<Primitive>())) {
				// collided with vertex (or close enough)
				pointAndTime = Expand(P2, t);
				return true;
			}
		}

		return false;
	}

	T1(Primitive) static bool FindCrashEvent(
		Vector3T<Primitive>& crash, 
		Vector2T<Primitive> edgeHead, Vector2T<Primitive> edgeTail, Vector2T<Primitive> motorVelocity)
	{
		// Look for a crash event involving this motorcycle & the given segment
		// Here, we assume that the motorcycle starts at the origin (ie, caller should
		// redefine the coordinate system to suit that requirement)

		auto mag = (Primitive)std::hypot(edgeHead[0] - edgeTail[0], edgeHead[1] - edgeTail[1]);
		assert(IsFiniteNumber(mag));
		if (AdaptiveEquivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
			return false;

		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;

		// first row tests for intersection with the edge segment (as it's moving along its normal)
		auto Nx = Primitive((edgeTail[1] - edgeHead[1]) * VelocityVectorScale<Primitive>::Value / mag);
		auto Ny = Primitive((edgeHead[0] - edgeTail[0]) * VelocityVectorScale<Primitive>::Value / mag);
		M(0, 0) = Nx;
		M(0, 1) = Ny;
		M(0, 2) = -Nx*Nx-Ny*Ny;
		res[0]  = edgeTail[0] * Nx + edgeTail[1] * Ny;

		// second row tests x component of motorcycle
		// x - t * motorVelocity[0] = 0
		M(1, 0) = Primitive(1);
		M(1, 1) = Primitive(0);
		M(1, 2) = -motorVelocity[0];
		res[1]  = Primitive(0);

		// third row tests y component of motorcycle
		// y - t * motorVelocity[1] = 0
		M(2, 0) = Primitive(0);
		M(2, 1) = Primitive(1);
		M(2, 2) = -motorVelocity[1];
		res[2]  = Primitive(0);

		if (!InvertInplaceSafe(M, GetEpsilon<Primitive>()))
			return false;

		crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return true;
	}

	T1(Primitive) static bool FindCrashEvent(
		Vector3T<Primitive>& crash, 
		Vector2T<Primitive> edgeHead, Vector2T<Primitive> edgeTail, 
		Vector2T<Primitive> motorPrev, Vector2T<Primitive> motorNext)
	{
		Matrix3x3T<Primitive> M;
		Vector3T<Primitive> res;
		Vector2T<Primitive> As[] = { edgeTail, motorPrev, Zero<Vector2T<Primitive>>() };
		Vector2T<Primitive> Bs[] = { edgeHead, Zero<Vector2T<Primitive>>(), motorNext };
		for (unsigned c=0; c<3; ++c) {
			auto mag = (Primitive)std::hypot(Bs[c][0] - As[c][0], Bs[c][1] - As[c][1]);
			assert(IsFiniteNumber(mag));
			// If pm1->p0 or p1->p2 are too small, we can't accurately calculate the collapse time. This can 
			// happen if there's an earlier collapse event on the left or right of this edge. In these cases,
			// we should process those collapse events first.
			if (AdaptiveEquivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
				return false;

			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		if (!InvertInplaceSafe(M, GetEpsilon<Primitive>()))
			return false;

		crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return true;
	}

	T1(Primitive) static bool BuildCrashEvent_SimultaneousV(
		Vector3T<Primitive>& pointAndTime,
		Vertex<Primitive> edgeHead, Vertex<Primitive> edgeTail,
		Vertex<Primitive> motorcycle)
	{
		const auto calcTime = std::max(std::max(edgeHead._initialTime, edgeTail._initialTime), motorcycle._initialTime);
		auto p0 = PositionAtTime(edgeHead, calcTime);
		auto p1 = PositionAtTime(edgeTail, calcTime);
		auto p2 = PositionAtTime(motorcycle, calcTime);
		auto res = FindCrashEvent<Primitive>(pointAndTime, p0-p2, p1-p2, motorcycle._velocity);
		if (!res || pointAndTime[2] < Primitive(0)) return false;

		pointAndTime += Expand(p2, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = PositionAtTime(edgeHead, pointAndTime[2]);
		p1 = PositionAtTime(edgeTail, pointAndTime[2]);
		p2 = Truncate(pointAndTime);
		return ((Dot(p1-p0, p2-p0) > Primitive(0)) && (Dot(p0-p1, p2-p1) > Primitive(0)))
			|| (AdaptiveEquivalent(p0, p2, GetEpsilon<Primitive>()) || AdaptiveEquivalent(p1, p2, GetEpsilon<Primitive>()));
	}

	T1(Primitive) static bool BuildCrashEvent_Simultaneous(
		Vector3T<Primitive>& pointAndTime,
		Vertex<Primitive> edgeHead, Vertex<Primitive> edgeTail,
		Vertex<Primitive> motorcyclePrev, Vertex<Primitive> motorcycle, Vertex<Primitive> motorcycleNext)
	{
		const auto calcTime = std::max(std::max(std::max(std::max(edgeHead._initialTime, edgeTail._initialTime), motorcyclePrev._initialTime), motorcycle._initialTime), motorcycleNext._initialTime);
		auto p0 = PositionAtTime(edgeHead, calcTime);
		auto p1 = PositionAtTime(edgeTail, calcTime);

		auto m0 = PositionAtTime(motorcyclePrev, calcTime);
		auto m1 = PositionAtTime(motorcycle, calcTime);
		auto m2 = PositionAtTime(motorcycleNext, calcTime);

		auto res = FindCrashEvent<Primitive>(pointAndTime, p0-m1, p1-m1, m0-m1, m2-m1);
		if (!res || pointAndTime[2] < Primitive(0)) return false;

		pointAndTime += Expand(m1, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = PositionAtTime(edgeHead, pointAndTime[2]);
		p1 = PositionAtTime(edgeTail, pointAndTime[2]);
		auto p2 = Truncate(pointAndTime);
		return ((Dot(p1-p0, p2-p0) > Primitive(0)) && (Dot(p0-p1, p2-p1) > Primitive(0)))
			|| (AdaptiveEquivalent(p0, p2, GetEpsilon<Primitive>()) || AdaptiveEquivalent(p1, p2, GetEpsilon<Primitive>()));
	}

    template<typename Iterator>
		static auto FindInAndOut(IteratorRange<Iterator> edges, unsigned pivotVertex) -> std::pair<Iterator, Iterator>
    {
        std::pair<Iterator, Iterator> result { nullptr, nullptr };
        for  (auto s=edges.begin(); s!=edges.end(); ++s) {
            if (s->_head == pivotVertex) { assert(!result.first); result.first = s; }
            else if (s->_tail == pivotVertex) { assert(!result.second); result.second = s; }
        }
        return result;
    }
    
    T1(Primitive) struct CrashEvent
	{
		Vector2T<Primitive> _crashPt;
		Primitive _time;
		unsigned _collisionEdgeHead;
		unsigned _collisionEdgeTail;
	};

	T1(Primitive) static CrashEvent<Primitive> CalculateCrashTime(
		unsigned motor, 
		IteratorRange<const WavefrontEdge*> segments,
		IteratorRange<const Vertex<Primitive>*> vertices)
	{
		CrashEvent<Primitive> bestCollisionEvent { Zero<Vector2T<Primitive>>(), std::numeric_limits<Primitive>::max(), ~0u, ~0u };

		const auto& v = vertices[motor];
		auto inAndOut = FindInAndOut(segments, motor);
		auto vPrev = vertices[inAndOut.first->_tail];
		auto vNext = vertices[inAndOut.second->_head];

		// Look for an intersection with segments
		for (const auto&e:segments) {
			if (e._head == motor || e._tail == motor) continue;	// (can't crash if part of the same segment)
			const auto& head = vertices[e._head];
			const auto& tail = vertices[e._tail];

			Vector3T<Primitive> pointAndTime;
			auto res = BuildCrashEvent_Simultaneous(pointAndTime, head, tail, vPrev, v, vNext);

			/*Vector3T<Primitive> compare2;
			auto resC = BuildCrashEvent_SimultaneousV(compare2, head, tail, v);
			(void)compare2, resC;*/

			/*Vector3T<Primitive> compare;
			auto resA = BuildCrashEvent_OldMethod(compare, head, tail, v);
			(void)resA, compare;*/

			if (res && pointAndTime[2] < bestCollisionEvent._time) {
				bestCollisionEvent._crashPt = Truncate(pointAndTime);
				bestCollisionEvent._time = pointAndTime[2];
				bestCollisionEvent._collisionEdgeHead = e._head;
				bestCollisionEvent._collisionEdgeTail = e._tail;
			}
		}

		return bestCollisionEvent;
	}

	T1(Primitive) static bool IsFrozen(const Vertex<Primitive>& v) { return AdaptiveEquivalent(v._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()); }

	T1(EdgeType) static void AddUnique(std::vector<EdgeType>& dst, const EdgeType& edge)
	{
		auto existing = std::find_if(dst.begin(), dst.end(), 
			[&edge](const EdgeType&e) { return e._head == edge._head && e._tail == edge._tail; });

		if (existing == dst.end()) {
			dst.push_back(edge);
		} else {
			assert(existing->_type == edge._type);
		}
	}

	T1(Primitive) static void AddEdge(StraightSkeleton<Primitive>& dest, unsigned headVertex, unsigned tailVertex, unsigned leftEdge, unsigned rightEdge, typename StraightSkeleton<Primitive>::EdgeType type)
	{
		if (headVertex == tailVertex) return;

		if (rightEdge != ~0u)
			AddUnique(dest._faces[rightEdge]._edges, {headVertex, tailVertex, type});
		if (leftEdge != ~0u)
			AddUnique(dest._faces[leftEdge]._edges, {tailVertex, headVertex, type});

		if (rightEdge == ~0u && leftEdge == ~0u)
			AddUnique(dest._unplacedEdges, {headVertex, tailVertex, type});
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) class Graph
	{
	public:
		std::vector<Vertex<Primitive>> _vertices;

		class WavefrontLoop
		{
		public:
			std::vector<WavefrontEdge> _edges;
			std::vector<MotorcycleSegment> _motorcycleSegments;
			Primitive _lastEventTime = Primitive(0);
		};
		std::vector<WavefrontLoop> _loops;
		size_t _boundaryPointCount;

		StraightSkeleton<Primitive> CalculateSkeleton(Primitive maxTime);

        #if defined(_DEBUG)
            std::vector<Vector2T<Primitive>> _sourceLoop;
        #endif

	private:
		void WriteWavefront(StraightSkeleton<Primitive>& dest, const WavefrontLoop& loop, Primitive time);
		void AddEdgeForVertexPath(StraightSkeleton<Primitive>& dst, const WavefrontLoop& loop, unsigned v, unsigned finalVertId);
		void ValidateState();

		void ProcessMotorcycleCrashes(
			WavefrontLoop& loop,
			IteratorRange<const std::pair<CrashEvent<Primitive>, unsigned>*> crashes,
			StraightSkeleton<Primitive>& result);

		struct EdgeEvent
		{
		public:
			size_t _edge;
			Vector2T<Primitive> _collapsePt;
			Primitive _time;
		};
		void ProcessEdgeEvents(
			WavefrontLoop& loop,
			IteratorRange<const EdgeEvent*> collapses,
			StraightSkeleton<Primitive>& result);

		Primitive FindCollapses(std::vector<EdgeEvent>& bestCollapse, const WavefrontLoop& loop);
		Primitive FindMotorcycleCrashes(std::vector<std::pair<CrashEvent<Primitive>, unsigned>>& bestMotorcycleCrash, const WavefrontLoop& loop, Primitive bestCollapseTime);
	};

	T1(Primitive) StraightSkeleton<Primitive> Graph<Primitive>::CalculateSkeleton(Primitive maxTime)
	{
		StraightSkeleton<Primitive> result;
		result._faces.resize(_boundaryPointCount);

		std::vector<EdgeEvent> bestCollapse;
		std::vector<std::pair<CrashEvent<Primitive>, unsigned>> bestMotorcycleCrash;
		bestCollapse.reserve(8);

		unsigned lastEvent = 0;
		std::vector<WavefrontLoop> completedLoops;

		while (!_loops.empty()) {
			ValidateState();

			auto& loop = _loops.front();
			if (loop._edges.size() <= 2) {
				completedLoops.emplace_back(std::move(loop));
				_loops.erase(_loops.begin());
				continue;
			}

			// Find the next event to occur
			//		-- either a edge collapse or a motorcycle collision
			bestCollapse.clear();
			auto bestCollapseTime = FindCollapses(bestCollapse, loop);

			// Also check for motorcycles colliding.
			//		These can collide with segments in the _wavefrontEdges list, or 
			//		other motorcycles, or boundary polygon edges
			bestMotorcycleCrash.clear();
			auto bestMotorcycleCrashTime = FindMotorcycleCrashes(bestMotorcycleCrash, loop, bestCollapseTime);

            // If we do not find any more events, the remaining wavefronts will expand infinitely.
            // This case isn't perfectly handled currently, we'll just complete the loop here if
            // it has started.  If it has not started, skip it.
            if (bestCollapse.empty() && bestMotorcycleCrash.empty()) {
                if (loop._lastEventTime != Primitive(0)) {
                    completedLoops.emplace_back(std::move(loop));
                }
				_loops.erase(_loops.begin());
                continue;
            }

			auto nextEvent = (!bestMotorcycleCrash.empty()) ? bestMotorcycleCrashTime : bestCollapseTime;
			if (nextEvent >= maxTime) {
				loop._lastEventTime = maxTime;
				completedLoops.emplace_back(std::move(loop));
				_loops.erase(_loops.begin());
				continue;
			}

			// If we get some motorcycle crashes, we're going to ignore the collapse events
			// and just process the motorcycle events
			if (!bestMotorcycleCrash.empty()) {
				ProcessMotorcycleCrashes(loop, MakeIteratorRange(bestMotorcycleCrash), result);
				lastEvent = 1;
			} else {
				ProcessEdgeEvents(loop, MakeIteratorRange(bestCollapse), result);
				lastEvent = 2;
			}
		}

		for (const auto&l:completedLoops)
			WriteWavefront(result, l, l._lastEventTime);

		return result;
	}

	T1(Primitive) Primitive Graph<Primitive>::FindCollapses(std::vector<EdgeEvent>& bestCollapse, const WavefrontLoop& loop)
	{
		auto bestCollapseTime = std::numeric_limits<Primitive>::max();
		for (size_t e=0; e<loop._edges.size(); ++e) {
			const auto& seg0 = loop._edges[(e+loop._edges.size()-1)%loop._edges.size()];
			const auto& seg1 = loop._edges[e];
			const auto& seg2 = loop._edges[(e+1)%loop._edges.size()];
			assert(seg0._head == seg1._tail && seg1._head == seg2._tail);	// ensure segments are correctly ordered

			const auto& vm1 = _vertices[seg0._tail];
			const auto& v0 = _vertices[seg1._tail];
			const auto& v1 = _vertices[seg1._head];
			const auto& v2 = _vertices[seg2._head];
			const auto calcTime = std::max(std::max(std::max(vm1._initialTime, v0._initialTime), v1._initialTime), v2._initialTime);
			auto collapse = CalculateEdgeCollapse_Offset(PositionAtTime(vm1, calcTime), PositionAtTime(v0, calcTime), PositionAtTime(v1, calcTime), PositionAtTime(v2, calcTime));
			if (collapse[2] < Primitive(0)) continue;

            // Add a little bit of leeway on this check. We can sometimes find that edges are
            // effectively already collapsed (or within a reasonable threshold for floating
            // point creep)
            // In these cases, it's ok if one of the vertices in already in a frozen state, because
            // we don't actually need to move it any significant distance
            if (collapse[2] > GetEpsilon<Primitive>()) {
			    assert(!IsFrozen(v0) && !IsFrozen(v1));
            }

			auto collapseTime = collapse[2] + calcTime;
			assert(collapseTime >= loop._lastEventTime);
			if (collapseTime < (bestCollapseTime - GetEpsilon<Primitive>())) {
				bestCollapse.clear();
				bestCollapse.push_back({e, Truncate(collapse), collapseTime});
				bestCollapseTime = collapseTime;
			} else if (collapseTime < (bestCollapseTime + GetEpsilon<Primitive>())) {
				bestCollapse.push_back({e, Truncate(collapse), collapseTime});
				bestCollapseTime = std::min(collapseTime, bestCollapseTime);
			}
		}

		// Always ensure that every entry in "bestCollapse" is within
		// "GetEpsilon<Primitive>()" of bestCollapseTime -- this can become untrue if there
		// are chains of events with very small gaps in between them
		bestCollapse.erase(
			std::remove_if(
				bestCollapse.begin(), bestCollapse.end(),
				[bestCollapseTime](const EdgeEvent& e) { return !(e._time < bestCollapseTime + GetEpsilon<Primitive>()); }), 
			bestCollapse.end());

		return bestCollapseTime;
	}

	T1(Primitive) Primitive Graph<Primitive>::FindMotorcycleCrashes(std::vector<std::pair<CrashEvent<Primitive>, unsigned>>& bestMotorcycleCrash, const WavefrontLoop& loop, Primitive bestCollapseTime)
	{
		auto bestMotorcycleCrashTime = std::numeric_limits<Primitive>::max();
		for (auto m=loop._motorcycleSegments.begin(); m!=loop._motorcycleSegments.end(); ++m) {
			#if defined(_DEBUG)
				const auto& head = _vertices[m->_head];
				assert(!AdaptiveEquivalent(head._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
				assert(head._initialTime == Primitive(0));
			#endif

			auto crashEvent = CalculateCrashTime<Primitive>(m->_head, MakeIteratorRange(loop._edges), MakeIteratorRange(_vertices));
			if (crashEvent._time < Primitive(0)) continue;
			assert(crashEvent._time >= loop._lastEventTime);

			// If our best motorcycle collision happens before our best collapse, then we
			// must do the motorcycle first, and recalculate edge collapses afterwards
			// But if they happen at the same time, we should do the edge collapse first,
			// and then recalculate the motorcycle collisions afterwards (ie, even if there's
			// a motorcycle collision at around the same time as the edge collapses, we're
			// going to ignore it for now)
			if (crashEvent._time < (bestCollapseTime + GetEpsilon<Primitive>())) {
				if (crashEvent._time < (bestMotorcycleCrashTime - GetEpsilon<Primitive>())) {
					bestMotorcycleCrash.clear();
					bestMotorcycleCrash.push_back(std::make_pair(crashEvent, m->_head));
					bestMotorcycleCrashTime = crashEvent._time;
				} else if (crashEvent._time < (bestMotorcycleCrashTime + GetEpsilon<Primitive>())) {
					bestMotorcycleCrash.push_back(std::make_pair(crashEvent, m->_head));
					bestMotorcycleCrashTime = std::min(crashEvent._time, bestMotorcycleCrashTime);
				}
			}
		}

		bestMotorcycleCrash.erase(
			std::remove_if(
				bestMotorcycleCrash.begin(), bestMotorcycleCrash.end(),
				[bestMotorcycleCrashTime](const std::pair<CrashEvent<Primitive>, unsigned>& e) { return !(e.first._time < bestMotorcycleCrashTime + GetEpsilon<Primitive>()); }), 
			bestMotorcycleCrash.end());

		return bestMotorcycleCrashTime;
	}

	T1(Primitive) Graph<Primitive> BuildGraphFromVertexLoop(IteratorRange<const Vector2T<Primitive>*> vertices)
	{
		assert(vertices.size() >= 2);
		const auto threshold = Primitive(1e-6f);

		// Construct the starting point for the straight skeleton calculations
		// We're expecting the input vertices to be a closed loop, in counter-clockwise order
		// The first and last vertices should *not* be the same vertex; there is an implied
		// segment between the first and last.
		Graph<Primitive> result;
		typename Graph<Primitive>::WavefrontLoop loop;
		loop._edges.reserve(vertices.size());
		result._vertices.reserve(vertices.size());
		for (size_t v=0; v<vertices.size(); ++v) {
			// Each segment of the polygon becomes an "edge segment" in the graph
			auto v0 = (v+vertices.size()-1)%vertices.size();
			auto v1 = v;
			auto v2 = (v+1)%vertices.size();
			loop._edges.emplace_back(WavefrontEdge{unsigned(v2), unsigned(v), ~0u, unsigned(v)});

			// We must calculate the velocity for each vertex, based on which segments it belongs to...
			auto velocity = CalculateVertexVelocity(vertices[v0], vertices[v1], vertices[v2]);
			assert(!AdaptiveEquivalent(velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
			result._vertices.emplace_back(Vertex<Primitive>{vertices[v], velocity, Primitive(0), BoundaryVertexFlag|unsigned(v)});
		}

		// Each reflex vertex in the graph must result in a "motocycle segment".
		// We already know the velocity of the head of the motorcycle; and it has a fixed tail that
		// stays at the original position
		for (size_t v=0; v<vertices.size(); ++v) {
			auto v0 = (v+vertices.size()-1)%vertices.size();
			auto v1 = v;
			auto v2 = (v+1)%vertices.size();

			// Since we're expecting counter-clockwise inputs, if "v1" is a convex vertex, we should
			// wind around to the left when going v0->v1->v2
			// If we wind to the right then it's a reflex vertex, and we must add a motorcycle edge
			if (CalculateWindingType(vertices[v0], vertices[v1], vertices[v2], threshold) == WindingType::Right) {
				auto fixedVertex = (unsigned)(result._vertices.size());
				result._vertices.emplace_back(Vertex<Primitive>{vertices[v], Zero<Vector2T<Primitive>>(), Primitive(0), BoundaryVertexFlag|unsigned(v)});
				loop._motorcycleSegments.emplace_back(MotorcycleSegment{unsigned(v), unsigned(fixedVertex), unsigned(v0), unsigned(v1)});
			}
		}

		result._loops.emplace_back(std::move(loop));
		result._boundaryPointCount = vertices.size();

        #if defined(_DEBUG)
            result._sourceLoop.insert(result._sourceLoop.begin(), vertices.begin(), vertices.end());
        #endif
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) void Graph<Primitive>::ValidateState()
	{
		#if defined(EXTRA_VALIDATION)
			for (auto&loop:_loops) {
				// validate vertex velocities
				for (unsigned v=0; v<_vertices.size(); ++v) {
					WavefrontEdge* in, *out;
					std::tie(in, out) = FindInAndOut(MakeIteratorRange(loop._edges), v);
					if (in && out) {
						if (in->_tail != out->_head) {
							auto calcTime = (_vertices[in->_tail]._initialTime + _vertices[v]._initialTime + _vertices[out->_head]._initialTime) / Primitive(3);
							auto v0 = PositionAtTime(_vertices[in->_tail], calcTime);
							auto v1 = PositionAtTime(_vertices[v], calcTime);
							auto v2 = PositionAtTime(_vertices[out->_head], calcTime);
							auto expectedVelocity = CalculateVertexVelocity(v0, v1, v2);
							assert(AdaptiveEquivalent(_vertices[v]._velocity, expectedVelocity, 10000*GetEpsilon<Primitive>()));
							assert(!AdaptiveEquivalent(expectedVelocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
						} else {
							assert(AdaptiveEquivalent(_vertices[v]._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
						}
					}
				}
				// Vertices in active loops should not be frozen
				/*if (loop._edges.size() > 2)
					for (const auto&e:loop._edges)
						assert(!IsFrozen(_vertices[e._head]) && !IsFrozen(_vertices[e._tail]));*/
				// every wavefront edge must have a collapse time (assuming it's vertices are not frozen)
				/*for (const auto&e:loop._edges) {
					if (IsFrozen(_vertices[e._head]) || IsFrozen(_vertices[e._tail])) continue;
					auto collapseTime = CalculateCollapseTime(_vertices[e._head], _vertices[e._tail]);
					assert(collapseTime != std::numeric_limits<Primitive>::max());		//it can be negative; because some edges are expanding
				}*/
			}
		#endif
	}

	T1(Primitive) void Graph<Primitive>::ProcessMotorcycleCrashes(
		WavefrontLoop& initialLoop,
		IteratorRange<const std::pair<CrashEvent<Primitive>, unsigned>*> crashesInit,
		StraightSkeleton<Primitive>& result)
	{
		std::vector<std::pair<CrashEvent<Primitive>, unsigned>> crashes(crashesInit.begin(), crashesInit.end());
		std::vector<WavefrontLoop> workingLoops;
		workingLoops.emplace_back(std::move(initialLoop));

		for (auto crashI=crashes.begin(); crashI!=crashes.end(); ++crashI) {
			auto crashEvent = crashI->first;
			auto motorHead = crashI->second;

			#if defined(_DEBUG)
				auto alreadyProcessed = std::find_if(
					crashes.begin(), crashI,
					[motorHead](const std::pair<CrashEvent<Primitive>, unsigned>& e) { return e.second == motorHead; });
				assert(alreadyProcessed == crashI);
			#endif

			// The motorcycleSegment should be part of one of our working loops
			//  -- but we don't know which one. We have to find it; both the crash
			// segment and the motorcycle should be part of the same loop
			MotorcycleSegment* motor = nullptr;
			typename std::vector<WavefrontLoop>::iterator loop;
			for (loop=workingLoops.begin(); loop!=workingLoops.end(); ++loop) {
				auto i = std::find_if(loop->_motorcycleSegments.begin(), loop->_motorcycleSegments.end(),
					[motorHead](const MotorcycleSegment& seg){ return seg._head == motorHead; });
				if (i != loop->_motorcycleSegments.end()) {
					motor = AsPointer(i);
					break;
				}
			}
			assert(motor != nullptr);

			auto crashPtSkeleton = AddSteinerVertex(result, Vector3T<Primitive>(crashEvent._crashPt, crashEvent._time));

			// Since we're removing "motor.head" from the simulation, we have to add a skeleton edge 
			// for vertex path along the motor cycle path  
			assert(_vertices[motor->_head]._skeletonVertexId != ~0u);
			AddEdge(result, 
					crashPtSkeleton, _vertices[motor->_head]._skeletonVertexId,
					motor->_leftFace, motor->_rightFace, StraightSkeleton<Primitive>::EdgeType::VertexPath);

			// We need to build 2 new WavefrontLoops -- one for the "tout" side and one for the "tin" side
			// In some cases, one side or the other than can be completely collapsed. But we're still going to
			// create it.
			WavefrontLoop outSide, inSide;
			outSide._lastEventTime = crashEvent._time;
			inSide._lastEventTime = crashEvent._time;
			{
				// Start at motor._head, and work around in order until we hit the crash segment.
				auto tout = std::find_if(loop->_edges.begin(), loop->_edges.end(),
					[motorHead](const WavefrontEdge& test) { return test._tail == motorHead; });
				assert(tout != loop->_edges.end());

				if (tout->_head != crashEvent._collisionEdgeTail) {
					auto starti = tout+1;
					if (starti == loop->_edges.end()) starti = loop->_edges.begin();
					auto i=starti;
					while (i->_head!=crashEvent._collisionEdgeTail) {
						outSide._edges.push_back(*i);
						++i;
						if (i == loop->_edges.end()) i = loop->_edges.begin();
					}
					outSide._edges.push_back(*i);
				}

				// Note that we can end up with colinear vertices here in two cases. When there are
				// colinear vertices, we can't calculate the vertex velocity values accurately 
				//   1) we might produce a fully collapsed 2-edge loop
				//   2) when there are simultaneous motorcycle crashes
				// In the second case, the loop is not fully collapsed yet; but after the remaining
				// crashes have been processed, it will be.
				auto newVertex = (unsigned)_vertices.size();
				_vertices.push_back(Vertex<Primitive>{crashEvent._crashPt, Zero<Vector2T<Primitive>>(), crashEvent._time, crashPtSkeleton});

                std::vector<WavefrontEdge>::iterator crashSegment;
                if (crashEvent._collisionEdgeHead != crashEvent._collisionEdgeTail) {
                    crashSegment = std::find_if(loop->_edges.begin(), loop->_edges.end(),
                        [crashEvent](const WavefrontEdge& e) { return e._head == crashEvent._collisionEdgeHead && e._tail == crashEvent._collisionEdgeTail; });
                    assert(crashSegment != loop->_edges.end());
                } else {
                    crashSegment = std::find_if(loop->_edges.begin(), loop->_edges.end(),
                        [crashEvent](const WavefrontEdge& e) { return e._head == crashEvent._collisionEdgeHead; });
                    assert(crashSegment != loop->_edges.end());
                }
                outSide._edges.push_back({newVertex, crashEvent._collisionEdgeTail, crashSegment->_leftFace, crashSegment->_rightFace});    // (hin)

				outSide._edges.push_back({tout->_head, newVertex, tout->_leftFace, tout->_rightFace});					// tout
			}

			{
				// Start at crashSegment._head, and work around in order until we hit the motor vertex
				auto hout = std::find_if(loop->_edges.begin(), loop->_edges.end(),
					[crashEvent](const WavefrontEdge& test) { return test._tail == crashEvent._collisionEdgeHead; });
				assert(hout != loop->_edges.end());

				auto starti = hout;
				auto i=starti;
				while (i->_head!=motorHead) {
					inSide._edges.push_back(*i);
					++i;
					if (i == loop->_edges.end())
						i = loop->_edges.begin();
				}

				auto tin = i;

				auto newVertex = (unsigned)_vertices.size();
				_vertices.push_back(Vertex<Primitive>{crashEvent._crashPt, Zero<Vector2T<Primitive>>(), crashEvent._time, crashPtSkeleton});
				inSide._edges.push_back({newVertex, tin->_tail, tin->_leftFace, tin->_rightFace});

                std::vector<WavefrontEdge>::iterator crashSegment;
                if (crashEvent._collisionEdgeHead != crashEvent._collisionEdgeTail) {
                    crashSegment = std::find_if(loop->_edges.begin(), loop->_edges.end(),
                        [crashEvent](const WavefrontEdge& e) { return e._head == crashEvent._collisionEdgeHead && e._tail == crashEvent._collisionEdgeTail; });
                    assert(crashSegment != loop->_edges.end());
                } else {
                    crashSegment = std::find_if(loop->_edges.begin(), loop->_edges.end(),
                        [crashEvent](const WavefrontEdge& e) { return e._tail == crashEvent._collisionEdgeTail; });
                    assert(crashSegment != loop->_edges.end());
                }

                inSide._edges.push_back({crashEvent._collisionEdgeHead, newVertex, crashSegment->_leftFace, crashSegment->_rightFace});
			}

			// We don't have to add more edges from the 2 new vertices to the crash point. Since 
			// we registered both new vertices with "crashPtSkeleton", we will automatically get 
			// this edge filled in when the vertex reaches the end of it's path.

			// Move the motorcycles from "loop" to "inSide" or "outSide" depending on which loop 
			// they are now a part of.
			for (const auto&m:loop->_motorcycleSegments) {
				if (&m == motor) continue;		// (skip this one, it's just been processed)

				auto inSideI = std::find_if(inSide._edges.begin(), inSide._edges.end(),
					[&m](const WavefrontEdge&e) { return e._head == m._head || e._tail == m._head; });
				if (inSideI != inSide._edges.end()) {
					inSide._motorcycleSegments.push_back(m);
					continue;
				}

				auto outSideI = std::find_if(outSide._edges.begin(), outSide._edges.end(),
					[&m](const WavefrontEdge&e) { return e._head == m._head || e._tail == m._head; });
				if (outSideI != outSide._edges.end()) {
					outSide._motorcycleSegments.push_back(m);
					continue;
				}

				// we can sometimes get here if boths edges to the left and right of
				// the motor cycle collapse before the motorcycle crashes 
			}

			// We may have to rename the crash segments for any future crashes. We remove 1 vertex
			// from the system every time we process a motorcycle crash. So, if one of the upcoming
			// crash events involves this vertex, we have rename it to either the new vertex on the
			// inSide, or on the outSide
            unsigned crashSegmentTail = crashEvent._collisionEdgeTail, crashSegmentHead = crashEvent._collisionEdgeHead;
			for (auto i=crashI+1; i!=crashes.end(); ++i) {
				auto inSideI = std::find_if(inSide._motorcycleSegments.begin(), inSide._motorcycleSegments.end(),
					[i](const MotorcycleSegment& s){ return s._head == i->second;});
				bool isInSide = inSideI != inSide._motorcycleSegments.end();
				#if defined(_DEBUG)
					auto outSideI = std::find_if(outSide._motorcycleSegments.begin(), outSide._motorcycleSegments.end(),
						[i](const MotorcycleSegment& s){ return s._head == i->second;});
					assert(isInSide || (outSideI != outSide._motorcycleSegments.end())); (void)outSideI;
				#endif
				
				auto replacementIdx = unsigned(isInSide?(_vertices.size()-1):(_vertices.size()-2));
				if (i->first._collisionEdgeHead == motorHead) i->first._collisionEdgeHead = replacementIdx;
				else if (i->first._collisionEdgeTail == motorHead) i->first._collisionEdgeTail = replacementIdx;

				if (isInSide) {
					if (i->first._collisionEdgeHead == crashSegmentTail) i->first._collisionEdgeHead = replacementIdx;
					else if (i->first._collisionEdgeTail == crashSegmentTail) i->first._collisionEdgeTail = replacementIdx;
				} else {
					if (i->first._collisionEdgeHead == crashSegmentHead) i->first._collisionEdgeHead = replacementIdx;
					else if (i->first._collisionEdgeTail == crashSegmentHead) i->first._collisionEdgeTail = replacementIdx;
				}
			}

			// Overwrite "loop" with outSide, and append inSide to the list of wavefront loops
			*loop = std::move(outSide);
			workingLoops.emplace_back(std::move(inSide));
		}

		// The "velocity" value for newly created vertices will not have been updated yet; we needed
		// to wait until all crash events were processed before we did. But 
		for (auto&loop:workingLoops) {
			if (loop._edges.size() <= 2)
				continue;
			auto prevEdge = loop._edges.end()-1;
			for (auto edge=loop._edges.begin(); edge!=loop._edges.end(); ++edge) {
				assert(prevEdge->_head == edge->_tail);
				auto& v0 = _vertices[prevEdge->_tail];
				auto& v1 = _vertices[edge->_tail];
				auto& v2 = _vertices[edge->_head];
				if (AdaptiveEquivalent(v1._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) {
					// we must calculate the velocity at the max initial time -- (this should always be the crash time)
					auto calcTime = std::max(std::max(v0._initialTime, v1._initialTime), v2._initialTime);
					assert(AdaptiveEquivalent(calcTime, loop._lastEventTime, GetEpsilon<Primitive>()));
					v1._velocity = CalculateVertexVelocity(PositionAtTime(v0, calcTime), PositionAtTime(v1, calcTime), PositionAtTime(v2, calcTime));
				}
				prevEdge = edge;
			}
		}

		// Merge "workingLoops" back into the main list of loops -- ensurign that
		// we overwrite the initial loop with something valid.
		initialLoop = std::move(workingLoops[0]);
		_loops.reserve(_loops.size() + workingLoops.size() - 1);
		for (auto i=workingLoops.begin()+1; i!=workingLoops.end(); ++i)
			_loops.emplace_back(std::move(*i));
	}

	T1(Primitive) void Graph<Primitive>::ProcessEdgeEvents(
		WavefrontLoop& loop,
		IteratorRange<const EdgeEvent*> collapses,
		StraightSkeleton<Primitive>& result)
	{
		if (collapses.empty()) return;

		Primitive bestCollapseTime = std::numeric_limits<Primitive>::max();
		for (auto&c:collapses) bestCollapseTime = std::min(c._time, bestCollapseTime);

		// Process the "edge" events... first separate the edges into collapse groups
		// Each collapse group collapses onto a single vertex. We will search through all
		// of the collapse events we're processing, and separate them into discrete groups.
		std::vector<unsigned> collapseGroups(collapses.size(), ~0u);
		struct CollapseGroupInfo { unsigned _head, _tail, _newVertex; };
		std::vector<CollapseGroupInfo> collapseGroupInfos;
		unsigned nextCollapseGroup = 0;
		for (size_t c=0; c<collapses.size(); ++c) {
			if (collapseGroups[c] != ~0u) continue;

			collapseGroups[c] = nextCollapseGroup;

			// got back as far as possible, from tail to tail
			auto searchingTail = loop._edges[collapses[c]._edge]._tail;
			for (;;) {
				auto i = std::find_if(collapses.begin(), collapses.end(),
					[searchingTail, &loop](const EdgeEvent& t)
					{ return loop._edges[t._edge]._head == searchingTail; });
				if (i == collapses.end()) break;
				if (collapseGroups[std::distance(collapses.begin(), i)] == nextCollapseGroup) break;
				assert(collapseGroups[std::distance(collapses.begin(), i)] == ~0u);
				collapseGroups[std::distance(collapses.begin(), i)] = nextCollapseGroup;
				searchingTail = loop._edges[i->_edge]._tail;
			}

			// also go forward head to head
			auto searchingHead = loop._edges[collapses[c]._edge]._head;
			for (;;) {
				auto i = std::find_if(collapses.begin(), collapses.end(),
					[searchingHead, &loop](const EdgeEvent& t)
					{ return loop._edges[t._edge]._tail == searchingHead; });
				if (i == collapses.end()) break;
				if (collapseGroups[std::distance(collapses.begin(), i)] == nextCollapseGroup) break;
				assert(collapseGroups[std::distance(collapses.begin(), i)] == ~0u);
				collapseGroups[std::distance(collapses.begin(), i)] = nextCollapseGroup;
				searchingHead = loop._edges[i->_edge]._head;
			}

			++nextCollapseGroup;
			collapseGroupInfos.push_back({searchingHead, searchingTail, ~0u});
		}

		// Each collapse group becomes a single new vertex. We can collate them together
		// now, and write out some segments to the output skeleton
		std::vector<unsigned> collapseGroupNewVertex(nextCollapseGroup, ~0u);
		for (auto collapseGroup=0u; collapseGroup<nextCollapseGroup; ++collapseGroup) {
			Vector2T<Primitive> collisionPt(Primitive(0), Primitive(0));
			unsigned contributors = 0;
			for (size_t c=0; c<collapses.size(); ++c) {
				if (collapseGroups[c] != collapseGroup) continue;
				collisionPt += collapses[c]._collapsePt;
				contributors += 1;

				// At this point they should not be frozen (but they will all be frozen later)
                // We add a little bit of leeway for vertices that were frozen at the same time as
                // the collapse event (at least within some reasonable threshold)
				const auto& seg = loop._edges[collapses[c]._edge];
                if ((bestCollapseTime - _vertices[seg._tail]._initialTime) > GetEpsilon<Primitive>()) {
				    assert(!IsFrozen(_vertices[seg._tail]));
                }
                if ((bestCollapseTime - _vertices[seg._head]._initialTime) > GetEpsilon<Primitive>()) {
				    assert(!IsFrozen(_vertices[seg._head]));
                }
				(void)seg;
			}
			collisionPt /= Primitive(contributors);

			// Validate that our "collisionPt" is close to all of the collapsing points
			#if defined(_DEBUG)
				for (size_t c=0; c<collapses.size(); ++c) {
					if (collapseGroups[c] != collapseGroup) continue;
					const auto& seg = loop._edges[collapses[c]._edge];
					auto one = PositionAtTime(_vertices[seg._head], bestCollapseTime);
					auto two = PositionAtTime(_vertices[seg._tail], bestCollapseTime);
                    if (!IsFrozen(_vertices[seg._head])) {
                        assert(AdaptiveEquivalent(one, collisionPt, 1000*GetEpsilon<Primitive>()));
                    }
                    if (!IsFrozen(_vertices[seg._tail])) {
                        assert(AdaptiveEquivalent(two, collisionPt, 1000*GetEpsilon<Primitive>()));
                    }

                    /*
                    std::cout << "Bad collision pt detected. Source loop:" << std::endl;
                    std::cout << "[" << std::endl;
                    for (auto v=_sourceLoop.begin(); v!=_sourceLoop.end(); ++v) {
                        if (v!=_sourceLoop.begin())
                            std::cout << "," << std::endl;
                        std::cout << "\t{\"x\": " << (*v)[0] << ", \"y\": " << (*v)[1] << "}";
                    }
                    std::cout << std::endl << "]" << std::endl;
                    */
				}
			#endif

			// add a steiner vertex into the output
			auto collapseVertId = AddSteinerVertex(result, Expand(collisionPt, bestCollapseTime));

			// We're removing vertices from active loops -- so, we must add their vertex path to the
			// output skeleton.
			// Note that since we're connecting both head and tail, we'll end up doubling up each edge
			for (size_t c=0; c<collapses.size(); ++c) {
				if (collapseGroups[c] != collapseGroup) continue;
				const auto& seg = loop._edges[collapses[c]._edge];
				for (auto v:{ seg._head, seg._tail }) {
					AddEdgeForVertexPath(result, loop, v, collapseVertId);

					// Also remove any motorcycles associated with these vertices (since they will be removed
					// from active loops, the motorcycle is no longer valid)
					auto m = std::find_if(loop._motorcycleSegments.begin(), loop._motorcycleSegments.end(),
						[v](const MotorcycleSegment& seg) { return seg._head == v; });
					if (m != loop._motorcycleSegments.end())
						loop._motorcycleSegments.erase(m);
				}
			}

			// create a new vertex in the graph to connect the edges to either side of the collapse
			// todo -- should this new vertex ever have a motorcycle?
			collapseGroupInfos[collapseGroup]._newVertex = (unsigned)_vertices.size();
			_vertices.push_back(Vertex<Primitive>{collisionPt, Zero<Vector2T<Primitive>>(), bestCollapseTime, collapseVertId});
		}

		// Remove all of the collapsed edges
		// (note, expecting bestCollapse to be sorted by "_edge")
		for (auto i=collapses.size()-1;;--i) {
			if (i!=collapses.size()-1) { assert(collapses[i]._edge < collapses[i+1]._edge); }
			loop._edges.erase(loop._edges.begin() + collapses[i]._edge);
			if (i == 0) break;
		}

		// For each collapse group, there should be one tail edge, and one head edge
		// We need to find these edges in order to calculate the velocity of the point in between
		// Let's resolve that now...
		// todo -- this process can be simplified and combined with the above loop now; because we
		//			ensure that the wavefront loops are kept in winding order
		for (const auto& group:collapseGroupInfos) {
			if (group._head == group._tail) continue;	// if we remove an entire loop, let's assume that there are no external links to it

			// reassign the edges on either side of the collapse group to
			// point to the new vertex
			auto tail = FindInAndOut(MakeIteratorRange(loop._edges).template Cast<WavefrontEdge*>(), group._tail).first;
			auto head = FindInAndOut(MakeIteratorRange(loop._edges).template Cast<WavefrontEdge*>(), group._head).second;
			assert(tail && head);
			tail->_head = group._newVertex;
			head->_tail = group._newVertex;

			#if defined(EXTRA_VALIDATION)
				{
					assert(CalculateCollapseTime(_vertices[tail->_tail], _vertices[group._newVertex]) >= _vertices[group._newVertex]._initialTime);
					assert(CalculateCollapseTime(_vertices[group._newVertex], _vertices[head->_head]) >= _vertices[group._newVertex]._initialTime);

					auto calcTime = (_vertices[tail->_tail]._initialTime + _vertices[group._newVertex]._initialTime + _vertices[head->_head]._initialTime) / Primitive(3);
					auto v0 = PositionAtTime(_vertices[tail->_tail], calcTime);
					auto v1 = PositionAtTime(_vertices[group._newVertex], calcTime);
					auto v2 = PositionAtTime(_vertices[head->_head], calcTime);
							
					auto validatedVelocity = CalculateVertexVelocity(v0, v1, v2);
					assert(AdaptiveEquivalent(validatedVelocity, _vertices[group._newVertex]._velocity, 100*GetEpsilon<Primitive>()));
				}
			#endif
		}

		// We must recalculate the velocities for vertices with zero velocity. Typically this will just
		// involve calculating the velocities for the new vertices created in this collapse operation.
		// But in some cases, velocities from new vertices calculated in motorcycle crash events can't be 
		// calculated until an edge collapse event is processed. In those cases, the velocity is 
		// temporarily zero; pending the collapse event.
		if (loop._edges.size() > 2) {
			auto prevEdge=loop._edges.end()-1;
			for (auto edge=loop._edges.begin(); edge!=loop._edges.end(); ++edge) {
				assert(prevEdge->_head == edge->_tail);
				auto& v0 = _vertices[prevEdge->_tail];
				auto& v1 = _vertices[edge->_tail];
				auto& v2 = _vertices[edge->_head];
				if (AdaptiveEquivalent(v1._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>())) {
					// We must calculate the velocity at the max initial time -- (this should always be the crash time)
					auto calcTime = std::max(std::max(v0._initialTime, v1._initialTime), v2._initialTime);
					assert(AdaptiveEquivalent(calcTime, loop._lastEventTime, GetEpsilon<Primitive>())
						|| AdaptiveEquivalent(calcTime, bestCollapseTime, GetEpsilon<Primitive>()));
					// Multiple adjacent vertices with zero velocity is a problem, because it means we can't calculate 
					// positions for them at times that agree
					assert(!AdaptiveEquivalent(v0._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()) 
						&& !AdaptiveEquivalent(v2._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
					v1._velocity = CalculateVertexVelocity(PositionAtTime(v0, calcTime), PositionAtTime(v1, calcTime), PositionAtTime(v2, calcTime));
					assert(!AdaptiveEquivalent(v1._velocity, Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
				}
				prevEdge = edge;
			}

			#if defined(_DEBUG)
				for (const auto&e:loop._edges)
					assert(!IsFrozen(_vertices[e._tail]) && !IsFrozen(_vertices[e._head]));
			#endif
		}

		loop._lastEventTime = bestCollapseTime;
	}

	T1(Primitive) static Primitive ClosestPointOnLine2D(Vector2T<Primitive> rayStart, Vector2T<Primitive> rayEnd, Vector2T<Primitive> testPt)
	{
		auto o = testPt - rayStart;
		auto l = rayEnd - rayStart;
		return Dot(o, l) / MagnitudeSquared(l);
	}

	T1(Primitive) static bool DoColinearLinesIntersect(Vector2T<Primitive> AStart, Vector2T<Primitive> AEnd, Vector2T<Primitive> BStart, Vector2T<Primitive> BEnd)
	{
		// return false if the lines share a point, but otherwise do not intersect.
		// but returns true if the lines overlap completely (even if the lines have zero length)
		auto closestBStart = ClosestPointOnLine2D(AStart, AEnd, BStart);
		auto closestBEnd = ClosestPointOnLine2D(AStart, AEnd, BEnd);
		return ((closestBStart > GetEpsilon<Primitive>()) && (closestBStart > Primitive(1)-GetEpsilon<Primitive>()))
			|| ((closestBEnd > GetEpsilon<Primitive>()) && (closestBEnd > Primitive(1)-GetEpsilon<Primitive>()))
			|| (AdaptiveEquivalent(AStart, BStart, GetEpsilon<Primitive>()) && AdaptiveEquivalent(AEnd, BEnd, GetEpsilon<Primitive>()))
			|| (AdaptiveEquivalent(AEnd, BStart, GetEpsilon<Primitive>()) && AdaptiveEquivalent(AStart, BEnd, GetEpsilon<Primitive>()))
			;
	}

	T1(Primitive) void Graph<Primitive>::WriteWavefront(StraightSkeleton<Primitive>& result, const WavefrontLoop& loop, Primitive time)
	{
		// Write the current wavefront to the destination skeleton. Each edge in 
		// _wavefrontEdges comes a segment in the output
		// However, we must check for overlapping / intersecting edges
		//	-- these happen very frequently
		// The best way to remove overlapping edges is just to go through the list of segments, 
		// and for each one look for other segments that intersect

		std::vector<WavefrontEdge> filteredSegments;
		std::stack<WavefrontEdge> segmentsToTest;

		// We need to combine overlapping points at this stage, also
		// (2 different vertices could end up at the same location at time 'time')

		for (auto i=loop._edges.begin(); i!=loop._edges.end(); ++i) {
			auto A = ClampedPositionAtTime(_vertices[i->_head], time);
			auto B = ClampedPositionAtTime(_vertices[i->_tail], time);
			auto v0 = AddSteinerVertex(result, A);
			auto v1 = AddSteinerVertex(result, B);
			if (v0 != v1)
				segmentsToTest.push(WavefrontEdge{v0, v1, i->_leftFace, i->_rightFace});

			// Also have to add the traced out path of the each vertex (but only if it doesn't already exist in the result)
			AddEdgeForVertexPath(result, loop, i->_head, v0);
			AddEdgeForVertexPath(result, loop, i->_tail, v1);
		}

		while (!segmentsToTest.empty()) {
			auto seg = segmentsToTest.top();
			segmentsToTest.pop();

			auto A = Truncate(result._steinerVertices[seg._head]);
			auto B = Truncate(result._steinerVertices[seg._tail]);
			bool filterOutSeg = false;

			// Compare against all edges already in "filteredSegments"
			for (auto i2=filteredSegments.begin(); i2!=filteredSegments.end();++i2) {

				if (i2->_head == seg._head && i2->_tail == seg._tail) {
					if (i2->_leftFace == ~0u) i2->_leftFace = seg._leftFace;
					if (i2->_rightFace == ~0u) i2->_rightFace = seg._rightFace;
					filterOutSeg = true; 
					break; // (overlap completely)
				} else if (i2->_head == seg._tail && i2->_tail == seg._head) {
					if (i2->_leftFace == ~0u) i2->_leftFace = seg._rightFace;
					if (i2->_rightFace == ~0u) i2->_rightFace = seg._leftFace;
					filterOutSeg = true; 
					break; // (overlap completely)
				}

				// If they intersect, they should be colinear, and at least one 
				// vertex in i2 should lie on i
				auto C = Truncate(result._steinerVertices[i2->_head]);
				auto D = Truncate(result._steinerVertices[i2->_tail]);
				auto closestC = ClosestPointOnLine2D(A, B, C);
				auto closestD = ClosestPointOnLine2D(A, B, D);

				bool COnLine = closestC > Primitive(0) && closestC < Primitive(1) && MagnitudeSquared(LinearInterpolate(A, B, closestC) - C) < GetEpsilon<Primitive>();
				bool DOnLine = closestD > Primitive(0) && closestD < Primitive(1) && MagnitudeSquared(LinearInterpolate(A, B, closestD) - D) < GetEpsilon<Primitive>();
				if (!COnLine && !DOnLine) { continue; }

				auto m0 = (B[1] - A[1]) / (B[0] - A[0]);
				auto m1 = (D[1] - C[1]) / (D[0] - C[0]);
				if (!AdaptiveEquivalent(m0, m1, GetEpsilon<Primitive>())) { continue; }

				if (i2->_head == seg._head) {
					if (closestD < Primitive(1)) {
						seg._head = i2->_tail;
					} else {
						i2->_head = seg._tail;
					}
				} else if (i2->_head == seg._tail) {
					if (closestD > Primitive(0)) {
						seg._tail = i2->_tail;
					} else {
						i2->_head = seg._head;
					}
				} else if (i2->_tail == seg._head) {
					if (closestC < Primitive(1)) {
						seg._head = i2->_head;
					} else {
						i2->_tail = seg._tail;
					}
				} else if (i2->_tail == seg._tail) {
					if (closestC > Primitive(0)) {
						seg._tail = i2->_head;
					} else {
						i2->_tail = seg._head;
					}
				} else {
					// The lines are colinear, and at least one point of i2 is on i
					// We must separate these 2 segments into 3 segments.
					// Replace i2 with something that is strictly within i2, and then schedule
					// the remaining split parts for intersection tests.
					WavefrontEdge newSeg;
					if (closestC < Primitive(0)) {
						if (closestD > Primitive(1)) newSeg = {seg._tail, i2->_tail};
						else { newSeg = {i2->_tail, seg._tail}; seg._tail = i2->_tail; }
						i2->_tail = seg._head;
					} else if (closestD < Primitive(0)) {
						if (closestC > Primitive(1)) newSeg = {seg._tail, i2->_head};
						else { newSeg = {i2->_head, seg._tail}; seg._tail = i2->_head; }
						i2->_head = seg._head;
					} else if (closestC < closestD) {
						if (closestD > Primitive(1)) newSeg = {seg._tail, i2->_tail};
						else { newSeg = {i2->_tail, seg._tail}; seg._tail = i2->_tail; }
						seg._tail = i2->_head;
					} else {
						if (closestC > Primitive(1)) newSeg = {seg._tail, i2->_head};
						else { newSeg = {i2->_head, seg._tail}; seg._tail = i2->_head; }
						seg._tail = i2->_tail;
					}

					assert(!DoColinearLinesIntersect(
						Truncate(result._steinerVertices[newSeg._head]),
						Truncate(result._steinerVertices[newSeg._tail]),
						Truncate(result._steinerVertices[seg._head]),
						Truncate(result._steinerVertices[seg._tail])));
					assert(!DoColinearLinesIntersect(
						Truncate(result._steinerVertices[newSeg._head]),
						Truncate(result._steinerVertices[newSeg._tail]),
						Truncate(result._steinerVertices[i2->_head]),
						Truncate(result._steinerVertices[i2->_tail])));
					assert(!DoColinearLinesIntersect(
						Truncate(result._steinerVertices[i2->_head]),
						Truncate(result._steinerVertices[i2->_tail]),
						Truncate(result._steinerVertices[seg._head]),
						Truncate(result._steinerVertices[seg._tail])));
					assert(newSeg._head != newSeg._tail);
					assert(i2->_head != i2->_tail);
					assert(seg._head != seg._tail);

					// We will continue testing "seg", and we will push "newSeg" onto the stack to
					// be tested later.
					// i2 has also been changed; it is now shorter and no longer intersects 'seg'
					segmentsToTest.push(newSeg);
				}

				// "seg" has changed, so we need to calculate the end points
				A = Truncate(result._steinerVertices[seg._head]);
				B = Truncate(result._steinerVertices[seg._tail]);
			}

			if (!filterOutSeg)
				filteredSegments.push_back(seg);
		}

		// Add all of the segments in "filteredSegments" to the skeleton
		for (const auto&seg:filteredSegments) {
			assert(seg._head != seg._tail);
			AddEdge(result, seg._head, seg._tail, seg._leftFace, seg._rightFace, StraightSkeleton<Primitive>::EdgeType::Wavefront);
		}
	}

	T1(Primitive) void Graph<Primitive>::AddEdgeForVertexPath(StraightSkeleton<Primitive>& dst, const WavefrontLoop& loop, unsigned v, unsigned finalVertId)
	{
		const auto& vert = _vertices[v];
		unsigned originVertex = vert._skeletonVertexId;
		if (originVertex == ~0u)
			originVertex = AddSteinerVertex(dst, Expand(vert._position, vert._initialTime));

		auto inAndOut = FindInAndOut(MakeIteratorRange(loop._edges).template Cast<const WavefrontEdge*>(), v);
		unsigned leftFace = ~0u, rightFace = ~0u;
		if (inAndOut.first) leftFace = inAndOut.first->_rightFace;
		if (inAndOut.second) rightFace = inAndOut.second->_rightFace;

		AddEdge(dst, finalVertId, originVertex, leftFace, rightFace, StraightSkeleton<Primitive>::EdgeType::VertexPath);

		if ((vert._skeletonVertexId != ~0u) && (vert._skeletonVertexId&BoundaryVertexFlag)) {
			auto q = vert._skeletonVertexId&(~BoundaryVertexFlag);
			AddEdge(dst, finalVertId, originVertex, unsigned((q+_boundaryPointCount-1) % _boundaryPointCount), q, StraightSkeleton<Primitive>::EdgeType::VertexPath);
		}
	}

	T1(Primitive) StraightSkeleton<Primitive> CalculateStraightSkeleton(IteratorRange<const Vector2T<Primitive>*> vertices, Primitive maxInset)
	{
		auto graph = BuildGraphFromVertexLoop(vertices);
		return graph.CalculateSkeleton(maxInset);
	}

	std::vector<std::vector<unsigned>> AsVertexLoopsOrdered(IteratorRange<const std::pair<unsigned, unsigned>*> segments)
	{
		// From a line segment soup, generate vertex loops. This requires searching
		// for segments that join end-to-end, and following them around until we
		// make a loop.
		// Let's assume for the moment there are no 3-or-more way junctions (this would
		// require using some extra math to determine which is the correct path)
		std::vector<std::pair<unsigned, unsigned>> pool(segments.begin(), segments.end());
		std::vector<std::vector<unsigned>> result;
		while (!pool.empty()) {
			std::vector<unsigned> workingLoop;
			{
				auto i = pool.end()-1;
				workingLoop.push_back(i->first);
				workingLoop.push_back(i->second);
				pool.erase(i);
			}
			for (;;) {
				assert(!pool.empty());	// if we hit this, we have open segments
				auto searching = *(workingLoop.end()-1);
				auto hit = pool.end(); 
				for (auto i=pool.begin(); i!=pool.end(); ++i) {
					if (i->first == searching /*|| i->second == searching*/) {
						assert(hit == pool.end());
						hit = i;
					}
				}
				assert(hit != pool.end());
				auto newVert = hit->second; // (hit->first == searching) ? hit->second : hit->first;
				pool.erase(hit);
				if (std::find(workingLoop.begin(), workingLoop.end(), newVert) != workingLoop.end())
					break;	// closed the loop
				workingLoop.push_back(newVert);
			}
			result.push_back(std::move(workingLoop));
		}

		return result;
	}

	T1(Primitive) std::vector<std::vector<unsigned>> StraightSkeleton<Primitive>::WavefrontAsVertexLoops()
	{
		std::vector<std::pair<unsigned, unsigned>> segmentSoup;
		for (auto&f:_faces)
			for (auto&e:f._edges)
				if (e._type == EdgeType::Wavefront)
					segmentSoup.push_back({e._head, e._tail});
		// We shouldn't need the edges in _unplacedEdges, so long as each edge has been correctly
		// assigned to it's source face
		return AsVertexLoopsOrdered(MakeIteratorRange(segmentSoup));
	}

	template StraightSkeleton<float> CalculateStraightSkeleton<float>(IteratorRange<const Vector2T<float>*> vertices, float maxInset);
	template StraightSkeleton<double> CalculateStraightSkeleton<double>(IteratorRange<const Vector2T<double>*> vertices, double maxInset);
	template class StraightSkeleton<float>;
	template class StraightSkeleton<double>;

}
