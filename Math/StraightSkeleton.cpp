// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StraightSkeleton.h"
#include "StraightSkeleton_Internal.h"
#include "../Math/Geometry.h"
#include <cmath>
#include <optional>
#include <deque>

#if defined(_DEBUG)
	// #define EXTRA_VALIDATION
#endif

#pragma warning(disable:4505) // 'SceneEngine::StraightSkeleton::ReplaceVertex': unreferenced local function has been removed


namespace XLEMath
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	using VertexId = unsigned;
	using LoopId = unsigned;

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

	T1(Primitive) struct WavefrontEdge
	{
		VertexId _head, _tail;
		PointAndTime<Primitive>	_collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
		bool _pendingCalculate = true;
	};

	T1(Primitive) struct MotorcycleSegment
	{
		VertexId _motor;
		PointAndTime<Primitive> _crashPt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
		VertexId _edgeHead = ~VertexId(0), _edgeTail = ~VertexId(0);
		LoopId _edgeLoop = ~LoopId(0);
		bool _pendingCalculate = true;
	};

	enum class EventType { Collapse, MotorcycleCrash, MultiLoopMotorcycleCrash, None };
	T1(Primitive) struct Event
	{
		Vector2T<Primitive> _eventPt = Zero<Vector2T<Primitive>>();
		Primitive _eventTime = std::numeric_limits<Primitive>::max();
		EventType _type = EventType::None;
		LoopId _edgeLoop = ~LoopId(0);

		// Collapse edge or collision edge
		VertexId _edgeHead = ~VertexId(0);
		VertexId _edgeTail = ~VertexId(0);

		// Motorcycle crash
		VertexId _motor = ~VertexId(0);
		LoopId _motorLoop = ~LoopId(0);

		static Event Collapse(LoopId loop, PointAndTime<Primitive> eventPt, VertexId head, VertexId tail)
		{
			Event result;
			result._eventPt = Truncate(eventPt);
			result._eventTime = eventPt[2];
			result._edgeLoop = loop;
			result._type = EventType::Collapse;
			result._edgeHead = head;
			result._edgeTail = tail;
			result._motor = ~VertexId(0);
			return result;
		}

		static Event MotorcycleCrash(LoopId edgeLoop, PointAndTime<Primitive> eventPt, VertexId motor, VertexId collisionHead, VertexId collisionTail, LoopId motorLoop)
		{
			Event result;
			result._eventPt = Truncate(eventPt);
			result._eventTime = eventPt[2];
			result._edgeLoop = edgeLoop;
			result._type = EventType::MotorcycleCrash;
			result._edgeHead = collisionHead;
			result._edgeTail = collisionTail;
			result._motor = motor;
			result._motorLoop = edgeLoop;
			if (motorLoop != ~LoopId(0) && motorLoop != edgeLoop) {
				result._type = EventType::MultiLoopMotorcycleCrash;
				result._motorLoop = motorLoop;
			}
			return result;
		}
	};

	T1(Primitive) struct WavefrontLoop
	{
		std::vector<WavefrontEdge<Primitive>> _edges;
		std::vector<MotorcycleSegment<Primitive>> _motorcycleSegments;
		Primitive _lastEventBatchEarliest = std::numeric_limits<Primitive>::max();
		Primitive _lastEventBatchLatest = -std::numeric_limits<Primitive>::max();
		unsigned _lastBatchIndex = 0;
		LoopId _loopId = ~LoopId(0);
	};

	template<typename Iterator>
		static auto FindInAndOut(IteratorRange<Iterator> edges, unsigned pivotVertex) -> std::pair<Iterator, Iterator>
    {
        Iterator first = edges.end(), second = edges.end();
        for  (auto s=edges.begin(); s!=edges.end(); ++s) {
            if (s->_head == pivotVertex) { assert(first == edges.end()); first = s; }
            else if (s->_tail == pivotVertex) { assert(second == edges.end()); second = s; }
        }
        return {first, second};
    }

	T1(Primitive) const Vertex<Primitive>& GetVertex(VertexSet<Primitive> vSet, VertexId v)
	{
		return vSet[v];
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) struct ProtoCrashEvent
	{
		enum class Type { Middle, Head, Tail };
		Type _type;
		PointAndTime<Primitive> _pointAndTime;
	};

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
		auto res = FindCrashEvent<Primitive>(p0-p2, p1-p2, motorcycle.Velocity());
		auto epsilon = GetEpsilon<Primitive>();
		if (!res || res.value()[2] < -epsilon) return {};
		// if (!res || res.value()[2] < 0) return {};

		auto pointAndTime = res.value();
		pointAndTime += Expand(p2, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = edgeHead.PositionAtTime(pointAndTime[2]);
		p1 = edgeTail.PositionAtTime(pointAndTime[2]);
		p2 = Truncate(pointAndTime);

		auto edgeMagSq = MagnitudeSquared(p1-p0);
		if (edgeMagSq < epsilon * epsilon)		// edge is collapsed at this point
			return {};

		// There might be a problem here if the edge has collapsed before the collision time-- we can still find a motor cycle collision
		// briefly after the collapse

		auto d0 = Dot(p1-p0, p2-p0);			// distance from p0 (projected onto edge) = d0 / Magnitude(p1-p0)
		auto d1 = Dot(p0-p1, p2-p1);			// distance from p1 (projected onto edge) = d1 / Magnitude(p1-p0)
		auto d0Sq = std::copysign(d0*d0, d0);
		auto d1Sq = std::copysign(d1*d1, d1);
		float eSq = epsilon * epsilon * edgeMagSq;
		if (d0Sq < -eSq || d1Sq < -eSq)
		// if (d0Sq < 0 || d1Sq < 0)
			return {};

		if (d0Sq < eSq) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		} else if (d1Sq < eSq) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		} else {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
		}
	}

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
		auto epsilon = GetEpsilon<Primitive>();
		if (!res || res.value()[2] < -epsilon)
			return {};

		auto pointAndTime = res.value();
		pointAndTime += Expand(m1, calcTime);

		// We have to test to ensure that the intersection point is actually lying within
		// the edge segment (we only know currently that it is colinear)
		p0 = edgeHead.PositionAtTime(pointAndTime[2]);
		p1 = edgeTail.PositionAtTime(pointAndTime[2]);
		auto p2 = Truncate(pointAndTime);

		auto edgeMagSq = MagnitudeSquared(p1-p0);
		if (edgeMagSq < epsilon * epsilon)		// edge is collapsed at this point
			return {};

		// There might be a problem here if the edge has collapsed before the collision time-- we can still find a motor cycle collision
		// briefly after the collapse

		auto d0 = Dot(p1-p0, p2-p0);			// distance from p0 (projected onto edge) = d0 / Magnitude(p1-p0)
		auto d1 = Dot(p0-p1, p2-p1);			// distance from p1 (projected onto edge) = d1 / Magnitude(p1-p0)
		auto d0Sq = std::copysign(d0*d0, d0);
		auto d1Sq = std::copysign(d1*d1, d1);
		float eSq = epsilon * epsilon * edgeMagSq;
		if (d0Sq < -eSq || d1Sq < -eSq)			// we need a little bit of tolerance here; because we can miss collisions if we test against zero (even though missing requires us to actually miss twice -- once on either edge to connecting to the vertex we're hitting)
			return {};

		if (d0Sq < eSq) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		} else if (d1Sq < eSq) {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		} else {
			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
		}
	}
    
	T1(Primitive) static std::optional<Event<Primitive>> CalculateCrashEvent(
		VertexId motor, 
		const WavefrontLoop<Primitive>& motorLoop,
		IteratorRange<const WavefrontLoop<Primitive>*> loops,
		VertexSet<Primitive> vertices)
	{
		std::optional<Event<Primitive>> bestCollisionEvent;
		auto bestEventTime = std::numeric_limits<Primitive>::max();

		auto inAndOut = FindInAndOut(MakeIteratorRange(motorLoop._edges), motor);
		auto motorPrev = inAndOut.first->_tail;
		auto motorNext = inAndOut.second->_head;

		// Look for an intersection with segments
		for (const auto&l:loops)
			for (const auto&e:l._edges) {
				if (e._head == motor || e._tail == motor) continue;	// (can't crash if part of the same segment)

				// "BuildCrashEvent_SimultaneousV" seems to do better here in the presence of near-colinear edges
				// since we use the vertex velocity we've already calculated, it takes into account all of the colinear protections
				// The downside is any floating point precision we picked up from there will impact the crash location calculation
				// auto res = BuildCrashEvent_Simultaneous(vertices, e._head, e._tail, motorPrev, motor, motorNext);
				auto res = BuildCrashEvent_SimultaneousV(vertices, e._head, e._tail, motor);

				if (res.has_value() && res.value()._pointAndTime[2] < bestEventTime) {
					auto protoCrash = res.value();
					if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Head) {
						if (e._head == motorPrev) continue;		// reject crashes when there is a direct edge between the motor and the point
						bestCollisionEvent = Event<Primitive>::MotorcycleCrash(l._loopId, protoCrash._pointAndTime, motor, e._head, e._head, motorLoop._loopId);
					} else if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Tail) {
						if (e._tail == motorNext) continue;		// reject crashes when there is a direct edge between the motor and the point
						bestCollisionEvent = Event<Primitive>::MotorcycleCrash(l._loopId, protoCrash._pointAndTime, motor, e._tail, e._tail, motorLoop._loopId);
					} else {
						bestCollisionEvent = Event<Primitive>::MotorcycleCrash(l._loopId, protoCrash._pointAndTime, motor, e._head, e._tail, motorLoop._loopId);
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
		auto vm2 = vSet[vm2i].PositionAtTime(calcTime);
		auto vm1 = vSet[vm1i].PositionAtTime(calcTime);
		auto v0 = vSet[v0i].PositionAtTime(calcTime);
		auto v1 = vSet[v1i].PositionAtTime(calcTime);
		auto v2 = vSet[v2i].PositionAtTime(calcTime);

		// "V" shape protection. If we attempt to calculate the velocity in these cases, we can't find it accurately
		// we often end up with vertices that fly off in wierd directions. Once we end up with these wierd colinear/flat V
		// shapes, the algorithm doesn't care where the vertices are on the line; and so we end up with wierd results
		// These cases should either collapse or change due to a motorcycle crash essentially immediately, so zero 
		// velocity should be fine
		auto epsilon = GetEpsilon<Primitive>();
		auto magFactor = Primitive(4) / MagnitudeSquared(v1 - vm1);
		auto winding = CalculateWindingType(vm1, v0, v1, epsilon*magFactor);
		if (winding.first == WindingType::FlatV)
			return vSet[v0i]._anchor0;

		auto collapse0 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vm2, vm1, v0, v1, v0);
		auto collapse1 = CalculateEdgeCollapse_Offset_ColinearTest_LargeTimeProtection(vm1, v0, v1, v2, v0);

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
			auto velocity = CalculateVertexVelocity_LineIntersection(vm1, v0, v1, Primitive(1));
			if (velocity)
				return vSet[v0i]._anchor0 + PointAndTime<Primitive>{velocity.value(), Primitive(1)};
			return vSet[v0i]._anchor0;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) class StraightSkeletonGraph
	{
	public:
		std::vector<Vertex<Primitive>> _vertices;

		std::vector<WavefrontLoop<Primitive>> _loops;
		size_t _boundaryPointCount = 0;
		LoopId _nextLoopId = 1; 
		unsigned _currentBatchIndex = 0;

		struct VertexPathEdge
		{
			VertexId _vertex;
			PointAndTime<Primitive> _beginPt, _endPt;
		};
		std::vector<VertexPathEdge> _vertexPathEdges;

		StraightSkeleton<Primitive> CalculateSkeleton(Primitive maxTime);
		typename std::vector<WavefrontLoop<Primitive>>::iterator GetLoop(LoopId id);
	private:
		void WriteFinalEdges(StraightSkeleton<Primitive>& dest, const WavefrontLoop<Primitive>& loop, Primitive time);

		void ProcessEvents(std::vector<Event<Primitive>>& evnts);
		void ProcessMotorcycleEvents(std::vector<Event<Primitive>>& evnts);
		void ProcessCollapseEvents(std::vector<Event<Primitive>>& evnts);

		void FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop);
		void FindMotorcycleCrashes(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop);
		void ProcessLoopMergeEvents(std::vector<Event<Primitive>>& evnts);

		void AddVertexPathEdge(unsigned vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end);
		void UpdateLoopStage1(WavefrontLoop<Primitive>& loop);
		void UpdateLoopStage2(WavefrontLoop<Primitive>& loop);
	};

	T1(Primitive) void StraightSkeletonGraph<Primitive>::FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop)
	{
		if (loop._edges.size() <= 2) return;

		auto maxEventChain = Primitive(128);
		#if 0
			for (size_t e=0; e<loop._edges.size(); ++e) {
				const auto& seg0 = loop._edges[(e+loop._edges.size()-1)%loop._edges.size()];
				const auto& seg1 = loop._edges[e];
				const auto& seg2 = loop._edges[(e+1)%loop._edges.size()];
				assert(seg0._head == seg1._tail && seg1._head == seg2._tail);	// ensure segments are correctly ordered

				auto collapse = CalculateCollapseEvent<Primitive>(seg0._tail, seg1._tail, seg1._head, seg2._head, _vertices);
				if (!collapse) continue;

				assert(Equivalent(collapse.value(), seg1._collapsePt, GetEpsilon<Primitive>()));

				auto collapseTime = collapse.value()[2];
				// if (collapseTime < loop._lastEventBatchEarliest && loop._lastEventBatchEarliest <= loop._lastEventBatchLatest) continue;
				assert(collapseTime >= loop._lastEventBatchEarliest || loop._lastEventBatchEarliest > loop._lastEventBatchLatest);
				if (collapseTime < (earliestTime + maxEventChain * GetTimeEpsilon<Primitive>())) {
					events.push_back(Event<Primitive>::Collapse(collapse.value(), seg1._head, seg1._tail));
					earliestTime = std::min(collapseTime, earliestTime);
				}
			}
		#else
			for (const auto&e:loop._edges) {
				auto collapseTime = e._collapsePt[2];
				assert(collapseTime >= loop._lastEventBatchEarliest || loop._lastEventBatchEarliest > loop._lastEventBatchLatest);
				if (collapseTime < (earliestTime + maxEventChain * GetTimeEpsilon<Primitive>())) {
					events.push_back(Event<Primitive>::Collapse(loop._loopId, e._collapsePt, e._head, e._tail));
					earliestTime = std::min(collapseTime, earliestTime);
				}
			}
		#endif
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::FindMotorcycleCrashes(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop)
	{
		if (loop._edges.size() <= 2) return;

		auto maxEventChain = Primitive(128);
		#if 0
			for (const auto& m:loop._motorcycleSegments) {
				#if defined(_DEBUG)
					auto head = GetVertex<Primitive>(_vertices, m._motor);
					assert(!Equivalent(head.Velocity(), Zero<Vector2T<Primitive>>(), GetEpsilon<Primitive>()));
				#endif

				auto crashEventOpt = CalculateCrashEvent<Primitive>(m._motor, MakeIteratorRange(loop._edges), MakeIteratorRange(_loops), MakeIteratorRange(_vertices));
				if (!crashEventOpt) continue;
				auto crashEvent = crashEventOpt.value();
				// if (crashEvent._eventTime < loop._lastEventBatchEarliest && loop._lastEventBatchEarliest <= loop._lastEventBatchLatest) continue;
				assert(crashEvent._eventTime >= loop._lastEventBatchEarliest || loop._lastEventBatchEarliest > loop._lastEventBatchLatest);
				
				assert(Equivalent(PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime}, m._crashPt, GetEpsilon<Primitive>()));
				assert(crashEvent._edgeHead == m._edgeHead);
				assert(crashEvent._edgeTail == m._edgeTail);

				if (crashEvent._eventTime < (earliestTime + maxEventChain * GetTimeEpsilon<Primitive>())) {
					events.push_back(crashEvent);
					earliestTime = std::min(crashEvent._eventTime, earliestTime);
				}
			}
		#else
			for (const auto& m:loop._motorcycleSegments) {
				auto crashTime = m._crashPt[2];
				if (crashTime < loop._lastEventBatchEarliest && loop._lastEventBatchEarliest <= loop._lastEventBatchLatest) continue;
				if (crashTime < (earliestTime + maxEventChain * GetTimeEpsilon<Primitive>())) {
					events.push_back(Event<Primitive>::MotorcycleCrash(m._edgeLoop, m._crashPt, m._motor, m._edgeHead, m._edgeTail, loop._loopId));
					earliestTime = std::min(crashTime, earliestTime);
				}
			}
		#endif
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) void StraightSkeletonGraph<Primitive>::UpdateLoopStage1(WavefrontLoop<Primitive>& loop)
	{
		// The "velocity" value for newly created vertices will not have been updated yet; we needed
		// to wait until all crash events were processed before we did. But 
		if (loop._edges.size() <= 2) {
			return;
		}

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

				// Each reflex vertex in the graph must result in a "motocycle segment".
				// We already know the velocity of the head of the motorcycle; and it has a fixed tail that
				// stays at the original position
				if (v0._anchor0 != v0._anchor1) {
					bool hasMotorCycle = std::find_if(loop._motorcycleSegments.begin(), loop._motorcycleSegments.end(),
						[v=edge->_tail](const auto&c) { return c._motor == v; }) != loop._motorcycleSegments.end();
					assert(!hasMotorCycle);
					auto v0 = GetVertex<Primitive>(_vertices, prevEdge->_tail).PositionAtTime(calcTime);
					auto v1 = GetVertex<Primitive>(_vertices, edge->_tail).PositionAtTime(calcTime);
					auto v2 = GetVertex<Primitive>(_vertices, edge->_head).PositionAtTime(calcTime);
					auto windingType = CalculateWindingType(v0, v1, v2, GetEpsilon<Primitive>()).first;
					if (windingType == WindingType::Right || windingType == WindingType::Straight)
						loop._motorcycleSegments.emplace_back(MotorcycleSegment<Primitive>{edge->_tail});
				}
			}

			prevPrevEdge = prevEdge;
			prevEdge = edge;
		}
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::UpdateLoopStage2(WavefrontLoop<Primitive>& loop)
	{
		if (loop._edges.size() <= 2) {
			loop._motorcycleSegments.clear();
			for (auto& e:loop._edges) {
				e._collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
				_vertices[e._tail]._anchor1 = _vertices[e._tail]._anchor0;
			}
			return;
		}

		// Calculate collapses for all of the new edges
		auto prevPrevE = loop._edges.end()-2;
		auto prevE = loop._edges.end()-1;
		for (auto e=loop._edges.begin(); e!=loop._edges.end(); prevPrevE=prevE, prevE=e, ++e) {
			auto& seg0 = *prevPrevE, &seg1 = *prevE, &seg2 = *e;
			if (!seg1._pendingCalculate) continue;

			auto collapse = CalculateCollapseEvent<Primitive>(seg0._tail, seg1._tail, seg1._head, seg2._head, _vertices);
			if (collapse) {
				seg1._collapsePt = collapse.value();
			} else {
				seg1._collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
			}

			// We have to compare each motorcycle against this edge; and from there see if there's any better crash points
			// Ie; we're comparing all new edges vs all motorcycles (except for those which we'll do a full recalculate)
			// We can narrow down the list of loops we need to check by only looking at the containing loop, any contained loops
			// and siblings. But that's still a lot to check... so might as well just check them all 
			for (auto& loopToCheck:_loops) {
				for (auto m=loopToCheck._motorcycleSegments.begin(); m!=loopToCheck._motorcycleSegments.end(); ++m) {
					if (m->_pendingCalculate) continue;
					if (seg1._head == m->_motor || seg1._tail == m->_motor) { m->_pendingCalculate = true; continue; }
					if (m->_edgeHead == seg1._head || m->_edgeHead == seg1._tail || m->_edgeTail == seg1._head || m->_edgeTail == seg1._tail) { m->_pendingCalculate = true; continue; }
					
					auto crashOpt = BuildCrashEvent_SimultaneousV<Primitive>(MakeIteratorRange(_vertices), seg1._head, seg1._tail, m->_motor);
					if (crashOpt && crashOpt.value()._pointAndTime[2] < m->_crashPt[2]) {
						auto protoCrash = crashOpt.value();
						if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Head) {
							auto inAndOut = FindInAndOut(MakeIteratorRange(loopToCheck._edges), m->_motor);
							if (seg1._head == inAndOut.first->_tail) continue;		// reject crashes when there is a direct edge between the motor and the point
							m->_edgeHead = m->_edgeTail = seg1._head;
						} else if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Tail) {
							auto inAndOut = FindInAndOut(MakeIteratorRange(loopToCheck._edges), m->_motor);
							if (seg1._tail == inAndOut.second->_head) continue;		// reject crashes when there is a direct edge between the motor and the point
							m->_edgeHead = m->_edgeTail = seg1._tail;
						} else {
							m->_edgeHead = seg1._head;
							m->_edgeTail = seg1._tail;
						}

						m->_crashPt = protoCrash._pointAndTime;
						m->_edgeLoop = (loopToCheck._loopId != loop._loopId) ? loopToCheck._loopId : ~LoopId(0);
					}
				}
			}

			prevE->_pendingCalculate = false;
		}

		for (auto m=loop._motorcycleSegments.begin(); m!=loop._motorcycleSegments.end(); ++m) {
			// if (!m->_pendingCalculate) continue;
			auto crashEventOpt = CalculateCrashEvent<Primitive>(m->_motor, loop, MakeIteratorRange(_loops), MakeIteratorRange(_vertices));
			if (crashEventOpt) {
				auto crashEvent = crashEventOpt.value();
				m->_crashPt = {crashEvent._eventPt, crashEvent._eventTime};
				m->_edgeTail = crashEvent._edgeTail;
				m->_edgeHead = crashEvent._edgeHead;
				m->_edgeLoop = crashEvent._edgeLoop;
			} else {
				m->_crashPt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
				m->_edgeTail = ~VertexId(0);
				m->_edgeHead = ~VertexId(0);
				m->_edgeLoop = ~LoopId(0);
			}
			m->_pendingCalculate = false;
		}
	}

	T1(Primitive) auto StraightSkeletonGraph<Primitive>::GetLoop(LoopId id) -> typename std::vector<WavefrontLoop<Primitive>>::iterator
	{
		for (auto l=_loops.begin(); l!=_loops.end(); ++l)
			if (l->_loopId == id)
				return l;
		return _loops.end();
	}

	T1(Primitive) static bool ContainsVertex(IteratorRange<const WavefrontEdge<Primitive>*> edges, unsigned v)
	{
		auto headSideI = std::find_if(edges.begin(), edges.end(),
			[v](const auto& e) { return e._head == v || e._tail == v; });
		return headSideI != edges.end();
	}

	T1(Primitive) static void SetEdgeLoop(const WavefrontLoop<Primitive>& loop, Event<Primitive>& evnt)
	{
		if (evnt._type != EventType::MotorcycleCrash && evnt._type != EventType::MultiLoopMotorcycleCrash)
			assert(evnt._edgeHead != evnt._edgeTail);
		if (evnt._edgeHead != evnt._edgeTail) {
			auto q = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[evnt](const auto& e){ return e._head == evnt._edgeHead && e._tail == evnt._edgeTail; });
			assert(q != loop._edges.end());
		} else {
			auto q = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[evnt](const auto& e){ return e._head == evnt._edgeHead || e._tail == evnt._edgeHead; });
			assert(q != loop._edges.end());
		}
		assert(loop._loopId != ~0u);
		evnt._edgeLoop = loop._loopId;
	}

	T1(Primitive) static void SetMotorLoop(WavefrontLoop<Primitive>& loop, Event<Primitive>& evnt);

	T1(Primitive) struct CrashEventInfo
	{
		PointAndTime<Primitive> _crashPtAndTime;
		VertexId _crashSegmentTail, _crashSegmentHead, _motor;
		VertexId _tailSideReplacement, _headSideReplacement;
		WavefrontLoop<Primitive> _tailSide, _headSide;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _tin;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _tout;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _hin;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _hout;
	};

	T1(Primitive) static bool IsCrash(Event<Primitive>& e) { return e._type == EventType::MotorcycleCrash || e._type == EventType::MultiLoopMotorcycleCrash; }

	T1(Primitive) static void HandleEdgeSplit(
		std::vector<Event<Primitive>>& evnts,
		VertexId splitEdgeTail, VertexId splitEdgeHead,
		VertexId tailSideReplacement, VertexId headSideReplacement,
		const WavefrontLoop<Primitive>& tailSide, const WavefrontLoop<Primitive>& headSide,
		LoopId originalLoopId,
		Vector2T<Primitive> splitPt,
		VertexSet<Primitive> vertices)
	{
		std::vector<Event<Primitive>> additionalEventsToAdd;
		for (auto& e:evnts) {
			if (e._edgeHead == splitEdgeHead && e._edgeTail == splitEdgeTail) {
				bool useTailSidePart = false, useHeadSidePart = false;
				if (IsCrash(e)) {
					if (e._motorLoop == originalLoopId) {
						// Use the side that contains the motor
						useHeadSidePart = ContainsVertex<Primitive>(headSide._edges, e._motor);
						useTailSidePart = ContainsVertex<Primitive>(tailSide._edges, e._motor);
						assert((useHeadSidePart^useTailSidePart)==1);
					} else {
						// Determine based on the position of the crash
						auto v0 = GetVertex<Primitive>(vertices, splitEdgeHead).PositionAtTime(e._eventTime);
						auto v2 = GetVertex<Primitive>(vertices, splitEdgeTail).PositionAtTime(e._eventTime);
						if (Dot(e._eventPt - splitPt, v0 - splitPt) > 0) {
							useHeadSidePart = true;
						} else {
							useTailSidePart = true;
						}
					}
				} else {
					assert(e._type == EventType::Collapse);
					useTailSidePart = useHeadSidePart = true;	// both in collapse case
				}
				
				if (useHeadSidePart && useTailSidePart) {
					auto headSideEvent = e;
					headSideEvent._edgeTail = headSideReplacement;
					SetEdgeLoop(headSide, headSideEvent);
					additionalEventsToAdd.push_back(headSideEvent);
					e._edgeHead = tailSideReplacement;
					SetEdgeLoop(tailSide, e);
				} else if (useHeadSidePart) {
					e._edgeTail = headSideReplacement;
					SetEdgeLoop(headSide, e);
				} else if (useTailSidePart) {
					e._edgeHead = tailSideReplacement;
					SetEdgeLoop(tailSide, e);
				}
			}
		}
		evnts.insert(evnts.end(), additionalEventsToAdd.begin(), additionalEventsToAdd.end());
	}

	T1(Primitive) static void HandleRemovedVertex(
		std::vector<Event<Primitive>>& evnts,
		VertexId removedVertex,
		VertexId tailSideReplacement, VertexId headSideReplacement,
		const WavefrontLoop<Primitive>& tailSide, const WavefrontLoop<Primitive>& headSide,
		LoopId originalLoopId)
	{
		std::vector<Event<Primitive>> additionalEventsToAdd;
		for (auto e=evnts.begin(); e!=evnts.end();) {
			if (e->_edgeHead == removedVertex || e->_edgeTail == removedVertex) {
				
				bool useHeadSidePart = true;
				if (e->_edgeHead != removedVertex || e->_edgeTail != removedVertex) {
					if (e->_edgeHead != removedVertex) useHeadSidePart = ContainsVertex<Primitive>(headSide._edges, e->_edgeHead);
					else useHeadSidePart = ContainsVertex<Primitive>(headSide._edges, e->_edgeTail);

					if (IsCrash(*e) && e->_motorLoop == originalLoopId)
						assert(ContainsVertex<Primitive>(useHeadSidePart ? headSide._edges : tailSide._edges, e->_motor));		// validate that the side of the motor matches the side of the edge
				} else {
					assert(IsCrash(*e));
					if (e->_motorLoop == originalLoopId) {
						useHeadSidePart = ContainsVertex<Primitive>(headSide._edges, e->_motor);
					} else {
						assert(0);		// no way to determine whether headSideReplacement or tailSideReplacement is better
					}
				}
				
				if (useHeadSidePart) {
					e->_edgeHead = (e->_edgeHead == removedVertex) ? headSideReplacement : e->_edgeHead;
					e->_edgeTail = (e->_edgeTail == removedVertex) ? headSideReplacement : e->_edgeTail;
					SetEdgeLoop(headSide, *e);
				} else {
					e->_edgeHead = (e->_edgeHead == removedVertex) ? tailSideReplacement : e->_edgeHead;
					e->_edgeTail = (e->_edgeTail == removedVertex) ? tailSideReplacement : e->_edgeTail;
					SetEdgeLoop(tailSide, *e);
				}
			} else if (e->_motor == removedVertex) {
				e = evnts.erase(e);
				continue;
			}
			++e;
		}
		evnts.insert(evnts.end(), additionalEventsToAdd.begin(), additionalEventsToAdd.end());
	}

	T1(Primitive) static void PostProcessEventsForMotorcycleCrash(
		CrashEventInfo<Primitive>& crashInfo,
		const WavefrontLoop<Primitive>& originalLoop,
		std::vector<Event<Primitive>>& evnts,
		VertexSet<Primitive> vertices)
	{
		// We may have to rename the crash segments for any future crashes. We remove 1 vertex
		// from the system every time we process a motorcycle crash. So, if one of the upcoming
		// crash events involves this vertex, we have rename it to either the new vertex on the
		// inSide, or on the tailSide
		unsigned crashSegmentTail = crashInfo._crashSegmentTail, crashSegmentHead = crashInfo._crashSegmentHead;
		auto crashPtAndTime = crashInfo._crashPtAndTime;

		// In the single vertex collision case, crashSegmentHead has been removed from the simulation; remove it's motor... 
		if (crashSegmentHead == crashSegmentTail)
			for (auto pendingEvent=evnts.begin(); pendingEvent!=evnts.end();++pendingEvent)
				if (pendingEvent->_motor == crashSegmentHead) { evnts.erase(pendingEvent); break; }

		// Process the crashSegmentHead <-- crashSegmentTail edge first
		if (crashSegmentHead != crashSegmentTail) {
			HandleEdgeSplit(
				evnts, 
				crashSegmentTail, crashSegmentHead,
				crashInfo._tailSideReplacement, crashInfo._headSideReplacement,  
				crashInfo._tailSide, crashInfo._headSide, originalLoop._loopId,
				Truncate(crashPtAndTime), vertices);
		} else {
			HandleRemovedVertex(evnts, crashSegmentTail, crashInfo._tailSideReplacement, crashInfo._headSideReplacement, crashInfo._tailSide, crashInfo._headSide, originalLoop._loopId);
		}
		
		std::vector<Event<Primitive>> additionalEventsToAdd;
		for (auto pendingEvent=evnts.begin(); pendingEvent!=evnts.end();) {
			bool collisionEdgeHeadIsHeadSide = ContainsVertex<Primitive>(crashInfo._headSide._edges, pendingEvent->_edgeHead);
			bool collisionEdgeTailIsHeadSide = ContainsVertex<Primitive>(crashInfo._headSide._edges, pendingEvent->_edgeTail);

			if (pendingEvent->_type == EventType::MotorcycleCrash || pendingEvent->_type == EventType::MultiLoopMotorcycleCrash) {
				if (ContainsVertex<Primitive>(crashInfo._headSide._edges, pendingEvent->_motor)) {
					// We might have collided with the crashEvent._motor <---- hin edge
					if (pendingEvent->_edgeHead == crashInfo._motor) {
						if (pendingEvent->_edgeHead == pendingEvent->_edgeTail) {
							pendingEvent->_edgeHead = pendingEvent->_edgeTail = crashInfo._headSideReplacement;
						} else {
							pendingEvent->_edgeHead = crashInfo._headSideReplacement;
							assert(collisionEdgeTailIsHeadSide);
						}
					} else if (pendingEvent->_edgeTail == crashInfo._motor) {
						// if the motor is on the head side, but we're colliding with an edge that should be on the tail
						// side, then we're potentially in trouble. However, this is fine if the collision point is
						// directly on crashEvent._motor -- because in this case, crashEvent._motor <---- hin is interchangable
						// with tout <--- crashEvent._motor
						assert(Equivalent(PointAndTime<Primitive>{pendingEvent->_eventPt, pendingEvent->_eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
						assert(pendingEvent->_edgeHead != pendingEvent->_edgeTail);		// this case gets caught by the condition above
						assert(crashSegmentHead != crashSegmentTail);
						pendingEvent->_edgeHead = crashInfo._headSideReplacement;
						pendingEvent->_edgeTail = crashInfo._hin->_tail;
					} /*else if (pendingEvent->_edgeTail == crashSegmentTail) {
						// When the motor is headside, if there's another crash on the same segment it should be
						// in the pendingEvent->_edgeHead <--- headSideReplacement part.
						// We have to consider the cases where either crashEvent or pendingEvent are single vertex collisions, as well
						if (pendingEvent->_edgeHead == pendingEvent->_edgeTail) {
							pendingEvent->_edgeHead = pendingEvent->_edgeTail = crashInfo._headSideReplacement;
						} else {
							assert((crashSegmentHead == crashSegmentTail) || pendingEvent->_edgeHead == crashSegmentHead);
							pendingEvent->_edgeTail = crashInfo._headSideReplacement;
						}
					} */else if (!collisionEdgeHeadIsHeadSide || !collisionEdgeTailIsHeadSide) {
						// the motor is on headside, but the edge is on tail side, and the edge is unrelated to crashEvent
						// this can happen in extreme cases of many vertices colliding in the on the same point
						// We would have to change the edge into some completely different edge in order to process it; but 
						// it's probably actually redundant

						assert(collisionEdgeHeadIsHeadSide || ContainsVertex<Primitive>(crashInfo._tailSide._edges, pendingEvent->_edgeHead));
						assert(collisionEdgeTailIsHeadSide || ContainsVertex<Primitive>(crashInfo._tailSide._edges, pendingEvent->_edgeTail));

						auto pt0 = vertices[pendingEvent->_edgeHead].PositionAtTime(pendingEvent->_eventTime);
						auto pt1 = vertices[pendingEvent->_edgeTail].PositionAtTime(pendingEvent->_eventTime);
						auto pt2 = vertices[pendingEvent->_motor].PositionAtTime(pendingEvent->_eventTime);
						if (pendingEvent->_edgeHead == pendingEvent->_edgeTail) {
							assert(Equivalent(pt0, pt2, GetEpsilon<Primitive>()));
						} else {
							assert(CalculateWindingType(pt0, pt2, pt1, GetEpsilon<Primitive>()).first == WindingType::Straight);
						}
						assert(Equivalent(pt2, pendingEvent->_eventPt, GetEpsilon<Primitive>()));

						assert(Equivalent(PointAndTime<Primitive>{pendingEvent->_eventPt, pendingEvent->_eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
						pendingEvent = evnts.erase(pendingEvent);
						continue;
					}
					SetEdgeLoop(crashInfo._headSide, *pendingEvent);
				} else if (ContainsVertex<Primitive>(crashInfo._tailSide._edges, pendingEvent->_motor)) {
					// We might have collided with the tout <--- crashEvent._motor edge
					if (pendingEvent->_edgeTail == crashInfo._motor) {
						if (pendingEvent->_edgeHead == pendingEvent->_edgeTail) {
							pendingEvent->_edgeHead = pendingEvent->_edgeTail = crashInfo._tailSideReplacement;
						} else {
							pendingEvent->_edgeTail = crashInfo._tailSideReplacement;
							assert(!collisionEdgeHeadIsHeadSide);
						}
					} else if (pendingEvent->_edgeHead == crashInfo._motor) {
						assert(Equivalent(PointAndTime<Primitive>{pendingEvent->_eventPt, pendingEvent->_eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
						assert(pendingEvent->_edgeHead != pendingEvent->_edgeTail);
						assert(crashSegmentHead != crashSegmentTail);
						pendingEvent->_edgeHead = crashInfo._tout->_head;
						pendingEvent->_edgeTail = crashInfo._tailSideReplacement;
					} /*else if (pendingEvent->_edgeHead == crashSegmentHead) {
						if (pendingEvent->_edgeHead == pendingEvent->_edgeTail) {
							pendingEvent->_edgeHead = pendingEvent->_edgeTail = crashInfo._tailSideReplacement;
						} else {
							assert((crashSegmentHead == crashSegmentTail) || pendingEvent->_edgeTail == crashSegmentTail);
							pendingEvent->_edgeHead = crashInfo._tailSideReplacement;							
						}
					} */else if (collisionEdgeHeadIsHeadSide || collisionEdgeTailIsHeadSide) {
						assert(Equivalent(PointAndTime<Primitive>{pendingEvent->_eventPt, pendingEvent->_eventTime}, crashPtAndTime, GetEpsilon<Primitive>()));
						pendingEvent = evnts.erase(pendingEvent);
						continue;
					}
					SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
				} else {
					// this could be a motor on another loop colliding with an edge we've just modified
					if (pendingEvent->_edgeHead == crashInfo._motor) {
						pendingEvent->_edgeHead = crashInfo._headSideReplacement;
						SetEdgeLoop(crashInfo._headSide, *pendingEvent);
					} else if (pendingEvent->_edgeTail == crashInfo._motor) {
						pendingEvent->_edgeHead = crashInfo._tailSideReplacement;
						SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
					} /*else if (pendingEvent->_edgeHead == crashSegmentHead || pendingEvent->_edgeTail == crashSegmentTail) {
						// This case is particular difficult, because we don't know if the event
						// should be on the headSideReplacement -> crashSegmentHead side or crashSegmentTail -> tailSideReplacement
						// it's easier when colliding with an individual point
						if (crashSegmentHead == crashSegmentTail && pendingEvent->_edgeHead == crashSegmentHead) {
							pendingEvent->_edgeHead = crashInfo._tailSideReplacement;
							SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
						} else if (crashSegmentHead == crashSegmentTail && pendingEvent->_edgeTail == crashSegmentTail) {
							pendingEvent->_edgeTail = crashInfo._headSideReplacement;
							SetEdgeLoop(crashInfo._headSide, *pendingEvent);
						} else {
							assert(pendingEvent->_edgeTail == crashSegmentTail);
							assert(0);		// bail on this for now!
							pendingEvent = evnts.erase(pendingEvent);
							continue;
						}
					} */else if (pendingEvent->_edgeLoop == originalLoop._loopId) {
						if (collisionEdgeHeadIsHeadSide) {
							SetEdgeLoop(crashInfo._headSide, *pendingEvent);
						} else {
							SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
						}
					}
				}
			} else {
				assert(pendingEvent->_type == EventType::Collapse);
				if (pendingEvent->_edgeLoop == originalLoop._loopId) {
					if (pendingEvent->_edgeHead == crashInfo._motor && pendingEvent->_edgeTail == crashInfo._motor) {
						pendingEvent->_edgeHead = crashInfo._headSideReplacement;
						pendingEvent->_edgeTail = crashInfo._headSideReplacement;
						SetEdgeLoop(crashInfo._headSide, *pendingEvent);
					} else if (pendingEvent->_edgeHead == crashInfo._motor) {
						assert(collisionEdgeTailIsHeadSide);
						pendingEvent->_edgeHead = crashInfo._headSideReplacement;
						SetEdgeLoop(crashInfo._headSide, *pendingEvent);
					} else if (pendingEvent->_edgeTail == crashInfo._motor) {
						assert(!collisionEdgeHeadIsHeadSide);
						pendingEvent->_edgeTail = crashInfo._tailSideReplacement;
						SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
					} /*else if (pendingEvent->_edgeHead == crashSegmentHead && pendingEvent->_edgeTail == crashSegmentTail) {
						// we crashed into an edge that was pending a collapse, anyway
						auto headSideEvent = *pendingEvent;
						headSideEvent._edgeTail = crashInfo._headSideReplacement;
						SetEdgeLoop(crashInfo._headSide, headSideEvent);
						additionalEventsToAdd.push_back(headSideEvent);
						pendingEvent->_edgeHead = crashInfo._tailSideReplacement;
						SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
					} else if (crashSegmentHead == crashSegmentTail && pendingEvent->_edgeHead == crashSegmentHead) {
						assert(!collisionEdgeTailIsHeadSide);
						pendingEvent->_edgeHead = crashInfo._tailSideReplacement;
						SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
					} else if (crashSegmentHead == crashSegmentTail && pendingEvent->_edgeTail == crashSegmentHead) {
						assert(collisionEdgeHeadIsHeadSide);
						pendingEvent->_edgeTail = crashInfo._headSideReplacement;
						SetEdgeLoop(crashInfo._headSide, *pendingEvent);
					} */else {
						assert(collisionEdgeHeadIsHeadSide == collisionEdgeTailIsHeadSide);
						if (collisionEdgeHeadIsHeadSide) {
							SetEdgeLoop(crashInfo._headSide, *pendingEvent);
						} else {
							SetEdgeLoop(crashInfo._tailSide, *pendingEvent);
						}
					}
				}
			}

			++pendingEvent;
		}
		evnts.insert(evnts.end(), additionalEventsToAdd.begin(), additionalEventsToAdd.end());

		// Move the motorcycles from "loop" to "headSide" or "tailSide" depending on which loop 
		// they are now a part of.
		// Then we have to perform the exact same transformation on the MotorcycleSegment objects
		for (auto m:originalLoop._motorcycleSegments) {
			if (m._motor == crashInfo._motor) continue;		// (skip this one, it's just been processed)
			if (crashSegmentHead == crashSegmentTail && m._motor == crashSegmentHead) continue;
			if (ContainsVertex<Primitive>(crashInfo._headSide._edges, m._motor)) {
				if (!m._pendingCalculate) {
					if (m._edgeHead == crashInfo._motor) {
						if (m._edgeHead == m._edgeTail) {
							m._edgeHead = m._edgeTail = crashInfo._headSideReplacement;
						} else {
							m._edgeHead = crashInfo._headSideReplacement;
						}
					} else if (m._edgeTail == crashInfo._motor) {
						m._edgeHead = crashInfo._headSideReplacement;
						m._edgeTail = crashInfo._hin->_tail;
					} else if (m._edgeTail == crashSegmentTail) {
						if (m._edgeHead == m._edgeTail) {
							m._edgeHead = m._edgeTail = crashInfo._headSideReplacement;
						} else {
							m._edgeTail = crashInfo._headSideReplacement;
						}
					} else {
						// If the edge vertices are no longer here, we should recalculate the motor
						auto headI = std::find_if(crashInfo._headSide._edges.begin(), crashInfo._headSide._edges.end(),
							[v=m._edgeHead](const auto& c) { return c._head == v; });
						auto tailI = std::find_if(crashInfo._headSide._edges.begin(), crashInfo._headSide._edges.end(),
							[v=m._edgeTail](const auto& c) { return c._head == v; });
						if (headI == crashInfo._headSide._edges.end() || tailI == crashInfo._headSide._edges.end())
							m._pendingCalculate = true;
					}
				}
				crashInfo._headSide._motorcycleSegments.push_back(m);
			} else {
				assert(ContainsVertex<Primitive>(crashInfo._tailSide._edges, m._motor));
				if (!m._pendingCalculate) {
					if (m._edgeTail == crashInfo._motor) {
						if (m._edgeHead == m._edgeTail) {
							m._edgeHead = m._edgeTail = crashInfo._tailSideReplacement;
						} else {
							m._edgeTail = crashInfo._tailSideReplacement;
						}
					} else if (m._edgeHead == crashInfo._motor) {
						m._edgeHead = crashInfo._tout->_head;
						m._edgeTail = crashInfo._tailSideReplacement;
					} else if (m._edgeHead == crashSegmentHead) {
						if (m._edgeHead == m._edgeTail) {
							m._edgeHead = m._edgeTail = crashInfo._tailSideReplacement;
						} else {
							m._edgeHead = crashInfo._tailSideReplacement;							
						}
					} else {
						// If the edge vertices are no longer here, we should recalculate the motor
						auto headI = std::find_if(crashInfo._tailSide._edges.begin(), crashInfo._tailSide._edges.end(),
							[v=m._edgeHead](const auto& c) { return c._head == v; });
						auto tailI = std::find_if(crashInfo._tailSide._edges.begin(), crashInfo._tailSide._edges.end(),
							[v=m._edgeTail](const auto& c) { return c._head == v; });
						if (headI == crashInfo._tailSide._edges.end() || tailI == crashInfo._tailSide._edges.end())
							m._pendingCalculate = true;
					}
				}
				crashInfo._tailSide._motorcycleSegments.push_back(m);
			}
		}

		// todo -- we also have to update motorcycles on contained and containing loops
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessMotorcycleEvents(std::vector<Event<Primitive>>& evnts)
	{
		assert(!evnts.empty() && evnts.begin()->_type == EventType::MotorcycleCrash);

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

		auto crashEvent = *evnts.begin();
		evnts.erase(evnts.begin());

		auto& initialLoop = *GetLoop(crashEvent._edgeLoop);

		// The motor can collapse to become a vertex of the collision edge during earlier steps.
		if (crashEvent._motor == crashEvent._edgeHead || crashEvent._motor == crashEvent._edgeTail) {
			assert(std::find_if(initialLoop._motorcycleSegments.begin(), initialLoop._motorcycleSegments.end(), 
				[v=crashEvent._motor](const auto& c) { return c._motor == v; }) == initialLoop._motorcycleSegments.end());
			return;
		}

		assert(initialLoop._edges.size() > 2);
		if (initialLoop._edges.size() <= 2) return;

		if (crashEvent._edgeHead == crashEvent._edgeTail) {
			// Sometimes crash events are converted into what should really be a collapse event. In these cases,
			// there should also be a collapse event queued
			auto motorIn = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._head == motorHead; });
			auto motorOut = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._tail == motorHead; });
			if (motorIn->_tail == crashEvent._edgeHead || motorOut->_head == crashEvent._edgeHead) {
				auto i = std::find_if(initialLoop._motorcycleSegments.begin(), initialLoop._motorcycleSegments.end(), 
					[v=crashEvent._motor](const auto& c) { return c._motor == v; });
				if (i != initialLoop._motorcycleSegments.end()) i->_pendingCalculate = true;
				return;
			}
		}

		// We need to build 2 new WavefrontLoops -- one for the "tout" side and one for the "tin" side
		// In some cases, one side or the other than can be completely collapsed. But we're still going to
		// create it.
		CrashEventInfo<Primitive> crashInfo;
		crashInfo._crashPtAndTime = PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime};
		crashInfo._motor = crashEvent._motor;
		crashInfo._crashSegmentTail = crashEvent._edgeTail;
		crashInfo._crashSegmentHead = crashEvent._edgeHead;
		if (initialLoop._lastBatchIndex == _currentBatchIndex) {
			crashInfo._headSide._lastEventBatchEarliest = crashInfo._tailSide._lastEventBatchEarliest = std::min(crashEvent._eventTime, initialLoop._lastEventBatchEarliest);
		} else {
			crashInfo._headSide._lastEventBatchEarliest = crashInfo._tailSide._lastEventBatchEarliest = crashEvent._eventTime;
		}
		crashInfo._headSide._lastBatchIndex = crashInfo._tailSide._lastBatchIndex = _currentBatchIndex;
		crashInfo._headSide._lastEventBatchLatest = crashInfo._tailSide._lastEventBatchLatest = crashEvent._eventTime;

		if (crashInfo._tailSide._edges.size() > crashInfo._headSide._edges.size()) {
			crashInfo._tailSide._loopId = initialLoop._loopId;
			crashInfo._headSide._loopId = _nextLoopId++;
		} else {
			crashInfo._tailSide._loopId = _nextLoopId++;
			crashInfo._headSide._loopId = initialLoop._loopId;
		}

		//////////////////////////////////////////////////////////////////////
				//   T A I L   S I D E
		// Start at motor._head, and work around in order until we hit the crash segment.
		{
			auto tout = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(),
				[motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& test) { return test._tail == motorHead; });
			assert(tout != initialLoop._edges.end());
			auto tin = tout;

			if (tout->_head != crashEvent._edgeTail) {
				++tin;
				if (tin == initialLoop._edges.end()) tin = initialLoop._edges.begin();
				while (tin->_head!=crashEvent._edgeTail) {
					crashInfo._tailSide._edges.push_back(*tin);
					assert(crashInfo._tailSide._edges.size() <= initialLoop._edges.size());
					++tin;
					if (tin == initialLoop._edges.end()) tin = initialLoop._edges.begin();
				}

				if (crashEvent._edgeHead == crashEvent._edgeTail) {
					tin = (tin == initialLoop._edges.begin()) ? (initialLoop._edges.end()-1) : (tin-1);
				} else
					crashInfo._tailSide._edges.push_back(*tin);
			} else if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(tout->_head != crashEvent._edgeHead);
			}

			crashInfo._tailSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime});
			crashInfo._tailSide._edges.push_back({crashInfo._tailSideReplacement, tin->_head});
			crashInfo._tailSide._edges.push_back({tout->_head, crashInfo._tailSideReplacement});
			
			crashInfo._tin = tin;
			crashInfo._tout = tout;
		}

		//////////////////////////////////////////////////////////////////////
				//   H E A D   S I D E
		// Start at crashSegment._head, and work around in order until we hit the motor vertex
		{
			auto hout = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(),
				[crashEvent](const WavefrontEdge<Primitive>& test) { return test._tail == crashEvent._edgeHead; });
			assert(hout != initialLoop._edges.end());

			if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(hout->_head != crashEvent._motor);		// this causes all manner of chaos, but should only happen if this event should really be a collapse
				++hout;
				if (hout == initialLoop._edges.end()) hout = initialLoop._edges.begin();
			}
			auto hin = hout;
			while (hin->_head!=crashEvent._motor) {
				crashInfo._headSide._edges.push_back(*hin);
				assert(crashInfo._headSide._edges.size() <= initialLoop._edges.size());
				++hin;
				if (hin == initialLoop._edges.end()) hin = initialLoop._edges.begin();
			}

			crashInfo._headSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime});
			crashInfo._headSide._edges.push_back({crashInfo._headSideReplacement, hin->_tail});
			crashInfo._headSide._edges.push_back({hout->_tail, crashInfo._headSideReplacement});

			crashInfo._hin = hin;
			crashInfo._hout = hout;
		}

		if (crashInfo._tin->_tail != crashInfo._motor)
			FindInAndOut(MakeIteratorRange(crashInfo._tailSide._edges), crashInfo._tin->_tail).first->_pendingCalculate = true;
		if (crashInfo._tout->_head != crashInfo._motor)
			FindInAndOut(MakeIteratorRange(crashInfo._tailSide._edges), crashInfo._tout->_head).second->_pendingCalculate = true;
		if (crashInfo._hin->_tail != crashInfo._motor)
			FindInAndOut(MakeIteratorRange(crashInfo._headSide._edges), crashInfo._hin->_tail).first->_pendingCalculate = true;
		if (crashInfo._hout->_head != crashInfo._motor)
			FindInAndOut(MakeIteratorRange(crashInfo._headSide._edges), crashInfo._hout->_head).second->_pendingCalculate = true;

		// Since we're removing "motor.head" from the simulation, we have to add a skeleton edge 
		// for vertex path along the motor cycle path
		AddVertexPathEdge(crashEvent._motor, GetVertex<Primitive>(_vertices, crashEvent._motor)._anchor0, crashInfo._crashPtAndTime);

		if (crashEvent._edgeTail == crashEvent._edgeHead) {
			// This vertex got removed from the simulation, and we have to explicitly add a final vertex path edge for it
			AddVertexPathEdge(crashEvent._edgeHead, GetVertex<Primitive>(_vertices, crashEvent._edgeHead)._anchor0, crashInfo._crashPtAndTime);
		}

		//////////////////////////////////////////////////////////////////////
		PostProcessEventsForMotorcycleCrash<Primitive>(crashInfo, initialLoop, evnts, _vertices);

		// Overwrite "loop" with tailSide, and append inSide to the list of wavefront loops
		// crashSegment, motorIn & motorOut should not make it into either tailSide or headSide
		#if defined(_DEBUG)
			auto motorIn = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._head == motorHead; });
			auto motorOut = std::find_if(initialLoop._edges.begin(), initialLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._tail == motorHead; });
			for (auto e=crashInfo._tailSide._edges.begin(); e!=crashInfo._tailSide._edges.end(); ++e) {
				assert(e->_head != crashEvent._edgeHead || e->_tail != crashEvent._edgeTail);
				assert(e->_head != motorIn->_head || e->_tail != motorIn->_tail);
				assert(e->_head != motorOut->_head || e->_tail != motorOut->_tail);
				assert(e->_head != e->_tail);
				auto next = e+1; if (next == crashInfo._tailSide._edges.end()) next = crashInfo._tailSide._edges.begin();
				assert(e->_head == next->_tail);
			}
			for (auto e=crashInfo._headSide._edges.begin(); e!=crashInfo._headSide._edges.end(); ++e) {
				assert(e->_head != crashEvent._edgeHead || e->_tail != crashEvent._edgeTail);
				assert(e->_head != motorIn->_head || e->_tail != motorIn->_tail);
				assert(e->_head != motorOut->_head || e->_tail != motorOut->_tail);
				assert(e->_head != e->_tail);
				auto next = e+1; if (next == crashInfo._headSide._edges.end()) next = crashInfo._headSide._edges.begin();
				assert(e->_head == next->_tail);
			}
		#endif

		// We have to patch up loop ids throughout the system
		// The original containing loop must now contain all of the loops generated
		// Each loop that was contained within one of the loops here must be 
		if (crashInfo._tailSide._loopId == initialLoop._loopId) {
			initialLoop = std::move(crashInfo._tailSide);
			_loops.emplace_back(std::move(crashInfo._headSide));
		} else {			
			initialLoop = std::move(crashInfo._headSide);
			_loops.emplace_back(std::move(crashInfo._tailSide));
		}
	}

	T1(Primitive) struct CollapseGroupInfo 
	{ 
		unsigned _head, _tail, _headSideReplacement = ~0u, _tailSideReplacement = ~0u; 
		PointAndTime<Primitive> _crashPtAndTime = Zero<PointAndTime<Primitive>>(); 
	};

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessCollapseEvents(std::vector<Event<Primitive>>& evnts)
	{
		// Process the first collapse group on the pending events list, but include in any
		// collapses on the pending event list that are directly connected
		assert(!evnts.empty() && evnts.begin()->_type == EventType::Collapse);

		CollapseGroupInfo<Primitive> collapseGroupInfo;
		std::vector<Event<Primitive>> collapses;
		collapses.push_back(*evnts.begin());
		evnts.erase(evnts.begin());

		auto& loop = *GetLoop(collapses.begin()->_edgeLoop);

		#if defined(_DEBUG)
			assert(collapses[0]._edgeHead != collapses[0]._edgeTail);
			auto q = std::find_if(loop._edges.begin(), loop._edges.end(), 
				[c=collapses[0]](const auto& e){ return e._head == c._edgeHead && e._tail == c._edgeTail; });
			assert(q != loop._edges.end());
		#endif

		// go back as far as possible, from tail to tail
		auto searchingTail = collapses[0]._edgeTail;
		for (;;) {
			auto i = std::find_if(evnts.begin(), evnts.end(),
				[searchingTail](const auto& t) { return t._type == EventType::Collapse && t._edgeHead == searchingTail; });
			if (i == evnts.end()) break;

			assert(ContainsVertex<Primitive>(loop._edges, i->_edgeHead) && ContainsVertex<Primitive>(loop._edges, i->_edgeTail));
			searchingTail = i->_edgeTail;
			collapses.push_back(*i);
			evnts.erase(i);
		}

		// also go forward head to head
		auto searchingHead = collapses[0]._edgeHead;
		for (;;) {
			auto i = std::find_if(evnts.begin(), evnts.end(),
				[searchingHead](const auto& t) { return t._type == EventType::Collapse && t._edgeTail == searchingHead; });
			if (i == evnts.end()) break;

			assert(ContainsVertex<Primitive>(loop._edges, i->_edgeHead) && ContainsVertex<Primitive>(loop._edges, i->_edgeTail));
			searchingHead = i->_edgeHead;
			collapses.push_back(*i);
			evnts.erase(i);
		}
		collapseGroupInfo._head = searchingHead;
		collapseGroupInfo._tail = searchingTail;

		// find the final collapse point for this group of collapses
		Primitive earliestCollapseTime = std::numeric_limits<Primitive>::max(), latestCollapseTime = -std::numeric_limits<Primitive>::max();
		{
			Vector2T<Primitive> collisionPt(Primitive(0), Primitive(0));
			unsigned contributors = 0;
			for (size_t c=0; c<collapses.size(); ++c) {
				collisionPt += collapses[c]._eventPt;
				contributors += 1;
				earliestCollapseTime = std::min(earliestCollapseTime, collapses[c]._eventTime);
				latestCollapseTime = std::max(latestCollapseTime, collapses[c]._eventTime);
			}
			collisionPt /= Primitive(contributors);

			// Validate that our "collisionPt" is close to all of the collapsing points
			#if defined(_DEBUG)
				for (size_t c=0; c<collapses.size(); ++c) {
					auto one = GetVertex<Primitive>(_vertices, collapses[c]._edgeHead).PositionAtTime(earliestCollapseTime);
					auto two = GetVertex<Primitive>(_vertices, collapses[c]._edgeTail).PositionAtTime(earliestCollapseTime);
					// assert(Equivalent(one, collisionPt, GetEpsilon<Primitive>()));
					// assert(Equivalent(two, collisionPt, GetEpsilon<Primitive>()));
				}
			#endif

			collapseGroupInfo._crashPtAndTime = PointAndTime<Primitive>{collisionPt, earliestCollapseTime};
		}

		// We're removing vertices from active loops -- so, we must add their vertex path to the
		// output skeleton.
		// Note that since we're connecting both head and tail, we'll end up doubling up each edge
		std::vector<VertexId> collapsedVertices;
		collapsedVertices.reserve(collapses.size()*2);
		for (size_t c=0; c<collapses.size(); ++c) {
			collapsedVertices.push_back(collapses[c]._edgeTail);
			collapsedVertices.push_back(collapses[c]._edgeHead);
		}
		std::sort(collapsedVertices.begin(), collapsedVertices.end());
		auto collapsedVerticesEnd = std::unique(collapsedVertices.begin(), collapsedVertices.end());
		for (auto v=collapsedVertices.begin(); v!=collapsedVerticesEnd; ++v) {
			AddVertexPathEdge(*v, GetVertex<Primitive>(_vertices, *v)._anchor0, collapseGroupInfo._crashPtAndTime);

			// Also remove any motorcycles associated with these vertices (since they will be removed
			// from active loops, the motorcycle is no longer valid)
			auto m = std::find_if(loop._motorcycleSegments.begin(), loop._motorcycleSegments.end(),
				[q=*v](const MotorcycleSegment<Primitive>& seg) { return seg._motor == q; });
			if (m != loop._motorcycleSegments.end())
				loop._motorcycleSegments.erase(m);
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

		if (loop._edges.size() > 1 && collapseGroupInfo._head != collapseGroupInfo._tail) {				
			auto tail = FindInAndOut(MakeIteratorRange(loop._edges), collapseGroupInfo._tail).first;
			auto head = FindInAndOut(MakeIteratorRange(loop._edges), collapseGroupInfo._head).second;
			assert(tail != loop._edges.end() && head != loop._edges.end());
			assert(tail != head);
			
			tail->_pendingCalculate = true;
			head->_pendingCalculate = true;
			// FindInAndOut(MakeIteratorRange(loop._edges), tail->_tail).first->_pendingCalculate = true;
			// FindInAndOut(MakeIteratorRange(loop._edges), head->_head).second->_pendingCalculate = true;

			auto preTailPt = GetVertex<Primitive>(_vertices, tail->_tail).PositionAtTime(collapseGroupInfo._crashPtAndTime[2]);
			auto postHeadPt = GetVertex<Primitive>(_vertices, head->_head).PositionAtTime(collapseGroupInfo._crashPtAndTime[2]);
			if (CalculateWindingType<Primitive>(preTailPt, Truncate(collapseGroupInfo._crashPtAndTime), postHeadPt, GetEpsilon<Primitive>()).first == WindingType::Straight
				|| (Equivalent(preTailPt, Truncate(collapseGroupInfo._crashPtAndTime), GetEpsilon<Primitive>()) && Equivalent(postHeadPt, Truncate(collapseGroupInfo._crashPtAndTime), GetEpsilon<Primitive>()))) {
				// avoid creating 2 colinear edges. Instead we'll just create a single new edge spanning the gap created
				// Alternatively; we could create a vertex but mark it with a flag highlighting that it is colinear
				collapseGroupInfo._tailSideReplacement = tail->_tail;
				collapseGroupInfo._headSideReplacement = head->_head;
				tail->_head = head->_head;
				loop._edges.erase(head);
			} else {
				// create a new vertex in the graph to connect the edges to either side of the collapse
				auto newVertex = (unsigned)_vertices.size();
				_vertices.push_back({collapseGroupInfo._crashPtAndTime, collapseGroupInfo._crashPtAndTime});

				// reassign the edges on either side of the collapse group to
				// point to the new vertex
				tail->_head = newVertex;
				head->_tail = newVertex;
				collapseGroupInfo._headSideReplacement = newVertex;
				collapseGroupInfo._tailSideReplacement = newVertex;

				assert(tail->_head != tail->_tail);
				assert(head->_head != head->_tail);
			}

			// rename collapsed vertices in pending events
			for (auto pendingEvent=evnts.begin(); pendingEvent!=evnts.end();) {

				if (pendingEvent->_type == EventType::MotorcycleCrash || pendingEvent->_type == EventType::MultiLoopMotorcycleCrash) {
					if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_motor) != collapsedVerticesEnd) {
						if (collapseGroupInfo._headSideReplacement == collapseGroupInfo._tailSideReplacement) {
							// It's possible that we already have a motorcycle event from another collapse, which will be
							// replaced to the same thing
							auto existing = std::find_if(evnts.begin(), pendingEvent, 
								[v=collapseGroupInfo._headSideReplacement](const auto& c) { return c._motor == v; });
							if (existing != pendingEvent) {
								assert(Equivalent(existing->_eventPt, pendingEvent->_eventPt, GetEpsilon<Primitive>()));
								assert(Equivalent(existing->_eventTime, pendingEvent->_eventTime, GetEpsilon<Primitive>()));
								pendingEvent = evnts.erase(pendingEvent);
								continue;
							} else
								pendingEvent->_motor = collapseGroupInfo._headSideReplacement;
						} else
							pendingEvent->_motor = ~0u;
					}

					if (collapseGroupInfo._tailSideReplacement != collapseGroupInfo._headSideReplacement) {
						if (	std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeTail) != collapsedVerticesEnd
							|| 	std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeHead) != collapsedVerticesEnd) {
							// The there is an motorcycle crash on an edge that was at least partially collapsed. This is the cases where
							// the collapse event does not generate a vertex -- we just get one larger edge that covers the entire collapsed
							// area.
							// Either the edge for pendingEvent was entirely collapsed, or one vertex must be the pre-tail
							// (collapseGroupInfo._tailSideReplacement) or one vertex must the post-head (collapseGroupInfo._headSideReplacement).
							// In other words, whereever the collapse is, it must be within the new super edge from collapseGroupInfo._tailSideReplacement
							// to collapseGroupInfo._headSideReplacement
							pendingEvent->_edgeTail = collapseGroupInfo._tailSideReplacement;
							pendingEvent->_edgeHead = collapseGroupInfo._headSideReplacement;
						}
					} else {
						if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeTail) != collapsedVerticesEnd)
							pendingEvent->_edgeTail = collapseGroupInfo._headSideReplacement;
						if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeHead) != collapsedVerticesEnd)
							pendingEvent->_edgeHead = collapseGroupInfo._headSideReplacement;
					}
				} else {
					assert(pendingEvent->_type == EventType::Collapse);
					if (pendingEvent->_edgeLoop == loop._loopId) {
						if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeTail) != collapsedVerticesEnd)
							pendingEvent->_edgeTail = collapseGroupInfo._headSideReplacement;
						if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeHead) != collapsedVerticesEnd)
							pendingEvent->_edgeHead = collapseGroupInfo._tailSideReplacement;
					}
				}

				assert(pendingEvent->_edgeTail != ~0u);
				assert(pendingEvent->_edgeHead != ~0u);
				++pendingEvent;
			}

			if (collapseGroupInfo._headSideReplacement != collapseGroupInfo._tailSideReplacement) {
				// We can have either 0, 1 or 2 collapses between group._headSideReplacement <--- group._tailSideReplacement
				// If is because collapses to either side of the collapse group will be renamed to this
				// If we have 1, we remove it; if we a have 2, we remove the earlier and keep the later
				auto collapseEvent = evnts.end();
				unsigned matchCount = 0;
				for (auto c=evnts.begin(); c!=evnts.end(); ++c) {
					if (c->_type == EventType::Collapse && c->_edgeHead == collapseGroupInfo._headSideReplacement && c->_edgeTail == collapseGroupInfo._tailSideReplacement) {
						++matchCount;
						if (collapseEvent == evnts.end()) collapseEvent = c;
					}
				}
				assert(matchCount <= 2);
				if (collapseEvent != evnts.end()) evnts.erase(collapseEvent);
			} 

			// We have to clean up some possible cases for motorcycle events on removed vertices 
			for (auto pendingEvent=evnts.begin(); pendingEvent!=evnts.end();) {
				assert (pendingEvent->_edgeTail != ~0u && pendingEvent->_edgeHead != ~0u);
				assert(!(pendingEvent->_type == EventType::Collapse && pendingEvent->_edgeTail == pendingEvent->_edgeHead));
				assert(pendingEvent->_edgeLoop != loop._loopId || ContainsVertex<Primitive>(loop._edges, pendingEvent->_edgeHead) && ContainsVertex<Primitive>(loop._edges, pendingEvent->_edgeTail));
				if ((pendingEvent->_type == EventType::MotorcycleCrash || pendingEvent->_type == EventType::MultiLoopMotorcycleCrash) && pendingEvent->_motor == ~0u) {
					pendingEvent = evnts.erase(pendingEvent);
					continue;
				}
				++pendingEvent;
			}

			for (auto&m:loop._motorcycleSegments) {
				if (m._edgeHead == ~0u && m._edgeTail == ~0u) continue;
				if (collapseGroupInfo._tailSideReplacement != collapseGroupInfo._headSideReplacement) {
					if (	std::find(collapsedVertices.begin(), collapsedVerticesEnd, m._edgeTail) != collapsedVerticesEnd
						|| 	std::find(collapsedVertices.begin(), collapsedVerticesEnd, m._edgeHead) != collapsedVerticesEnd) {
						m._edgeTail = collapseGroupInfo._tailSideReplacement;
						m._edgeHead = collapseGroupInfo._headSideReplacement;
					}
				} else {
					if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, m._edgeTail) != collapsedVerticesEnd)
						m._edgeTail = collapseGroupInfo._headSideReplacement;
					if (std::find(collapsedVertices.begin(), collapsedVerticesEnd, m._edgeHead) != collapsedVerticesEnd)
						m._edgeHead = collapseGroupInfo._headSideReplacement;
				}
			}
		}

		if (loop._lastBatchIndex == _currentBatchIndex) {
			loop._lastEventBatchEarliest = std::min(loop._lastEventBatchEarliest, earliestCollapseTime);
		} else {
			loop._lastEventBatchEarliest = earliestCollapseTime;
			loop._lastBatchIndex = _currentBatchIndex;
		}
		loop._lastEventBatchLatest = latestCollapseTime;
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessLoopMergeEvents(std::vector<Event<Primitive>>& evnts)
	{
		assert(!evnts.empty() && evnts.begin()->_type == EventType::MultiLoopMotorcycleCrash);
		auto crashEvent = *evnts.begin();
		evnts.erase(evnts.begin());

		assert(crashEvent._motorLoop != crashEvent._edgeLoop);
		auto motorLoop = GetLoop(crashEvent._motorLoop);
		auto edgeLoop = GetLoop(crashEvent._edgeLoop);

		// This is like a normal motorcycle crash event, except that we take 2 loops as input and end up with one as output
		// headSideReplacement -> hout around to tin -> tailSideReplacement, then onto tout around to hin
		// the motor is removed from all loops
		CrashEventInfo<Primitive> crashInfo;
		crashInfo._crashPtAndTime = PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime};

		crashInfo._tailSideReplacement = (unsigned)_vertices.size();
		_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime});
		crashInfo._headSideReplacement = (unsigned)_vertices.size();
		_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime});

		std::vector<WavefrontEdge<Primitive>> newEdges;
		{
			auto edgeLoopOut = std::find_if(edgeLoop->_edges.begin(), edgeLoop->_edges.end(),
				[v=crashEvent._edgeTail](const WavefrontEdge<Primitive>& test) { return test._tail == v; });
			newEdges.push_back({edgeLoopOut->_head, crashInfo._headSideReplacement});
			auto edgeLoopIn = edgeLoopOut+1;
			if (edgeLoopIn == edgeLoop->_edges.end()) edgeLoopIn = edgeLoop->_edges.begin();
			while (edgeLoopIn->_head != crashEvent._edgeHead) {
				newEdges.push_back(*edgeLoopIn);
				++edgeLoopIn;
				if (edgeLoopIn == edgeLoop->_edges.end()) edgeLoopIn = edgeLoop->_edges.begin();
			}
			newEdges.push_back({crashInfo._tailSideReplacement, edgeLoopIn->_tail});
		}
		{
			auto motorLoopOut = std::find_if(motorLoop->_edges.begin(), motorLoop->_edges.end(),
				[v=crashEvent._motor](const WavefrontEdge<Primitive>& test) { return test._tail == v; });
			newEdges.push_back({motorLoopOut->_head, crashInfo._tailSideReplacement});
			auto motorLoopIn = motorLoopOut+1;
			if (motorLoopIn == motorLoop->_edges.end()) motorLoopIn = motorLoop->_edges.begin();
			while (motorLoopIn->_head != crashEvent._motor) {
				newEdges.push_back(*motorLoopIn);
				++motorLoopIn;
				if (motorLoopIn == motorLoop->_edges.end()) motorLoopIn = motorLoop->_edges.begin();
			}
			newEdges.push_back({crashInfo._headSideReplacement, motorLoopIn->_tail});
		}

		motorLoop->_edges = std::move(newEdges);
		motorLoop->_motorcycleSegments.insert(motorLoop->_motorcycleSegments.end(), edgeLoop->_motorcycleSegments.begin(), edgeLoop->_motorcycleSegments.end());

		if (motorLoop->_lastBatchIndex == edgeLoop->_lastBatchIndex) {
			motorLoop->_lastEventBatchEarliest = std::min(motorLoop->_lastEventBatchEarliest, edgeLoop->_lastEventBatchEarliest);
			motorLoop->_lastEventBatchLatest = std::min(motorLoop->_lastEventBatchLatest, edgeLoop->_lastEventBatchLatest);
		} else if (motorLoop->_lastBatchIndex < edgeLoop->_lastBatchIndex) {
			motorLoop->_lastEventBatchEarliest = edgeLoop->_lastEventBatchEarliest;
			motorLoop->_lastEventBatchLatest =  edgeLoop->_lastEventBatchLatest;
			motorLoop->_lastBatchIndex = edgeLoop->_lastBatchIndex;
		}

		// crashEvent._motor is frozen
		AddVertexPathEdge(crashEvent._motor, GetVertex<Primitive>(_vertices, crashEvent._motor)._anchor0, crashInfo._crashPtAndTime);
		for (auto m=motorLoop->_motorcycleSegments.begin(); m!=motorLoop->_motorcycleSegments.end(); ++m)
			if (m->_motor == crashEvent._motor) { motorLoop->_motorcycleSegments.erase(m); break; }
		// collision vertex is frozen if this is a single vertex collision
		if (crashEvent._edgeHead == crashEvent._edgeTail) {
			AddVertexPathEdge(crashEvent._edgeHead, GetVertex<Primitive>(_vertices, crashEvent._edgeHead)._anchor0, crashInfo._crashPtAndTime);
			for (auto m=motorLoop->_motorcycleSegments.begin(); m!=motorLoop->_motorcycleSegments.end(); ++m)
				if (m->_motor == crashEvent._edgeHead) { motorLoop->_motorcycleSegments.erase(m); break; }
		}

		// Update loop ids in all evnts and motorcycles
		for (auto& evnt:evnts) {
			if (evnt._edgeLoop == edgeLoop->_loopId) evnt._edgeLoop = motorLoop->_loopId;
			if (evnt._motorLoop == edgeLoop->_loopId) evnt._motorLoop = motorLoop->_loopId;
			if (evnt._type == EventType::MultiLoopMotorcycleCrash && evnt._edgeLoop == evnt._motorLoop)
				evnt._type = EventType::MotorcycleCrash;
		}			
		for (auto& l:_loops)
			for (auto& m:l._motorcycleSegments)
				if (m._edgeLoop == edgeLoop->_loopId)
					m._edgeLoop = motorLoop->_loopId;

		for (auto& evnt:evnts) {
			if (crashEvent._edgeHead != crashEvent._edgeTail) {
				if (evnt._edgeHead == crashEvent._edgeHead) {
					assert(evnt._edgeTail == crashEvent._edgeTail);
					// We need to replace this with either crashEvent._edgeHead <-- headSideReplacement
					// or tailSideReplacement <-- crashEvent._edgeTail. The clearest way to determine 
					// this is by looking at the crash location and seeing where it fits
					auto v0 = GetVertex<Primitive>(_vertices, crashEvent._edgeHead).PositionAtTime(evnt._eventTime);
					auto v2 = GetVertex<Primitive>(_vertices, crashEvent._edgeTail).PositionAtTime(evnt._eventTime);
					auto& splitPt = crashEvent._eventPt;
					auto d0 = evnt._eventPt - splitPt;
					if (Dot(d0, v0 - splitPt) > 0) {
						evnt._edgeTail = crashInfo._headSideReplacement;
					} else {
						evnt._edgeHead = crashInfo._tailSideReplacement;
					}
				}
			} else {
				// single vertex collision case
				if (evnt._edgeHead == crashEvent._edgeHead) evnt._edgeHead = crashInfo._tailSideReplacement;
				if (evnt._edgeTail == crashEvent._edgeHead) evnt._edgeTail = crashInfo._headSideReplacement;
			}

			if (evnt._edgeHead == crashEvent._motor && evnt._edgeTail == crashEvent._motor) {
				// less clear if it should be tail side or head side here
				assert(0);
				evnt._edgeHead = evnt._edgeTail = crashInfo._headSideReplacement;	
			} else if (evnt._edgeHead == crashEvent._motor) evnt._edgeHead = crashInfo._headSideReplacement;
			else if (evnt._edgeTail == crashEvent._motor) evnt._edgeTail = crashInfo._tailSideReplacement;
		}

		_loops.erase(edgeLoop);
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessEvents(std::vector<Event<Primitive>>& evnts)
	{
		// Keep processing events until there are no more to do
		while (!evnts.empty()) {
			if (evnts.begin()->_type == EventType::Collapse) {
				ProcessCollapseEvents(evnts);
			} else if (evnts.begin()->_type == EventType::MotorcycleCrash) {
				ProcessMotorcycleEvents(evnts);
			} else {
				assert(evnts.begin()->_type == EventType::MultiLoopMotorcycleCrash);
				// For a multicycle between 2 loops; we start off by merging the 2 loops
				// After the merge, we can handle this has a standard motorcycle crash
				ProcessLoopMergeEvents(evnts);
			}
		}
	}

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

	T1(Primitive) void StraightSkeletonGraph<Primitive>::AddVertexPathEdge(unsigned vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end)
	{
		_vertexPathEdges.push_back({vertex, begin, end});
	}

	T1(Primitive) StraightSkeleton<Primitive> StraightSkeletonGraph<Primitive>::CalculateSkeleton(Primitive maxTime)
	{
		for (;;) {
			std::vector<Event<Primitive>> events;
			Primitive earliestEvent = std::numeric_limits<Primitive>::max();
			for (auto l=_loops.begin(); l!=_loops.end(); ) {
				if (l->_lastBatchIndex == _currentBatchIndex) {
					UpdateLoopStage1(*l);
					UpdateLoopStage2(*l);
				}

				if (l->_edges.empty()) {
					l =_loops.erase(l);
					continue;
				}

				FindCollapses(events, earliestEvent, *l);
				FindMotorcycleCrashes(events, earliestEvent, *l);
				++l;
			}

			++_currentBatchIndex;

            // If we do not find any more events, the remaining wavefronts will expand infinitely.
            // This case isn't perfectly handled currently, we'll just complete the loop here if
            // it has started.  If it has not started, skip it.
            if (events.empty() || earliestEvent >= maxTime)
				break;

			// We will process events up to the point where the gap between subsequent events is large than GetTimeEpsilon
			std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
			auto end = events.begin()+1;
			for (;end != events.end(); ++end) {
				auto gap = end->_eventTime - (end-1)->_eventTime;
				if (gap > GetTimeEpsilon<Primitive>()) break;
			}
			events.erase(end, events.end());

			ProcessEvents(events);
		}

		StraightSkeleton<Primitive> result;
		result._boundaryPointCount = _boundaryPointCount;
		for (const auto& e:_vertexPathEdges)
			AddEdge(
				result,
				(e._vertex <  _boundaryPointCount) ? e._vertex : AddSteinerVertex(result, e._beginPt),
				AddSteinerVertex(result, e._endPt), 
				StraightSkeleton<Primitive>::EdgeType::VertexPath);
		for (const auto&l:_loops)
			WriteFinalEdges(result, l, (l._edges.size()<=2) ? l._lastEventBatchLatest : maxTime);
		return result;
	}
	
	T1(Primitive) void StraightSkeletonGraph<Primitive>::WriteFinalEdges(StraightSkeleton<Primitive>& result, const WavefrontLoop<Primitive>& loop, Primitive time)
	{
		for (auto i=loop._edges.begin(); i!=loop._edges.end(); ++i) {
			auto A = PointAndTime<Primitive>{_vertices[i->_head].PositionAtTime(time), time};
			auto B = PointAndTime<Primitive>{_vertices[i->_tail].PositionAtTime(time), time};
			auto v0 = AddSteinerVertex(result, A);
			auto v1 = AddSteinerVertex(result, B);
			if (v0 != v1)
				AddEdge(result, v0, v1, StraightSkeleton<Primitive>::EdgeType::Wavefront);
			AddEdge(
				result, 
				(i->_tail < _boundaryPointCount) ? i->_tail : AddSteinerVertex(result, _vertices[i->_tail]._anchor0), 
				v1, 
				StraightSkeleton<Primitive>::EdgeType::VertexPath);
		}
	}

	T1(Primitive) void StraightSkeletonCalculator<Primitive>::AddLoop(IteratorRange<const Vector2T<Primitive>*> vertices)
	{
		assert(vertices.size() >= 2);
		// Construct the starting point for the straight skeleton calculations
		// We're expecting the input vertices to be a closed loop, in counter-clockwise order
		// The first and last vertices should *not* be the same vertex; there is an implied
		// segment between the first and last.
		WavefrontLoop<Primitive> loop;
		loop._edges.reserve(vertices.size());
		_graph->_vertices.reserve(_graph->_vertices.size() + vertices.size());
		unsigned vertexOffset = (unsigned)_graph->_vertices.size();
		for (size_t v=0; v<vertices.size(); ++v) {
			loop._edges.emplace_back(WavefrontEdge<Primitive>{vertexOffset + unsigned((v+1)%vertices.size()), vertexOffset + unsigned(v)});
			_graph->_vertices.push_back(Vertex<Primitive>{PointAndTime<Primitive>{vertices[v], Primitive(0)}, PointAndTime<Primitive>{vertices[v], Primitive(0)}});
		}
		auto resultId = loop._loopId = _graph->_nextLoopId++;
		/*loop._containingLoop = containingLoop;
		if (containingLoop != ~0u)
			_graph->GetLoop(containingLoop)->_containedLoops.push_back(resultId);*/
		_graph->_loops.emplace_back(std::move(loop));
		_graph->_boundaryPointCount += vertices.size();
	}

	T1(Primitive) StraightSkeleton<Primitive> StraightSkeletonCalculator<Primitive>::Calculate(Primitive maxInset)
	{
		// Note that CalculateSkeleton is destructive -- we can't call it again later, or change the loop after beginning it
		return _graph->CalculateSkeleton(maxInset);
	}

	T1(Primitive) StraightSkeletonCalculator<Primitive>::StraightSkeletonCalculator()
	{
		_graph = std::make_unique<StraightSkeletonGraph<Primitive>>();
	}

	T1(Primitive) StraightSkeletonCalculator<Primitive>::~StraightSkeletonCalculator()
	{}

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

	template class StraightSkeleton<float>;
	template class StraightSkeleton<double>;
	template class StraightSkeletonCalculator<float>;
	template class StraightSkeletonCalculator<double>;

}
