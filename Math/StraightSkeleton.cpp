// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StraightSkeleton.h"
#include "StraightSkeleton_Internal.h"
#include "../Math/Geometry.h"
#include <stack>
#include <cmath>
#include <random>	// (for validation testing)
#include <optional>

#if defined(_DEBUG)
	// #define EXTRA_VALIDATION
#endif

#pragma warning(disable:4505) // 'SceneEngine::StraightSkeleton::ReplaceVertex': unreferenced local function has been removed


namespace XLEMath
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	using VertexId = unsigned;
	using FaceId = unsigned;
	using EdgeId = size_t;

	T1(Primitive) using PointAndTime = Vector3T<Primitive>;
	T1(Primitive) struct Vertex
	{
		PointAndTime<Primitive>	_anchor0;
		PointAndTime<Primitive>	_anchor1;

		Primitive InitialTime() const { return _anchor0[2]; }

		Vector2T<Primitive> PositionAtTime(Primitive time) const
		{
			if (_anchor1[2] == _anchor0[2]) return Truncate(_anchor0);		// bitwise comparison intended
			float w1 = (time - _anchor0[2]) / (_anchor1[2] - _anchor0[2]);
			float w0 = 1.0f - w1;
			return w0 * Truncate(_anchor0) + w1 * Truncate(_anchor1);
		}

		Vector2T<Primitive> Velocity() const
		{
			if (_anchor1[2] == _anchor0[2]) return Zero<Vector2T<Primitive>>();		// bitwise comparison intended
			return (Truncate(_anchor1) - Truncate(_anchor0)) / (_anchor1[2] - _anchor0[2]);
		}
	};

	T1(Primitive) using VertexSet = IteratorRange<const Vertex<Primitive>*>;

	struct WavefrontEdge
	{
		VertexId _head, _tail;
	};

	struct MotorcycleSegment
	{
		VertexId _head;
	};

	enum class EventType { Collapse, MotorcycleCrash, None };
	T1(Primitive) struct Event
	{
		Vector2T<Primitive> _eventPt = Zero<Vector2T<Primitive>>();
		Primitive _eventTime = Primitive(-1);
		EventType _type = EventType::None;

		// Collapse edge or collision edge
		VertexId _edgeHead = ~VertexId(0);
		VertexId _edgeTail = ~VertexId(0);

		// Motorcycle crash
		VertexId _motor = ~VertexId(0);

		static Event Collapse(Vector2T<Primitive> eventPt, Primitive eventTime, VertexId head, VertexId tail)
		{
			Event result;
			result._eventPt = eventPt;
			result._eventTime = eventTime;
			result._type = EventType::Collapse;
			result._edgeHead = head;
			result._edgeTail = tail;
			result._motor = ~VertexId(0);
			return result;
		}

		static Event MotorcycleCrash(Vector2T<Primitive> eventPt, Primitive eventTime, VertexId motor, VertexId collisionHead, VertexId collisionTail)
		{
			Event result;
			result._eventPt = eventPt;
			result._eventTime = eventTime;
			result._type = EventType::MotorcycleCrash;
			result._edgeHead = collisionHead;
			result._edgeTail = collisionTail;
			result._motor = motor;
			return result;
		}
	};

#if 0
	T1(Primitive) static Primitive CalculateCollapseTime(const Vertex<Primitive>& v0, const Vertex<Primitive>& v1)
	{		
		// At some point the trajectories of v0 & v1 may intersect
		// We need to pick out a specific time on the timeline, and find both v0 and v1 at that
		// time. 
		auto calcTime = std::max(v0._anchorTime, v1._anchorTime);
		auto p0 = v0.PositionAtTime(calcTime);
		auto p1 = v1.PositionAtTime(calcTime);
		return calcTime + CalculateCollapseTime(p0, v0._velocity, p1, v1._velocity);
	}
#endif

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

	T1(Primitive) const Vertex<Primitive>& GetVertex(VertexSet<Primitive> vSet, VertexId v)
	{
		return vSet[v];
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
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
		auto p0 = edgeHead.PositionAtTime(calcTime);
		auto p1 = edgeTail.PositionAtTime(calcTime);
		auto v0 = edgeHead._velocity;
		auto v1 = edgeTail._velocity;

		auto p2 = motorcycle.PositionAtTime(calcTime);
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
		if (Equivalent(a, Primitive(0), GetEpsilon<Primitive>())) return false;
			
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
			auto P0 = edgeHead.PositionAtTime(t);
			auto P1 = edgeTail.PositionAtTime(t);
			auto P2 = motorcycle.PositionAtTime(t);
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
		if (Equivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
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

		if (!InvertInplaceSafe(M))
			return false;

		crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return true;
	}
#endif

	T1(Primitive) static std::optional<PointAndTime<Primitive>> FindCrashEvent(
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
			//if (Equivalent(mag, Primitive(0), GetEpsilon<Primitive>()))
				//return {};

			auto Nx = Primitive((As[c][1] - Bs[c][1]) * VelocityVectorScale<Primitive>::Value / mag);
			auto Ny = Primitive((Bs[c][0] - As[c][0]) * VelocityVectorScale<Primitive>::Value / mag);
			M(c, 0) = Nx;
			M(c, 1) = Ny;
			M(c, 2) = -Nx*Nx-Ny*Ny;
			res[c]  = As[c][0] * Nx + As[c][1] * Ny;
		}
		if (!InvertInplaceSafe(M))
			return {};

		PointAndTime<Primitive> crash = M * res;
		assert(IsFiniteNumber(crash[0]) && IsFiniteNumber(crash[1]) && IsFiniteNumber(crash[2]));
		return crash;
	}

	T1(Primitive) struct ProtoCrashEvent
	{
		enum class Type { Middle, Head, Tail };
		Type _type;
		PointAndTime<Primitive> _pointAndTime;
	};

#if 0
	T1(Primitive) static std::optional<ProtoCrashEvent<Primitive>> BuildCrashEvent_SimultaneousV(
		VertexSet<Primitive> vertices,
		VertexId edgeHeadId, VertexId edgeTailId,
		VertexId motorcycleId)
	{
		auto edgeHead = GetVertex(vertices, edgeHeadId);
		auto edgeTail = GetVertex(vertices, edgeTailId);
		auto motorcycle = GetVertex(vertices, motorcycleId);

		const auto calcTime = std::max(std::max(edgeHead.InitialTime(), edgeTail.InitialTime()), motorcycle.InitialTime());
		auto p0 = edgeHead.PositionAtTime(calcTime);
		auto p1 = edgeTail.PositionAtTime(calcTime);
		auto p2 = motorcycle.PositionAtTime(calcTime);
		auto res = FindCrashEvent<Primitive>(p0-p2, p1-p2, motorcycle._velocity);
		if (!res || res.value()[2] < Primitive(0)) return {};

		auto pointAndTime = res.value();
		pointAndTime += Expand(p2, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = edgeHead.PositionAtTime(pointAndTime[2]);
		p1 = edgeTail.PositionAtTime(pointAndTime[2]);
		p2 = Truncate(pointAndTime);

		auto edgeMagSq = Magnitude(p1-p0);
		if (edgeMagSq < GetEpsilon<Primitive>() * GetEpsilon<Primitive>())		// edge is collapsed at this point
			return {};

		// There might be a problem here if the edge has collapsed before the collision time-- we can still find a motor cycle collision
		// briefly after the collapse

		auto d0 = Dot(p1-p0, p2-p0);			// distance from p0 (projected onto edge) = d0 / Magnitude(p1-p0)
		auto d1 = Dot(p0-p1, p2-p1);			// distance from p1 (projected onto edge) = d1 / Magnitude(p1-p0)
		float e = GetEpsilon<Primitive>() * std::sqrt(edgeMagSq);
		if (d0 < -e || d1 < -e)
			return {};

		if (d0 < e) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		} else if (d1 < e) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		} else {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
		}
	}
#endif

	T1(Primitive) static std::optional<ProtoCrashEvent<Primitive>> BuildCrashEvent_Simultaneous(
		VertexSet<Primitive> vertices,
		VertexId edgeHeadId, VertexId edgeTailId,
		VertexId motorcyclePrevId, VertexId motorcycleId, VertexId motorcycleNextId)
	{
		auto edgeHead = GetVertex(vertices, edgeHeadId);
		auto edgeTail = GetVertex(vertices, edgeTailId);
		auto motorcyclePrev = GetVertex(vertices, motorcyclePrevId);
		auto motorcycle = GetVertex(vertices, motorcycleId);
		auto motorcycleNext = GetVertex(vertices, motorcycleNextId);

		const auto calcTime = std::max(std::max(std::max(std::max(edgeHead.InitialTime(), edgeTail.InitialTime()), motorcyclePrev.InitialTime()), motorcycle.InitialTime()), motorcycleNext.InitialTime());
		auto p0 = edgeHead.PositionAtTime(calcTime);
		auto p1 = edgeTail.PositionAtTime(calcTime);

		auto m0 = motorcyclePrev.PositionAtTime(calcTime);
		auto m1 = motorcycle.PositionAtTime(calcTime);
		auto m2 = motorcycleNext.PositionAtTime(calcTime);

		auto res = FindCrashEvent<Primitive>(p0-m1, p1-m1, m0-m1, m2-m1);
		if (!res || res.value()[2] < GetEpsilon<Primitive>())
			return {};

		auto pointAndTime = res.value();
		pointAndTime += Expand(m1, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = edgeHead.PositionAtTime(pointAndTime[2]);
		p1 = edgeTail.PositionAtTime(pointAndTime[2]);
		auto p2 = Truncate(pointAndTime);

		auto edgeMagSq = Magnitude(p1-p0);
		if (edgeMagSq < GetEpsilon<Primitive>() * GetEpsilon<Primitive>())		// edge is collapsed at this point
			return {};

		// There might be a problem here if the edge has collapsed before the collision time-- we can still find a motor cycle collision
		// briefly after the collapse

		auto d0 = Dot(p1-p0, p2-p0);			// distance from p0 (projected onto edge) = d0 / Magnitude(p1-p0)
		auto d1 = Dot(p0-p1, p2-p1);			// distance from p1 (projected onto edge) = d1 / Magnitude(p1-p0)
		float e = GetEpsilon<Primitive>() * std::sqrt(edgeMagSq);
		if (d0 < -e || d1 < -e)			// we need a little bit of tolerance here; because we can miss collisions if we test against zero (even though missing requires us to actually miss twice -- once on either edge to connecting to the vertex we're hitting)
			return {};

		if (d0 < e) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		} else if (d1 < e) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		} else {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
		}
	}
    
	T1(Primitive) static std::optional<Event<Primitive>> CalculateCrashEvent(
		VertexId motor, 
		IteratorRange<const WavefrontEdge*> segments,
		VertexSet<Primitive> vertices)
	{
		std::optional<Event<Primitive>> bestCollisionEvent;
		auto bestEventTime = std::numeric_limits<Primitive>::max();

		auto inAndOut = FindInAndOut(segments, motor);
		auto motorPrev = inAndOut.first->_tail;
		auto motorNext = inAndOut.second->_head;

		// Look for an intersection with segments
		for (const auto&e:segments) {
			if (e._head == motor || e._tail == motor) continue;	// (can't crash if part of the same segment)
			if (e._head == motorPrev || e._tail == motorNext) continue; // don't crash into neighbours, either -- this should be handled as a collapse

			auto res = BuildCrashEvent_Simultaneous(vertices, e._head, e._tail, motorPrev, motor, motorNext);

			/*Vector3T<Primitive> compare2;
			auto resC = BuildCrashEvent_SimultaneousV(compare2, head, tail, v);
			(void)compare2, resC;*/

			/*Vector3T<Primitive> compare;
			auto resA = BuildCrashEvent_OldMethod(compare, head, tail, v);
			(void)resA, compare;*/

			if (res.has_value() && res.value()._pointAndTime[2] < bestEventTime) {
				auto protoCrash = res.value();
				if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Head) {
					bestCollisionEvent = Event<Primitive>::MotorcycleCrash(Truncate(protoCrash._pointAndTime), protoCrash._pointAndTime[2], motor, e._head, e._head);
				} else if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Tail) {
					bestCollisionEvent = Event<Primitive>::MotorcycleCrash(Truncate(protoCrash._pointAndTime), protoCrash._pointAndTime[2], motor, e._tail, e._tail);
				} else {
					bestCollisionEvent = Event<Primitive>::MotorcycleCrash(Truncate(protoCrash._pointAndTime), protoCrash._pointAndTime[2], motor, e._head, e._tail);
				}
				bestEventTime = protoCrash._pointAndTime[2];
			}
		}

		return bestCollisionEvent;
	}

	T1(Primitive) static PointAndTime<Primitive> OffsetTime(PointAndTime<Primitive> input, float offsetTime)
	{
		return { input[0], input[1], input[2] + offsetTime };
	}

	T1(Primitive) static std::optional<PointAndTime<Primitive>> CalculateCollapseEvent(
		unsigned vm1, unsigned v0, unsigned v1, unsigned v2, 
		VertexSet<Primitive> vertices)
	{
		auto& legacyVM1 = GetVertex(vertices, vm1);
		auto& legacyV0 = GetVertex(vertices, v0);
		auto& legacyV1 = GetVertex(vertices, v1);
		auto& legacyV2 = GetVertex(vertices, v2);
		const auto calcTime = std::max(std::max(std::max(legacyVM1.InitialTime(), legacyV0.InitialTime()), legacyV1.InitialTime()), legacyV2.InitialTime());
		auto res = CalculateEdgeCollapse_Offset_ColinearTest(
			legacyVM1.PositionAtTime(calcTime),
			legacyV0.PositionAtTime(calcTime),
			legacyV1.PositionAtTime(calcTime),
			legacyV2.PositionAtTime(calcTime)); 
		if (!res) return {};
		if (res.value()[2] < 0) return {};		// this happens when an edge is expanding, not collapsing
		return OffsetTime(res.value(), calcTime);
	}

	T1(Primitive) static PointAndTime<Primitive> CalculateAnchor1(VertexId vm2i, VertexId vm1i, VertexId v0i, VertexId v1i, VertexId v2i, VertexSet<Primitive> vSet, float calcTime)
	{
		auto& vm2 = vSet[vm2i];
		auto& vm1 = vSet[vm1i];
		auto& v0 = vSet[v0i];
		auto& v1 = vSet[v1i];
		auto& v2 = vSet[v2i];

		auto collapse0 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vm2.PositionAtTime(calcTime), vm1.PositionAtTime(calcTime), v0.PositionAtTime(calcTime), v1.PositionAtTime(calcTime), v0.PositionAtTime(calcTime));
		auto collapse1 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vm1.PositionAtTime(calcTime), v0.PositionAtTime(calcTime), v1.PositionAtTime(calcTime), v2.PositionAtTime(calcTime), v0.PositionAtTime(calcTime));

		/*if (collapse1 && std::abs(collapse1.value()[2]) < 1e-8)
			collapse1 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vm1.PositionAtTime(calcTime), v0.PositionAtTime(calcTime), v1.PositionAtTime(calcTime), v2.PositionAtTime(calcTime), v0.PositionAtTime(calcTime));
		assert(!collapse0 || std::abs(collapse0.value()[2]) > 1e-8);
		assert(!collapse1 || std::abs(collapse1.value()[2]) > 1e-8);*/

		if (collapse0.has_value() && collapse1.has_value()) {
			// the collapses should both be in the same direction, but let's choose the sooner one
			if (collapse0.value()[2] > 0 && collapse0.value()[2] < collapse1.value()[2]) {
				return OffsetTime(collapse0.value(), calcTime);
			} else {
				return OffsetTime(collapse1.value(), calcTime);
			}
		} else if (collapse0.has_value()) {
			return OffsetTime(collapse0.value(), calcTime);
		} else if (collapse1.has_value()) {
			return OffsetTime(collapse1.value(), calcTime);
		} else {
			// Some edges won't collapse (due to parallel edges, etc)
			auto velocity = CalculateVertexVelocity_LineIntersection(vm1.PositionAtTime(calcTime), v0.PositionAtTime(calcTime), v1.PositionAtTime(calcTime), Primitive(1));
			if (velocity)
				return v0._anchor0 + PointAndTime<Primitive>{velocity.value(), Primitive(1)};
			return v0._anchor0;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) class Graph
	{
	public:
		std::vector<Vertex<Primitive>> _vertices;

		struct WavefrontLoop
		{
			std::vector<WavefrontEdge> _edges;
			std::vector<MotorcycleSegment> _motorcycleSegments;
			Primitive _lastEventBatchEarliest = std::numeric_limits<Primitive>::max();
			Primitive _lastEventBatchLatest = -std::numeric_limits<Primitive>::max();
			std::vector<Event<Primitive>> _pendingEvents;
		};
		std::vector<WavefrontLoop> _loops;
		size_t _boundaryPointCount;

		struct VertexPathEdge
		{
			VertexId _vertex;
			PointAndTime<Primitive> _beginPt, _endPt;
		};
		std::vector<VertexPathEdge> _vertexPathEdges;

		StraightSkeleton<Primitive> CalculateSkeleton(Primitive maxTime);

        #if defined(_DEBUG)
            std::vector<Vector2T<Primitive>> _sourceLoop;
        #endif

	private:
		void WriteWavefront(StraightSkeleton<Primitive>& dest, const WavefrontLoop& loop, Primitive time);
		void WriteVertexPaths(StraightSkeleton<Primitive>& result, const WavefrontLoop& loop, Primitive time);
		void ValidateState();

		void ProcessEvents(typename std::vector<WavefrontLoop>::iterator loop);

		std::vector<WavefrontLoop> ProcessMotorcycleEvents(WavefrontLoop& loop);
		void ProcessCollapseEvents(WavefrontLoop& loop);

		void FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop& loop);
		void FindMotorcycleCrashes(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop& loop);

		void AddVertexPathEdge(unsigned vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end);
		void UpdateLoop(WavefrontLoop& loop);
	};

	T1(Primitive) void Graph<Primitive>::FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop& loop)
	{
		for (size_t e=0; e<loop._edges.size(); ++e) {
			const auto& seg0 = loop._edges[(e+loop._edges.size()-1)%loop._edges.size()];
			const auto& seg1 = loop._edges[e];
			const auto& seg2 = loop._edges[(e+1)%loop._edges.size()];
			assert(seg0._head == seg1._tail && seg1._head == seg2._tail);	// ensure segments are correctly ordered

			auto collapse = CalculateCollapseEvent<Primitive>(seg0._tail, seg1._tail, seg1._head, seg2._head, _vertices);
			if (!collapse) continue;

			auto collapseTime = collapse.value()[2];
			// if (collapseTime < loop._lastEventBatchBegin) continue;
			assert(collapseTime >= loop._lastEventBatchEarliest || loop._lastEventBatchEarliest > loop._lastEventBatchLatest);
			if (collapseTime < (earliestTime + GetTimeEpsilon<Primitive>())) {
				events.push_back(Event<Primitive>::Collapse(Truncate(collapse.value()), collapse.value()[2], seg1._head, seg1._tail));
				earliestTime = std::min(collapseTime, earliestTime);
			}
		}

		// Always ensure that every entry in "bestCollapse" is within
		// "GetEpsilon<Primitive>()" of bestCollapseTime -- this can become untrue if there
		// are chains of events with very small gaps in between them
		events.erase(
			std::remove_if(
				events.begin(), events.end(),
				[earliestTime](const auto& e) { return !(e._eventTime < earliestTime + GetTimeEpsilon<Primitive>()); }), 
			events.end());
	}

	T1(Primitive) void Graph<Primitive>::FindMotorcycleCrashes(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop& loop)
	{
		for (const auto& m:loop._motorcycleSegments) {
			#if defined(_DEBUG)
				auto head = GetVertex<Primitive>(_vertices, m._head);
				assert(!Equivalent(head.Velocity(), Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
			#endif

			auto crashEventOpt = CalculateCrashEvent<Primitive>(m._head, MakeIteratorRange(loop._edges), MakeIteratorRange(_vertices));
			if (!crashEventOpt) continue;
			auto crashEvent = crashEventOpt.value();
			// if (crashEvent._eventTime < loop._lastEventBatchBegin) continue;
			assert(crashEvent._eventTime >= loop._lastEventBatchEarliest || loop._lastEventBatchEarliest > loop._lastEventBatchLatest);

			if (crashEvent._eventTime < (earliestTime + GetTimeEpsilon<Primitive>())) {
				events.push_back(crashEvent);
				earliestTime = std::min(crashEvent._eventTime, earliestTime);
			}
		}

		events.erase(
			std::remove_if(
				events.begin(), events.end(),
				[earliestTime](const auto& e) { return !(e._eventTime < earliestTime + GetTimeEpsilon<Primitive>()); }), 
			events.end());
	}

	T1(Primitive) Graph<Primitive> BuildGraphFromVertexLoop(IteratorRange<const Vector2T<Primitive>*> vertices)
	{
		assert(vertices.size() >= 2);

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
			auto vm2 = (v+vertices.size()-2)%vertices.size();
			auto vm1 = (v+vertices.size()-1)%vertices.size();
			auto v0 = v;
			auto v1 = (v+1)%vertices.size();
			auto v2 = (v+2)%vertices.size();
			loop._edges.emplace_back(WavefrontEdge{unsigned(v1), unsigned(v0)});

			// We must calculate the velocity for each vertex, based on which segments it belongs to...
			auto collapse0 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vertices[vm2], vertices[vm1], vertices[v0], vertices[v1], vertices[v0]);
			auto collapse1 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vertices[vm1], vertices[v0], vertices[v1], vertices[v2], vertices[v0]);
			if (collapse0.has_value() && collapse1.has_value()) {
				// the collapses should both be in the same direction, but let's choose the sooner one
				if (collapse0.value()[2] > 0 && collapse0.value()[2] < collapse1.value()[2]) {
					result._vertices.push_back(Vertex<Primitive>{PointAndTime<Primitive>{vertices[v], Primitive(0)}, collapse0.value()});
				} else {
					result._vertices.push_back(Vertex<Primitive>{PointAndTime<Primitive>{vertices[v], Primitive(0)}, collapse1.value()});
				}
			} else if (collapse0.has_value()) {
				result._vertices.push_back(Vertex<Primitive>{PointAndTime<Primitive>{vertices[v], Primitive(0)}, collapse0.value()});
			} else if (collapse1.has_value()) {
				result._vertices.push_back(Vertex<Primitive>{PointAndTime<Primitive>{vertices[v], Primitive(0)}, collapse1.value()});
			} else {
				// Some edges won't collapse (due to parallel edges, etc)
				auto anchor0 = PointAndTime<Primitive>{vertices[v], Primitive(0)};
				auto anchor1 = anchor0;
				auto velocity = CalculateVertexVelocity_LineIntersection(vertices[vm1], vertices[v0], vertices[v1], Primitive(1));
				if (velocity)
					anchor1 = PointAndTime<Primitive>{vertices[v] + velocity.value(), Primitive(1)};
				result._vertices.push_back(Vertex<Primitive>{anchor0, anchor1});
			}
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
			if (CalculateWindingType(vertices[v0], vertices[v1], vertices[v2], GetEpsilon<Primitive>()) != WindingType::Left
				&& result._vertices[v]._anchor0 != result._vertices[v]._anchor1)
				loop._motorcycleSegments.emplace_back(MotorcycleSegment{VertexId(v)});
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
							assert(AdaptiveEquivalent(_vertices[v]._velocity, expectedVelocity, GetEpsilon<Primitive>()));
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

	T1(Primitive) void Graph<Primitive>::UpdateLoop(WavefrontLoop& loop)
	{
		// The "velocity" value for newly created vertices will not have been updated yet; we needed
		// to wait until all crash events were processed before we did. But 
		if (loop._edges.size() <= 2)
			return;

		auto prevPrevEdge = loop._edges.end()-2;
		auto prevEdge = loop._edges.end()-1;
		for (auto edge=loop._edges.begin(); edge!=loop._edges.end(); ++edge) {
			assert(edge->_head != edge->_tail);
			assert(prevEdge->_head == edge->_tail);
			auto& v0 = _vertices[edge->_tail];
			if (v0._anchor0 == v0._anchor1) {
				auto next = edge+1;
				if (next == loop._edges.end()) next = loop._edges.begin();
				// we must calculate the velocity at the max initial time -- (this should always be the crash time)
				auto calcTime = v0.InitialTime();
				// assert(calcTime >= loop._lastEventBatchLatest && calcTime <= loop._lastEventBatchEarliest);
				v0._anchor1 = CalculateAnchor1<Primitive>(
					prevPrevEdge->_tail, prevEdge->_tail, edge->_tail, edge->_head, next->_head, 
					_vertices, calcTime);

				bool hasMotorCycle = std::find_if(loop._motorcycleSegments.begin(), loop._motorcycleSegments.end(),
					[v=edge->_tail](const auto&c) { return c._head == v; }) != loop._motorcycleSegments.end();
				if (!hasMotorCycle && v0._anchor0 != v0._anchor1) {
					auto v0 = GetVertex<Primitive>(_vertices, prevEdge->_tail).PositionAtTime(calcTime);
					auto v1 = GetVertex<Primitive>(_vertices, edge->_tail).PositionAtTime(calcTime);
					auto v2 = GetVertex<Primitive>(_vertices, edge->_head).PositionAtTime(calcTime);
					if (CalculateWindingType(v0, v1, v2, GetEpsilon<Primitive>()) != WindingType::Left)
						loop._motorcycleSegments.emplace_back(MotorcycleSegment{edge->_tail});
				}
			}

			prevPrevEdge = prevEdge;
			prevEdge = edge;
		}
	}

	static bool ContainsVertex(IteratorRange<const WavefrontEdge*> edges, unsigned v)
	{
		auto headSideI = std::find_if(edges.begin(), edges.end(),
			[v](const auto& e) { return e._head == v || e._tail == v; });
		return headSideI != edges.end();
	}

	T1(Primitive) static void AddEvent(typename Graph<Primitive>::WavefrontLoop& loop, const Event<Primitive>& evnt)
	{
		if (evnt._type != EventType::MotorcycleCrash)
			assert(evnt._edgeHead != evnt._edgeTail);
		if (evnt._edgeHead != evnt._edgeTail) {
			auto q = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[evnt](const auto& e){ return e._head == evnt._edgeHead && e._tail == evnt._edgeTail; });
			assert(q != loop._edges.end());
		}

		/*if (evnt._type == EventType::MotorcycleCrash) {
			auto q0 = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[evnt](const auto& e) { return e._head == evnt._edgeTail && e._tail == evnt._motor; });
			if (q0 != loop._edges.end()) {
				auto adjustedEvnt = evnt;
				adjustedEvnt._type = EventType::Collapse;
				adjustedEvnt._edgeHead = evnt._edgeTail;
				adjustedEvnt._edgeTail = evnt._motor;
				loop._pendingEvents.push_back(adjustedEvnt);
				return;
			}

			auto q1 = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[evnt](const auto& e) { return e._head == evnt._motor && e._tail == evnt._edgeHead; });
			if (q1 != loop._edges.end()) {
				auto adjustedEvnt = evnt;
				adjustedEvnt._type = EventType::Collapse;
				adjustedEvnt._edgeHead = evnt._motor;
				adjustedEvnt._edgeTail = evnt._edgeHead;
				loop._pendingEvents.push_back(adjustedEvnt);
				return;
			}
		}*/

		loop._pendingEvents.push_back(evnt);
	}

	T1(Primitive) auto Graph<Primitive>::ProcessMotorcycleEvents(WavefrontLoop& initialLoop) -> std::vector<WavefrontLoop>
	{
		std::vector<WavefrontLoop> workingLoops;
		workingLoops.emplace_back(std::move(initialLoop));

		//
		//		_edgeHead <---------------------------------------------------- _edgeTail
		//						(hout)												 (tin)
		//											   _motor
		//												/\
		//				(headSide)					   /  \						(tailSide)
		//											  /    \
		//											 /      \
		//									    (hin)		(tout)
		//
		//			2 vertices generated, 1 loop becomes 2
		//			hin -> headSideNew -> hout
		//			tin -> tailSideNew -> tout
		//

		for (auto loop=workingLoops.begin(); loop!=workingLoops.end(); ++loop) {
			// Keep processing events until either there are no events, or the next events are all collapse events
			if (loop->_pendingEvents.empty() || loop->_pendingEvents.begin()->_type != EventType::MotorcycleCrash) continue;

			auto crashEvent = *loop->_pendingEvents.begin();
			loop->_pendingEvents.erase(loop->_pendingEvents.begin());

			auto motorIn = std::find_if(loop->_edges.begin(), loop->_edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge& e) { return e._head == motorHead; });
			auto motorOut = std::find_if(loop->_edges.begin(), loop->_edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge& e) { return e._tail == motorHead; });

			if (crashEvent._edgeHead == crashEvent._edgeTail) {
				// Sometimes crash events are converted into what should really be a collapse event. In these cases,
				// there should also be a collapse event queued
				if (motorIn->_tail == crashEvent._edgeHead || motorOut->_head == crashEvent._edgeHead)
					continue;
			}

			// The motor can collapse to become a vertex of the collision edge during earlier steps.
			if (crashEvent._motor == crashEvent._edgeHead && crashEvent._motor == crashEvent._edgeTail) continue;
			assert(crashEvent._motor != crashEvent._edgeHead && crashEvent._motor != crashEvent._edgeTail);
			// if (motorIn->_tail == crashEvent._edgeHead) continue;
			// if (motorOut->_head == crashEvent._edgeTail) continue;

			// Since we're removing "motor.head" from the simulation, we have to add a skeleton edge 
			// for vertex path along the motor cycle path
			auto crashPtAndTime = PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime};
			AddVertexPathEdge(crashEvent._motor, GetVertex<Primitive>(_vertices, crashEvent._motor)._anchor0, crashPtAndTime);

			// We need to build 2 new WavefrontLoops -- one for the "tout" side and one for the "tin" side
			// In some cases, one side or the other than can be completely collapsed. But we're still going to
			// create it.
			WavefrontLoop tailSide, headSide;
			tailSide._lastEventBatchEarliest = std::min(crashEvent._eventTime, loop->_lastEventBatchEarliest);
			tailSide._lastEventBatchLatest = crashEvent._eventTime;
			headSide._lastEventBatchEarliest = std::min(crashEvent._eventTime, loop->_lastEventBatchEarliest);
			headSide._lastEventBatchLatest = crashEvent._eventTime;

			//////////////////////////////////////////////////////////////////////
					//   T A I L   S I D E
			// Start at motor._head, and work around in order until we hit the crash segment.
			auto tout = std::find_if(loop->_edges.begin(), loop->_edges.end(),
				[motorHead=crashEvent._motor](const WavefrontEdge& test) { return test._tail == motorHead; });
			assert(tout != loop->_edges.end());
			auto tin = tout;

			if (tout->_head != crashEvent._edgeTail) {
				++tin;
				if (tin == loop->_edges.end()) tin = loop->_edges.begin();
				while (tin->_head!=crashEvent._edgeTail) {
					tailSide._edges.push_back(*tin);
					++tin;
					if (tin == loop->_edges.end()) tin = loop->_edges.begin();
				}

				if (crashEvent._edgeHead == crashEvent._edgeTail) {
					tin = (tin == loop->_edges.begin()) ? (loop->_edges.end()-1) : (tin-1);
				} else
					tailSide._edges.push_back(*tin);
			} else if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(tout->_head != crashEvent._edgeHead);
			}

			auto tailSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashPtAndTime, crashPtAndTime});
			tailSide._edges.push_back({tailSideReplacement, tin->_head});
			tailSide._edges.push_back({tout->_head, tailSideReplacement});
			
			//////////////////////////////////////////////////////////////////////
					//   H E A D   S I D E
			// Start at crashSegment._head, and work around in order until we hit the motor vertex
			auto hout = std::find_if(loop->_edges.begin(), loop->_edges.end(),
				[crashEvent](const WavefrontEdge& test) { return test._tail == crashEvent._edgeHead; });
			assert(hout != loop->_edges.end());

			if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(hout->_head != crashEvent._motor);		// this causes all manner of chaos, but should only happen if this event should really be a collapse
				++hout;
				if (hout == loop->_edges.end()) hout = loop->_edges.begin();
			}
			auto hin = hout;
			while (hin->_head!=crashEvent._motor) {
				headSide._edges.push_back(*hin);
				++hin;
				if (hin == loop->_edges.end()) hin = loop->_edges.begin();
			}

			auto headSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashPtAndTime, crashPtAndTime});
			headSide._edges.push_back({headSideReplacement, hin->_tail});
			headSide._edges.push_back({hout->_tail, headSideReplacement});

			//////////////////////////////////////////////////////////////////////
			// Move the motorcycles from "loop" to "inSide" or "outSide" depending on which loop 
			// they are now a part of.
			for (const auto&m:loop->_motorcycleSegments) {
				if (m._head == crashEvent._motor) continue;		// (skip this one, it's just been processed)
				if (crashEvent._edgeHead == crashEvent._edgeTail && m._head == crashEvent._edgeHead) continue;
				if (ContainsVertex(headSide._edges, m._head)) {
					headSide._motorcycleSegments.push_back(m);
				} else {
					assert(ContainsVertex(tailSide._edges, m._head));
					tailSide._motorcycleSegments.push_back(m);
				}
			}

			if (crashEvent._edgeTail == crashEvent._edgeHead) {
				// This vertex got removed from the simulation, and we have to explicitly add a final vertex path edge for it
				AddVertexPathEdge(crashEvent._edgeHead, GetVertex<Primitive>(_vertices, crashEvent._edgeHead)._anchor0, crashPtAndTime);
			}

			// We may have to rename the crash segments for any future crashes. We remove 1 vertex
			// from the system every time we process a motorcycle crash. So, if one of the upcoming
			// crash events involves this vertex, we have rename it to either the new vertex on the
			// inSide, or on the tailSide
			unsigned crashSegmentTail = crashEvent._edgeTail, crashSegmentHead = crashEvent._edgeHead;
			for (auto pendingEvent:loop->_pendingEvents) {

				bool collisionEdgeHeadIsHeadSide = ContainsVertex(headSide._edges, pendingEvent._edgeHead);
				bool collisionEdgeTailIsHeadSide = ContainsVertex(headSide._edges, pendingEvent._edgeTail);

				if (pendingEvent._type == EventType::MotorcycleCrash) {
					if (crashEvent._edgeHead == crashEvent._edgeTail && pendingEvent._motor == crashEvent._edgeHead) continue;	// (this vertex removed)

					// If the crashEvent._motor is on the pending event edge, and the pending event motor is on crashEvent edge, then this was
					// probably 2 vertices colliding head on; and we don't need to process the pending event any further...?
					/*if (	(pendingEvent._motor == crashEvent._edgeHead || pendingEvent._motor == crashEvent._edgeTail)
						&& 	(crashEvent._motor == pendingEvent._edgeHead || crashEvent._motor == pendingEvent._edgeTail)) {
						assert(Equivalent(PointAndTime<Primitive>{pendingEvent._eventPt, pendingEvent._eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
						continue;
					}*/

					bool motorIsHeadSide = ContainsVertex(headSide._edges, pendingEvent._motor);
					assert(motorIsHeadSide || ContainsVertex(tailSide._edges, pendingEvent._motor));

					if (motorIsHeadSide) {
						// We might have collided with the crashEvent._motor <---- hin edge
						if (pendingEvent._edgeHead == crashEvent._motor) {
							if (pendingEvent._edgeHead == pendingEvent._edgeTail) {
								pendingEvent._edgeHead = pendingEvent._edgeTail = headSideReplacement;
							} else {
								pendingEvent._edgeHead = headSideReplacement;
								assert(collisionEdgeTailIsHeadSide);
							}
						} else if (pendingEvent._edgeTail == crashEvent._motor) {
							// if the motor is on the head side, but we're colliding with an edge that should be on the tail
							// side, then we're potentially in trouble. However, this is fine if the collision point is
							// directly on crashEvent._motor -- because in this case, crashEvent._motor <---- hin is interchangable
							// with tout <--- crashEvent._motor
							assert(Equivalent(PointAndTime<Primitive>{pendingEvent._eventPt, pendingEvent._eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
							assert(pendingEvent._edgeHead != pendingEvent._edgeTail);		// this case gets caught by the condition above
							assert(crashEvent._edgeHead != crashEvent._edgeTail);
							pendingEvent._edgeHead = headSideReplacement;
							pendingEvent._edgeTail = hin->_tail;
						} else if (pendingEvent._edgeTail == crashSegmentTail) {
							// When the motor is headside, if there's another crash on the same segment it should be
							// in the pendingEvent._edgeHead <--- headSideReplacement part.
							// We have to consider the cases where either crashEvent or pendingEvent are single vertex collisions, as well
							if (pendingEvent._edgeHead == pendingEvent._edgeTail) {
								pendingEvent._edgeHead = pendingEvent._edgeTail = headSideReplacement;
							} else {
								assert((crashSegmentHead == crashSegmentTail) || pendingEvent._edgeHead == crashSegmentHead);
								pendingEvent._edgeTail = headSideReplacement;
							}
						} else if (!collisionEdgeHeadIsHeadSide || !collisionEdgeTailIsHeadSide) {
							// the motor is on headside, but the edge is on tail side, and the edge is unrelated to crashEvent
							// this can happen in extreme cases of many vertices colliding in the on the same point
							// We would have to change the edge into some completely different edge in order to process it; but 
							// it's probably actually redundant

							auto pt0 = _vertices[pendingEvent._edgeHead].PositionAtTime(pendingEvent._eventTime);
							auto pt1 = _vertices[pendingEvent._edgeTail].PositionAtTime(pendingEvent._eventTime);
							auto pt2 = _vertices[pendingEvent._motor].PositionAtTime(pendingEvent._eventTime);
							if (pendingEvent._edgeHead == pendingEvent._edgeTail) {
								assert(Equivalent(pt0, pt2, GetEpsilon<Primitive>()));
							} else {
								assert(CalculateWindingType(pt0, pt2, pt1, GetEpsilon<Primitive>()) == WindingType::Straight);
							}
							assert(Equivalent(pt2, pendingEvent._eventPt, GetEpsilon<Primitive>()));

							assert(Equivalent(PointAndTime<Primitive>{pendingEvent._eventPt, pendingEvent._eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
							continue;
						}
						AddEvent(headSide, pendingEvent);
					} else {
						// We might have collided with the tout <--- crashEvent._motor edge
						if (pendingEvent._edgeTail == crashEvent._motor) {
							if (pendingEvent._edgeHead == pendingEvent._edgeTail) {
								pendingEvent._edgeHead = pendingEvent._edgeTail = tailSideReplacement;
							} else {
								pendingEvent._edgeTail = tailSideReplacement;
								assert(!collisionEdgeHeadIsHeadSide);
							}
						} else if (pendingEvent._edgeHead == crashEvent._motor) {
							assert(Equivalent(PointAndTime<Primitive>{pendingEvent._eventPt, pendingEvent._eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
							assert(pendingEvent._edgeHead != pendingEvent._edgeTail);
							assert(crashEvent._edgeHead != crashEvent._edgeTail);
							pendingEvent._edgeHead = tout->_head;
							pendingEvent._edgeTail = tailSideReplacement;
						} else if (pendingEvent._edgeHead == crashSegmentHead) {
							if (pendingEvent._edgeHead == pendingEvent._edgeTail) {
								pendingEvent._edgeHead = pendingEvent._edgeTail = tailSideReplacement;
							} else {
								assert((crashSegmentHead == crashSegmentTail) || pendingEvent._edgeTail == crashSegmentTail);
								pendingEvent._edgeHead = tailSideReplacement;							
							}
						} else if (collisionEdgeHeadIsHeadSide || collisionEdgeTailIsHeadSide) {
							assert(Equivalent(PointAndTime<Primitive>{pendingEvent._eventPt, pendingEvent._eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
							continue;
						}
						AddEvent(tailSide, pendingEvent);
					}
				} else {
					assert(pendingEvent._type == EventType::Collapse);
					if (pendingEvent._edgeHead == crashEvent._motor && pendingEvent._edgeTail == crashEvent._motor) {
						pendingEvent._edgeHead = headSideReplacement;
						pendingEvent._edgeTail = headSideReplacement;
						AddEvent(headSide, pendingEvent);
					} else if (pendingEvent._edgeHead == crashEvent._motor) {
						assert(collisionEdgeTailIsHeadSide);
						pendingEvent._edgeHead = headSideReplacement;
						AddEvent(headSide, pendingEvent);
					} else if (pendingEvent._edgeTail == crashEvent._motor) {
						assert(!collisionEdgeHeadIsHeadSide);
						pendingEvent._edgeTail = tailSideReplacement;
						AddEvent(tailSide, pendingEvent);
					} else if (pendingEvent._edgeHead == crashEvent._edgeHead && pendingEvent._edgeTail == crashEvent._edgeTail) {
						// we crashed into an edge that was pending a collapse, anyway
						auto headSideEvent = pendingEvent;
						headSideEvent._edgeTail = headSideReplacement;
						AddEvent(headSide, headSideEvent);
						pendingEvent._edgeHead = tailSideReplacement;
						AddEvent(tailSide, pendingEvent);
					} else if (crashEvent._edgeHead == crashEvent._edgeTail && pendingEvent._edgeHead == crashEvent._edgeHead) {
						assert(!collisionEdgeTailIsHeadSide);
						pendingEvent._edgeHead = tailSideReplacement;
						AddEvent(tailSide, pendingEvent);
					} else if (crashEvent._edgeHead == crashEvent._edgeTail && pendingEvent._edgeTail == crashEvent._edgeHead) {
						assert(collisionEdgeHeadIsHeadSide);
						pendingEvent._edgeTail = headSideReplacement;
						AddEvent(headSide, pendingEvent);
					} else {
						assert(collisionEdgeHeadIsHeadSide == collisionEdgeTailIsHeadSide);
						if (collisionEdgeHeadIsHeadSide) {
							AddEvent(headSide, pendingEvent);
						} else {
							AddEvent(tailSide, pendingEvent);
						}
					}
				}
			}

			// Overwrite "loop" with tailSide, and append inSide to the list of wavefront loops
			// crashSegment, motorIn & motorOut should not make it into either tailSide or headSide
			for (auto e=tailSide._edges.begin(); e!=tailSide._edges.end(); ++e) {
				assert(e->_head != crashSegmentHead || e->_tail != crashSegmentTail);
				assert(e->_head != motorIn->_head || e->_tail != motorIn->_tail);
				assert(e->_head != motorOut->_head || e->_tail != motorOut->_tail);
				assert(e->_head != e->_tail);
				auto next = e+1; if (next == tailSide._edges.end()) next = tailSide._edges.begin();
				assert(e->_head == next->_tail);
			}
			for (auto e=headSide._edges.begin(); e!=headSide._edges.end(); ++e) {
				assert(e->_head != crashSegmentHead || e->_tail != crashSegmentTail);
				assert(e->_head != motorIn->_head || e->_tail != motorIn->_tail);
				assert(e->_head != motorOut->_head || e->_tail != motorOut->_tail);
				assert(e->_head != e->_tail);
				auto next = e+1; if (next == headSide._edges.end()) next = headSide._edges.begin();
				assert(e->_head == next->_tail);
			}

			size_t loopIdx = std::distance(workingLoops.begin(), loop);
			*loop = std::move(tailSide);
			workingLoops.emplace_back(std::move(headSide));
			loop = workingLoops.begin() + loopIdx;
		}

		return workingLoops;
	}

	T1(Primitive) void Graph<Primitive>::ProcessCollapseEvents(WavefrontLoop& loop)
	{
		auto i = loop._pendingEvents.begin();
		while (i != loop._pendingEvents.end() && i->_type == EventType::Collapse) ++i;

		auto collapses = MakeIteratorRange(loop._pendingEvents.begin(), i);
		if (collapses.empty()) return;

		// Process the "edge" events... first separate the edges into collapse groups
		// Each collapse group collapses onto a single vertex. We will search through all
		// of the collapse events we're processing, and separate them into discrete groups.
		std::vector<unsigned> collapseGroups(collapses.size(), ~0u);
		struct CollapseGroupInfo { unsigned _head, _tail, _newVertex = ~0u; PointAndTime<Primitive> _crashPtAndTime; };
		std::vector<CollapseGroupInfo> collapseGroupInfos;
		unsigned nextCollapseGroup = 0;
		for (size_t c=0; c<collapses.size(); ++c) {
			if (collapseGroups[c] != ~0u) continue;

			#if defined(_DEBUG)
				assert(collapses[c]._edgeHead != collapses[c]._edgeTail);
				auto q = std::find_if(loop._edges.begin(), loop._edges.end(), 
					[c=collapses[c]](const auto& e){ return e._head == c._edgeHead && e._tail == c._edgeTail; });
				assert(q != loop._edges.end());
			#endif

			collapseGroups[c] = nextCollapseGroup;

			// got back as far as possible, from tail to tail
			auto searchingTail = collapses[c]._edgeTail;
			for (;;) {
				auto i = std::find_if(collapses.begin(), collapses.end(),
					[searchingTail, &loop](const auto& t)
					{ return t._edgeHead == searchingTail; });
				if (i == collapses.end()) break;
				if (collapseGroups[std::distance(collapses.begin(), i)] == nextCollapseGroup) break;
				assert(collapseGroups[std::distance(collapses.begin(), i)] == ~0u);
				collapseGroups[std::distance(collapses.begin(), i)] = nextCollapseGroup;
				searchingTail = i->_edgeTail;
			}

			// also go forward head to head
			auto searchingHead = collapses[c]._edgeHead;
			for (;;) {
				auto i = std::find_if(collapses.begin(), collapses.end(),
					[searchingHead, &loop](const auto& t)
					{ return t._edgeTail == searchingHead; });
				if (i == collapses.end()) break;
				if (collapseGroups[std::distance(collapses.begin(), i)] == nextCollapseGroup) break;
				assert(collapseGroups[std::distance(collapses.begin(), i)] == ~0u);
				collapseGroups[std::distance(collapses.begin(), i)] = nextCollapseGroup;
				searchingHead = i->_edgeHead;
			}

			++nextCollapseGroup;
			collapseGroupInfos.push_back({searchingHead, searchingTail, ~0u});
		}

		// Each collapse group becomes a single new vertex. We can collate them together
		// now, and write out some segments to the output skeleton
		Primitive earliestCollapseTime = std::numeric_limits<Primitive>::max(), latestCollapseTime = -std::numeric_limits<Primitive>::max();
		for (auto collapseGroup=0u; collapseGroup<nextCollapseGroup; ++collapseGroup) {
			Vector2T<Primitive> collisionPt(Primitive(0), Primitive(0));
			unsigned contributors = 0;
			Primitive groupCollapseTime = std::numeric_limits<Primitive>::max();
			for (size_t c=0; c<collapses.size(); ++c) {
				if (collapseGroups[c] != collapseGroup) continue;
				collisionPt += collapses[c]._eventPt;
				contributors += 1;
				groupCollapseTime = std::min(collapses[c]._eventTime, groupCollapseTime);
			}
			collisionPt /= Primitive(contributors);
			earliestCollapseTime = std::min(earliestCollapseTime, groupCollapseTime);
			latestCollapseTime = std::max(latestCollapseTime, groupCollapseTime);

			// Validate that our "collisionPt" is close to all of the collapsing points
			#if defined(_DEBUG)
				for (size_t c=0; c<collapses.size(); ++c) {
					if (collapseGroups[c] != collapseGroup) continue;
					auto one = GetVertex<Primitive>(_vertices, collapses[c]._edgeHead).PositionAtTime(groupCollapseTime);
					auto two = GetVertex<Primitive>(_vertices, collapses[c]._edgeTail).PositionAtTime(groupCollapseTime);
					// assert(Equivalent(one, collisionPt, GetEpsilon<Primitive>()));
					// assert(Equivalent(two, collisionPt, GetEpsilon<Primitive>()));
				}
			#endif

			collapseGroupInfos[collapseGroup]._crashPtAndTime = PointAndTime<Primitive>{collisionPt, groupCollapseTime};

			// We're removing vertices from active loops -- so, we must add their vertex path to the
			// output skeleton.
			// Note that since we're connecting both head and tail, we'll end up doubling up each edge
			for (size_t c=0; c<collapses.size(); ++c) {
				if (collapseGroups[c] != collapseGroup) continue;
				for (auto v:{ collapses[c]._edgeHead, collapses[c]._edgeTail }) {
					AddVertexPathEdge(v, GetVertex<Primitive>(_vertices, v)._anchor0, collapseGroupInfos[collapseGroup]._crashPtAndTime);

					// Also remove any motorcycles associated with these vertices (since they will be removed
					// from active loops, the motorcycle is no longer valid)
					auto m = std::find_if(loop._motorcycleSegments.begin(), loop._motorcycleSegments.end(),
						[v](const MotorcycleSegment& seg) { return seg._head == v; });
					if (m != loop._motorcycleSegments.end())
						loop._motorcycleSegments.erase(m);
				}
			}
		}

		// Remove all of the collapsed edges
		for(auto i=loop._edges.begin(); i!=loop._edges.end();) {
			auto q = std::find_if(collapses.begin(), collapses.end(), 
				[i](const auto& c){ return c._edgeHead == i->_head && c._edgeTail == i->_tail; });
			if (q == collapses.end()) {
				++i;
			} else
				i = loop._edges.erase(i);
		}

		if (loop._edges.size() == 1) {
			assert(collapseGroupInfos.size() == 1);
			assert(loop._edges[0]._head == collapseGroupInfos[0]._tail);
			assert(loop._edges[0]._tail == collapseGroupInfos[0]._head);
			loop._edges.clear();
		} else {
			for (auto& group:collapseGroupInfos) {
				if (group._head == group._tail) continue;	// if we remove an entire loop, let's assume that there are no external links to it

				// create a new vertex in the graph to connect the edges to either side of the collapse
				group._newVertex = (unsigned)_vertices.size();
				_vertices.push_back({group._crashPtAndTime, group._crashPtAndTime});

				// reassign the edges on either side of the collapse group to
				// point to the new vertex
				auto tail = FindInAndOut(MakeIteratorRange(loop._edges).template Cast<WavefrontEdge*>(), group._tail).first;
				auto head = FindInAndOut(MakeIteratorRange(loop._edges).template Cast<WavefrontEdge*>(), group._head).second;
				assert(tail && head);
				assert(tail != head);
				tail->_head = group._newVertex;
				head->_tail = group._newVertex;

				assert(tail->_head != tail->_tail);
				assert(head->_head != head->_tail);
			}
		}

		if (!loop._edges.empty()) {
			// rename collapsed vertices in pending events
			for (auto pendingEvent=collapses.end(); pendingEvent!=loop._pendingEvents.end(); ++pendingEvent) {
				auto headCollapse = std::find_if(collapses.begin(), collapses.end(), [v=pendingEvent->_edgeHead](const auto& c) { return c._edgeHead == v || c._edgeTail == v; });
				auto tailCollapse = std::find_if(collapses.begin(), collapses.end(), [v=pendingEvent->_edgeTail](const auto& c) { return c._edgeHead == v || c._edgeTail == v; });

				if (tailCollapse != collapses.end())
					pendingEvent->_edgeTail = collapseGroupInfos[collapseGroups[std::distance(collapses.begin(), tailCollapse)]]._newVertex;
				if (headCollapse != collapses.end())
					pendingEvent->_edgeHead = collapseGroupInfos[collapseGroups[std::distance(collapses.begin(), headCollapse)]]._newVertex;
				assert(pendingEvent->_edgeTail != ~0u);
				assert(pendingEvent->_edgeHead != ~0u);

				if (pendingEvent->_type == EventType::MotorcycleCrash) {
					auto motorCollapse = std::find_if(collapses.begin(), collapses.end(), [v=pendingEvent->_motor](const auto& c) { return c._edgeHead == v || c._edgeTail == v; });
					if (motorCollapse != collapses.end())
						pendingEvent->_motor = collapseGroupInfos[collapseGroups[std::distance(collapses.begin(), motorCollapse)]]._newVertex;
					assert(pendingEvent->_motor != ~0u);
				} else {
					assert(pendingEvent->_type == EventType::Collapse);
					// we will sometimes need to rename collapse events if there is a motorcycle in the middle
				}
			}
			for (auto i=loop._motorcycleSegments.begin(); i!=loop._motorcycleSegments.end();) {
				if (ContainsVertex(loop._edges, i->_head)) ++i;
				else i = loop._motorcycleSegments.erase(i);
			}
			loop._pendingEvents.erase(collapses.begin(), collapses.end());	
		} else {
			loop._pendingEvents.clear();
			loop._motorcycleSegments.clear();
		}
		
		loop._lastEventBatchEarliest = std::min(loop._lastEventBatchEarliest, earliestCollapseTime);
		loop._lastEventBatchLatest = latestCollapseTime;
	}

	T1(Primitive) void Graph<Primitive>::ProcessEvents(typename std::vector<WavefrontLoop>::iterator loop)
	{
		loop->_lastEventBatchEarliest = std::numeric_limits<Primitive>::max();
		loop->_lastEventBatchLatest = -std::numeric_limits<Primitive>::max();
		std::vector<WavefrontLoop> workingLoops;
		workingLoops.push_back(std::move(*loop));

		// Keep processing events until there are no more to do
		for (;;) {
			for (auto& l:workingLoops)
				if (!l._pendingEvents.empty() && l._pendingEvents.begin()->_type == EventType::Collapse)
					ProcessCollapseEvents(l);

			bool gotCrashEvent = false;
			for (auto l=workingLoops.begin(); l!=workingLoops.end(); ++l)
				if (!l->_pendingEvents.empty() && l->_pendingEvents.begin()->_type == EventType::MotorcycleCrash) {
					auto newLoops = ProcessMotorcycleEvents(*l);
					assert(!newLoops.empty());
					*l = std::move(*newLoops.begin());
					for (auto i=newLoops.begin()+1; i!=newLoops.end(); ++i)
						workingLoops.push_back(std::move(*i));
					gotCrashEvent = true;
					break;
				}

			if (!gotCrashEvent) break;
		}

		assert(!workingLoops.empty());
		*loop = std::move(*workingLoops.begin());
		for (auto l=workingLoops.begin()+1; l!=workingLoops.end(); ++l) {
			if (!l->_edges.empty())
				_loops.push_back(std::move(*l));
		}
	}

#if 0
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
#endif

	T1(Primitive) static unsigned AddSteinerVertex(StraightSkeleton<Primitive>& skeleton, const Vector3T<Primitive>& vertex)
	{
		assert(IsFiniteNumber(vertex[0]) && IsFiniteNumber(vertex[1]) && IsFiniteNumber(vertex[2]));
		assert(vertex[0] != std::numeric_limits<Primitive>::max() && vertex[1] != std::numeric_limits<Primitive>::max() && vertex[2] != std::numeric_limits<Primitive>::max());

		auto existing = std::find_if(
			skeleton._steinerVertices.begin(), skeleton._steinerVertices.end(),
			[vertex](const auto& c) { return AdaptiveEquivalent(vertex, c, GetEpsilon<Primitive>()); });
		if (existing != skeleton._steinerVertices.end())
			return unsigned(skeleton._boundaryPointCount + std::distance(skeleton._steinerVertices.begin(), existing));

		auto result = (unsigned)skeleton._steinerVertices.size();
		skeleton._steinerVertices.push_back(vertex);
		return skeleton._boundaryPointCount + result;
	}

	T1(EdgeType) static void AddUnique(std::vector<EdgeType>& dst, const EdgeType& edge)
	{
		auto existing = std::find_if(
			dst.begin(), dst.end(), 
			[&edge](const EdgeType&e) { return e._head == edge._head && e._tail == edge._tail; });
		if (existing == dst.end()) {
			dst.push_back(edge);
		} else {
			assert(existing->_type == edge._type);
		}
	}

	T1(Primitive) static void AddEdge(StraightSkeleton<Primitive>& dest, unsigned headVertex, unsigned tailVertex, typename StraightSkeleton<Primitive>::EdgeType type)
	{
		if (headVertex == tailVertex) return;
		AddUnique(dest._edges, {headVertex, tailVertex, type});
	}

	T1(Primitive) void Graph<Primitive>::AddVertexPathEdge(unsigned vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end)
	{
		_vertexPathEdges.push_back({vertex, begin, end});
	}

	T1(Primitive) StraightSkeleton<Primitive> Graph<Primitive>::CalculateSkeleton(Primitive maxTime)
	{
		std::vector<WavefrontLoop> completedLoops;
		while (!_loops.empty()) {
			ValidateState();

			auto& loop = _loops.front();
			if (loop._edges.size() <= 2) {
				if (loop._lastEventBatchEarliest > loop._lastEventBatchLatest)
					loop._lastEventBatchEarliest = loop._lastEventBatchLatest = maxTime;
				if (!loop._edges.empty())
					completedLoops.emplace_back(std::move(loop));
				_loops.erase(_loops.begin());
				continue;
			}

			UpdateLoop(loop);

			// Find the next event to occur
			//		-- either a edge collapse or a motorcycle collision
			std::vector<Event<Primitive>> events;
			Primitive earliestEvent = std::numeric_limits<Primitive>::max();
			FindCollapses(events, earliestEvent, loop);
			FindMotorcycleCrashes(events, earliestEvent, loop);

            // If we do not find any more events, the remaining wavefronts will expand infinitely.
            // This case isn't perfectly handled currently, we'll just complete the loop here if
            // it has started.  If it has not started, skip it.
            if (events.empty()) {
				if (loop._lastEventBatchEarliest > loop._lastEventBatchLatest)
					loop._lastEventBatchEarliest = loop._lastEventBatchLatest = maxTime;
				if (!loop._edges.empty())
					completedLoops.emplace_back(std::move(loop));
				_loops.erase(_loops.begin());
                continue;
            }

			if (earliestEvent >= maxTime) {
				loop._lastEventBatchEarliest = loop._lastEventBatchLatest = maxTime;
				if (!loop._edges.empty())
					completedLoops.emplace_back(std::move(loop));
				_loops.erase(_loops.begin());
				continue;
			}

			std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
			loop._pendingEvents = std::move(events);

			ProcessEvents(_loops.begin());
		}

		StraightSkeleton<Primitive> result;
		result._boundaryPointCount = _boundaryPointCount;
		for (const auto&l:completedLoops)
			WriteWavefront(result, l, l._lastEventBatchLatest);
		for (const auto&l:completedLoops)
			WriteVertexPaths(result, l, l._lastEventBatchLatest);
		for (const auto&l:_loops)
			WriteVertexPaths(result, l, l._lastEventBatchLatest);
		for (const auto& e:_vertexPathEdges) {
			AddEdge(
				result,
				(e._vertex <  _boundaryPointCount) ? e._vertex : AddSteinerVertex(result, e._beginPt),
				AddSteinerVertex(result, e._endPt), 
				StraightSkeleton<Primitive>::EdgeType::VertexPath);
		}
		return result;
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
			auto A = PointAndTime<Primitive>{_vertices[i->_head].PositionAtTime(time), time};
			auto B = PointAndTime<Primitive>{_vertices[i->_tail].PositionAtTime(time), time};
			auto v0 = AddSteinerVertex(result, A);
			auto v1 = AddSteinerVertex(result, B);
			if (v0 != v1)
				segmentsToTest.push(WavefrontEdge{v0, v1});
		}

#if 0
		while (!segmentsToTest.empty()) {
			auto seg = segmentsToTest.top();
			segmentsToTest.pop();

			auto A = Truncate(result._steinerVertices[seg._head]);
			auto B = Truncate(result._steinerVertices[seg._tail]);
			bool filterOutSeg = false;

			// Compare against all edges already in "filteredSegments"
			for (auto i2=filteredSegments.begin(); i2!=filteredSegments.end();++i2) {

				if (i2->_head == seg._head && i2->_tail == seg._tail) {
					filterOutSeg = true; 
					break; // (overlap completely)
				} else if (i2->_head == seg._tail && i2->_tail == seg._head) {
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
#else
		while (!segmentsToTest.empty()) {
			auto seg = segmentsToTest.top();
			segmentsToTest.pop();

			bool filterOutSeg = false;
			for (auto i2=filteredSegments.begin(); i2!=filteredSegments.end();++i2) {
				if (i2->_head == seg._head && i2->_tail == seg._tail) {
					filterOutSeg = true; 
					break; // (overlap completely)
				} else if (i2->_head == seg._tail && i2->_tail == seg._head) {
					filterOutSeg = true; 
					break; // (overlap completely)
				}
			}
			if (!filterOutSeg)
				filteredSegments.push_back(seg);
		}
#endif

		// Add all of the segments in "filteredSegments" to the skeleton
		for (const auto&seg:filteredSegments) {
			assert(seg._head != seg._tail);
			AddEdge(result, seg._head, seg._tail, StraightSkeleton<Primitive>::EdgeType::Wavefront);
		}
	}

	T1(Primitive) void Graph<Primitive>::WriteVertexPaths(StraightSkeleton<Primitive>& result, const WavefrontLoop& loop, Primitive time)
	{
		for (auto i = loop._edges.begin(); i!=loop._edges.end(); ++i) {
			auto v0 = GetVertex<Primitive>(_vertices, i->_tail);
			auto finalPt = v0.PositionAtTime(loop._lastEventBatchLatest);
			AddEdge(
				result, 
				(i->_tail < _boundaryPointCount) ? i->_tail : AddSteinerVertex(result, v0._anchor0), 
				AddSteinerVertex(result, PointAndTime<Primitive>{finalPt, loop._lastEventBatchLatest}), 
				StraightSkeleton<Primitive>::EdgeType::VertexPath);
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
		for (auto&e:_edges)
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
