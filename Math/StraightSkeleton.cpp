// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StraightSkeleton.h"
#include "StraightSkeleton_Internal.h"
#include "../Core/Exceptions.h"
#include <cmath>
#include <optional>

namespace XLEMath
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	using VertexId = unsigned;
	using LoopId = unsigned;
	using FaceId = unsigned;

	enum class VertexMotorcycleState { Uncalculated, NotMotor, Motor };

	T1(Primitive) struct Vertex
	{
		PointAndTime<Primitive>	_anchor0;
		PointAndTime<Primitive>	_anchor1;
		PointAndTime<Primitive>	_lastValidAnchor1;
		FaceId _insideFace = ~FaceId(0), _outsideFace = ~FaceId(0);

		VertexMotorcycleState _motorcycleState = VertexMotorcycleState::Uncalculated;

		Primitive InitialTime() const { return _anchor0[2]; }

		Vector2T<Primitive> PositionAtTime(Primitive time) const
		{
			if (_anchor1[2] == _anchor0[2]) return Truncate(_anchor0);		// bitwise comparison intended
			Primitive w1 = (time - _anchor0[2]) / (_anchor1[2] - _anchor0[2]);
			Primitive w0 = Primitive(1) - w1;
			return w0 * Truncate(_anchor0) + w1 * Truncate(_anchor1);
		}

		Vector2T<Primitive> PositionAtTimeUsingLastValid(Primitive time) const
		{
			if (_lastValidAnchor1[2] == _anchor0[2]) return Truncate(_anchor0);		// bitwise comparison intended
			Primitive w1 = (time - _anchor0[2]) / (_lastValidAnchor1[2] - _anchor0[2]);
			Primitive w0 = Primitive(1) - w1;
			return w0 * Truncate(_anchor0) + w1 * Truncate(_lastValidAnchor1);
		}

		Vector2T<Primitive> Velocity() const
		{
			if (_anchor1[2] == _anchor0[2]) return Zero<Vector2T<Primitive>>();		// bitwise comparison intended
			return (Truncate(_anchor1) - Truncate(_anchor0)) / (_anchor1[2] - _anchor0[2]);
		}

		Vertex(
			PointAndTime<Primitive>	anchor0,
			PointAndTime<Primitive>	anchor1,
			FaceId insideFace = ~FaceId(0), FaceId outsideFace = ~FaceId(0))
		: _anchor0(anchor0), _anchor1(anchor1), _lastValidAnchor1(anchor1), _insideFace(insideFace), _outsideFace(outsideFace)
		{}
	};

	T1(Primitive) using VertexSet = IteratorRange<const Vertex<Primitive>*>;

	T1(Primitive) struct WavefrontEdge
	{
		VertexId _head, _tail;
		PointAndTime<Primitive>	_collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
		bool _pendingCalculate = true;
	};

	enum class EventType { Collapse, MotorcycleCrash, None };
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
			if (motorLoop != ~LoopId(0)) result._motorLoop = motorLoop;
			return result;
		}
	};

	T1(Primitive) struct WavefrontLoop
	{
		std::vector<WavefrontEdge<Primitive>> _edges;
		Primitive _lastEventBatchEarliest = std::numeric_limits<Primitive>::max();
		Primitive _lastEventBatchLatest = std::numeric_limits<Primitive>::lowest();
		unsigned _lastBatchIndex = 0;
		LoopId _loopId = ~LoopId(0);
		Primitive _signOfInitialLoop = 0;
		Primitive _signedAreaAtLatestEvent = 0;
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

	T1(Primitive) static bool HasVertex(IteratorRange<const WavefrontEdge<Primitive>*> edges, VertexId v)
	{
		auto headSideI = std::find_if(edges.begin(), edges.end(),
			[v](const auto& e) { return e._head == v || e._tail == v; });
		return headSideI != edges.end();
	}

	T1(Primitive) static bool HasEdge(IteratorRange<const WavefrontEdge<Primitive>*> edges, VertexId head, VertexId tail)
	{
		if (head != tail) {
			auto q = std::find_if(b2e(edges), [head, tail](const auto& e){ return e._head == head && e._tail == tail; });
			return q != edges.end();
		} else {
			auto q = std::find_if(b2e(edges), [head](const auto& e){ return e._head == head || e._tail == head; });
			return q != edges.end();
		}
	}

	T1(Primitive)
		static Primitive CalculateSignedAreaAtTime(IteratorRange<const WavefrontEdge<Primitive>*> edges, VertexSet<Primitive> vSet, Primitive time)
	{
		if (edges.empty()) return 0;
		Primitive result = 0;
		auto tailPosition = GetVertex(vSet, (edges.end()-1)->_head).PositionAtTime(time);
		auto tailVertexId = (edges.end()-1)->_head;
		for (unsigned c=0; c<edges.size(); ++c) {
			assert(edges[c]._tail == tailVertexId);
			auto headPosition = GetVertex(vSet, edges[c]._head).PositionAtTime(time);
			result += (headPosition[0] - tailPosition[0]) * (headPosition[1] + tailPosition[1]);

			tailVertexId = edges[c]._head;
			tailPosition = headPosition;
		}
		assert(std::abs(result) < 1e12);
		return result / Primitive(2);
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

		auto pointAndTime = res.value();
		pointAndTime += Expand(p2, calcTime);

		// special case -- if two points are equivalent in the initial loop, we will
		// consider them to have crashed (even if they are supposed to move away from
		// each other). Sometimes this is useful, though, and the alternative of two
		// different loops with a shared point won't work.
		if (pointAndTime[2] < epsilon) return {};

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

		// Give a little additional tolerance for determining if it is a vertex to vertex motor. This can reduce the number
		// of vertices generated and sometimes prevents short edges. BuildCrashEvent_Simultaneous may find these cases more
		// accurately. We could also scale epsilon based on the the time passed from the edge and motor anchors
		const Primitive additionalSlop = 4;
		Primitive eSq = additionalSlop * epsilon * epsilon * edgeMagSq;
		if (d0Sq < -eSq || d1Sq < -eSq)
			return {};

		if (d0Sq < eSq) 		return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		else if (d1Sq < eSq) 	return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		else 					return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
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
		Primitive eSq = epsilon * epsilon * edgeMagSq;
		if (d0Sq < -eSq || d1Sq < -eSq)			// we need a little bit of tolerance here; because we can miss collisions if we test against zero (even though missing requires us to actually miss twice -- once on either edge to connecting to the vertex we're hitting)
			return {};

		if (d0Sq < eSq)			return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Head, pointAndTime };
		else if (d1Sq < eSq)	return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Tail, pointAndTime };
		else					return ProtoCrashEvent<Primitive> { ProtoCrashEvent<Primitive>::Type::Middle, pointAndTime };
	}

	T1(Primitive) static bool ConsiderStationary(const WavefrontLoop<Primitive>& loop)
	{
		bool tripwire = false; // ((loop._signedAreaAtLatestEvent > 0) != (loop._signOfInitialLoop > 0)) || (loop._signOfInitialLoop == 0);
		return loop._edges.size() <= 2 || tripwire;
	}

	T1(Primitive) static bool CheckForMotorcycles(const WavefrontLoop<Primitive>& loop0, const WavefrontLoop<Primitive>& loop1)
	{
		if (loop0._loopId == loop1._loopId) {
			// Any expanding loop can motorcycle into itself. And a expanding loop can have a contracting part, which
			// can motorcycle into itself (imagine an exaggerated crescent moon)
			return true;
		}

		// Two separate contracting loops shouldn't motorcycle -- (at least not until one has become stationary
		// and can be absorbed into the other)
		return loop0._signOfInitialLoop > 0 || loop1._signOfInitialLoop > 0;
	}

	T1(Primitive) static PointAndTime<Primitive> OffsetTime(PointAndTime<Primitive> input, Primitive offsetTime)
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
		if (res._type != EdgeCollapseType::Collapse) return {};
		if (res._pointAndTime[2] < 0) return {};		// this happens when an edge is expanding, not collapsing
		return OffsetTime(res._pointAndTime, calcTime);
	}

	T1(Primitive) static PointAndTime<Primitive> CalculateAnchor1(VertexId vm2i, VertexId vm1i, VertexId v0i, VertexId v1i, VertexId v2i, VertexSet<Primitive> vSet, Primitive calcTime)
	{
		auto vm2 = vSet[vm2i].PositionAtTime(calcTime);
		auto vm1 = vSet[vm1i].PositionAtTime(calcTime);
		auto v0 = vSet[v0i].PositionAtTime(calcTime);
		auto v1 = vSet[v1i].PositionAtTime(calcTime);
		auto v2 = vSet[v2i].PositionAtTime(calcTime);

		// "V" shape protection. If we attempt to calculate the velocity in these cases, we can't find it accurately
		// we often end up with vertices that fly off in weird directions. Once we end up with these weird colinear/flat V
		// shapes, the algorithm doesn't care where the vertices are on the line; and so we end up with weird results
		// These cases should either collapse or change due to a motorcycle crash essentially immediately, so zero
		// velocity should be fine
		// auto epsilon = GetEpsilon<Primitive>();
		// // auto magFactor = Primitive(4) / MagnitudeSquared(v1 - vm1);
		// Primitive magFactor = 1;
		// auto winding = CalculateWindingType(vm1, v0, v1, epsilon*magFactor);
		// if (winding.first == WindingType::FlatV)
		// 	return vSet[v0i]._anchor0;

		auto collapse0 = CalculateEdgeCollapse_Offset_ColinearTest<Primitive>(vm2-v0, vm1-v0, Zero<Vector2T<Primitive>>(), v1-v0);
		auto collapse1 = CalculateEdgeCollapse_Offset_ColinearTest<Primitive>(vm1-v0, Zero<Vector2T<Primitive>>(), v1-v0, v2-v0);

		if (collapse0._type != EdgeCollapseType::NonCollapse && collapse1._type != EdgeCollapseType::NonCollapse) {
			// the collapses should both be in the same direction, but let's choose the sooner one
			if (collapse0._pointAndTime[2] > 0 && collapse0._pointAndTime[2] < collapse1._pointAndTime[2]) {
				return collapse0._pointAndTime + Vector3T<Primitive>{v0, calcTime};
			} else {
				return collapse1._pointAndTime + Vector3T<Primitive>{v0, calcTime};
			}
		} else if (collapse0._type != EdgeCollapseType::NonCollapse) {
			return collapse0._pointAndTime + Vector3T<Primitive>{v0, calcTime};
		} else if (collapse1._type != EdgeCollapseType::NonCollapse) {
			return collapse1._pointAndTime + Vector3T<Primitive>{v0, calcTime};
		} else {
			// Some edges won't collapse (due to parallel edges, etc). We will attempt to get a velocity
			// by looking at the vertex in isolation
			auto velocity = CalculateVertexVelocity_LineIntersection(vm1, v0, v1, Primitive(1));
			if (velocity)
				return vSet[v0i]._anchor0 + PointAndTime<Primitive>{velocity.value(), Primitive(1)};
			return vSet[v0i]._anchor0;
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) struct CrashEventInfo;

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
		std::vector<std::pair<VertexId, VertexId>> _originalBoundaryEdges;
		std::vector<std::pair<FaceId, FaceId>> _mergedFaces;

		std::vector<Event<Primitive>> _futureEvents;
		std::vector<Event<Primitive>> _processedEvents;

		StraightSkeleton<Primitive> CalculateSkeleton(Primitive maxTime);
		typename std::vector<WavefrontLoop<Primitive>>::iterator GetLoop(LoopId id);

		const Vertex<Primitive>& GetVertex(VertexId idx) const { return XLEMath::GetVertex<Primitive>(_vertices, idx); }

		static constexpr Primitive s_maxEventChain = Primitive(16);

	private:
		void WriteFinalEdges(StraightSkeleton<Primitive>& dest, const WavefrontLoop<Primitive>& loop, Primitive time);

		void ProcessEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime);
		void ProcessMotorcycleEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime);
		void ProcessCollapseEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime);
		void ProcessLoopMergeEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime);

		void FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop);

		void PostProcessEventsForMotorcycleCrash_Phase0(CrashEventInfo<Primitive>& crashInfo, std::vector<Event<Primitive>>& evnts);
		void PostProcessEventsForMotorcycleCrash_Phase1(CrashEventInfo<Primitive>& crashInfo, std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime);

		struct NewMotorcycle;
		void UpdateLoopStage1(WavefrontLoop<Primitive>& loop, std::vector<NewMotorcycle>& newMotorcycles);
		void UpdateLoopStage2(WavefrontLoop<Primitive>& loop, bool updatedLoop, IteratorRange<const NewMotorcycle*> newMotorcycles);

		void AddVertexPathEdge(VertexId vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end);
	};

	T1(Primitive) void StraightSkeletonGraph<Primitive>::FindCollapses(std::vector<Event<Primitive>>& events, Primitive& earliestTime, const WavefrontLoop<Primitive>& loop)
	{
		if (loop._edges.size() <= 2) return;

		// loops that have reached the limit of precision will have their winding order inverted
		// We have to be careful of loops expanding infinitely after this point
		bool tripwire = (loop._signedAreaAtLatestEvent > 0) != (loop._signOfInitialLoop > 0);
		if (tripwire) return;

		for (const auto&e:loop._edges) {
			auto collapseTime = e._collapsePt[2];
			// Protect against processing collapses from the past. These might be a result of precision errors. We could also consider just collapsing
			// the vertex now to the average of the two positions
			Primitive mustBeAfter = std::max(loop._lastEventBatchLatest, loop._lastEventBatchLatest);
			Primitive mustBeBefore = earliestTime + s_maxEventChain * GetTimeEpsilon<Primitive>();
			if (mustBeAfter < collapseTime && collapseTime < mustBeBefore) {
				events.push_back(Event<Primitive>::Collapse(loop._loopId, e._collapsePt, e._head, e._tail));
				earliestTime = std::min(collapseTime, earliestTime);
			}
		}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Primitive) struct StraightSkeletonGraph<Primitive>::NewMotorcycle
	{
		const WavefrontLoop<Primitive>* _loop = nullptr;
		VertexId _motor = VertexId(~0u);
	};

	T1(Primitive) void StraightSkeletonGraph<Primitive>::UpdateLoopStage1(WavefrontLoop<Primitive>& loop, std::vector<NewMotorcycle>& newMotorcycles)
	{
		// The "velocity" value for newly created vertices will not have been updated yet; we needed
		// to wait until all crash events were processed before we did. But
		if (ConsiderStationary(loop))
			return;

		auto prevPrevEdge = loop._edges.end()-2;
		auto prevEdge = loop._edges.end()-1;
		for (auto edge=loop._edges.begin(); edge!=loop._edges.end(); ++edge) {
			assert(edge->_head != edge->_tail);
			assert(prevEdge->_head == edge->_tail);
			auto& v0 = _vertices[edge->_tail];
			if (v0._anchor0 == v0._anchor1) {
				assert(v0._motorcycleState == VertexMotorcycleState::Uncalculated);
				auto next = edge+1;
				if (next == loop._edges.end()) next = loop._edges.begin();
				// we must calculate the velocity at the max initial time -- (this should always be the crash time)
				auto calcTime = v0.InitialTime();
				// assert(calcTime >= loop._lastEventBatchLatest && calcTime <= loop._lastEventBatchEarliest);
				v0._anchor1 = CalculateAnchor1<Primitive>(
					prevPrevEdge->_tail, prevEdge->_tail, edge->_tail, edge->_head, next->_head,
					_vertices, calcTime);

				// if the anchors only differ in the time element, we'll consider them identical
				if (v0._anchor0[0] == v0._anchor1[0] && v0._anchor0[1] == v0._anchor1[1])		// binary comparison intended
					v0._anchor1 = v0._anchor0;

				// Each reflex vertex or colinear vertex in the graph must result in a "motorcycle segment".
				// Colinear vertices can motorcycle into parallel edges (at which point there's often a series of instantaneous motorcycles)
				// We already know the velocity of the head of the motorcycle; and it has a fixed tail that
				// stays at the original position
				if (v0._anchor0 != v0._anchor1) {
					v0._lastValidAnchor1 = v0._anchor1;

					auto vp0 = GetVertex(prevEdge->_tail).PositionAtTime(calcTime);
					auto vp1 = GetVertex(edge->_tail).PositionAtTime(calcTime);
					auto vp2 = GetVertex(edge->_head).PositionAtTime(calcTime);
					auto windingType = CalculateWindingType(vp0, vp1, vp2, GetEpsilon<Primitive>()).first;
					v0._motorcycleState = (windingType != WindingType::Left) ? VertexMotorcycleState::Motor : VertexMotorcycleState::NotMotor;
					if (v0._motorcycleState == VertexMotorcycleState::Motor)
						newMotorcycles.emplace_back(NewMotorcycle{&loop, edge->_tail});
				} else {
					// If you hit this, it means there are stationary vertices in the input. That might lead to precision errors, and it's
					// better to sanitize the loop and remove them
					assert(v0._anchor0[2] > 0.f);
				}
			}

			prevPrevEdge = prevEdge;
			prevEdge = edge;
		}
	}

	T1(Primitive) static std::optional<Event<Primitive>> AsMotorcycleCrash(const ProtoCrashEvent<Primitive>& protoCrash, VertexId edgeTail, VertexId edgeHead, LoopId edgeLoop, VertexId motor, const WavefrontLoop<Primitive>& motorLoop)
	{
		Event<Primitive> event;
		event._eventPt = Truncate(protoCrash._pointAndTime);
		event._eventTime = protoCrash._pointAndTime[2];
		event._edgeLoop = edgeLoop;
		event._motor = motor;
		event._motorLoop = motorLoop._loopId;
		event._type = EventType::MotorcycleCrash;
		if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Head) {
			auto inAndOut = FindInAndOut(MakeIteratorRange(motorLoop._edges), motor); assert(inAndOut.first != motorLoop._edges.end()); assert(inAndOut.second != motorLoop._edges.end());
			if (edgeHead == inAndOut.first->_tail) return {};		// reject crashes when there is a direct edge between the motor and the point
			event._edgeHead = event._edgeTail = edgeHead;
		} else if (protoCrash._type == ProtoCrashEvent<Primitive>::Type::Tail) {
			auto inAndOut = FindInAndOut(MakeIteratorRange(motorLoop._edges), motor); assert(inAndOut.first != motorLoop._edges.end()); assert(inAndOut.second != motorLoop._edges.end());
			if (edgeTail == inAndOut.second->_head) return {};		// reject crashes when there is a direct edge between the motor and the point
			event._edgeHead = event._edgeTail = edgeTail;
		} else {
			event._edgeHead = edgeHead;
			event._edgeTail = edgeTail;
		}
		assert(event._motor != event._edgeHead && event._motor != event._edgeTail);
		return event;
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::UpdateLoopStage2(WavefrontLoop<Primitive>& loop, bool updatedLoop, IteratorRange<const NewMotorcycle*> newMotorcycles)
	{
		if (ConsiderStationary(loop)) {
			// remove all motorcycle crash events against this loop
			if (updatedLoop) {
				auto i = std::remove_if(b2e(_futureEvents), [loopId=loop._loopId](const Event<Primitive>& q) { return q._motorLoop == loopId || q._edgeLoop == loopId; });
				_futureEvents.erase(i, _futureEvents.end());
			}
			return;
		}

		// Calculate collapses for all of the new edges
		bool possibleFlattenedLoop = false;
		std::vector<Event<Primitive>> newEvents;

		{
			auto prevPrevE = loop._edges.end()-2;
			auto prevE = loop._edges.end()-1;
			for (auto e=loop._edges.begin(); e!=loop._edges.end(); prevPrevE=prevE, prevE=e, ++e) {
				auto& seg0 = *prevPrevE, &seg1 = *prevE, &seg2 = *e;

				if (seg1._pendingCalculate) {
					auto collapse = CalculateCollapseEvent<Primitive>(seg0._tail, seg1._tail, seg1._head, seg2._head, _vertices);
					if (collapse) {
						seg1._collapsePt = collapse.value();
					} else {
						seg1._collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
					}
				}

				// If our neighbors are identical, they were probably produced in a loop merge operation. When this happens, the loop
				// is actually zero-area, but may have bends in it (like a line with a kink). We have to be careful with these cases,
				// because although they rarely result in collapses, they can result in motorcycle (even between two of these flat loops)
				possibleFlattenedLoop |= Truncate(_vertices[seg0._tail]._anchor0) == Truncate(_vertices[seg1._head]._anchor0);		// binary comparison intended
			}
		}

		auto TestMotorcycle = [this, &newEvents](const WavefrontEdge<Primitive>& seg1, VertexId motor, const WavefrontLoop<Primitive>& motorLoop, const WavefrontLoop<Primitive>& segmentLoop) {

			auto& motorv = GetVertex(motor);
			if (motorv._motorcycleState != VertexMotorcycleState::Motor) return;
			if (motor == seg1._head || motor == seg1._tail) return;		// don't motorcycle into yourself

			// Boundary check, to reduce crash event calculations... We have an idea of the event horizon
			// for the motor based on previous crash event calculations & the motor's edge collapses.
			// If the edge is too far, we can just skip it

			Primitive mustBeAfter = std::max(motorLoop._lastEventBatchLatest, segmentLoop._lastEventBatchLatest);
			Primitive mustBeBefore = seg1._collapsePt[2] + GetTimeEpsilon<Primitive>();

			const bool doBoundaryTest = true;
			if (doBoundaryTest) {
				auto motorMins = motorv.PositionAtTime(mustBeAfter - 100*GetEpsilon<Primitive>()), motorMaxs = motorv.PositionAtTime(mustBeBefore + 100*GetEpsilon<Primitive>());
				if (motorMins[0] > motorMaxs[0]) std::swap(motorMins[0], motorMaxs[0]);
				if (motorMins[1] > motorMaxs[1]) std::swap(motorMins[1], motorMaxs[1]);
				Vector2T<Primitive> segMins, segMaxs;
				{
					segMins = segMaxs = _vertices[seg1._head].PositionAtTime(mustBeAfter - 100*GetEpsilon<Primitive>());
					auto t = _vertices[seg1._head].PositionAtTime(mustBeBefore + 100*GetEpsilon<Primitive>());
					segMins[0] = std::min(segMins[0], t[0]); segMins[1] = std::min(segMins[1], t[1]);
					segMaxs[0] = std::max(segMaxs[0], t[0]); segMaxs[1] = std::max(segMaxs[1], t[1]);
					t = _vertices[seg1._tail].PositionAtTime(mustBeAfter - 100*GetEpsilon<Primitive>());
					segMins[0] = std::min(segMins[0], t[0]); segMins[1] = std::min(segMins[1], t[1]);
					segMaxs[0] = std::max(segMaxs[0], t[0]); segMaxs[1] = std::max(segMaxs[1], t[1]);
					t = _vertices[seg1._tail].PositionAtTime(mustBeBefore + 100*GetEpsilon<Primitive>());
					segMins[0] = std::min(segMins[0], t[0]); segMins[1] = std::min(segMins[1], t[1]);
					segMaxs[0] = std::max(segMaxs[0], t[0]); segMaxs[1] = std::max(segMaxs[1], t[1]);
				}

				if (motorMins[0] > segMaxs[0] || motorMaxs[0] < segMins[0] || motorMins[1] > segMaxs[1] || motorMaxs[1] < segMins[1])
					return;
			}

			std::optional<ProtoCrashEvent<Primitive>> protoCrash;
			
			// "BuildCrashEvent_SimultaneousV" seems to do better here in the presence of near-colinear edges
			// since we use the vertex velocity we've already calculated, it takes into account all of the colinear protections
			// The downside is any floating point precision we picked up from there will impact the crash location calculation
			// In some cases, BuildCrashEvent_Simultaneous can identify vertex to vertex motors better
			protoCrash = BuildCrashEvent_SimultaneousV<Primitive>(MakeIteratorRange(_vertices), seg1._head, seg1._tail, motor);
			/*protoCrash = BuildCrashEvent_Simultaneous<Primitive>(
				MakeIteratorRange(_vertices), seg1._head, seg1._tail,
				motorLoop._edges[(m-motorLoop._edges.begin()+motorLoop._edges.size()-1)%motorLoop._edges.size()]._tail,
				motor, m->_head);*/

			if (protoCrash) {
				// We must ensure the crash is in a valid time range. It's possible to calculate a motorcycle in the past
				// This must be rejected, because it can lead to infinite loops.
				if (mustBeAfter < protoCrash->_pointAndTime[2] && protoCrash->_pointAndTime[2] < mustBeBefore)
					if (auto e = AsMotorcycleCrash(*protoCrash, seg1._tail, seg1._head, segmentLoop._loopId, motor, motorLoop))
						newEvents.emplace_back(std::move(*e));
			}

		};

		if (!possibleFlattenedLoop) {

			auto prevPrevE = loop._edges.end()-2;
			auto prevE = loop._edges.end()-1;
			for (auto e=loop._edges.begin(); e!=loop._edges.end(); prevPrevE=prevE, prevE=e, ++e) {
				auto& seg0 = *prevPrevE, &seg1 = *prevE, &seg2 = *e;
				if (seg1._pendingCalculate && updatedLoop) {

					// We have to compare each motorcycle against this edge; and from there see if there's any better crash points
					// Ie; we're comparing all new edges vs all motorcycles (except for those which we'll do a full recalculate)
					// We can narrow down the list of loops we need to check by only looking at the containing loop, any contained loops
					// and siblings. But that's still a lot to check... so might as well just check them all
					for (auto& motorLoop:_loops) {
						if (ConsiderStationary(motorLoop) || !CheckForMotorcycles(loop, motorLoop)) continue;
						for (auto m=motorLoop._edges.begin(); m!=motorLoop._edges.end(); ++m)
							TestMotorcycle(seg1, m->_tail, motorLoop, loop);
					}

					seg1._pendingCalculate = false;

				} else {

					//  This is not a new edge. Check for motorcycles, but only against the new motorcycles
					assert(!seg1._pendingCalculate);
					for (auto v:newMotorcycles) {
						auto& motorLoop = *v._loop;
						if (ConsiderStationary(motorLoop) || !CheckForMotorcycles(loop, motorLoop)) continue;
						TestMotorcycle(seg1, v._motor, motorLoop, loop);
					}

				}
			}

		} else {

			// delete events associated with this loop for safety
			for (auto& e:loop._edges) {
				e._collapsePt = PointAndTime<Primitive>{0,0,std::numeric_limits<Primitive>::max()};
				e._pendingCalculate = false;
			}
			auto i = std::remove_if(b2e(_futureEvents), [id=loop._loopId](const auto& q) { return q._edgeLoop==id||q._motorLoop==id; });
			_futureEvents.erase(i, _futureEvents.end());

		}

		if (!newEvents.empty()) {
			std::stable_sort(b2e(newEvents), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
			auto midway = _futureEvents.size();
			_futureEvents.insert(_futureEvents.end(), newEvents.begin(), newEvents.end());
			std::inplace_merge(_futureEvents.begin(), _futureEvents.begin()+midway, _futureEvents.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
		}
	}

	T1(Primitive) StraightSkeleton<Primitive> StraightSkeletonGraph<Primitive>::CalculateSkeleton(Primitive maxTime)
	{
		for (;;) {
			std::vector<Event<Primitive>> collapseEvents;		// collapse events are retained -- we rebuild this list every step
			Primitive earliestEvent = std::numeric_limits<Primitive>::max();
			std::vector<NewMotorcycle> newMotorcycles;
			for (auto l=_loops.begin(); l!=_loops.end(); ++l)
				if (l->_lastBatchIndex == _currentBatchIndex)
					UpdateLoopStage1(*l, newMotorcycles);

			for (auto l=_loops.begin(); l!=_loops.end(); ++l) {
				UpdateLoopStage2(*l, l->_lastBatchIndex == _currentBatchIndex, newMotorcycles);
				FindCollapses(collapseEvents, earliestEvent, *l);
			}

			_loops.erase(std::remove_if(b2e(_loops), [](const auto& q) { return q._edges.empty(); }), _loops.end());

			++_currentBatchIndex;

			// We will process events up to the point where the gap between subsequent events is larger than GetTimeEpsilon
			// Motorcycle events are already in our "_events" list... but we have to merge in the collapse events we just calculated
			std::sort(collapseEvents.begin(), collapseEvents.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
			Primitive cutoff = maxTime;
			std::vector<Event<Primitive>> mergedEvents;
			{
				mergedEvents.reserve(_futureEvents.size() + collapseEvents.size());
				auto i = _futureEvents.begin(), i2 = collapseEvents.begin();
				while (i!=_futureEvents.end() && i2!=collapseEvents.end()) {
					if (i->_eventTime < i2->_eventTime) {
						if (i->_eventTime > cutoff) break;
						if (!mergedEvents.empty() && (i->_eventTime-mergedEvents.back()._eventTime) > GetTimeEpsilon<Primitive>()) break;
						cutoff = std::min(cutoff, i->_eventTime+s_maxEventChain*GetTimeEpsilon<Primitive>());
						mergedEvents.push_back(*i);
						++i;
					} else {
						if (i2->_eventTime > cutoff) break;
						if (!mergedEvents.empty() && (i2->_eventTime-mergedEvents.back()._eventTime) > GetTimeEpsilon<Primitive>()) break;
						cutoff = std::min(cutoff, i2->_eventTime+s_maxEventChain*GetTimeEpsilon<Primitive>());
						mergedEvents.push_back(*i2);
						++i2;
					}
				}
				while (i2!=collapseEvents.end()) {
					if (i2->_eventTime > cutoff) break;
					if (!mergedEvents.empty() && (i2->_eventTime-mergedEvents.back()._eventTime) > GetTimeEpsilon<Primitive>()) break;
					cutoff = std::min(cutoff, i2->_eventTime+s_maxEventChain*GetTimeEpsilon<Primitive>());
					mergedEvents.push_back(*i2);
					++i2;
				}
				if (!mergedEvents.empty()) cutoff = mergedEvents.back()._eventTime;
				mergedEvents.insert(mergedEvents.end(), i, _futureEvents.end());
			}

			_futureEvents = std::move(mergedEvents);

			// If we do not find any more events, the remaining wavefronts will expand infinitely.
			// This case isn't perfectly handled currently, we'll just complete the loop here if
			// it has started.  If it has not started, skip it.
			if (_futureEvents.empty() || _futureEvents.front()._eventTime >= maxTime)
				break;

			ProcessEvents(_futureEvents, cutoff);
		}

		StraightSkeleton<Primitive> result;
		result._boundaryPointCount = _boundaryPointCount;
		result._edgesByFace.resize(_boundaryPointCount);
		for (const auto& e:_originalBoundaryEdges)
			result._edgesByFace[e.second].push_back({e.first, e.second, StraightSkeleton<Primitive>::EdgeType::OriginalBoundary});		// _originalBoundaryEdges is head then tail
		for (const auto& e:_vertexPathEdges)
			AddEdge(
				result,
				AddSteinerVertex(result, e._endPt),
				(e._vertex <  _boundaryPointCount) ? e._vertex : AddSteinerVertex(result, e._beginPt),
				GetVertex(e._vertex)._insideFace, GetVertex(e._vertex)._outsideFace,
				StraightSkeleton<Primitive>::EdgeType::VertexPath);
		for (const auto&l:_loops)
			WriteFinalEdges(result, l, maxTime);
		std::sort(_mergedFaces.begin(), _mergedFaces.end(), [](const auto&lhs, const auto&rhs) { return lhs.second > rhs.second; });
		for (auto mergedFace:_mergedFaces) {
			assert(mergedFace.first < mergedFace.second);
			assert(mergedFace.second < result._edgesByFace.size());
			assert(mergedFace.first != mergedFace.second);
			for (const auto&e:result._edgesByFace[mergedFace.second])
				AddUnique(result._edgesByFace[mergedFace.first], e);
			result._edgesByFace[mergedFace.second].clear();
		}
		return result;
	}

	T1(Primitive) auto StraightSkeletonGraph<Primitive>::GetLoop(LoopId id) -> typename std::vector<WavefrontLoop<Primitive>>::iterator
	{
		for (auto l=_loops.begin(); l!=_loops.end(); ++l)
			if (l->_loopId == id)
				return l;
		return _loops.end();
	}

	T1(Primitive) static void SetEdgeLoop(const WavefrontLoop<Primitive>& loop, Event<Primitive>& evnt)
	{
		assert(evnt._type == EventType::MotorcycleCrash || (evnt._edgeHead != evnt._edgeTail));
		assert(HasEdge<Primitive>(loop._edges, evnt._edgeHead, evnt._edgeTail));
		assert(loop._loopId != ~0u);
		evnt._edgeLoop = loop._loopId;
	}

	T1(Primitive) static void SetMotorLoop(const WavefrontLoop<Primitive>& loop, Event<Primitive>& evnt)
	{
		assert(evnt._type == EventType::MotorcycleCrash);
		auto q = std::find_if(loop._edges.begin(), loop._edges.end(),
			[evnt](const auto& e){ return e._head == evnt._motor || e._tail == evnt._motor; });
		assert(q != loop._edges.end());
		assert(loop._loopId != ~0u);
		evnt._motorLoop = loop._loopId;
	}

	T1(Primitive) struct CrashEventInfo
	{
		PointAndTime<Primitive> _crashPtAndTime;
		VertexId _crashSegmentTail, _crashSegmentHead, _motor;
		VertexId _tailSideReplacement, _headSideReplacement;
		WavefrontLoop<Primitive> _tailSide, _headSide;
		LoopId _originalSegmentLoop, _originalMotorLoop;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _tin;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _tout;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _hin;
		typename std::vector<WavefrontEdge<Primitive>>::iterator _hout;
	};

	T1(Primitive) static bool IsCrash(Event<Primitive>& e) { return e._type == EventType::MotorcycleCrash; }

	T1(Primitive) static void HandleEdgeSplit(
		std::vector<Event<Primitive>>& evnts, VertexSet<Primitive> vSet,
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
					if (e._motorLoop == originalLoopId && headSide._loopId != tailSide._loopId) {
						// Use the side that contains the motor
						useHeadSidePart = HasVertex<Primitive>(headSide._edges, e._motor);
						useTailSidePart = HasVertex<Primitive>(tailSide._edges, e._motor);
						assert((useHeadSidePart^useTailSidePart)==1);
					} else {
						// Determine based on the position of the crash
						// there's a good chance that one or more of the vertices are frozen / uncalculated -- so outside the
						// event horizon we will probably only get trash
						auto v0 = GetVertex(vertices, splitEdgeHead).PositionAtTime(e._eventTime);
						auto v2 = GetVertex(vertices, splitEdgeTail).PositionAtTime(e._eventTime);
						if (Dot(e._eventPt - splitPt, v0 - splitPt) > 0) {
							useHeadSidePart = true;
						} else {
							// assert(Dot(e._eventPt - splitPt, v2 - splitPt) > 0);		not reliable, given the assumptions here
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
					assert(headSideEvent._motor != headSideEvent._edgeTail && headSideEvent._motor != headSideEvent._edgeHead);
					SetEdgeLoop(headSide, headSideEvent);
					additionalEventsToAdd.push_back(headSideEvent);
					e._edgeHead = tailSideReplacement;
					assert(e._motor != e._edgeTail && e._motor != e._edgeHead);
					SetEdgeLoop(tailSide, e);
				} else if (useHeadSidePart) {
					e._edgeTail = headSideReplacement;
					assert(e._motor != e._edgeTail && e._motor != e._edgeHead);
					SetEdgeLoop(headSide, e);
				} else if (useTailSidePart) {
					e._edgeHead = tailSideReplacement;
					assert(e._motor != e._edgeTail && e._motor != e._edgeHead);
					SetEdgeLoop(tailSide, e);
				}
			}
		}
		std::sort(additionalEventsToAdd.begin(), additionalEventsToAdd.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
		auto midway = evnts.size();
		evnts.insert(evnts.end(), additionalEventsToAdd.begin(), additionalEventsToAdd.end());
		std::inplace_merge(evnts.begin(), evnts.begin()+midway, evnts.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
	}

	T1(Primitive) static void HandleRemovedVertex(
		std::vector<Event<Primitive>>& evnts, VertexSet<Primitive> vSet,
		VertexId removedVertex,
		VertexId tailSideReplacement, VertexId headSideReplacement,
		const WavefrontLoop<Primitive>& tailSide, const WavefrontLoop<Primitive>& headSide,
		LoopId originalSegmentLoopId, Primitive eventBatchCutoffTime)
	{
		std::vector<Event<Primitive>> additionalEventsToAdd;
		for (auto e=evnts.begin(); e!=evnts.end();) {
			if (e->_edgeHead == removedVertex || e->_edgeTail == removedVertex) {

				if (e->_motor == removedVertex) {
					e=evnts.erase(e);		// collapsed into a degenerate event, protect against the calculations below going haywire
					continue;
				}
				
				bool useHeadSidePart = true;
				if (e->_edgeHead != removedVertex || e->_edgeTail != removedVertex) {		// If this is not a single vertex collision (with that vertex being the removed one)
					if (tailSide._loopId != headSide._loopId) {
						if (e->_edgeHead != removedVertex) useHeadSidePart = HasVertex<Primitive>(headSide._edges, e->_edgeHead);
						else useHeadSidePart = HasVertex<Primitive>(headSide._edges, e->_edgeTail);
					} else {
						bool headSideExists = HasEdge<Primitive>(headSide._edges, (e->_edgeHead == removedVertex) ? headSideReplacement : e->_edgeHead, (e->_edgeTail == removedVertex) ? headSideReplacement : e->_edgeTail);
						bool tailSideExists = HasEdge<Primitive>(tailSide._edges, (e->_edgeHead == removedVertex) ? tailSideReplacement : e->_edgeHead, (e->_edgeTail == removedVertex) ? tailSideReplacement : e->_edgeTail);
						assert((tailSideExists ^ headSideExists) == 1);		// two men say they're Jesus; one of them must be wrong
						useHeadSidePart = headSideExists;
					}
				} else {
					// This is a single vertex collision, and the vertex is the removed one
					assert(IsCrash(*e));
					if (e->_motorLoop == originalSegmentLoopId && headSide._loopId != tailSide._loopId) {
						// don't use this when headSide and tailSide are the same, because in that case it will just always pick the headside loop
						useHeadSidePart = HasVertex<Primitive>(headSide._edges, e->_motor);
						assert(HasVertex<Primitive>(headSide._edges, e->_motor) ^ HasVertex<Primitive>(tailSide._edges, e->_motor));
					} else {

						// Unfortunately we have to solve this, because it can happen... I've seen a good example with a 3 loop collision, two expanding
						// and one contracting
						//
						// We can get this in complex cases, such as a loop merge. It can also happen after a vertex to vertex crash loop merge. However,
						// in that vertex to vertex case, only one side of the edge will remain
						//
						// We don't get any hints from the loop itself, but we can try to look at the directions tailSideReplacement and headSideReplacement
						// are moving. They should be moving in opposite directions, so we'll pick the one moving opposite to the direction of the motor

						bool headSideExists = HasEdge<Primitive>(headSide._edges, (e->_edgeHead == removedVertex) ? headSideReplacement : e->_edgeHead, (e->_edgeTail == removedVertex) ? headSideReplacement : e->_edgeTail);
						bool tailSideExists = HasEdge<Primitive>(tailSide._edges, (e->_edgeHead == removedVertex) ? tailSideReplacement : e->_edgeHead, (e->_edgeTail == removedVertex) ? tailSideReplacement : e->_edgeTail);
						assert(tailSideExists || headSideExists);
						if (!tailSideExists) useHeadSidePart = true;
						else if (!headSideExists) useHeadSidePart = false;
						else {
							Vector2T<Primitive> headSideDirection = Zero<Vector2T<Primitive>>();
							Vector2T<Primitive> tailSideDirection = Zero<Vector2T<Primitive>>();

							// "useAnchorBasedMovementDetermination" is similar to anchor calculation similar UpdateLoopStage1
							// but it's more complicated than the alternative, and should produce the same direction (precision errors aside)
							// note that this doesn't find good movement for vertices on colinear or near-colinear lines
							const bool useAnchorBasedMovementDetermination = false;
							if (!ConsiderStationary(headSide)) {
								auto prevPrevEdge = headSide._edges.end()-2;
								auto prevEdge = headSide._edges.end()-1;
								for (auto edge=headSide._edges.begin(); edge!=headSide._edges.end(); ++edge) {
									assert(edge->_head != edge->_tail);
									assert(prevEdge->_head == edge->_tail);
									if (edge->_tail == headSideReplacement) {
										if (useAnchorBasedMovementDetermination) {
											auto& v0 = vSet[edge->_tail];
											auto next = edge+1;
											if (next == headSide._edges.end()) next = headSide._edges.begin();
											auto calcTime = v0.InitialTime();
											auto anchor1 = CalculateAnchor1<Primitive>(
												prevPrevEdge->_tail, prevEdge->_tail, edge->_tail, edge->_head, next->_head,
												vSet, calcTime);
											headSideDirection = Truncate(anchor1) - Truncate(v0._anchor0);
										} else {
											headSideDirection = EdgeTangentToMovementDir<Primitive>(vSet[edge->_head].PositionAtTime(e->_eventTime) - vSet[edge->_tail].PositionAtTime(e->_eventTime))
												+ EdgeTangentToMovementDir<Primitive>(vSet[prevEdge->_head].PositionAtTime(e->_eventTime) - vSet[prevEdge->_tail].PositionAtTime(e->_eventTime));
										}
									}

									prevPrevEdge = prevEdge;
									prevEdge = edge;
								}
							}

							if (!ConsiderStationary(tailSide)) {
								auto prevPrevEdge = tailSide._edges.end()-2;
								auto prevEdge = tailSide._edges.end()-1;
								for (auto edge=tailSide._edges.begin(); edge!=tailSide._edges.end(); ++edge) {
									assert(edge->_head != edge->_tail);
									assert(prevEdge->_head == edge->_tail);
									if (edge->_tail == tailSideReplacement) {
										if (useAnchorBasedMovementDetermination) {
											auto& v0 = vSet[edge->_tail];
											auto next = edge+1;
											if (next == tailSide._edges.end()) next = tailSide._edges.begin();
											auto calcTime = v0.InitialTime();
											auto anchor1 = CalculateAnchor1<Primitive>(
												prevPrevEdge->_tail, prevEdge->_tail, edge->_tail, edge->_head, next->_head,
												vSet, calcTime);
											tailSideDirection = Truncate(anchor1) - Truncate(v0._anchor0);
										} else {
											tailSideDirection = EdgeTangentToMovementDir<Primitive>(vSet[edge->_head].PositionAtTime(e->_eventTime) - vSet[edge->_tail].PositionAtTime(e->_eventTime))
												+ EdgeTangentToMovementDir<Primitive>(vSet[prevEdge->_head].PositionAtTime(e->_eventTime) - vSet[prevEdge->_tail].PositionAtTime(e->_eventTime));
										}
									}

									prevPrevEdge = prevEdge;
									prevEdge = edge;
								}
							}

							// if we couldn't calculate the head side direction, try to get it from the tail side
							auto motorDirection = e->_eventPt - Truncate(vSet[e->_motor]._anchor0);
							// look for the one moving in the opposite direction -- use the magnitude of the "direction" value as an estimate for accuracy
							if (MagnitudeSquared(headSideDirection) > MagnitudeSquared(tailSideDirection)) {
								useHeadSidePart = Dot(motorDirection, headSideDirection) < 0.f;
							} else {
								useHeadSidePart = Dot(motorDirection, tailSideDirection) > 0.f;
							}
						}
					}
				}

				if (useHeadSidePart) {
					e->_edgeHead = (e->_edgeHead == removedVertex) ? headSideReplacement : e->_edgeHead;
					e->_edgeTail = (e->_edgeTail == removedVertex) ? headSideReplacement : e->_edgeTail;
					assert(e->_edgeHead != e->_motor && e->_edgeTail != e->_motor);
					assert(e->_edgeHead != tailSideReplacement && e->_edgeTail != tailSideReplacement);	// very awkward situation that can cause the loop to get re-merged
					SetEdgeLoop(headSide, *e);
				} else {
					e->_edgeHead = (e->_edgeHead == removedVertex) ? tailSideReplacement : e->_edgeHead;
					e->_edgeTail = (e->_edgeTail == removedVertex) ? tailSideReplacement : e->_edgeTail;
					assert(e->_edgeHead != e->_motor && e->_edgeTail != e->_motor);
					assert(e->_edgeHead != headSideReplacement && e->_edgeTail != headSideReplacement);	// very awkward situation that can cause the loop to get re-merged
					SetEdgeLoop(tailSide, *e);
				}

			} else if (e->_motor == removedVertex) {

				// If the event is beyond our event horizon, we must remove the event and recalculate
				// the crashes for this vertex... The replacement vertex may not be moving in the same direction 
				// as the old vertex
				if (e->_eventTime > eventBatchCutoffTime) {
					e=evnts.erase(e);
					continue;
				}

				// We decide what replacement to use based on where the crash segment is
				// The crash segment should not be split across the two new loops, because
				// only the edge involved in the instigating motorcycle crash is split like that
				// (and we should not have that same edge involved with another motor that needs
				// replacing)
				assert(e->_motorLoop == originalSegmentLoopId);
				if (e->_edgeLoop == originalSegmentLoopId) {
					bool useHeadSidePart = HasVertex<Primitive>(headSide._edges, e->_edgeHead);
					assert((HasVertex<Primitive>(tailSide._edges, e->_edgeHead) != useHeadSidePart) || headSide._loopId == tailSide._loopId);		// headSide & tailSide is the same in loop merge events
					assert(useHeadSidePart == HasVertex<Primitive>(headSide._edges, e->_edgeTail));

					if (useHeadSidePart) {
						e->_motor = headSideReplacement;
						assert(e->_edgeHead != e->_motor && e->_edgeTail != e->_motor);
						assert(e->_edgeHead != tailSideReplacement && e->_edgeTail != tailSideReplacement);	// very awkward situation that can cause the loop to get re-merged
						SetMotorLoop(headSide, *e);
					} else {
						e->_motor = tailSideReplacement;
						assert(e->_edgeHead != e->_motor && e->_edgeTail != e->_motor);
						assert(e->_edgeHead != headSideReplacement && e->_edgeTail != headSideReplacement);	// very awkward situation that can cause the loop to get re-merged
						SetMotorLoop(tailSide, *e);
					}
				} else {
					// The motor was replaced; but it was schedule for loop merge event by smashing into another loop
					// We need to split it into two events
					auto additionalEvent = *e;
					e->_motor = headSideReplacement;
					assert(e->_edgeHead != e->_motor && e->_edgeTail != e->_motor);
					assert(e->_edgeHead != tailSideReplacement && e->_edgeTail != tailSideReplacement);	// very awkward situation that can cause the loop to get re-merged
					SetMotorLoop(headSide, *e);
					additionalEvent._motor = tailSideReplacement;
					assert(additionalEvent._edgeHead != additionalEvent._motor && additionalEvent._edgeTail != additionalEvent._motor);
					assert(additionalEvent._edgeHead != headSideReplacement && additionalEvent._edgeTail != headSideReplacement);	// very awkward situation that can cause the loop to get re-merged
					SetMotorLoop(tailSide, additionalEvent);
					additionalEventsToAdd.emplace_back(additionalEvent);
				}

			}

			++e;
		}
		std::sort(additionalEventsToAdd.begin(), additionalEventsToAdd.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
		auto midway = evnts.size();
		evnts.insert(evnts.end(), additionalEventsToAdd.begin(), additionalEventsToAdd.end());
		std::inplace_merge(evnts.begin(), evnts.begin()+midway, evnts.end(), [](const auto& lhs, const auto& rhs) { return lhs._eventTime < rhs._eventTime; });
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::PostProcessEventsForMotorcycleCrash_Phase0(
		CrashEventInfo<Primitive>& crashInfo,
		std::vector<Event<Primitive>>& evnts)
	{
		unsigned crashSegmentTail = crashInfo._crashSegmentTail, crashSegmentHead = crashInfo._crashSegmentHead;

		// In the single vertex collision case, we need to look for a complementary motor, which is just the same thing, but reversed
		// It's safe to remove this, since the only processing required is what we've already done
		// Sometimes the complementary case will look like a motor vs edge case
		if (crashSegmentHead == crashSegmentTail) {
			auto i = std::remove_if(b2e(evnts),
				[&](const auto& q) {
					return 
						(q._motor == crashSegmentHead && q._edgeTail == crashInfo._motor && q._edgeHead == crashInfo._motor)		// simple complement
						|| (q._motor == crashSegmentHead && (q._edgeTail == crashInfo._motor || q._edgeHead == crashInfo._motor));	// complex complement
				});
			evnts.erase(i, evnts.end());
		} else {
			// as above, look for the complex complement (edge head or tail crashing to the motor as a vertex to vertex motor)
			auto i = std::remove_if(b2e(evnts),
				[&](const auto& q) {
					return (q._motor == crashSegmentHead || q._motor == crashSegmentTail) && q._edgeTail == crashInfo._motor && q._edgeHead == crashInfo._motor;
				});
			evnts.erase(i, evnts.end());
		}

		{
			// We can end up duplicating the same motorcycle event multiple times, due to a lack of filtering when we create the events
			// For crash vs vertex, we want to remove both duplicate crash vs vertex, and crashes vs edges containing that vertex
			// and flipped for crash vs edge
			if (crashInfo._crashSegmentTail == crashInfo._crashSegmentHead) {
				auto i = std::remove_if(b2e(evnts),
					[&](const auto& q) {
						return q._motor == crashInfo._motor && (q._edgeTail == crashInfo._crashSegmentTail || q._edgeHead == crashInfo._crashSegmentTail);
					});
				evnts.erase(i, evnts.end());
			} else {
				auto i = std::remove_if(b2e(evnts),
					[&](const auto& q) {
						return q._motor == crashInfo._motor && (
							(q._edgeTail == crashInfo._crashSegmentTail && q._edgeHead == crashInfo._crashSegmentHead)
							|| (q._edgeTail == crashInfo._crashSegmentTail && q._edgeHead == crashInfo._crashSegmentTail)	// crash vs vertex case 0
							|| (q._edgeTail == crashInfo._crashSegmentHead && q._edgeHead == crashInfo._crashSegmentHead)	// crash vs vertex case 1
							);
					});
				evnts.erase(i, evnts.end());
			}
		}
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::PostProcessEventsForMotorcycleCrash_Phase1(
		CrashEventInfo<Primitive>& crashInfo,
		std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime)
	{
		// We may have to rename the crash segments for any future crashes. We remove 1 vertex
		// from the system every time we process a motorcycle crash. So, if one of the upcoming
		// crash events involves this vertex, we have rename it to either the new vertex on the
		// inSide, or on the tailSide

		// Process the crashSegmentHead <-- crashSegmentTail edge first
		if (crashInfo._crashSegmentHead != crashInfo._crashSegmentTail) {
			HandleEdgeSplit<Primitive>(
				evnts, _vertices,
				crashInfo._crashSegmentTail, crashInfo._crashSegmentHead,
				crashInfo._tailSideReplacement, crashInfo._headSideReplacement,
				crashInfo._tailSide, crashInfo._headSide, crashInfo._originalSegmentLoop,
				Truncate(crashInfo._crashPtAndTime), _vertices);
		} else {
			HandleRemovedVertex<Primitive>(evnts, _vertices, crashInfo._crashSegmentTail, crashInfo._tailSideReplacement, crashInfo._headSideReplacement, crashInfo._tailSide, crashInfo._headSide, crashInfo._originalSegmentLoop, eventBatchCutoffTime);
		}

		HandleRemovedVertex<Primitive>(evnts, _vertices, crashInfo._motor, crashInfo._tailSideReplacement, crashInfo._headSideReplacement, crashInfo._tailSide, crashInfo._headSide, crashInfo._originalSegmentLoop, eventBatchCutoffTime);

		for (auto e=evnts.begin(); e!=evnts.end();) {

			if (e->_edgeLoop == crashInfo._originalSegmentLoop) {
				if (HasVertex<Primitive>(crashInfo._headSide._edges, e->_edgeHead)) {
					SetEdgeLoop(crashInfo._headSide, *e);
				} else {
					SetEdgeLoop(crashInfo._tailSide, *e);
				}
			}
			if (IsCrash(*e) && e->_motorLoop == crashInfo._originalSegmentLoop) {
				if (HasVertex<Primitive>(crashInfo._headSide._edges, e->_motor)) {
					SetMotorLoop(crashInfo._headSide, *e);
				} else {
					SetMotorLoop(crashInfo._tailSide, *e);
				}

				// Precision accuracy can cause loop splits with re-merge immediately afterwards, creating
				// a bit of a mess...
				// Unfortunately we can't easily distinguish that case from two separate and legit splits
				// and merges -- which might be happening simultaneously at different parts of a loop
				// previously there was a hack here to try to prevent a mess in the first case, but it was
				// causing more problems than it was solving.
			}
			++e;
		}
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessMotorcycleEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime)
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
		_processedEvents.emplace_back(crashEvent);

		auto& initialEdgeLoop = *GetLoop(crashEvent._edgeLoop);

		// The motor can collapse to become a vertex of the collision edge during earlier steps.
		if (crashEvent._motor == crashEvent._edgeHead || crashEvent._motor == crashEvent._edgeTail) return;

		if (crashEvent._edgeHead == crashEvent._edgeTail) {
			// Sometimes crash events are converted into what should really be a collapse event. In these cases,
			// there should also be a collapse event queued
			auto motorIn = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._head == motorHead; });
			auto motorOut = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._tail == motorHead; });
			if (motorIn->_tail == crashEvent._edgeHead || motorOut->_head == crashEvent._edgeHead)
				return;
		}

		assert(initialEdgeLoop._edges.size() > 2);
		if (initialEdgeLoop._edges.size() <= 2) return;

		// We need to build 2 new WavefrontLoops -- one for the "tout" side and one for the "tin" side
		// In some cases, one side or the other than can be completely collapsed. But we're still going to
		// create it.
		CrashEventInfo<Primitive> crashInfo;
		crashInfo._crashPtAndTime = PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime};
		crashInfo._motor = crashEvent._motor;
		crashInfo._crashSegmentTail = crashEvent._edgeTail;
		crashInfo._crashSegmentHead = crashEvent._edgeHead;
		crashInfo._originalSegmentLoop = initialEdgeLoop._loopId;
		crashInfo._originalMotorLoop = crashEvent._motorLoop;
		if (initialEdgeLoop._lastBatchIndex == _currentBatchIndex) {
			crashInfo._headSide._lastEventBatchEarliest = crashInfo._tailSide._lastEventBatchEarliest = std::min(crashEvent._eventTime, initialEdgeLoop._lastEventBatchEarliest);
		} else {
			crashInfo._headSide._lastEventBatchEarliest = crashInfo._tailSide._lastEventBatchEarliest = crashEvent._eventTime;
		}
		crashInfo._headSide._lastBatchIndex = crashInfo._tailSide._lastBatchIndex = _currentBatchIndex;
		crashInfo._headSide._lastEventBatchLatest = crashInfo._tailSide._lastEventBatchLatest = crashEvent._eventTime;

		if (crashInfo._tailSide._edges.size() > crashInfo._headSide._edges.size()) {
			crashInfo._tailSide._loopId = initialEdgeLoop._loopId;
			crashInfo._headSide._loopId = _nextLoopId++;
		} else {
			crashInfo._tailSide._loopId = _nextLoopId++;
			crashInfo._headSide._loopId = initialEdgeLoop._loopId;
		}

		//////////////////////////////////////////////////////////////////////
				//   T A I L   S I D E
		// Start at motor._head, and work around in order until we hit the crash segment.
		{
			auto tout = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(),
				[motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& test) { return test._tail == motorHead; });
			assert(tout != initialEdgeLoop._edges.end());
			auto tin = tout;

			if (tout->_head != crashEvent._edgeTail) {
				++tin;
				if (tin == initialEdgeLoop._edges.end()) tin = initialEdgeLoop._edges.begin();
				while (tin->_head!=crashEvent._edgeTail) {
					crashInfo._tailSide._edges.push_back(*tin);
					assert(crashInfo._tailSide._edges.size() <= initialEdgeLoop._edges.size());
					++tin;
					if (tin == initialEdgeLoop._edges.end()) tin = initialEdgeLoop._edges.begin();
				}

				if (crashEvent._edgeHead == crashEvent._edgeTail) {
					tin = (tin == initialEdgeLoop._edges.begin()) ? (initialEdgeLoop._edges.end()-1) : (tin-1);
				} else
					crashInfo._tailSide._edges.push_back(*tin);
			} else if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(tout->_head != crashEvent._edgeHead);
			}

			crashInfo._tailSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime, GetVertex(crashEvent._edgeHead)._insideFace, GetVertex(crashEvent._motor)._outsideFace});
			crashInfo._tailSide._edges.push_back({crashInfo._tailSideReplacement, tin->_head});
			crashInfo._tailSide._edges.push_back({tout->_head, crashInfo._tailSideReplacement});

			crashInfo._tin = tin;
			crashInfo._tout = tout;
		}

		//////////////////////////////////////////////////////////////////////
				//   H E A D   S I D E
		// Start at crashSegment._head, and work around in order until we hit the motor vertex
		{
			auto hout = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(),
				[crashEvent](const WavefrontEdge<Primitive>& test) { return test._tail == crashEvent._edgeHead; });
			assert(hout != initialEdgeLoop._edges.end());

			if (crashEvent._edgeHead == crashEvent._edgeTail) {
				assert(hout->_head != crashEvent._motor);		// this causes all manner of chaos, but should only happen if this event should really be a collapse
				++hout;
				if (hout == initialEdgeLoop._edges.end()) hout = initialEdgeLoop._edges.begin();
			}
			auto hin = hout;
			while (hin->_head!=crashEvent._motor) {
				crashInfo._headSide._edges.push_back(*hin);
				assert(crashInfo._headSide._edges.size() <= initialEdgeLoop._edges.size());
				++hin;
				if (hin == initialEdgeLoop._edges.end()) hin = initialEdgeLoop._edges.begin();
			}

			crashInfo._headSideReplacement = (unsigned)_vertices.size();
			_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime, GetVertex(crashEvent._motor)._insideFace, GetVertex(crashEvent._edgeTail)._outsideFace});
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
		AddVertexPathEdge(crashEvent._motor, GetVertex(crashEvent._motor)._anchor0, crashInfo._crashPtAndTime);

		if (crashEvent._edgeTail == crashEvent._edgeHead) {
			// This vertex got removed from the simulation, and we have to explicitly add a final vertex path edge for it
			AddVertexPathEdge(crashEvent._edgeHead, GetVertex(crashEvent._edgeHead)._anchor0, crashInfo._crashPtAndTime);
		}

		// Check for loop inversion, and update _signedAreaAtLatestEvent
		// Do before PostProcessEventsForMotorcycleCrash to update ConsiderStationary state
		// Note that an expanding loop can have a contracting part, which can result in a contracting loop
		// being cut off from an otherwise expanding one
		//		\----------\
		//		 \|-----\  \
		//				\  \
		//		 /|-----\  \
		//		\----------\
		// But I think there should always be at least one contracting loop in the output
		if (crashInfo._tailSide._edges.size() > 2) {
			crashInfo._tailSide._signedAreaAtLatestEvent = CalculateSignedAreaAtTime<Primitive>(crashInfo._tailSide._edges, _vertices, crashEvent._eventTime);
			crashInfo._tailSide._signOfInitialLoop = std::copysign(Primitive(1), crashInfo._tailSide._signedAreaAtLatestEvent);		// subject to precision error, unfortunately
		}
		if (crashInfo._headSide._edges.size() > 2) {
			crashInfo._headSide._signedAreaAtLatestEvent = CalculateSignedAreaAtTime<Primitive>(crashInfo._headSide._edges, _vertices, crashEvent._eventTime);
			crashInfo._headSide._signOfInitialLoop = std::copysign(Primitive(1), crashInfo._headSide._signedAreaAtLatestEvent);		// subject to precision error, unfortunately
		}

		// If the original loop was contracting, don't allow either side to be expanding -- trying to prevent a fly-away scenario early
		if (initialEdgeLoop._signOfInitialLoop < 0) {
			if (crashInfo._tailSide._signOfInitialLoop > 0)
				crashInfo._tailSide._signOfInitialLoop = 0;
			if (crashInfo._headSide._signOfInitialLoop > 0)
				crashInfo._headSide._signOfInitialLoop = 0;
		}
		// if both loop as marked expanding, we'll consider this precision creep and mark the small one stationary
		if ((crashInfo._tailSide._signOfInitialLoop > 0) && (crashInfo._headSide._signOfInitialLoop > 0)) {
			if (crashInfo._tailSide._signedAreaAtLatestEvent < crashInfo._headSide._signedAreaAtLatestEvent) {
				crashInfo._tailSide._signOfInitialLoop = 0;
			} else
				crashInfo._headSide._signOfInitialLoop = 0;
		}
		assert((crashInfo._tailSide._signOfInitialLoop <= 0) || (crashInfo._headSide._signOfInitialLoop <= 0));	// one should be contracting

		//////////////////////////////////////////////////////////////////////
		PostProcessEventsForMotorcycleCrash_Phase0(crashInfo, evnts);
		PostProcessEventsForMotorcycleCrash_Phase1(crashInfo, evnts, eventBatchCutoffTime);

		// Overwrite "loop" with tailSide, and append inSide to the list of wavefront loops
		// crashSegment, motorIn & motorOut should not make it into either tailSide or headSide
		#if defined(_DEBUG)
			auto motorIn = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._head == motorHead; });
			auto motorOut = std::find_if(initialEdgeLoop._edges.begin(), initialEdgeLoop._edges.end(), [motorHead=crashEvent._motor](const WavefrontEdge<Primitive>& e) { return e._tail == motorHead; });
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
		if (crashInfo._tailSide._loopId == initialEdgeLoop._loopId) {
			initialEdgeLoop = std::move(crashInfo._tailSide);
			_loops.emplace_back(std::move(crashInfo._headSide));
		} else {
			initialEdgeLoop = std::move(crashInfo._headSide);
			_loops.emplace_back(std::move(crashInfo._tailSide));
		}
	}

	T1(Primitive) struct CollapseGroupInfo
	{
		unsigned _head, _tail, _headSideReplacement = ~0u, _tailSideReplacement = ~0u;
		PointAndTime<Primitive> _crashPtAndTime = Zero<PointAndTime<Primitive>>();
	};

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessCollapseEvents(std::vector<Event<Primitive>>& evnts, Primitive cutoff)
	{
		// Process the first collapse group on the pending events list, but include in any
		// collapses on the pending event list that are directly connected
		assert(!evnts.empty() && evnts.begin()->_type == EventType::Collapse);

		CollapseGroupInfo<Primitive> collapseGroupInfo;
		std::vector<Event<Primitive>> collapses;
		collapses.push_back(*evnts.begin());
		evnts.erase(evnts.begin());

		size_t eventsCutoffEnd = 0;
		while (evnts.begin()+eventsCutoffEnd != evnts.end() && evnts[eventsCutoffEnd]._eventTime <= cutoff) ++eventsCutoffEnd;

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
			auto i = std::find_if(evnts.begin(), evnts.begin()+eventsCutoffEnd,
				[searchingTail](const auto& t) { return t._type == EventType::Collapse && t._edgeHead == searchingTail; });
			if (i == evnts.begin()+eventsCutoffEnd) break;

			assert(HasVertex<Primitive>(loop._edges, i->_edgeHead) && HasVertex<Primitive>(loop._edges, i->_edgeTail));
			searchingTail = i->_edgeTail;
			collapses.push_back(*i);
			evnts.erase(i); --eventsCutoffEnd;
		}

		// also go forward head to head
		auto searchingHead = collapses[0]._edgeHead;
		for (;;) {
			auto i = std::find_if(evnts.begin(), evnts.begin()+eventsCutoffEnd,
				[searchingHead](const auto& t) { return t._type == EventType::Collapse && t._edgeTail == searchingHead; });
			if (i == evnts.begin()+eventsCutoffEnd) break;

			assert(HasVertex<Primitive>(loop._edges, i->_edgeHead) && HasVertex<Primitive>(loop._edges, i->_edgeTail));
			searchingHead = i->_edgeHead;
			collapses.push_back(*i);
			evnts.erase(i); --eventsCutoffEnd;
		}
		collapseGroupInfo._head = searchingHead;
		collapseGroupInfo._tail = searchingTail;

		_processedEvents.insert(_processedEvents.end(), collapses.begin(), collapses.end());

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
			// Note -- disabling this because it can trigger in cases where it's not an error currently
			// Sometimes the code in CalculateAnchor1() will not generate a correct anchor, even though
			// CalculateCollapseEvent() will. In these cases, the vertex is given zero velocity, but the
			// collapse event is actually the correct collapse
			#if 0 // defined(_DEBUG)
				for (size_t c=0; c<collapses.size(); ++c) {
					auto one = GetVertex(collapses[c]._edgeHead).PositionAtTime(earliestCollapseTime);
					auto two = GetVertex(collapses[c]._edgeTail).PositionAtTime(earliestCollapseTime);
					assert(Equivalent(one, collisionPt, GetEpsilon<Primitive>()));
					assert(Equivalent(two, collisionPt, GetEpsilon<Primitive>()));
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
			AddVertexPathEdge(*v, GetVertex(*v)._anchor0, collapseGroupInfo._crashPtAndTime);

			// Also remove any motorcycles associated with these vertices (since they will be removed
			// from active loops, the motorcycle is no longer valid)
			assert(*v != VertexId(~0u));
			auto i = std::remove_if(b2e(evnts), [q=*v](const auto& e) { return e._motor == q; });
			evnts.erase(i, evnts.end());
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

		// If we have a loop A -> B -> C, sometimes we can get A -> B and B -> C collapsing, but we're not
		// yet ready to collapse the return edge C -> A. In these cases, A, B & C get combined into a collapse
		// group and all are considered removed... so we're forced to cleanup the return edge, also.
		for(auto i=loop._edges.begin(); i!=loop._edges.end(); ++i)
			if (i->_head == collapseGroupInfo._tail && i->_tail == collapseGroupInfo._head) {
				loop._edges.erase(i);
				break;
			}
		assert(loop._edges.size() != 1);

		if (loop._edges.size() > 1 && collapseGroupInfo._head != collapseGroupInfo._tail) {
			auto tail = FindInAndOut(MakeIteratorRange(loop._edges), collapseGroupInfo._tail).first;
			auto head = FindInAndOut(MakeIteratorRange(loop._edges), collapseGroupInfo._head).second;
			assert(tail != loop._edges.end() && head != loop._edges.end());
			assert(tail != head);

			tail->_pendingCalculate = true;
			head->_pendingCalculate = true;
			// FindInAndOut(MakeIteratorRange(loop._edges), tail->_tail).first->_pendingCalculate = true;
			// FindInAndOut(MakeIteratorRange(loop._edges), head->_head).second->_pendingCalculate = true;

			auto preTailPt = GetVertex(tail->_tail).PositionAtTime(collapseGroupInfo._crashPtAndTime[2]);
			auto postHeadPt = GetVertex(head->_head).PositionAtTime(collapseGroupInfo._crashPtAndTime[2]);
			if (CalculateWindingType<Primitive>(preTailPt, Truncate(collapseGroupInfo._crashPtAndTime), postHeadPt, GetEpsilon<Primitive>()).first == WindingType::Straight
				|| (Equivalent(preTailPt, Truncate(collapseGroupInfo._crashPtAndTime), GetEpsilon<Primitive>()) && Equivalent(postHeadPt, Truncate(collapseGroupInfo._crashPtAndTime), GetEpsilon<Primitive>()))) {
				// avoid creating 2 colinear edges. Instead we'll just create a single new edge spanning the gap created
				// Alternatively; we could create a vertex but mark it with a flag highlighting that it is colinear
				collapseGroupInfo._tailSideReplacement = tail->_tail;
				collapseGroupInfo._headSideReplacement = head->_head;
				tail->_head = head->_head;
				loop._edges.erase(head);

				auto merge0 = GetVertex(collapseGroupInfo._tail)._insideFace, merge1 = GetVertex(collapseGroupInfo._head)._outsideFace;
				if (merge0 != merge1) {
					auto dst = std::min(merge0, merge1), src = std::max(merge0, merge1);
					_mergedFaces.push_back({dst, src});

					for (auto&v:_vertices) {
						if (v._insideFace == src) v._insideFace = dst;
						if (v._outsideFace == src) v._outsideFace = dst;
					}
				}
			} else {
				// create a new vertex in the graph to connect the edges to either side of the collapse
				auto newVertex = (unsigned)_vertices.size();
				_vertices.push_back({collapseGroupInfo._crashPtAndTime, collapseGroupInfo._crashPtAndTime, GetVertex(collapseGroupInfo._tail)._insideFace, GetVertex(collapseGroupInfo._head)._outsideFace});

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

				if (pendingEvent->_type == EventType::MotorcycleCrash) {
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
						} else {
							assert(0);		// cancelling the motor
							pendingEvent->_motor = ~0u;
						}
					}

					if (collapseGroupInfo._tailSideReplacement != collapseGroupInfo._headSideReplacement) {
						if (	std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeTail) != collapsedVerticesEnd
							|| 	std::find(collapsedVertices.begin(), collapsedVerticesEnd, pendingEvent->_edgeHead) != collapsedVerticesEnd) {
							// The there is an motorcycle crash on an edge that was at least partially collapsed. This is the cases where
							// the collapse event does not generate a vertex -- we just get one larger edge that covers the entire collapsed
							// area.
							// Either the edge for pendingEvent was entirely collapsed, or one vertex must be the pre-tail
							// (collapseGroupInfo._tailSideReplacement) or one vertex must the post-head (collapseGroupInfo._headSideReplacement).
							// In other words, wherever the collapse is, it must be within the new super edge from collapseGroupInfo._tailSideReplacement
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
				if (pendingEvent->_edgeHead == pendingEvent->_motor || pendingEvent->_edgeTail == pendingEvent->_motor) {
					// this collapse resolved the motor; now it's a motor against it's own segment
					assert(IsCrash(*pendingEvent));
					pendingEvent = evnts.erase(pendingEvent);
					continue;
				}
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
				assert(pendingEvent->_edgeLoop != loop._loopId || HasVertex<Primitive>(loop._edges, pendingEvent->_edgeHead) && HasVertex<Primitive>(loop._edges, pendingEvent->_edgeTail));
				if ((pendingEvent->_type == EventType::MotorcycleCrash) && pendingEvent->_motor == ~0u) {
					pendingEvent = evnts.erase(pendingEvent);
					continue;
				}
				++pendingEvent;
			}

		} else {
			// this loop got entirely collapsed. Remove all events and motorcycle references
			evnts.erase(std::remove_if(b2e(evnts), [id=loop._loopId](const auto& q) { return q._edgeLoop == id;  }), evnts.end());
		}

		if (loop._lastBatchIndex == _currentBatchIndex) {
			loop._lastEventBatchEarliest = std::min(loop._lastEventBatchEarliest, earliestCollapseTime);
		} else {
			loop._lastEventBatchEarliest = earliestCollapseTime;
			loop._lastBatchIndex = _currentBatchIndex;
		}
		loop._lastEventBatchLatest = latestCollapseTime;

		if (loop._edges.size() > 2) {
			auto finalSignedArea = CalculateSignedAreaAtTime<Primitive>(loop._edges, _vertices, latestCollapseTime);
			// assert(std::abs(finalSignedArea) < GetEpsilon<Primitive>() || (finalSignedArea > 0) == (loop._signOfInitialLoop > 0));		 // subject to precision error
			loop._signedAreaAtLatestEvent = finalSignedArea;
		}
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessLoopMergeEvents(std::vector<Event<Primitive>>& evnts, Primitive eventBatchCutoffTime)
	{
		assert(!evnts.empty() && evnts.begin()->_type == EventType::MotorcycleCrash);
		auto crashEvent = *evnts.begin();
		evnts.erase(evnts.begin());
		_processedEvents.emplace_back(crashEvent);

		assert(crashEvent._motorLoop != crashEvent._edgeLoop);
		auto motorLoop = GetLoop(crashEvent._motorLoop);
		auto edgeLoop = GetLoop(crashEvent._edgeLoop);

		// We can prevent many loop remerge events by just rejecting merges with loops that have already flattened to 2
		// Without this, convergences of motorcycles will result in a sequence of loop splits and merges, and depending on the
		// order of processing we can end up with zero area loops of 4 or more edges -- these tend to confuse later processing
		if (motorLoop->_edges.size() <= 2) return;
		if (edgeLoop->_edges.size() <= 2) return;

		// There should only be 2 possibilities -- one loop expanding (1) while the other is contracting (-1), or 2 loops expanding (1)
		// However, we tend to trigger the following assert, either due to precision errors or from loop remerge issues.
		// assert((motorLoop->_signOfInitialLoop > 0) || (edgeLoop->_signOfInitialLoop > 0));

		// This is like a normal motorcycle crash event, except that we take 2 loops as input and end up with one as output
		// headSideReplacement -> hout around to tin -> tailSideReplacement, then onto tout around to hin
		// the motor is removed from all loops
		CrashEventInfo<Primitive> crashInfo;
		crashInfo._crashPtAndTime = PointAndTime<Primitive>{crashEvent._eventPt, crashEvent._eventTime};
		crashInfo._motor = crashEvent._motor;
		crashInfo._crashSegmentTail = crashEvent._edgeTail;
		crashInfo._crashSegmentHead = crashEvent._edgeHead;
		crashInfo._originalSegmentLoop = crashEvent._edgeLoop;
		crashInfo._originalMotorLoop = crashEvent._motorLoop;

		crashInfo._tailSideReplacement = (unsigned)_vertices.size();
		// assert(GetVertex(crashEvent._edgeTail)._outsideFace == GetVertex(crashEvent._edgeHead)._insideFace); -- have to check merged faces for this, also
		_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime, GetVertex(crashEvent._edgeHead)._insideFace, GetVertex(crashEvent._motor)._outsideFace});

		crashInfo._headSideReplacement = (unsigned)_vertices.size();
		_vertices.push_back({crashInfo._crashPtAndTime, crashInfo._crashPtAndTime, GetVertex(crashEvent._motor)._insideFace, GetVertex(crashEvent._edgeTail)._outsideFace});

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

		if (motorLoop->_lastBatchIndex == edgeLoop->_lastBatchIndex) {
			motorLoop->_lastEventBatchEarliest = std::min(motorLoop->_lastEventBatchEarliest, edgeLoop->_lastEventBatchEarliest);
		} else if (motorLoop->_lastBatchIndex < edgeLoop->_lastBatchIndex) {
			motorLoop->_lastEventBatchEarliest = edgeLoop->_lastEventBatchEarliest;
			motorLoop->_lastBatchIndex = edgeLoop->_lastBatchIndex;
		}
		if (motorLoop->_lastBatchIndex == _currentBatchIndex) {
			motorLoop->_lastEventBatchEarliest = std::min(motorLoop->_lastEventBatchEarliest, crashEvent._eventTime);
		} else {
			motorLoop->_lastEventBatchEarliest = crashEvent._eventTime;
			motorLoop->_lastBatchIndex = _currentBatchIndex;
		}
		motorLoop->_lastEventBatchLatest = crashEvent._eventTime;

		// crashEvent._motor is frozen
		AddVertexPathEdge(crashEvent._motor, GetVertex(crashEvent._motor)._anchor0, crashInfo._crashPtAndTime);
		// collision vertex is frozen if this is a single vertex collision
		if (crashEvent._edgeHead == crashEvent._edgeTail)
			AddVertexPathEdge(crashEvent._edgeHead, GetVertex(crashEvent._edgeHead)._anchor0, crashInfo._crashPtAndTime);

		// Update signed area & "ConsiderStationary" state
		// Do before HandleRemovedVertex to update ConsiderStationary state
		if (motorLoop->_edges.size() > 2) {
			// if either was contracting originally, the result is contracting also
			if ((motorLoop->_signOfInitialLoop < 0) || (edgeLoop->_signOfInitialLoop < 0))
				motorLoop->_signOfInitialLoop = -1;
			motorLoop->_signedAreaAtLatestEvent = CalculateSignedAreaAtTime<Primitive>(motorLoop->_edges, _vertices, crashEvent._eventTime);
		}

		// Update loop ids in all evnts and motorcycles
		for (auto e=evnts.begin(); e!=evnts.end();) {
			// bool originallyInternalLoop = e->_edgeLoop == e->_motorLoop;
			if (e->_edgeLoop == edgeLoop->_loopId) e->_edgeLoop = motorLoop->_loopId;
			if (e->_motorLoop == edgeLoop->_loopId) e->_motorLoop = motorLoop->_loopId;
			++e;
		}

		// phase0 post-processing applies here
		PostProcessEventsForMotorcycleCrash_Phase0(crashInfo, evnts);

		if (crashEvent._edgeHead != crashEvent._edgeTail) {
			HandleEdgeSplit<Primitive>(
				evnts, _vertices,
				crashEvent._edgeTail, crashEvent._edgeHead,
				crashInfo._tailSideReplacement, crashInfo._headSideReplacement,
				*motorLoop, *motorLoop, crashInfo._originalSegmentLoop,
				Truncate(crashInfo._crashPtAndTime), _vertices);
		} else {
			HandleRemovedVertex<Primitive>(evnts, _vertices, crashEvent._edgeTail, crashInfo._tailSideReplacement, crashInfo._headSideReplacement, *motorLoop, *motorLoop, motorLoop->_loopId, eventBatchCutoffTime);
		}

		HandleRemovedVertex<Primitive>(evnts, _vertices, crashEvent._motor, crashInfo._tailSideReplacement, crashInfo._headSideReplacement, *motorLoop, *motorLoop, motorLoop->_loopId, eventBatchCutoffTime);

		#if defined(_DEBUG)
			for (auto& evnt:evnts) {
				if (evnt._edgeLoop == motorLoop->_loopId) {
					assert(HasVertex<Primitive>(motorLoop->_edges, evnt._edgeHead));
					assert(HasVertex<Primitive>(motorLoop->_edges, evnt._edgeTail));
				}
				if (evnt._type != EventType::Collapse && evnt._motorLoop == motorLoop->_loopId)
					assert(HasVertex<Primitive>(motorLoop->_edges, evnt._motor));
				assert(evnt._edgeLoop != edgeLoop->_loopId && evnt._motorLoop != edgeLoop->_loopId);
			}

			for (auto e=motorLoop->_edges.begin(); e!=motorLoop->_edges.end(); ++e) {
				assert(e->_head != crashEvent._edgeHead || e->_tail != crashEvent._edgeTail);
				assert(e->_head != e->_tail);
				auto next = e+1; if (next == motorLoop->_edges.end()) next = motorLoop->_edges.begin();
				assert(e->_head == next->_tail);
			}
		#endif

		_loops.erase(edgeLoop);
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::ProcessEvents(std::vector<Event<Primitive>>& evnts, Primitive cutoff)
	{
		// It may make sense to resolve all collapses first, because those calculations are simpler, and
		// might help prevent near-identical vertex positions from creating precision error in motorcycle
		// calculations
		const bool resolveCollapsesFirst = false;
		if (resolveCollapsesFirst) {
			auto evntEnd = evnts.begin(); while (evntEnd!=evnts.end() && evntEnd->_eventTime <= cutoff) ++evntEnd;
			std::stable_sort(
				evnts.begin(), evntEnd,
				[](const auto& lhs, const auto& rhs) {
					return (lhs._type == EventType::Collapse) && (rhs._type != EventType::Collapse);
				});
		}

		// Keep processing events until there are no more to do
		while (!evnts.empty() && evnts.front()._eventTime <= cutoff) {
			if (evnts.front()._type == EventType::Collapse) {
				ProcessCollapseEvents(evnts, cutoff);
			} else if (evnts.front()._type == EventType::MotorcycleCrash && evnts.front()._edgeLoop == evnts.front()._motorLoop) {
				ProcessMotorcycleEvents(evnts, cutoff);
			} else {
				assert(evnts.front()._type == EventType::MotorcycleCrash);
				// For a motorcycle between 2 loops; we start off by merging the 2 loops
				// After the merge, we can handle this has a standard motorcycle crash1
				ProcessLoopMergeEvents(evnts, cutoff);
			}
		}
	}

	T1(Primitive) static unsigned AddSteinerVertex(StraightSkeleton<Primitive>& skeleton, const Vector3T<Primitive>& vertex)
	{
		assert(IsFiniteNumber(vertex[0]) && IsFiniteNumber(vertex[1]) && IsFiniteNumber(vertex[2]));
		assert(vertex[0] != std::numeric_limits<Primitive>::max() && vertex[1] != std::numeric_limits<Primitive>::max() && vertex[2] != std::numeric_limits<Primitive>::max());
		assert(vertex[2] < 1e6f);
		assert(vertex[2] != 0);

		auto existing = std::find_if(
			skeleton._steinerVertices.begin(), skeleton._steinerVertices.end(),
			[vertex](const auto& c) { return AdaptiveEquivalent(vertex, c, GetEpsilon<Primitive>()); });
		if (existing != skeleton._steinerVertices.end())
			return unsigned(skeleton._boundaryPointCount + std::distance(skeleton._steinerVertices.begin(), existing));

		auto result = (unsigned)skeleton._steinerVertices.size();
		skeleton._steinerVertices.push_back(vertex);
		return unsigned(skeleton._boundaryPointCount + result);
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

	T1(Primitive) static void AddEdge(
		StraightSkeleton<Primitive>& dest,
		VertexId laterVertex, VertexId earlierVertex,		// or head, tail
		FaceId insideFace, FaceId outsideFace,
		typename StraightSkeleton<Primitive>::EdgeType type)
	{
		if (earlierVertex == laterVertex) return;

		// when adding a wavefront edge,
		//		laterVertex = head, earlierVertex = tail
		AddUnique(dest._edges, {laterVertex, earlierVertex, type});

		// We need to try to maintain the counter clockwise ordering when we construct _edgesByFace.
		// See the coordinate system diagram, but note that edge is constructed head, then tail
		if (insideFace != ~FaceId(0)) {
			if (dest._edgesByFace.size() <= insideFace)
				dest._edgesByFace.resize(insideFace+1);
			AddUnique(dest._edgesByFace[insideFace], {laterVertex, earlierVertex, type});
		}
		if (outsideFace != ~FaceId(0)) {
			if (dest._edgesByFace.size() <= outsideFace)
				dest._edgesByFace.resize(outsideFace+1);
			AddUnique(dest._edgesByFace[outsideFace], {earlierVertex, laterVertex, type});
		}
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::AddVertexPathEdge(VertexId vertex, PointAndTime<Primitive> begin, PointAndTime<Primitive> end)
	{
		assert(end[2] < 1e6f);
		_vertexPathEdges.push_back({vertex, begin, end});
	}

	T1(Primitive) void StraightSkeletonGraph<Primitive>::WriteFinalEdges(StraightSkeleton<Primitive>& result, const WavefrontLoop<Primitive>& loop, Primitive maxTime)
	{
		Primitive time = maxTime;

		bool stationary = ConsiderStationary(loop);
		stationary |= std::abs(loop._signedAreaAtLatestEvent) < GetEpsilon<Primitive>();

		// If any vertices in the loop are stationary, they must limit the entire loop
		for (auto& e:loop._edges)
			if (_vertices[e._tail]._anchor0 == _vertices[e._tail]._anchor1) {
				assert(_vertices[e._tail]._anchor0[2] > 0);		// this can be triggered if there are stationary vertices in the input
				time = std::min(time, _vertices[e._tail]._anchor0[2]);
				stationary = true;
			}

		if (!stationary) {
			// If the loop is contracting, and there are no valid collapses, we are subject to precision errors. If we don't clamp time, these loops
			// will invert and expand infinitely
			if (loop._signOfInitialLoop <= 0) {
				bool atLeastOneValidCollapse = false;
				for (auto& e:loop._edges)
					atLeastOneValidCollapse |= e._collapsePt[2] != std::numeric_limits<Primitive>::max();
				if (!atLeastOneValidCollapse)
					stationary = true;
			}
		}

		if (!stationary) {
			// Check signed area agrees with expectation. If the loop has inverted, we assume this loop is subject to precision errors
			// and rewind time to the last event the loop was involved in
			auto signedArea = CalculateSignedAreaAtTime<Primitive>(loop._edges, _vertices, time);
			if (signedArea < 0.f != loop._signedAreaAtLatestEvent < 0.f || signedArea < 0.f != loop._signOfInitialLoop < 0.f)
				stationary = true;
		}

		// clamp time at last event for stationary loops
		if (stationary && loop._lastEventBatchLatest != -std::numeric_limits<Primitive>::max())
			time = std::min(time, loop._lastEventBatchLatest);

		for (auto i=loop._edges.begin(); i!=loop._edges.end(); ++i) {
			// Use the "LastValid" movement for each vertex here. This is required to distinguish between a true part of
			// the wavefront, and a vertex path that collapsed into a 2-vertex loop. Once the loop is reduced to 2-vertices,
			// movement can no longer be calculated, so the vertex is updated to appear stationary. However, that makes it
			// difficult to tell which case this loop originates from
			// It's also awkward to attempt to prevent any vertex path type loops from getting here -- because that might
			// damage event handling for other vertices.
			auto A = PointAndTime<Primitive>{_vertices[i->_head].PositionAtTimeUsingLastValid(time), time};
			auto B = PointAndTime<Primitive>{_vertices[i->_tail].PositionAtTimeUsingLastValid(time), time};
			auto vHead = AddSteinerVertex(result, A);
			auto vTail = AddSteinerVertex(result, B);
			if (vHead != vTail) {
				if (loop._edges.size() > 2) {
					// We allow some "stationary" edges through this path after precision errors. They are really vertex paths,
					// however there may be some errors (such as not having inside/outside faces on every edge). Most likely we will
					// have some vertices getting combined by the higher equivalence thresholds when writing out vertices
					AddEdge(
						result,
						vHead, vTail,
						~0u, GetVertex(i->_tail)._outsideFace,
						stationary ? StraightSkeleton<Primitive>::EdgeType::VertexPath : StraightSkeleton<Primitive>::EdgeType::Wavefront);
				} else {
					// This should be two overlapping edges. They were frozen like this after a motorcycle or a collapse
					// with no further events. It must be a vertex path (going both ways), since there's no area within
					// the wavefront
					assert(loop._edges[0]._head == loop._edges[1]._tail && loop._edges[0]._tail == loop._edges[1]._head);
					assert(GetVertex(i->_head)._outsideFace != ~0u);
					assert(GetVertex(i->_tail)._outsideFace != ~0u);
					AddEdge(
						result,
						vHead, vTail,
						GetVertex(i->_head)._outsideFace, GetVertex(i->_tail)._outsideFace,
						StraightSkeleton<Primitive>::EdgeType::VertexPath);
				}
			}
			// note that AddEdge() will reject the VertexPath for the stationary vertex (if this is actually a vertex path)
			AddEdge(
				result,
				vTail,
				(i->_tail < _boundaryPointCount) ? i->_tail : AddSteinerVertex(result, _vertices[i->_tail]._anchor0),
				GetVertex(i->_tail)._insideFace, GetVertex(i->_tail)._outsideFace,
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
			if (Equivalent(vertices[v], vertices[(v+1)%vertices.size()], GetEpsilon<Primitive>()))
				Throw(std::runtime_error("Duplicate sequential vertices in straight skeleton input"));	// duplicate vertices will throw off the vertex paths, and cause a lot of unnecessary complications
			_graph->_originalBoundaryEdges.push_back({vertexOffset + unsigned((v+1)%vertices.size()), vertexOffset + unsigned(v)});
			_graph->_vertices.emplace_back(PointAndTime<Primitive>{vertices[v], Primitive(0)}, PointAndTime<Primitive>{vertices[v], Primitive(0)}, FaceId(vertexOffset+((v+vertices.size()-1)%vertices.size())), FaceId(vertexOffset+v));
		}
		loop._loopId = _graph->_nextLoopId++;
		loop._signedAreaAtLatestEvent = CalculateSignedAreaAtTime<Primitive>(loop._edges, _graph->_vertices, 0);
		loop._signOfInitialLoop = std::copysign(Primitive(1), loop._signedAreaAtLatestEvent);
		_graph->_loops.emplace_back(std::move(loop));
		_graph->_boundaryPointCount += vertices.size();
	}

	T1(Primitive) void StraightSkeletonCalculator<Primitive>::AddLoop(size_t count, const Primitive xComponents[], const Primitive yComponents[], size_t stride)
	{
		assert(count >= 2);
		WavefrontLoop<Primitive> loop;
		loop._edges.reserve(count);
		_graph->_vertices.reserve(_graph->_vertices.size() + count);
		unsigned vertexOffset = (unsigned)_graph->_vertices.size();
		for (size_t v=0; v<count; ++v) {
			loop._edges.emplace_back(WavefrontEdge<Primitive>{vertexOffset + unsigned((v+1)%count), vertexOffset + unsigned(v)});
			if (	Equivalent(*PtrAdd(xComponents, v*stride), *PtrAdd(xComponents, ((v+1)%count)*stride), GetEpsilon<Primitive>())
				&& 	Equivalent(*PtrAdd(yComponents, v*stride), *PtrAdd(yComponents, ((v+1)%count)*stride), GetEpsilon<Primitive>()))
				Throw(std::runtime_error("Duplicate sequential vertices in straight skeleton input"));	// duplicate vertices will throw off the vertex paths, and cause a lot of unnecessary complications
			_graph->_originalBoundaryEdges.push_back({vertexOffset + unsigned((v+1)%count), vertexOffset + unsigned(v)});
			_graph->_vertices.emplace_back(
				PointAndTime<Primitive>{*PtrAdd(xComponents, v*stride), *PtrAdd(yComponents, v*stride), Primitive(0)},
				PointAndTime<Primitive>{*PtrAdd(xComponents, v*stride), *PtrAdd(yComponents, v*stride), Primitive(0)},
				FaceId(vertexOffset+((v+count-1)%count)), FaceId(vertexOffset+v));
		}
		loop._loopId = _graph->_nextLoopId++;
		loop._signedAreaAtLatestEvent = CalculateSignedAreaAtTime<Primitive>(loop._edges, _graph->_vertices, 0);
		loop._signOfInitialLoop = std::copysign(Primitive(1), loop._signedAreaAtLatestEvent);
		_graph->_loops.emplace_back(std::move(loop));
		_graph->_boundaryPointCount += count;
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

	T1(Primitive) std::vector<unsigned> StraightSkeleton<Primitive>::WavefrontLoops() const
	{
		std::vector<std::pair<unsigned, unsigned>> segmentSoup;
		for (auto&e:_edges)
			if (e._type == EdgeType::Wavefront)
				segmentSoup.emplace_back(e._head, e._tail);
		// We shouldn't need the edges in _unplacedEdges, so long as each edge has been correctly
		// assigned to it's source face
		return AsVertexLoopsDirected(MakeIteratorRange(segmentSoup));
	}

	T1(Primitive) std::vector<unsigned> StraightSkeleton<Primitive>::VertexLoopsForFace(unsigned faceIdx) const
	{
		if (_edgesByFace[faceIdx].empty()) return {};

		std::vector<std::pair<unsigned, unsigned>> segmentSoup;
		segmentSoup.reserve(_edgesByFace[faceIdx].size());
		size_t offset = 0;
		while (offset < _edgesByFace[faceIdx].size() && _edgesByFace[faceIdx][offset]._type != EdgeType::Wavefront && _edgesByFace[faceIdx][offset]._type != EdgeType::OriginalBoundary) ++offset;
		for (size_t c=0; c<_edgesByFace[faceIdx].size(); ++c) {
			auto& e = _edgesByFace[faceIdx][(c+offset)%_edgesByFace[faceIdx].size()];
			segmentSoup.emplace_back(e._head, e._tail);
		}
		return AsVertexLoopsDirected(MakeIteratorRange(segmentSoup));
	}

	T1(Primitive) Primitive StraightSkeleton<Primitive>::LastEventTime() const
	{
		Primitive result = 0.f;
		for (auto&v:_steinerVertices)
			result = std::max(result, v[2]);
		return result;
	}

	T1(Primitive) static bool NonColinearLineLineIntersection(
		Vector2T<Primitive> firstSegment0, Vector2T<Primitive> firstSegment1,
		Vector2T<Primitive> secondSegment0, Vector2T<Primitive> secondSegment1)
	{
		// Assuming that the line segments are not colinear, there is an intersection between these segments, only if:
		// (firstSegment0, firstSegment1, secondSegment0) and (firstSegment0, firstSegment1, secondSegment1) have different winding determinants signs
		// (secondSegment0, secondSegment1, firstSegment0) and (secondSegment0, secondSegment1, firstSegment1) have different winding determinants signs

		auto A = WindingDeterminant(firstSegment0, firstSegment1, secondSegment0);
		auto B = WindingDeterminant(firstSegment0, firstSegment1, secondSegment1);
		if ((A < 0) == (B < 0)) return false;

		auto C = WindingDeterminant(secondSegment0, secondSegment1, firstSegment0);
		auto D = WindingDeterminant(secondSegment0, secondSegment1, firstSegment1);
		return (C < 0) != (D < 0);
	}

	T1(Primitive) bool ValidatePolygonLoop(IteratorRange<const Vector2T<Primitive>*> vertices)
	{
		if (vertices.size() <= 2) return false;

		{
			auto prevV = vertices.end()-1;
			auto prevPrevV = vertices.end()-2;
			for (auto v=vertices.begin(); v!=vertices.end(); prevPrevV=prevV, prevV=v, ++v) {
				// check if an edge is colinear with the next connected edge
				auto windingType = CalculateWindingType(*prevPrevV, *prevV, *v, GetEpsilon<Primitive>());
				if (windingType.first == WindingType::Straight)
					return false;
			}
		}

		// look for duplicated vertices
		for (auto v=vertices.begin()+1; v!=vertices.end(); ++v)
			for (auto v2=vertices.begin(); v2!=v; ++v2)
				if (Equivalent(*v, *v2, GetEpsilon<Primitive>())) {
					return false;
				}

		// look for edges crossing other edges
		{
			for (auto v=vertices.begin(); (v+2)!=vertices.end(); ++v) {
				for (auto v2=v+2; (v2+1)!=vertices.end(); ++v2) {
					// testing v --> v+1 against prevV2 --> v2
					if (NonColinearLineLineIntersection(*v, *(v+1), *v2, *(v2+1)))
						return false;
				}
			}
			for (auto v=vertices.begin()+1; (v+2)!=vertices.end(); ++v)
				if (NonColinearLineLineIntersection(*(vertices.end()-1), *vertices.begin(), *v, *(v+1)))
					return false;
		}

		// survived the gauntlet
		return true;
	}

	template class StraightSkeleton<float>;
	template class StraightSkeleton<double>;
	template class StraightSkeletonCalculator<float>;
	template class StraightSkeletonCalculator<double>;
	template bool ValidatePolygonLoop(IteratorRange<const Vector2T<float>*>);
	template bool ValidatePolygonLoop(IteratorRange<const Vector2T<double>*>);

}
