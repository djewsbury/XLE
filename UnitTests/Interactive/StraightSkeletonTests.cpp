// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputListener.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../Math/ProjectionMath.h"
#include "../../Math/Transformations.h"
#include "../../Math/Geometry.h"
#include "../../Math/StraightSkeleton.h"
#include "../../OSServices/Log.h"
#include "../../Utility/ArithmeticUtils.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <random>
#include <fstream>
#include <filesystem>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	class HexCellField
	{
	public:
		std::vector<Int2> _enabledCells;

		struct BoundaryGroup
		{
			std::vector<Int2> _boundaryCells;
		};
		std::vector<BoundaryGroup> _interiorGroups;
		BoundaryGroup _exteriorGroup;
	};

	void GetAdjacentCells(Int2 dst[6], Int2 centerCell)
	{
		// in our coordinate cell, we imagine the hex cell having alternating
		// rows as we preceed up the Y axis, each offset half a hex
		if (centerCell[1] & 1) {
			// odds
			dst[0] = centerCell + Int2( 0,  1);       // top left
			dst[1] = centerCell + Int2( 1,  1);       // top right
			dst[2] = centerCell + Int2(-1,  0);       // left
			dst[3] = centerCell + Int2( 1,  0);       // right
			dst[4] = centerCell + Int2( 0, -1);       // bottom left
			dst[5] = centerCell + Int2( 1, -1);       // bottom right
		} else {
			// evens
			dst[0] = centerCell + Int2(-1,  1);       // top left
			dst[1] = centerCell + Int2( 0,  1);       // top right
			dst[2] = centerCell + Int2(-1,  0);       // left
			dst[3] = centerCell + Int2( 1,  0);       // right
			dst[4] = centerCell + Int2(-1, -1);       // bottom left
			dst[5] = centerCell + Int2( 0, -1);       // bottom right
		}
	}

	// counter clockwise
	//      0
	//     / \
	//    1   5
	//    |   |
	//    2   4
	//    \  /
	//     3
	//   ^
	//   |
	//   Y    X -->
	static const float s_cos30 = std::sqrtf(3.f / 4.f);
	static const float s_2cos30 = std::sqrtf(3.f);
	static Float2 s_hexCornersEvens[] {
		Float2 { 0.f, 1.0f },
		Float2 { -s_cos30, 0.5f },
		Float2 { -s_cos30, -0.5f },
		Float2 { 0.f, -1.0f },
		Float2 { s_cos30, -0.5f },
		Float2 { s_cos30, 0.5f }
	};

	static Float2 s_hexCornersOdds[] {
		Float2 { s_cos30, 1.0f },
		Float2 { 0.f, 0.5f },
		Float2 { 0.f, -0.5f },
		Float2 { s_cos30, -1.0f },
		Float2 { s_2cos30, -0.5f },
		Float2 { s_2cos30, 0.5f }
	};

	// Matches order of adjacent cells from GetAdjacentCells
	static const std::pair<unsigned, unsigned> s_hexEdges[] {
		std::make_pair(0, 1),
		std::make_pair(5, 0),
		std::make_pair(1, 2),
		std::make_pair(4, 5),
		std::make_pair(2, 3),
		std::make_pair(3, 4),
	};

	static bool IsInteriorBoundaryGroup(
		const HexCellField::BoundaryGroup& group, 
		const std::vector<Int2>& enabledCells)
	{
		assert(!group._boundaryCells.empty());
		assert(!enabledCells.empty());

		// We need at least 6 cells to define an exterior group, so
		// smaller groups can only be interior
		if (group._boundaryCells.size() < 6) return true;

		// We're going to use a the polygon line testing trick here (even intersections means a point is inside, odd intersections means it's inside)
		// however, we'll only use lines parallel to the X axis, because the intersection tests become much simplier
		// There must always be at least one boundary cell that is immediately to the left or right of a enabled cell

		auto i = group._boundaryCells.begin();
		bool goingNegativeX = false;
		for (;i != group._boundaryCells.end(); ++i) {
			Int2 check((*i)[0] - 1, (*i)[1]);
			if (std::find(enabledCells.begin(), enabledCells.end(), check) != enabledCells.end()) {
				goingNegativeX = true;
				break;
			}

			check = Int2((*i)[0] + 1, (*i)[1]);
			if (std::find(enabledCells.begin(), enabledCells.end(), check) != enabledCells.end()) {
				goingNegativeX = false;
				break;
			}
		}
		assert(i != group._boundaryCells.end());

		auto i2 = i+1;
		unsigned intersectionCount = 1;		// first intersection from the boundary cell through it's edge always counts 
		for(; i2 != group._boundaryCells.end(); ++i2) {
			assert(*i2 != *i);
			if ((*i2)[1] != (*i)[1]) continue;
			if (goingNegativeX) {
				if ((*i2)[0] > (*i)[0]) continue;
			} else {
				if ((*i2)[0] < (*i)[0]) continue;
			}

			// We're on the same Y coord, and we're on the right side of *i
			// check both the left and right edges of this cell
			Int2 check((*i2)[0] - 1, (*i2)[1]);
			if (std::find(enabledCells.begin(), enabledCells.end(), check) != enabledCells.end())
				++intersectionCount;

			check = Int2((*i2)[0] + 1, (*i2)[1]);
			if (std::find(enabledCells.begin(), enabledCells.end(), check) != enabledCells.end())
				++intersectionCount;
		}

		return intersectionCount & 1;		// odd == interior
	}

	static HexCellField CreateRandomHexCellField(unsigned cellCount, std::mt19937_64& rng)
	{
		assert(cellCount > 0);
		HexCellField result;

		// Each time, select a cell from the boundary, and make it an enabled cell
		// update the boundary as we go along
		// could be done much faster with just a little sorting

		result._enabledCells.push_back(Int2(0, 0));
		Int2 adjacent[6];
		GetAdjacentCells(adjacent, Int2(0, 0));
		std::vector<Int2> workingBoundaryCells;
		for (auto c:adjacent) workingBoundaryCells.push_back(c);

		while (result._enabledCells.size() < cellCount) {
			assert(workingBoundaryCells.size());
			auto idx = std::uniform_int_distribution<size_t>(0, workingBoundaryCells.size()-1)(rng);
			auto i = workingBoundaryCells.begin() + idx;
			assert(std::find(result._enabledCells.begin(), result._enabledCells.end(), *i) == result._enabledCells.end());
			GetAdjacentCells(adjacent, *i);
			result._enabledCells.push_back(*i);
			workingBoundaryCells.erase(i);

			for (auto c:adjacent) {
				if (std::find(result._enabledCells.begin(), result._enabledCells.end(), c) != result._enabledCells.end()) continue;
				if (std::find(workingBoundaryCells.begin(), workingBoundaryCells.end(), c) != workingBoundaryCells.end()) continue;
				workingBoundaryCells.push_back(c);
			}
		}

		// Separate the boundary cells by the groups they belong to by just walking through
		// their connections
		while (!workingBoundaryCells.empty()) {
			std::vector<Int2> localNetwork;
			localNetwork.push_back(*(workingBoundaryCells.end()-1));
			workingBoundaryCells.erase(workingBoundaryCells.end()-1);

			HexCellField::BoundaryGroup group;
			while (!localNetwork.empty()) {
				GetAdjacentCells(adjacent, *(localNetwork.end()-1));
				group._boundaryCells.push_back(*(localNetwork.end()-1));
				localNetwork.erase(localNetwork.end()-1);

				for (auto a:adjacent) {
					auto i = std::find(workingBoundaryCells.begin(), workingBoundaryCells.end(), a);
					if (i == workingBoundaryCells.end()) continue;
					localNetwork.push_back(*i);
					workingBoundaryCells.erase(i);
				}
			}

			// We need to know if each boundary group is interior or exterior
			bool interior = IsInteriorBoundaryGroup(group, result._enabledCells);
			if (interior) {
				result._interiorGroups.push_back(std::move(group));
			} else {
				// We can only have one exterior group because we're creating a contiguous shape
				assert(result._exteriorGroup._boundaryCells.empty());
				result._exteriorGroup = std::move(group);
			}
		}
		
		return result;
	}

	static HexCellField CreateRegularHexField(unsigned radius)
	{
		assert(radius > 0);
		HexCellField result;

		// Each time, select a cell from the boundary, and make it an enabled cell
		// update the boundary as we go along
		// could be done much faster with just a little sorting

		result._enabledCells.push_back(Int2(0, 0));
		Int2 adjacent[6];
		GetAdjacentCells(adjacent, Int2(0, 0));

		std::vector<Int2> workingBoundaryCells;
		for (auto c:adjacent) workingBoundaryCells.push_back(c);

		for (unsigned r=1; r<radius; ++r) {
			auto nextBoundaryCells = std::move(workingBoundaryCells);

			for (auto cell:nextBoundaryCells) {
				assert(std::find(result._enabledCells.begin(), result._enabledCells.end(), cell) == result._enabledCells.end());
				result._enabledCells.push_back(cell);

				GetAdjacentCells(adjacent, cell);
				for (auto a:adjacent) {
					auto i = std::find(workingBoundaryCells.begin(), workingBoundaryCells.end(), a);
					if (i != workingBoundaryCells.end()) continue;
					i = std::find(result._enabledCells.begin(), result._enabledCells.end(), a);
					if (i != result._enabledCells.end()) continue;
					i = std::find(nextBoundaryCells.begin(), nextBoundaryCells.end(), a);
					if (i != nextBoundaryCells.end()) continue;
					workingBoundaryCells.push_back(a);
				}
			}
		}
		result._exteriorGroup._boundaryCells = std::move(workingBoundaryCells);

		return result;
	}

	static void DrawBoundary(RenderOverlays::IOverlayContext& overlayContext, const HexCellField& cellField, const HexCellField::BoundaryGroup& group, RenderOverlays::ColorB color)
	{
		std::vector<Float3> boundaryLines;
		boundaryLines.reserve(group._boundaryCells.size() * 2 * 6);

		// Super primitive; but.. for each boundary cell, check which neighbours are enabled and draw
		// a line between them
		for (auto cell:group._boundaryCells) {
			Int2 adjacent[6];
			GetAdjacentCells(adjacent, cell);
			for (unsigned a=0; a<dimof(adjacent); ++a) {
				if (std::find(cellField._enabledCells.begin(), cellField._enabledCells.end(), adjacent[a]) == cellField._enabledCells.end()) continue;
				Float2 cellCenter { s_2cos30 * (float)cell[0], 1.5f * (float)cell[1] };
				if (cell[1] & 1) {
					// odd
					boundaryLines.push_back(Float3(s_hexCornersOdds[s_hexEdges[a].first] + cellCenter, 0.f));
					boundaryLines.push_back(Float3(s_hexCornersOdds[s_hexEdges[a].second] + cellCenter, 0.f));
				} else {
					// even
					boundaryLines.push_back(Float3(s_hexCornersEvens[s_hexEdges[a].first] + cellCenter, 0.f));
					boundaryLines.push_back(Float3(s_hexCornersEvens[s_hexEdges[a].second] + cellCenter, 0.f));
				}
			}
		}

		overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, boundaryLines.data(), boundaryLines.size(), color);
	}

	template<typename T> StraightSkeleton<T> CalculateStraightSkeleton(IteratorRange<const Vector2T<T>*> vertices, T maxInset = std::numeric_limits<T>::max())
	{
		StraightSkeletonCalculator<T> calculator;
		calculator.AddLoop(vertices);
		return calculator.Calculate(maxInset);
	}

	template<typename Primitive> static std::vector<Vector2T<Primitive>> MakeBoundaryLoop(StraightSkeletonCalculator<Primitive>& calculator, const HexCellField& cellField, const HexCellField::BoundaryGroup& group)
	{
		std::vector<Vector2T<Primitive>> boundaryLines;
		boundaryLines.reserve(group._boundaryCells.size() * 2 * 6);

		for (auto cell:group._boundaryCells) {
			Int2 adjacent[6];
			GetAdjacentCells(adjacent, cell);
			for (unsigned a=0; a<dimof(adjacent); ++a) {
				if (std::find(cellField._enabledCells.begin(), cellField._enabledCells.end(), adjacent[a]) == cellField._enabledCells.end()) continue;
				// cellCenter[0] = std::sqrt(Primitive(3)) * (Primitive)cell[0]
				// However, to retain more digits of precision, we will do the sqrt last
				Vector2T<Primitive> cellCenter { std::sqrt(Primitive(3) * (Primitive)cell[0] * (Primitive)cell[0]), Primitive(1.5) * (Primitive)cell[1] };
				if (cell[0] < 0) cellCenter[0] = -cellCenter[0];
				if (cell[1] & 1) {
					// odd
					boundaryLines.push_back(s_hexCornersOdds[s_hexEdges[a].first] + cellCenter);
					boundaryLines.push_back(s_hexCornersOdds[s_hexEdges[a].second] + cellCenter);
				} else {
					// even
					boundaryLines.push_back(s_hexCornersEvens[s_hexEdges[a].first] + cellCenter);
					boundaryLines.push_back(s_hexCornersEvens[s_hexEdges[a].second] + cellCenter);
				}
			}
		}

		assert(!boundaryLines.empty());
		
		std::vector<Vector2T<Primitive>> boundary; 
		boundary.reserve(1+boundaryLines.size()/2);
		boundary.push_back(*(boundaryLines.end()-2));
		boundary.push_back(*(boundaryLines.end()-1));
		boundaryLines.erase(boundaryLines.end()-2, boundaryLines.end());
		while (boundaryLines.size() > 2) {
			auto i = boundaryLines.begin();
			for (;i!=boundaryLines.end(); i+=2)
				if (Equivalent(*i, *(boundary.end()-1), 1e-3f))
					break;
			assert(i != boundaryLines.end());
			boundary.push_back(*(i+1));
			boundaryLines.erase(i, i+2);
		}

		// last line should wrap around back to the first
		assert(Equivalent(*(boundaryLines.end()-1), boundary[0], 1e-3f));

		// reverse to get the ordering that the straight skeleton algorithm is expecting
		std::reverse(boundary.begin(), boundary.end());
		return boundary;
	}

	template<typename Primitive> static std::vector<unsigned> AsFaceOrderedVertexList(IteratorRange<const typename StraightSkeleton<Primitive>::Edge*> edges, unsigned faceIdx, unsigned faceCount)
	{
		if (!edges.size()) return {};
		std::vector<unsigned> result;

		auto originalBoundaryEdge = std::find_if(edges.begin(), edges.end(), [](const auto& c) { return c._type == StraightSkeleton<Primitive>::EdgeType::OriginalBoundary; });
		assert(originalBoundaryEdge != edges.end());

		unsigned searchingVertex = originalBoundaryEdge->_head;
		unsigned endVtx = originalBoundaryEdge->_tail;
		unsigned prevVtx = originalBoundaryEdge->_tail;
		result.push_back(prevVtx);
		while (searchingVertex != endVtx) {
			result.push_back(searchingVertex);
			auto out = std::find_if(edges.begin(), edges.end(), [searchingVertex, prevVtx](const auto& c) { return c._tail == searchingVertex && c._head != prevVtx; });
			auto in = std::find_if(edges.begin(), edges.end(), [searchingVertex, prevVtx](const auto& c) { return c._head == searchingVertex && c._tail != prevVtx; });
			assert(out!=edges.end()||in!=edges.end());
			prevVtx = searchingVertex;
			searchingVertex = (out != edges.end()) ? out->_head : in->_tail;
		}
		return result;
	}

	template<typename Primitive> static void WriteStraightSkeletonAsPLY(std::ostream& str, const StraightSkeleton<Primitive>& skeleton, IteratorRange<const Vector2T<Primitive>*> boundaryLoop)
	{
		str << "ply" << std::endl;
		str << "format ascii 1.0" << std::endl;
		str << "element vertex " << skeleton._boundaryPointCount + skeleton._steinerVertices.size() << std::endl;
		str << "property float x" << std::endl;
		str << "property float y" << std::endl;
		str << "property float z" << std::endl;
		str << "element face " << skeleton._edgesByFace.size() << std::endl;
		str << "property list uchar int vertex_index" << std::endl;
		str << "end_header" << std::endl;

		for (unsigned c=0; c<skeleton._boundaryPointCount; ++c)
			str << boundaryLoop[c][0] << " " << boundaryLoop[c][1] << " 0" << std::endl;
		for (auto v:skeleton._steinerVertices)
			str << v[0] << " " << v[1] << " " << v[2] << std::endl;

		for (auto f=skeleton._edgesByFace.begin(); f!=skeleton._edgesByFace.end(); ++f) {
			auto orderedVertices = AsFaceOrderedVertexList<Primitive>(*f, (unsigned)std::distance(skeleton._edgesByFace.begin(), f), (unsigned)skeleton._edgesByFace.size());
			str << orderedVertices.size();
			for (auto v:orderedVertices) str << " " << v;
			str << std::endl;
		}
	}

	template<typename Primitive> static void SaveStraightSkeletonToFile(const StraightSkeleton<Primitive>& ss, IteratorRange<const Float2*> boundaryLoop, const std::string& name)
	{
		auto outputName = std::filesystem::temp_directory_path() / "xle-unit-tests" / (name + ".ply");
		std::ofstream plyOut(outputName);
		WriteStraightSkeletonAsPLY(plyOut, ss, boundaryLoop);
	}
	
	static void SaveStraightSkeletonToFile(IteratorRange<const Float2*> boundaryLoop, const std::string& name)
	{
		auto ss = CalculateStraightSkeleton(boundaryLoop);
		SaveStraightSkeletonToFile(ss, boundaryLoop, name);
	}

	template<typename Primitive>
		class StraightSkeletonPreview
	{
	public:
		StraightSkeleton<Primitive> _straightSkeleton;
		using BoundaryLoop = std::vector<Vector2T<Primitive>>;
		std::vector<BoundaryLoop> _orderedBoundaryPts;

		static constexpr unsigned BoundaryVertexFlag = 1u<<31u;

		Float3 GetPt(unsigned ptIdx) const
		{
			auto i = _orderedBoundaryPts.begin();
			while (i!=_orderedBoundaryPts.end() && ptIdx >= i->size()) {
				ptIdx -= i->size();
				++i;
			}
			if (i!=_orderedBoundaryPts.end()) {
				return Float3 { (*i)[ptIdx], 0 };
			} else {
				REQUIRE(ptIdx < _straightSkeleton._steinerVertices.size());
				return _straightSkeleton._steinerVertices[ptIdx];
			}
		}

		void Draw(RenderOverlays::IOverlayContext& overlayContext) const
		{
			const RenderOverlays::ColorB waveFrontColor { 64, 230, 64 };
			const RenderOverlays::ColorB pathColor { 64, 64, 230 };
			const RenderOverlays::ColorB originalShapeColor { 128, 128, 128 };

			std::vector<Float3> wavefrontLines, pathLines;
			wavefrontLines.reserve(_straightSkeleton._edges.size() * 2);
			for (const auto& e:_straightSkeleton._edges) {
				if (e._type == StraightSkeleton<Primitive>::EdgeType::Wavefront) {
					REQUIRE(e._head >= _straightSkeleton._boundaryPointCount);
					REQUIRE(e._tail >= _straightSkeleton._boundaryPointCount);
					wavefrontLines.push_back(GetPt(e._head));
					wavefrontLines.push_back(GetPt(e._tail));
				} else {
					assert(e._type == StraightSkeleton<Primitive>::EdgeType::VertexPath);
					pathLines.push_back(GetPt(e._head));
					pathLines.push_back(GetPt(e._tail));
				}
			}

			overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, wavefrontLines.data(), wavefrontLines.size(), waveFrontColor);
			overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, pathLines.data(), pathLines.size(), pathColor);

			for (auto& b:_orderedBoundaryPts) {
				std::vector<Float3> originalShapeLines;
				originalShapeLines.reserve(b.size()*2);

				for (size_t c=0; c<b.size(); ++c) {
					originalShapeLines.push_back(Float3(b[c], 0));
					originalShapeLines.push_back(Float3(b[(c+1)%b.size()], 0));
				}
				overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, originalShapeLines.data(), originalShapeLines.size(), originalShapeColor);
			}

			const float vertexSize = 0.1f;
			for (auto& b:_orderedBoundaryPts)
				for (unsigned c=0; c<b.size(); ++c)
					overlayContext.DrawQuad(RenderOverlays::ProjectionMode::P2D, Float3{b[c] - Float2{vertexSize, vertexSize}, 0.f}, Float3{b[c] + Float2{vertexSize, vertexSize}, 0.f}, RenderOverlays::ColorB(0x7f, c>>8, c&0xff));
			for (unsigned c=0; c<_straightSkeleton._steinerVertices.size(); ++c)
				overlayContext.DrawQuad(RenderOverlays::ProjectionMode::P2D, Float3{Truncate(_straightSkeleton._steinerVertices[c]) - Float2{vertexSize, vertexSize}, 0.f}, Float3{Truncate(_straightSkeleton._steinerVertices[c]) + Float2{vertexSize, vertexSize}, 0.f}, RenderOverlays::ColorB(0x7f, (c+_orderedBoundaryPts.size())>>8, (c+_orderedBoundaryPts.size())&0xff));
		}

		StraightSkeletonPreview(const HexCellField& cellField, Primitive maxInset = std::numeric_limits<Primitive>::max())
		{
			StraightSkeletonCalculator<Primitive> calculator;
			
			{
				auto boundary = MakeBoundaryLoop<Primitive>(calculator, cellField, cellField._exteriorGroup);
				calculator.AddLoop(MakeIteratorRange(boundary));
				_orderedBoundaryPts.push_back(std::move(boundary));
			}
			for (auto& group:cellField._interiorGroups) {
				auto boundary = MakeBoundaryLoop<Primitive>(calculator, cellField, group);
				calculator.AddLoop(MakeIteratorRange(boundary));
				_orderedBoundaryPts.push_back(std::move(boundary));
			}

			_straightSkeleton = calculator.Calculate(maxInset);
			std::vector<Vector2T<Primitive>> flatteningBoundaryLoop;
			for (const auto&o:_orderedBoundaryPts) flatteningBoundaryLoop.insert(flatteningBoundaryLoop.begin(), o.begin(), o.end());
			SaveStraightSkeletonToFile<Primitive>(_straightSkeleton, MakeIteratorRange(flatteningBoundaryLoop), "straightskeleton-hexgrid");
		}

		StraightSkeletonPreview(IteratorRange<const Vector2T<Primitive>*> inputPts, Primitive maxInset = std::numeric_limits<Primitive>::max())
		: _straightSkeleton(CalculateStraightSkeleton(inputPts, maxInset))
		{
			std::vector<Vector2T<Primitive>> boundary(inputPts.begin(), inputPts.end());
			_orderedBoundaryPts.push_back(std::move(boundary));
		}

		void AddBoundaryLoop(BoundaryLoop&& boundary) { _orderedBoundaryPts.push_back(std::move(boundary)); }

		StraightSkeletonPreview(StraightSkeleton<Primitive>&& input)
		: _straightSkeleton(std::move(input))
		{
		}

		StraightSkeletonPreview() {}
	};

	static RenderCore::Techniques::CameraDesc StartingCamera(float scale = 1.f)
	{
		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, 0.0f, -1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{0.0f, 0.0f, 200.0f});
		visCamera._projection = RenderCore::Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = -50.f * scale;
		visCamera._right = 50.f * scale;
		visCamera._top = -50.f * scale;
		visCamera._bottom = 50.f * scale;
		return visCamera;
	}

	TEST_CASE( "StraightSkeletonHexGrid", "[math]" )
	{
		// static constexpr unsigned randomCellCount = 9u;
		static constexpr unsigned randomCellCount = 32u;
		// static constexpr unsigned randomCellCount = 256u;
		// static constexpr unsigned randomCellCount = 2048u;

		using namespace RenderCore;
		class HexGridStraightSkeleton : public IInteractiveTestOverlay
		{
		public:
			virtual void Render(
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				{
					auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
						threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);
					DrawBoundary(*overlayContext, _cellField, _cellField._exteriorGroup, RenderOverlays::ColorB{32, 190, 32});
					for (const auto&g:_cellField._interiorGroups)
						DrawBoundary(*overlayContext, _cellField, g, RenderOverlays::ColorB{64, 140, 210});
					_preview.Draw(*overlayContext);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const PlatformRig::InputSnapshot& evnt,
				IInteractiveTestHelper& testHelper) override
			{
				if (evnt._pressedChar == 'r') {
					_cellField = CreateRandomHexCellField(randomCellCount, _rng);
					_preview = StraightSkeletonPreview<float>(_cellField, maxInset);
				} else if (evnt._pressedChar == 'q' || evnt._pressedChar == 'Q') {
					maxInset += (evnt._pressedChar == 'Q') ? (20.f * 0.01f) : 0.01f;
					_preview = StraightSkeletonPreview<float>(_cellField, maxInset);
				} else if (evnt._pressedChar == 'a' || evnt._pressedChar == 'A') {
					maxInset -= (evnt._pressedChar == 'A') ? (20.f * 0.01f) : 0.01f;
					_preview = StraightSkeletonPreview<float>(_cellField, maxInset);
				} else if (evnt._pressedChar == ' ') {
					_preview = StraightSkeletonPreview<float>(_cellField, maxInset);
				}
				return false;
			}

			HexCellField _cellField;
			StraightSkeletonPreview<float> _preview;
			std::mt19937_64 _rng;
			float maxInset = 10.f;
			
			HexGridStraightSkeleton(std::mt19937_64&& rng)
			: _rng(std::move(rng))
			{
				_cellField = CreateRandomHexCellField(randomCellCount, _rng);
				// _cellField = CreateRegularHexField(5);
				_preview = StraightSkeletonPreview<float>(_cellField, maxInset);
			}
		};

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		{
			std::mt19937_64 rng(619047819);
			auto tester = std::make_shared<HexGridStraightSkeleton>(std::move(rng));
			testHelper->Run(StartingCamera(0.5f), tester);
		}
	}

	class BasicDrawStraightSkeleton : public IInteractiveTestOverlay
	{
	public:
		virtual void Render(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper) override
		{
			{
				auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
					threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);
				for (const auto& preview:_previews)
					preview.Draw(*overlayContext);
			}

			auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, RenderCore::LoadStore::Clear);
			testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
		}

		std::vector<StraightSkeletonPreview<float>> _previews;
	};

	static unsigned int AsUnsignedBits(float input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; unsigned int i; };
		Converter c; c.f = input; 
		return c.i;
	}

	static float AsFloatBits(unsigned input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; unsigned int i; };
		Converter c; c.i = input; 
		return c.f;
	}

	static unsigned ReverseBits(unsigned input)
	{
		// from https://graphics.stanford.edu/~seander/bithacks.html
		input = ((input >> 1u) & 0x55555555u) | ((input & 0x55555555u) << 1u);
		input = ((input >> 2u) & 0x33333333u) | ((input & 0x33333333u) << 2u);
		input = ((input >> 4u) & 0x0F0F0F0Fu) | ((input & 0x0F0F0F0Fu) << 4u);
		input = ((input >> 8u) & 0x00FF00FFu) | ((input & 0x00FF00FFu) << 8u);
		input = ( input >> 16u              ) | ( input               << 16u);
		return input;
	}

	TEST_CASE( "StraightSkeletonSimpleShapes", "[math]" )
	{
		using namespace RenderCore;
		
		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		Float2 rectangleCollapse[] = {
			Float2 {  10.f,  15.f } + Float2 { 25, 25 },
			Float2 { -10.f,  15.f } + Float2 { 25, 25 },
			Float2 { -10.f, -15.f } + Float2 { 25, 25 },
			Float2 {  10.f, -15.f } + Float2 { 25, 25 }
		};

		Float2 singleMotorcycle[] = {
			Float2 { -10.f,  7.5f } + Float2 { 25, -25 },
			Float2 {   0.f,   0.f } + Float2 { 25, -25 },
			Float2 { -10.f, -7.5f } + Float2 { 25, -25 },
			Float2 {  10.f, -15.f } + Float2 { 25, -25 },
			Float2 {  10.f,  15.f } + Float2 { 25, -25 }
		};

		Float2 doubleMotorcycle[] = {
			Float2 { -10.f,  7.5f } + Float2 { -25, -25 },
			Float2 { -2.5f,   0.f } + Float2 { -25, -25 },
			Float2 { -10.f, -7.5f } + Float2 { -25, -25 },
			Float2 {   0.f, -15.f } + Float2 { -25, -25 },
			Float2 {  10.f, -7.5f } + Float2 { -25, -25 },
			Float2 {  2.5f,   0.f } + Float2 { -25, -25 },
			Float2 {  10.f,  7.5f } + Float2 { -25, -25 },
			Float2 {   0.f,  15.f } + Float2 { -25, -25 }
		};

		Float2 colinearCollapse[] = {
			Float2 { -10.f,  2.5f } + Float2 { -25,  25 },
			Float2 { -10.f, -2.5f } + Float2 { -25,  25 },
			Float2 {   0.f, -15.f } + Float2 { -25,  25 },
			Float2 {  10.f, -2.5f } + Float2 { -25,  25 },
			Float2 {  10.f,  2.5f } + Float2 { -25,  25 },
			Float2 {   0.f,  15.f } + Float2 { -25,  25 }
		};

		Float2 colinearEdges[] = {
			Float2 {   0.f,  10.f },
			Float2 { -15.f,  10.f },
			Float2 { -15.f,   0.f },
			Float2 { -15.f, -10.f },
			Float2 {   0.f, -10.f },
			Float2 {  15.f, -10.f },
			Float2 {  15.f,   0.f },
			Float2 {  15.f,  10.f }
		};

#if 0
		// While the above shapes can be calculated correctly in their default orientations, sometimes
		// if we rotate them we hit numeric precision issues
		// We want to rotate through every rotation between 0 -> pi, so we'll do this by increasing the
		// integer representation.
		// However -- to go through the numbers a little faster we'll walk through the integer bits in
		// reverse order. This will actually go a little beyond thetaEnd; but that shouldn't matter here 
		unsigned thetaStart = AsUnsignedBits(0.1f), thetaEnd = AsUnsignedBits(gPI);
		unsigned highestDifferentBit = 32 - xl_clz4(thetaEnd-thetaStart);
		unsigned workingBitMask = (1u<<highestDifferentBit) - 1u;
		unsigned i = 0;
		for (unsigned c=0; c<(thetaEnd-thetaStart); ++c) {
			auto reversed = ReverseBits(i) >> (32-highestDifferentBit);
			++reversed;
			i = ReverseBits(reversed << (32-highestDifferentBit));
			auto theta = AsFloatBits(thetaStart + i);

			float sinTheta = std::sin(theta), cosTheta = std::cos(theta);
			std::vector<Float2> rotatedRectangle; rotatedRectangle.insert(rotatedRectangle.end(), rectangleCollapse, &rectangleCollapse[dimof(rectangleCollapse)]);
			std::vector<Float2> rotatedSingleMotorcycle; rotatedSingleMotorcycle.insert(rotatedSingleMotorcycle.end(), singleMotorcycle, &singleMotorcycle[dimof(singleMotorcycle)]);
			std::vector<Float2> rotatedDoubleMotorcycle; rotatedDoubleMotorcycle.insert(rotatedDoubleMotorcycle.end(), doubleMotorcycle, &doubleMotorcycle[dimof(doubleMotorcycle)]);
			std::vector<Float2> rotatedColinearCollapse; rotatedColinearCollapse.insert(rotatedColinearCollapse.end(), colinearCollapse, &colinearCollapse[dimof(colinearCollapse)]);
			std::vector<Float2> rotatedColinearEdges; rotatedColinearEdges.insert(rotatedColinearEdges.end(), colinearEdges, &colinearEdges[dimof(colinearEdges)]);
			for (auto& c:rotatedRectangle) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
			for (auto& c:rotatedSingleMotorcycle) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
			for (auto& c:rotatedDoubleMotorcycle) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
			for (auto& c:rotatedColinearCollapse) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
			for (auto& c:rotatedColinearEdges) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };

			auto s0 = CalculateStraightSkeleton<float>(rotatedRectangle);
			auto s1 = CalculateStraightSkeleton<float>(rotatedSingleMotorcycle);
			auto s2 = CalculateStraightSkeleton<float>(rotatedDoubleMotorcycle);
			auto s3 = CalculateStraightSkeleton<float>(rotatedColinearCollapse);
			auto s4 = CalculateStraightSkeleton<float>(rotatedColinearEdges);
			(void)s0; (void)s1; (void)s2; (void)s3; (void)s4;
		}
#endif

#if 1
		float theta = 2.1267482f;
		float sinTheta = std::sin(theta), cosTheta = std::cos(theta);
		for (auto& c:rectangleCollapse) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
		for (auto& c:singleMotorcycle) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
		for (auto& c:doubleMotorcycle) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
		for (auto& c:colinearCollapse) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
		for (auto& c:colinearEdges) c = Float2 { c[0] * cosTheta + c[1] * sinTheta, c[0] * -sinTheta + c[1] * cosTheta };
#endif

		SaveStraightSkeletonToFile(MakeIteratorRange(rectangleCollapse), "rectangle-collapse");
		SaveStraightSkeletonToFile(MakeIteratorRange(singleMotorcycle), "single-motorcycle");
		SaveStraightSkeletonToFile(MakeIteratorRange(doubleMotorcycle), "double-motorcycle");
		SaveStraightSkeletonToFile(MakeIteratorRange(colinearCollapse), "colinear-collapse");
		SaveStraightSkeletonToFile(MakeIteratorRange(colinearEdges), "colinear-edges");

		{
			auto tester = std::make_shared<BasicDrawStraightSkeleton>();
			tester->_previews.emplace_back(MakeIteratorRange(rectangleCollapse));
			tester->_previews.emplace_back(MakeIteratorRange(singleMotorcycle));
			tester->_previews.emplace_back(MakeIteratorRange(doubleMotorcycle));
			tester->_previews.emplace_back(MakeIteratorRange(colinearCollapse));
			tester->_previews.emplace_back(MakeIteratorRange(colinearEdges));
			testHelper->Run(StartingCamera(), tester);
		}
	}

	TEST_CASE( "StraightSkeletonVariousPolygons", "[math]" )
	{
		using namespace RenderCore;
		
		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		// The polygons here are created from SVG files processed via https://betravis.github.io/shape-tools/path-to-polygon/
		// From https://freesvg.org/eagle-silhouette-clip-art (marked with Licence: Public Domain)
		Float2 eagle[] = {
			Float2{120.520, 135.610}, Float2{111.427, 130.654}, Float2{107.641, 129.755}, Float2{96.377, 123.570}, Float2{79.579, 113.619}, Float2{77.436, 116.008}, Float2{69.080, 120.646}, Float2{63.127, 124.159}, Float2{60.686, 128.619}, Float2{63.812, 129.948}, Float2{64.535, 133.323}, Float2{59.377, 135.066}, Float2{47.723, 132.148}, Float2{36.377, 132.437}, Float2{45.656, 127.619}, Float2{60.062, 122.202}, Float2{64.641, 118.057}, Float2{62.635, 114.088}, Float2{59.027, 104.545}, Float2{52.640, 94.938}, Float2{43.799, 88.835}, Float2{36.023, 77.308}, Float2{28.403, 65.619}, Float2{24.494, 67.369}, Float2{22.776, 67.211}, Float2{30.039, 58.882}, Float2{45.467, 58.151}, Float2{62.980, 46.619}, Float2{82.876, 25.678}, Float2{102.376, 23.158}, Float2{100.857, 25.639}, Float2{110.107, 27.894}, Float2{147.696, 35.297}, Float2{149.324, 38.181}, Float2{145.376, 39.618}, Float2{141.258, 41.260}, Float2{127.886, 42.285}, Float2{110.876, 41.092}, Float2{117.392, 44.910}, Float2{147.212, 58.902}, Float2{147.903, 61.574}, Float2{136.436, 61.635}, Float2{132.679, 60.982}, Float2{136.894, 64.184}, Float2{136.375, 69.754}, Float2{130.135, 68.489}, Float2{131.376, 71.772}, Float2{130.060, 76.429}, Float2{124.264, 75.040}, Float2{118.514, 74.162}, Float2{116.376, 76.082}, Float2{108.876, 80.025}, Float2{101.673, 81.153}, Float2{99.470, 81.827}, Float2{101.558, 84.650}, Float2{96.570, 90.877}, Float2{92.991, 93.094}, Float2{92.027, 97.155}, Float2{95.023, 103.531}, Float2{98.793, 110.490}, Float2{128.126, 131.294}, Float2{131.935, 136.618}, Float2{128.876, 137.618}
		};
		for (auto& c:eagle) c = c * 0.5f - Float2{50, 50};

		// From https://freesvg.org/old-man-with-tail-vector-graphics (marked with Licence: Public Domain)
		Float2 figure0[] = {
			Float2{160.610, 38.505}, Float2{160.610, 35.764}, Float2{162.802, 28.090}, Float2{158.965, 35.216}, Float2{158.965, 28.638}, Float2{156.773, 21.512}, Float2{160.610, 13.838}, Float2{155.129, 13.290}, Float2{141.974, 22.060}, Float2{140.877, 25.897}, Float2{138.136, 26.993}, Float2{126.077, 67.556}, Float2{130.462, 68.104}, Float2{135.396, 62.623}, Float2{142.522, 68.653}, Float2{146.907, 66.461}, Float2{146.907, 62.624}, Float2{143.070, 60.432}, Float2{148.551, 60.432}, Float2{148.551, 68.106}, Float2{140.877, 70.847}, Float2{138.136, 67.558}, Float2{137.040, 71.943}, Float2{133.751, 72.491}, Float2{136.491, 75.780}, Float2{140.329, 75.232}, Float2{137.040, 79.069}, Float2{132.106, 75.780}, Float2{126.625, 80.165}, Float2{132.655, 84.002}, Float2{140.329, 80.713}, Float2{146.906, 82.906}, Float2{149.647, 86.195}, Float2{159.513, 91.129}, Float2{191.854, 105.929}, Float2{200.625, 113.603}, Float2{200.625, 117.988}, Float2{208.299, 127.855}, Float2{227.485, 155.810}, Float2{211.589, 163.484}, Float2{221.456, 170.062}, Float2{222.552, 165.129}, Float2{229.678, 174.447}, Float2{224.744, 174.447}, Float2{228.033, 181.025}, Float2{231.322, 187.055}, Float2{241.737, 195.277}, Float2{258.729, 213.914}, Float2{256.537, 252.833}, Float2{233.515, 267.085}, Float2{232.419, 307.100}, Float2{233.515, 315.323}, Float2{219.263, 310.937}, Float2{217.618, 319.708}, Float2{206.655, 318.063}, Float2{211.041, 324.641}, Float2{215.975, 324.093}, Float2{224.745, 332.863}, Float2{226.390, 339.441}, Float2{222.553, 353.693}, Float2{205.012, 355.885}, Float2{203.368, 348.759}, Float2{181.990, 359.722}, Float2{152.938, 358.077}, Float2{138.686, 361.914}, Float2{144.715, 359.174}, Float2{169.930, 348.211}, Float2{175.959, 343.825}, Float2{180.345, 344.922}, Float2{183.085, 341.633}, Float2{172.122, 343.278}, Float2{159.515, 347.664}, Float2{134.849, 343.278}, Float2{117.856, 342.730}, Float2{123.337, 341.085}, Float2{141.426, 335.604}, Float2{144.167, 336.700}, Float2{148.004, 334.508}, Float2{152.938, 337.249}, Float2{156.775, 335.604}, Float2{158.967, 324.093}, Float2{164.996, 324.093}, Float2{158.419, 320.804}, Float2{164.448, 308.197}, Float2{171.574, 309.842}, Float2{175.959, 314.227}, Float2{173.219, 308.746}, Float2{151.293, 297.783}, Float2{136.492, 299.975}, Float2{137.589, 285.175}, Float2{138.137, 274.760}, Float2{141.975, 258.864}, Float2{115.115, 239.679}, Float2{39.471, 225.975}, Float2{41.115, 235.842}, Float2{45.500, 236.938}, Float2{55.367, 256.124}, Float2{34.537, 275.309}, Float2{11.515, 270.376}, Float2{10.418, 267.635}, Float2{4.389, 257.769}, Float2{6.581, 238.583}, Float2{26.863, 221.591}, Float2{0.004, 190.895}, Float2{16.997, 173.354}, Float2{24.671, 195.828}, Float2{25.219, 200.762}, Float2{26.864, 185.414}, Float2{20.834, 178.288}, Float2{15.353, 178.288}, Float2{6.582, 184.318}, Float2{4.937, 191.992}, Float2{12.063, 203.504}, Float2{18.641, 206.792}, Float2{32.345, 218.852}, Float2{71.263, 215.563}, Float2{103.055, 224.334}, Float2{107.988, 225.979}, Float2{110.729, 228.720}, Float2{120.048, 233.105}, Float2{129.915, 241.876}, Float2{144.715, 249.550}, Float2{144.167, 246.810}, Float2{146.907, 242.973}, Float2{162.803, 194.188}, Float2{166.092, 191.995}, Float2{161.706, 186.514}, Float2{156.225, 193.092}, Float2{155.677, 188.707}, Float2{148.003, 166.781}, Float2{137.588, 167.329}, Float2{133.751, 167.329}, Float2{127.173, 164.588}, Float2{113.469, 162.943}, Float2{107.439, 156.365}, Float2{100.313, 152.528}, Float2{95.380, 152.528}, Float2{111.276, 145.402}, Float2{81.128, 143.210}, Float2{92.639, 138.825}, Float2{102.506, 138.277}, Float2{106.343, 136.632}, Float2{125.528, 139.373}, Float2{148.550, 156.366}, Float2{153.484, 154.174}, Float2{145.810, 143.211}, Float2{141.424, 144.855}, Float2{132.106, 133.892}, Float2{134.298, 132.796}, Float2{124.980, 109.774}, Float2{123.335, 104.840}, Float2{122.239, 104.292}, Float2{116.210, 105.389}, Float2{89.351, 104.841}, Float2{81.129, 127.863}, Float2{72.907, 144.855}, Float2{69.618, 155.270}, Float2{72.907, 151.981}, Float2{70.166, 156.366}, Float2{76.195, 169.522}, Float2{70.166, 170.070}, Float2{65.232, 174.455}, Float2{69.069, 175.551}, Float2{65.232, 177.196}, Float2{72.906, 180.485}, Float2{72.906, 187.611}, Float2{69.617, 195.833}, Float2{71.809, 187.063}, Float2{71.809, 182.677}, Float2{65.780, 181.032}, Float2{70.165, 167.328}, Float2{66.876, 159.106}, Float2{64.135, 154.721}, Float2{70.165, 140.470}, Float2{64.684, 133.344}, Float2{63.039, 137.181}, Float2{60.298, 151.433}, Float2{60.846, 163.492}, Float2{61.394, 159.107}, Float2{58.653, 154.174}, Float2{57.008, 145.952}, Float2{59.749, 145.952}, Float2{60.297, 128.959}, Float2{69.067, 115.803}, Float2{63.586, 115.803}, Float2{61.941, 113.611}, Float2{67.422, 105.937}, Float2{60.844, 105.389}, Float2{61.940, 100.455}, Float2{42.755, 110.870}, Float2{45.496, 99.359}, Float2{44.948, 94.974}, Float2{49.881, 87.848}, Float2{50.977, 84.011}, Float2{52.074, 83.463}, Float2{57.007, 70.856}, Float2{52.074, 73.597}, Float2{50.977, 70.856}, Float2{46.592, 74.693}, Float2{59.731, 56.604}, Float2{49.864, 51.123}, Float2{56.442, 48.930}, Float2{50.961, 49.478}, Float2{56.991, 46.737}, Float2{55.346, 52.766}, Float2{61.924, 42.899}, Float2{68.502, 36.321}, Float2{75.628, 34.676}, Float2{94.813, 18.232}, Float2{118.932, 5.077}, Float2{145.243, 0.143}, Float2{150.177, 1.788}, Float2{155.658, 2.884}, Float2{166.073, 13.847}, Float2{174.295, 18.781}, Float2{172.103, 37.418}, Float2{171.555, 30.840}, Float2{162.784, 45.092}, Float2{165.525, 36.870}, Float2{157.302, 44.544}, Float2{163.331, 37.418}
		};
		for (auto& c:figure0) c = c * 0.5f - Float2{50, 50};

		// From https://freesvg.org/female-archer (marked with Licence: Public Domain)
		Float2 archer[] = {
			Float2{580.926, 1572.974}, Float2{589.147, 1592.608}, Float2{594.900, 1613.146}, Float2{593.045, 1626.453}, Float2{588.514, 1642.812}, Float2{555.985, 1676.820}, Float2{560.549, 1669.614}, Float2{582.038, 1635.393}, Float2{581.237, 1607.140}, Float2{579.774, 1597.724}, Float2{579.756, 1596.803}, Float2{565.606, 1576.376}, Float2{486.600, 1485.108}, Float2{402.404, 1406.122}, Float2{322.548, 1312.152}, Float2{249.065, 1201.545}, Float2{234.598, 1179.383}, Float2{232.788, 1174.026}, Float2{231.912, 1172.308}, Float2{225.953, 1167.725}, Float2{204.870, 1140.442}, Float2{202.610, 1134.312}, Float2{201.912, 1132.732}, Float2{183.992, 1101.772}, Float2{188.582, 1093.556}, Float2{203.100, 1088.224}, Float2{204.173, 1083.899}, Float2{186.018, 1065.307}, Float2{157.051, 1031.690}, Float2{159.603, 1002.862}, Float2{166.609, 995.734}, Float2{175.497, 960.978}, Float2{155.284, 929.056}, Float2{156.094, 922.835}, Float2{171.586, 916.820}, Float2{165.464, 913.164}, Float2{162.131, 906.678}, Float2{168.919, 902.937}, Float2{179.797, 904.295}, Float2{183.924, 902.044}, Float2{180.321, 899.881}, Float2{168.837, 888.506}, Float2{177.004, 879.295}, Float2{169.553, 878.626}, Float2{158.206, 868.191}, Float2{169.311, 859.191}, Float2{182.791, 859.191}, Float2{185.639, 856.610}, Float2{182.647, 855.074}, Float2{176.162, 854.962}, Float2{166.016, 837.417}, Float2{132.245, 837.404}, Float2{109.261, 836.987}, Float2{101.788, 842.073}, Float2{91.811, 845.904}, Float2{43.299, 834.022}, Float2{21.693, 831.386}, Float2{49.210, 829.183}, Float2{93.746, 816.015}, Float2{101.263, 819.271}, Float2{107.030, 824.031}, Float2{176.006, 824.923}, Float2{181.139, 820.144}, Float2{182.010, 809.709}, Float2{173.094, 787.548}, Float2{160.727, 777.415}, Float2{156.682, 773.723}, Float2{158.643, 768.010}, Float2{170.174, 761.476}, Float2{172.249, 709.291}, Float2{159.629, 693.186}, Float2{158.726, 669.422}, Float2{187.772, 633.451}, Float2{223.135, 601.033}, Float2{223.067, 594.297}, Float2{216.462, 587.536}, Float2{214.681, 573.925}, Float2{226.223, 555.639}, Float2{229.255, 549.207}, Float2{247.071, 525.095}, Float2{331.029, 414.594}, Float2{491.551, 250.560}, Float2{561.305, 190.650}, Float2{581.136, 170.246}, Float2{596.957, 149.581}, Float2{607.624, 133.221}, Float2{608.089, 121.378}, Float2{601.315, 91.565}, Float2{594.918, 75.228}, Float2{596.657, 75.572}, Float2{619.776, 105.494}, Float2{623.242, 136.728}, Float2{611.057, 162.449}, Float2{609.909, 167.036}, Float2{610.049, 166.986}, Float2{607.050, 166.872}, Float2{596.061, 179.721}, Float2{554.865, 219.775}, Float2{405.881, 361.132}, Float2{314.821, 484.040}, Float2{293.240, 546.467}, Float2{293.223, 597.731}, Float2{296.218, 622.523}, Float2{287.857, 630.170}, Float2{268.187, 623.165}, Float2{262.013, 623.849}, Float2{222.557, 666.254}, Float2{217.694, 706.529}, Float2{228.274, 741.841}, Float2{232.671, 759.766}, Float2{242.053, 770.136}, Float2{243.151, 777.757}, Float2{237.105, 782.181}, Float2{224.616, 792.146}, Float2{225.042, 798.972}, Float2{255.018, 815.729}, Float2{270.563, 825.662}, Float2{276.591, 827.595}, Float2{315.751, 827.783}, Float2{325.822, 822.672}, Float2{330.047, 820.710}, Float2{376.767, 820.206}, Float2{383.429, 821.574}, Float2{404.884, 817.243}, Float2{461.591, 800.073}, Float2{478.627, 798.534}, Float2{485.452, 799.096}, Float2{528.510, 791.403}, Float2{537.044, 794.681}, Float2{533.245, 800.683}, Float2{531.185, 802.095}, Float2{512.904, 822.804}, Float2{521.289, 821.412}, Float2{534.781, 827.375}, Float2{538.638, 830.733}, Float2{554.108, 831.553}, Float2{705.073, 833.728}, Float2{744.632, 828.905}, Float2{801.809, 817.383}, Float2{841.515, 814.923}, Float2{842.508, 815.037}, Float2{871.490, 814.546}, Float2{914.349, 808.916}, Float2{920.930, 804.935}, Float2{927.008, 805.375}, Float2{939.514, 800.491}, Float2{948.157, 786.931}, Float2{943.958, 784.147}, Float2{919.713, 788.931}, Float2{917.477, 791.914}, Float2{920.094, 802.029}, Float2{915.377, 791.592}, Float2{911.771, 787.836}, Float2{897.792, 771.411}, Float2{892.933, 758.131}, Float2{881.647, 728.937}, Float2{876.529, 721.958}, Float2{872.880, 711.095}, Float2{866.529, 688.352}, Float2{866.277, 691.081}, Float2{854.952, 630.059}, Float2{868.504, 728.082}, Float2{867.017, 725.942}, Float2{850.088, 625.047}, Float2{846.636, 618.107}, Float2{845.710, 636.823}, Float2{844.035, 615.190}, Float2{842.197, 607.645}, Float2{823.350, 575.853}, Float2{734.826, 422.266}, Float2{640.213, 244.123}, Float2{625.805, 215.148}, Float2{626.248, 214.806}, Float2{660.838, 278.309}, Float2{725.199, 400.146}, Float2{811.637, 552.585}, Float2{841.631, 603.311}, Float2{844.497, 607.881}, Float2{852.141, 580.449}, Float2{890.849, 518.787}, Float2{914.782, 504.016}, Float2{918.541, 500.946}, Float2{935.645, 489.589}, Float2{958.035, 486.343}, Float2{971.114, 484.393}, Float2{997.167, 481.809}, Float2{1025.415, 487.403}, Float2{1071.157, 514.435}, Float2{1084.791, 527.633}, Float2{1120.026, 605.207}, Float2{1120.654, 657.557}, Float2{1119.223, 660.740}, Float2{1117.032, 661.751}, Float2{1114.869, 641.330}, Float2{1114.407, 641.317}, Float2{1112.967, 663.576}, Float2{1109.695, 680.528}, Float2{1109.734, 680.631}, Float2{1108.993, 675.185}, Float2{1103.583, 691.455}, Float2{1103.611, 691.538}, Float2{1104.784, 683.805}, Float2{1103.779, 683.540}, Float2{1100.239, 693.577}, Float2{1082.934, 735.857}, Float2{1079.849, 737.925}, Float2{1080.055, 737.931}, Float2{1079.953, 738.051}, Float2{1069.040, 749.933}, Float2{1069.084, 750.042}, Float2{1067.920, 750.053}, Float2{1067.958, 749.945}, Float2{1061.195, 753.488}, Float2{1057.093, 758.940}, Float2{1061.084, 786.457}, Float2{1066.554, 790.407}, Float2{1082.468, 795.372}, Float2{1087.071, 793.939}, Float2{1088.554, 769.485}, Float2{1086.496, 759.715}, Float2{1090.876, 752.101}, Float2{1121.977, 734.143}, Float2{1129.012, 733.319}, Float2{1132.067, 735.002}, Float2{1140.613, 732.698}, Float2{1229.050, 593.539}, Float2{1244.604, 567.600}, Float2{1245.592, 561.137}, Float2{1245.958, 555.776}, Float2{1253.117, 532.229}, Float2{1255.970, 517.626}, Float2{1267.063, 494.933}, Float2{1267.125, 495.157}, Float2{1266.955, 495.029}, Float2{1286.270, 467.836}, Float2{1281.806, 486.938}, Float2{1279.226, 496.578}, Float2{1277.138, 508.077}, Float2{1277.036, 507.950}, Float2{1278.210, 510.051}, Float2{1275.902, 516.879}, Float2{1275.450, 519.292}, Float2{1273.925, 520.049}, Float2{1273.282, 522.264}, Float2{1273.127, 522.097}, Float2{1273.126, 522.367}, Float2{1273.820, 519.943}, Float2{1275.339, 519.227}, Float2{1275.796, 516.782}, Float2{1280.418, 511.403}, Float2{1302.543, 476.560}, Float2{1309.614, 475.828}, Float2{1311.795, 482.602}, Float2{1307.287, 491.174}, Float2{1305.580, 491.957}, Float2{1305.675, 492.127}, Float2{1305.667, 491.816}, Float2{1307.190, 491.091}, Float2{1312.895, 487.214}, Float2{1304.893, 509.005}, Float2{1313.644, 506.710}, Float2{1321.030, 506.232}, Float2{1320.908, 506.103}, Float2{1320.897, 506.360}, Float2{1329.100, 495.121}, Float2{1334.220, 494.369}, Float2{1339.809, 493.505}, Float2{1356.402, 476.003}, Float2{1349.812, 494.346}, Float2{1346.384, 503.314}, Float2{1341.941, 515.268}, Float2{1354.027, 501.839}, Float2{1353.930, 501.931}, Float2{1376.679, 476.474}, Float2{1380.332, 487.421}, Float2{1380.697, 491.857}, Float2{1374.553, 499.665}, Float2{1372.427, 504.368}, Float2{1370.833, 504.775}, Float2{1370.903, 504.965}, Float2{1370.809, 504.632}, Float2{1372.362, 504.317}, Float2{1375.087, 502.127}, Float2{1374.978, 502.029}, Float2{1364.714, 517.557}, Float2{1362.292, 523.224}, Float2{1360.970, 524.049}, Float2{1359.808, 524.968}, Float2{1359.536, 525.167}, Float2{1359.683, 524.873}, Float2{1360.857, 523.934}, Float2{1362.194, 523.135}, Float2{1391.736, 486.163}, Float2{1398.729, 485.814}, Float2{1400.344, 493.036}, Float2{1392.982, 502.969}, Float2{1391.688, 505.150}, Float2{1392.871, 502.877}, Float2{1396.586, 501.294}, Float2{1381.319, 525.713}, Float2{1396.940, 512.053}, Float2{1396.908, 511.949}, Float2{1398.077, 511.949}, Float2{1398.034, 512.060}, Float2{1420.719, 488.039}, Float2{1415.635, 502.175}, Float2{1406.147, 525.135}, Float2{1405.792, 525.135}, Float2{1406.059, 525.016}, Float2{1405.972, 528.277}, Float2{1404.190, 533.112}, Float2{1402.939, 534.031}, Float2{1401.890, 535.033}, Float2{1401.774, 534.933}, Float2{1402.834, 533.927}, Float2{1404.088, 533.015}, Float2{1435.436, 498.158}, Float2{1442.770, 498.597}, Float2{1443.604, 505.565}, Float2{1433.999, 516.962}, Float2{1432.580, 517.637}, Float2{1431.988, 519.234}, Float2{1431.734, 519.249}, Float2{1431.916, 519.092}, Float2{1432.511, 517.582}, Float2{1433.902, 516.857}, Float2{1439.047, 514.084}, Float2{1438.941, 513.981}, Float2{1424.497, 532.734}, Float2{1420.312, 539.586}, Float2{1429.015, 535.260}, Float2{1442.458, 529.054}, Float2{1446.982, 529.998}, Float2{1446.893, 529.899}, Float2{1437.374, 543.489}, Float2{1414.803, 565.839}, Float2{1369.242, 593.187}, Float2{1364.542, 595.622}, Float2{1321.414, 643.325}, Float2{1207.706, 772.989}, Float2{1206.678, 779.260}, Float2{1209.780, 795.267}, Float2{1210.318, 799.982}, Float2{1218.131, 791.572}, Float2{1227.021, 793.091}, Float2{1230.540, 792.774}, Float2{1283.555, 772.688}, Float2{1291.156, 768.967}, Float2{1303.214, 767.701}, Float2{1325.948, 768.696}, Float2{1353.840, 763.039}, Float2{1363.554, 759.273}, Float2{1369.362, 757.988}, Float2{1376.083, 761.479}, Float2{1372.480, 766.811}, Float2{1351.153, 786.397}, Float2{1356.756, 787.853}, Float2{1367.801, 793.736}, Float2{1375.361, 797.686}, Float2{1397.512, 812.805}, Float2{1412.827, 836.038}, Float2{1407.021, 867.668}, Float2{1382.858, 887.332}, Float2{1375.196, 891.799}, Float2{1360.356, 895.707}, Float2{1347.675, 894.598}, Float2{1341.903, 894.794}, Float2{1241.387, 921.973}, Float2{1187.989, 955.147}, Float2{1169.750, 980.192}, 
			Float2{1168.116, 986.136}, Float2{1167.280, 1024.367}, Float2{1155.626, 1069.792}, Float2{1154.254, 1094.213}, Float2{1152.842, 1121.651}, Float2{1141.883, 1136.256}, Float2{1133.392, 1145.903}, Float2{1128.294, 1183.948}, Float2{1131.888, 1190.038}, Float2{1135.341, 1195.193}, Float2{1138.793, 1231.807}, Float2{1139.847, 1236.999}, Float2{1143.229, 1245.321}, Float2{1150.713, 1252.611}, Float2{1157.755, 1260.227}, Float2{1163.130, 1278.386}, Float2{1163.447, 1282.726}, Float2{1164.308, 1292.912}, Float2{1172.202, 1299.586}, Float2{1184.510, 1319.197}, Float2{1180.092, 1338.314}, Float2{1180.695, 1343.571}, Float2{1201.350, 1397.088}, Float2{1246.883, 1502.485}, Float2{1281.013, 1639.857}, Float2{1292.117, 1774.264}, Float2{1301.671, 1861.047}, Float2{1305.828, 1865.633}, Float2{1323.431, 1866.908}, Float2{1331.572, 1868.701}, Float2{1334.413, 1873.560}, Float2{1335.668, 1899.716}, Float2{1337.564, 1905.970}, Float2{1338.805, 1898.655}, Float2{1345.076, 1896.415}, Float2{1349.917, 1902.051}, Float2{1369.723, 1935.167}, Float2{1370.898, 1937.371}, Float2{1380.565, 1951.299}, Float2{1390.921, 1970.240}, Float2{1400.591, 2001.584}, Float2{1400.580, 2006.313}, Float2{1396.934, 2005.545}, Float2{1406.604, 2050.494}, Float2{1409.016, 2070.512}, Float2{1413.398, 2096.508}, Float2{1413.761, 2097.947}, Float2{1416.988, 2127.924}, Float2{1420.052, 2142.470}, Float2{1422.118, 2156.188}, Float2{1423.834, 2160.055}, Float2{1430.054, 2176.169}, Float2{1430.121, 2177.149}, Float2{1442.182, 2226.332}, Float2{1445.133, 2230.917}, Float2{1449.041, 2263.531}, Float2{1449.011, 2271.232}, Float2{1463.464, 2306.519}, Float2{1484.768, 2351.467}, Float2{1494.533, 2375.401}, Float2{1499.284, 2380.901}, Float2{1539.051, 2418.389}, Float2{1546.071, 2433.656}, Float2{1546.171, 2435.590}, Float2{1543.929, 2449.196}, Float2{1544.536, 2451.440}, Float2{1541.370, 2462.672}, Float2{1539.722, 2466.066}, Float2{1529.684, 2475.306}, Float2{1526.762, 2477.009}, Float2{1505.843, 2484.676}, Float2{1488.344, 2470.824}, Float2{1480.144, 2463.455}, Float2{1450.511, 2437.096}, Float2{1426.910, 2401.268}, Float2{1420.197, 2398.730}, Float2{1395.792, 2387.337}, Float2{1391.758, 2391.581}, Float2{1387.372, 2386.512}, Float2{1373.318, 2365.333}, Float2{1365.532, 2331.672}, Float2{1364.336, 2322.246}, Float2{1361.771, 2308.450}, Float2{1360.712, 2297.652}, Float2{1355.595, 2283.339}, Float2{1352.234, 2274.526}, Float2{1338.553, 2237.214}, Float2{1323.100, 2222.658}, Float2{1316.013, 2214.284}, Float2{1315.102, 2212.587}, Float2{1306.040, 2194.028}, Float2{1304.878, 2189.771}, Float2{1289.573, 2151.794}, Float2{1283.510, 2142.357}, Float2{1274.013, 2126.871}, Float2{1265.382, 2115.175}, Float2{1259.853, 2103.595}, Float2{1258.630, 2099.442}, Float2{1253.777, 2081.854}, Float2{1250.215, 2075.266}, Float2{1239.445, 2068.305}, Float2{1229.796, 2047.600}, Float2{1221.158, 2009.763}, Float2{1214.019, 1995.215}, Float2{1206.448, 1972.166}, Float2{1201.540, 1949.898}, Float2{1203.611, 1943.964}, Float2{1201.937, 1939.693}, Float2{1192.911, 1932.656}, Float2{1194.817, 1923.176}, Float2{1196.864, 1915.971}, Float2{1199.793, 1897.495}, Float2{1199.869, 1894.298}, Float2{1187.419, 1852.295}, Float2{1124.167, 1700.990}, Float2{1056.033, 1564.980}, Float2{1040.956, 1513.233}, Float2{1035.166, 1497.899}, Float2{1028.842, 1498.005}, Float2{1017.339, 1505.637}, Float2{993.187, 1501.364}, Float2{995.342, 1525.199}, Float2{995.599, 1609.422}, Float2{974.697, 1726.852}, Float2{971.579, 1771.131}, Float2{970.263, 1830.610}, Float2{972.662, 1875.497}, Float2{969.824, 1914.139}, Float2{970.080, 1918.802}, Float2{977.023, 1943.968}, Float2{972.927, 1952.613}, Float2{969.315, 1956.002}, Float2{970.300, 1962.018}, Float2{978.640, 1975.984}, Float2{982.544, 1981.273}, Float2{989.510, 1991.253}, Float2{989.213, 2000.741}, Float2{982.194, 2010.206}, Float2{980.091, 2012.937}, Float2{982.432, 2043.469}, Float2{982.483, 2062.718}, Float2{981.077, 2089.018}, Float2{982.326, 2091.570}, Float2{984.483, 2115.285}, Float2{981.373, 2117.602}, Float2{974.492, 2126.005}, Float2{961.290, 2207.738}, Float2{963.522, 2216.798}, Float2{964.099, 2236.303}, Float2{962.469, 2238.042}, Float2{955.160, 2248.066}, Float2{949.387, 2264.545}, Float2{945.326, 2293.223}, Float2{943.200, 2321.197}, Float2{946.066, 2333.564}, Float2{947.963, 2340.972}, Float2{945.384, 2359.245}, Float2{943.248, 2371.521}, Float2{939.929, 2378.694}, Float2{937.131, 2408.219}, Float2{931.497, 2424.378}, Float2{915.525, 2443.299}, Float2{913.623, 2452.726}, Float2{898.999, 2484.310}, Float2{896.967, 2487.529}, Float2{882.491, 2501.706}, Float2{863.521, 2494.657}, Float2{852.190, 2500.710}, Float2{834.306, 2493.217}, Float2{821.102, 2485.954}, Float2{816.826, 2480.426}, Float2{818.304, 2466.692}, Float2{840.992, 2431.579}, Float2{858.153, 2396.953}, Float2{857.231, 2382.072}, Float2{855.198, 2379.583}, Float2{853.026, 2371.476}, Float2{854.835, 2356.101}, Float2{854.772, 2355.614}, Float2{858.670, 2333.871}, Float2{859.777, 2331.197}, Float2{856.884, 2296.304}, Float2{851.127, 2275.338}, Float2{845.907, 2241.284}, Float2{845.195, 2239.073}, Float2{841.073, 2229.791}, Float2{843.506, 2210.123}, Float2{842.461, 2194.825}, Float2{834.903, 2162.241}, Float2{828.854, 2123.811}, Float2{824.937, 2117.518}, Float2{823.293, 2114.115}, Float2{822.267, 2084.438}, Float2{821.928, 2078.985}, Float2{821.620, 2003.642}, Float2{820.411, 1998.610}, Float2{819.281, 1995.949}, Float2{822.559, 1971.880}, Float2{825.082, 1956.726}, Float2{831.078, 1947.992}, Float2{831.745, 1946.017}, Float2{836.720, 1924.281}, Float2{846.062, 1918.105}, Float2{860.583, 1909.081}, Float2{861.029, 1904.331}, Float2{853.601, 1831.763}, Float2{835.567, 1755.040}, Float2{806.452, 1588.450}, Float2{812.354, 1480.925}, Float2{818.402, 1426.314}, Float2{831.349, 1361.185}, Float2{834.827, 1342.023}, Float2{834.845, 1338.140}, Float2{831.371, 1313.370}, Float2{838.732, 1302.774}, Float2{849.406, 1292.777}, Float2{853.265, 1285.784}, Float2{852.485, 1280.181}, Float2{851.614, 1274.714}, Float2{858.651, 1256.035}, Float2{864.164, 1250.739}, Float2{866.955, 1248.258}, Float2{872.570, 1233.838}, Float2{872.382, 1230.102}, Float2{870.159, 1222.402}, Float2{877.232, 1196.349}, Float2{878.099, 1192.614}, Float2{873.658, 1169.862}, Float2{858.415, 1125.171}, Float2{851.103, 1087.088}, Float2{841.814, 1075.355}, Float2{839.118, 1076.204}, Float2{836.154, 1080.133}, Float2{835.714, 1079.757}, Float2{836.538, 1077.971}, Float2{836.337, 1067.875}, Float2{827.600, 1062.692}, Float2{818.954, 1057.009}, Float2{816.713, 1044.370}, Float2{826.741, 1012.748}, Float2{829.017, 994.098}, Float2{830.768, 984.614}, Float2{840.672, 976.393}, Float2{842.291, 970.582}, Float2{827.694, 946.186}, Float2{787.189, 921.928}, Float2{747.858, 919.074}, Float2{629.620, 913.212}, Float2{575.107, 915.099}, Float2{535.741, 917.970}, Float2{532.832, 920.346}, Float2{521.907, 925.912}, Float2{507.603, 921.896}, Float2{467.297, 922.374}, Float2{439.590, 919.755}, Float2{427.494, 921.435}, Float2{415.485, 922.222}, Float2{409.497, 919.251}, Float2{393.043, 919.914}, Float2{376.942, 915.824}, Float2{367.246, 914.381}, Float2{318.016, 922.202}, Float2{313.662, 920.487}, Float2{309.998, 917.330}, Float2{297.141, 913.328}, Float2{291.801, 905.898}, Float2{282.474, 898.892}, Float2{264.214, 903.986}, Float2{222.996, 915.647}, Float2{228.708, 919.796}, Float2{242.874, 925.927}, Float2{243.421, 930.300}, Float2{227.808, 957.825}, Float2{213.345, 1000.623}, Float2{213.747, 1028.981}, Float2{247.036, 1070.674}, Float2{251.966, 1071.514}, Float2{272.924, 1064.952}, Float2{284.238, 1072.851}, Float2{284.182, 1092.818}, Float2{282.150, 1111.434}, Float2{280.930, 1135.342}, Float2{281.746, 1158.680}, Float2{305.893, 1234.031}, Float2{379.705, 1349.066}, Float2{469.846, 1445.267}, Float2{559.504, 1543.158}, Float2{581.034, 1573.060}
		};
		for (auto& c:archer) c = c * 0.5f - Float2{50, 50};

		// From https://freesvg.org/woman-with-spear (marked with Licence: Public Domain)
		Float2 womanWithSpear[] = {
			Float2{1150.100, 0.000}, Float2{1025.000, 72.500}, Float2{974.824, 210.975}, Float2{960.600, 253.613}, Float2{922.074, 362.051}, Float2{892.676, 473.801}, Float2{795.012, 673.762}, Float2{772.512, 758.537}, Float2{715.301, 829.037}, Float2{661.988, 880.762}, Float2{614.975, 937.162}, Float2{575.313, 906.938}, Float2{557.750, 915.438}, Float2{589.738, 976.125}, Float2{583.537, 985.900}, Float2{534.813, 973.787}, Float2{526.063, 1005.375}, Float2{549.150, 1018.613}, Float2{452.676, 1055.375}, Float2{396.676, 1069.162}, Float2{357.500, 1128.762}, Float2{310.176, 1153.500}, Float2{356.287, 1242.400}, Float2{419.563, 1316.725}, Float2{439.563, 1349.275}, Float2{532.938, 1345.387}, Float2{637.875, 1240.238}, Float2{709.926, 1231.225}, Float2{744.025, 1196.512}, Float2{776.988, 1124.512}, Float2{807.801, 1078.588}, Float2{801.275, 1126.250}, Float2{768.912, 1227.676}, Float2{761.262, 1332.525}, Float2{771.088, 1368.676}, Float2{812.063, 1283.238}, Float2{804.449, 1191.963}, Float2{822.500, 1138.287}, Float2{863.537, 1379.313}, Float2{865.051, 1387.662}, Float2{868.662, 1377.662}, Float2{880.338, 1426.275}, Float2{881.750, 1417.463}, Float2{896.125, 1441.363}, Float2{889.350, 1405.037}, Float2{892.838, 1409.863}, Float2{890.088, 1378.000}, Float2{893.100, 1388.037}, Float2{892.313, 1356.463}, Float2{901.262, 1400.463}, Float2{895.137, 1338.500}, Float2{908.574, 1421.324}, Float2{916.488, 1433.650}, Float2{912.426, 1404.875}, Float2{910.887, 1375.150}, Float2{912.426, 1404.875}, Float2{940.213, 1467.588}, Float2{930.900, 1425.225}, Float2{940.213, 1439.850}, Float2{938.500, 1325.051}, Float2{936.350, 1367.688}, Float2{942.650, 1328.600}, Float2{947.600, 1398.463}, Float2{953.838, 1324.012}, Float2{956.313, 1347.324}, Float2{954.850, 1308.162}, Float2{957.613, 1306.463}, Float2{929.762, 1153.738}, Float2{964.188, 1377.699}, Float2{968.699, 1431.113}, Float2{974.850, 1430.250}, Float2{975.650, 1416.762}, Float2{988.637, 1463.551}, Float2{10.400, 1729.926}, Float2{2.125, 1760.199}, Float2{28.574, 1803.301}, Float2{1014.100, 1536.887}, Float2{987.074, 1600.537}, Float2{937.475, 1688.725}, Float2{874.088, 1772.838}, Float2{800.887, 1833.400}, Float2{713.574, 1939.813}, Float2{451.463, 2150.301}, Float2{449.463, 2271.188}, Float2{385.338, 2366.613}, Float2{521.713, 2431.600}, Float2{631.713, 2294.213}, Float2{707.574, 2155.088}, Float2{789.875, 1982.426}, Float2{699.838, 2188.563}, Float2{571.225, 2494.225}, Float2{610.137, 2573.676}, Float2{762.275, 2628.000}, Float2{708.699, 2739.949}, Float2{562.463, 3056.100}, Float2{542.725, 3111.238}, Float2{520.938, 3175.900}, Float2{511.037, 3218.850}, Float2{489.438, 3263.051}, Float2{462.512, 3352.350}, Float2{446.625, 3407.775}, Float2{328.588, 3564.850}, Float2{287.199, 3619.600}, Float2{259.051, 3667.275}, Float2{310.801, 3696.301}, Float2{350.225, 3697.600}, Float2{381.875, 3720.000}, Float2{394.363, 3720.000}, Float2{457.350, 3676.063}, Float2{491.863, 3638.125}, Float2{564.176, 3517.938}, Float2{581.063, 3486.963}, Float2{611.938, 3467.037}, Float2{618.912, 3438.051}, Float2{634.838, 3399.063}, Float2{655.225, 3380.137}, Float2{674.100, 3327.537}, Float2{706.074, 3267.238}, Float2{725.850, 3235.938}, Float2{742.949, 3217.588}, Float2{752.787, 3196.537}, Float2{759.926, 3159.813}, Float2{758.125, 3140.412}, Float2{823.838, 3036.324}, Float2{912.949, 2845.463}, Float2{1041.463, 2595.600}, Float2{1135.150, 2506.412}, Float2{1186.225, 2230.213}, Float2{1246.250, 2113.588}, Float2{1258.150, 2386.824}, Float2{1276.824, 2432.574}, Float2{1310.162, 2427.926}, Float2{1419.025, 2329.988}, Float2{1512.625, 2338.787}, Float2{1572.938, 2264.750}, Float2{1876.375, 2474.863}, Float2{1977.887, 2530.762}, Float2{1985.775, 2578.338}, Float2{1937.387, 2654.850}, Float2{1909.463, 2864.350}, Float2{1930.338, 2966.563}, Float2{1929.625, 3000.824}, Float2{1928.500, 3020.375}, Float2{1921.000, 3035.051}, Float2{1929.824, 3051.637}, Float2{1928.287, 3085.975}, Float2{1926.012, 3100.137}, Float2{1931.363, 3145.688}, Float2{1935.699, 3164.813}, Float2{1938.449, 3198.801}, Float2{1935.500, 3217.213}, Float2{1939.637, 3251.188}, Float2{1940.662, 3305.574}, Float2{1935.762, 3327.750}, Float2{1944.563, 3343.400}, Float2{1932.012, 3449.250}, Float2{1954.463, 3528.000}, Float2{2055.824, 3545.912}, Float2{2203.287, 3526.688}, Float2{2275.762, 3535.588}, Float2{2390.225, 3531.588}, Float2{2477.475, 3528.838}, Float2{2483.188, 3492.926}, Float2{2441.324, 3479.713}, Float2{2326.238, 3446.262}, Float2{2240.563, 3398.801}, Float2{2217.037, 3386.762}, Float2{2189.275, 3364.838}, Float2{2171.162, 3346.137}, Float2{2141.613, 3319.912}, Float2{2138.438, 3282.262}, Float2{2136.551, 3222.400}, Float2{2140.313, 3200.363}, Float2{2141.949, 3174.000}, Float2{2141.438, 3159.738}, Float2{2134.500, 3126.338}, Float2{2143.926, 3108.838}, Float2{2137.363, 3085.537}, Float2{2147.512, 3045.162}, Float2{2141.199, 3018.637}, Float2{2139.051, 2975.574}, Float2{2163.199, 2795.688}, Float2{2192.551, 2650.012}, Float2{2230.850, 2523.287}, Float2{2155.025, 2347.475}, Float2{1976.525, 2139.725}, Float2{1767.213, 1933.113}, Float2{1728.162, 1873.100}, Float2{1705.213, 1813.574}, Float2{1753.963, 1849.787}, Float2{1839.600, 1954.287}, Float2{1902.125, 1925.563}, Float2{1879.363, 1872.600}, Float2{1849.037, 1848.588}, Float2{1686.162, 1728.824}, Float2{1570.000, 1656.275}, Float2{1535.738, 1598.213}, Float2{1521.838, 1544.100}, Float2{1513.488, 1509.188}, Float2{1507.488, 1403.137}, Float2{1635.176, 1367.688}, Float2{1702.225, 1366.551}, Float2{1709.213, 1348.463}, Float2{1779.738, 1329.150}, Float2{1843.688, 1381.463}, Float2{1874.000, 1358.988}, Float2{1908.463, 1307.350}, Float2{1945.488, 1289.824}, Float2{2012.600, 1290.225}, Float2{2014.012, 1264.762}, Float2{2215.438, 1210.525}, Float2{2224.838, 1217.438}, Float2{2234.525, 1208.188}, Float2{2537.012, 1111.449}, Float2{2551.875, 1107.375}, Float2{2708.301, 1191.137}, Float2{2768.012, 1170.512}, Float2{3389.125, 853.400}, Float2{3387.762, 850.850}, Float2{2662.188, 894.738}, Float2{2630.301, 907.900}, Float2{2537.926, 1054.725}, Float2{2522.488, 1058.801}, Float2{2220.412, 1126.813}, Float2{2201.162, 1124.963}, Float2{2194.775, 1136.162}, Float2{1976.563, 1194.801}, Float2{1939.738, 1149.988}, Float2{1908.475, 1153.613}, Float2{1883.074, 1152.250}, Float2{1826.051, 1196.037}, Float2{1714.400, 1054.262}, Float2{1652.762, 965.938}, Float2{1582.488, 885.012}, Float2{1520.328, 803.867}, Float2{1485.449, 771.875}, Float2{1363.650, 636.688}, Float2{1369.650, 563.213}, Float2{1379.775, 494.775}, Float2{1400.275, 287.025}, Float2{1383.988, 197.250}, Float2{1367.125, 100.338}, Float2{1207.863, 12.162}, Float2{1153.637, 0.000}
		};
		for (auto& c:womanWithSpear) c = c * 0.1f - Float2{50, 50};

		// From https://freesvg.org/dancing-man (marked with Licence: Public Domain)
		Float2 dancingMan[] = {
			Float2{211.988, 0.258}, Float2{203.271, 2.240}, Float2{192.188, 6.037}, Float2{186.170, 7.959}, Float2{160.457, 39.355}, Float2{158.730, 43.018}, Float2{155.607, 42.180}, Float2{148.414, 46.951}, Float2{149.527, 49.691}, Float2{151.408, 49.461}, Float2{151.502, 48.984}, Float2{152.289, 47.813}, Float2{153.750, 48.850}, Float2{155.109, 49.367}, Float2{155.945, 50.203}, Float2{153.750, 51.563}, Float2{151.537, 52.969}, Float2{149.092, 54.375}, Float2{148.359, 56.357}, Float2{154.902, 54.066}, Float2{155.562, 55.609}, Float2{156.324, 55.473}, Float2{158.088, 53.291}, Float2{159.238, 57.816}, Float2{161.166, 65.287}, Float2{177.961, 78.365}, Float2{185.117, 80.592}, Float2{183.965, 81.443}, Float2{179.918, 80.164}, Float2{164.975, 87.213}, Float2{151.682, 86.598}, Float2{145.385, 82.500}, Float2{140.299, 85.313}, Float2{130.383, 86.387}, Float2{121.875, 86.268}, Float2{112.525, 90.938}, Float2{106.875, 87.188}, Float2{98.604, 84.984}, Float2{90.895, 84.201}, Float2{80.006, 75.791}, Float2{76.510, 71.771}, Float2{70.303, 66.328}, Float2{61.875, 61.898}, Float2{59.531, 63.750}, Float2{57.188, 66.539}, Float2{56.619, 68.438}, Float2{49.063, 61.572}, Float2{41.221, 42.871}, Float2{37.570, 39.467}, Float2{34.947, 37.635}, Float2{33.564, 36.295}, Float2{29.469, 30.271}, Float2{27.866, 24.604}, Float2{26.509, 24.785}, Float2{24.224, 33.957}, Float2{20.547, 29.430}, Float2{22.969, 27.328}, Float2{27.790, 21.746}, Float2{27.125, 18.750}, Float2{26.959, 18.781}, Float2{26.959, 18.609}, Float2{25.098, 19.674}, Float2{20.201, 23.570}, Float2{11.611, 36.094}, Float2{10.693, 33.750}, Float2{10.125, 25.678}, Float2{9.554, 18.297}, Float2{7.583, 18.293}, Float2{6.137, 17.234}, Float2{4.316, 17.977}, Float2{4.146, 17.922}, Float2{2.539, 29.158}, Float2{5.444, 52.670}, Float2{10.393, 61.641}, Float2{15.154, 65.625}, Float2{17.662, 67.408}, Float2{28.211, 73.070}, Float2{39.248, 79.717}, Float2{45.068, 84.410}, Float2{48.328, 91.467}, Float2{55.352, 100.584}, Float2{57.625, 100.176}, Float2{57.148, 103.371}, Float2{57.414, 105.854}, Float2{62.246, 113.738}, Float2{67.762, 112.699}, Float2{72.008, 111.172}, Float2{74.676, 114.025}, Float2{80.678, 118.461}, Float2{85.201, 123.326}, Float2{93.467, 129.316}, Float2{97.572, 129.748}, Float2{101.400, 129.496}, Float2{105.078, 129.443}, Float2{109.859, 130.299}, Float2{113.861, 132.045}, Float2{125.385, 133.824}, Float2{129.744, 134.422}, Float2{139.303, 134.602}, Float2{142.559, 134.119}, Float2{150.525, 134.986}, Float2{173.527, 128.119}, Float2{180.340, 125.625}, Float2{186.166, 137.344}, Float2{194.307, 156.563}, Float2{202.156, 179.531}, Float2{202.561, 213.645}, Float2{201.563, 219.066}, Float2{199.811, 218.504}, Float2{197.416, 218.209}, Float2{193.459, 218.564}, Float2{186.010, 222.594}, Float2{181.875, 227.861}, Float2{180.996, 229.438}, Float2{179.959, 232.156}, Float2{178.463, 235.313}, Float2{177.082, 248.840}, Float2{172.141, 255.922}, Float2{165.646, 263.203}, Float2{170.361, 270.000}, Float2{176.668, 272.543}, Float2{180.762, 275.260}, Float2{176.631, 279.981}, Float2{142.906, 329.631}, Float2{138.852, 337.969}, Float2{131.553, 357.945}, Float2{136.602, 372.953}, Float2{142.332, 377.576}, Float2{145.609, 379.686}, Float2{145.813, 381.463}, Float2{154.322, 386.250}, Float2{169.129, 396.307}, Float2{172.418, 401.553}, Float2{176.250, 405.764}, Float2{177.223, 408.410}, Float2{177.223, 411.939}, Float2{176.244, 416.545}, Float2{169.701, 428.713}, Float2{165.000, 433.453}, Float2{162.666, 439.291}, Float2{164.590, 447.729}, Float2{181.789, 444.344}, Float2{188.863, 441.563}, Float2{197.713, 431.918}, Float2{204.426, 424.309}, Float2{210.955, 417.811}, Float2{213.740, 410.768}, Float2{204.832, 395.625}, Float2{203.098, 394.219}, Float2{200.420, 392.826}, Float2{199.281, 392.900}, Float2{197.797, 391.564}, Float2{199.457, 387.168}, Float2{196.641, 384.770}, Float2{195.484, 381.471}, Float2{191.631, 375.000}, Float2{189.844, 375.938}, Float2{188.566, 376.875}, Float2{185.938, 365.855}, Float2{187.896, 355.990}, Float2{186.809, 349.965}, Float2{188.301, 338.525}, Float2{194.805, 330.920}, Float2{205.006, 320.139}, Float2{217.018, 308.773}, Float2{231.785, 298.125}, Float2{241.760, 293.240}, Float2{264.932, 282.045}, Float2{266.246, 288.738}, Float2{267.264, 296.158}, Float2{265.875, 305.156}, Float2{264.775, 315.469}, Float2{266.248, 323.906}, Float2{270.574, 343.068}, Float2{267.209, 366.430}, Float2{269.063, 371.217}, Float2{265.781, 376.215}, Float2{262.500, 384.162}, Float2{261.412, 393.229}, Float2{261.488, 403.186}, Float2{261.590, 412.840}, Float2{262.018, 421.520}, Float2{264.857, 426.578}, Float2{265.869, 445.361}, Float2{263.445, 470.156}, Float2{261.881, 481.057}, Float2{263.438, 481.291}, Float2{262.266, 482.844}, Float2{263.203, 484.635}, Float2{262.969, 489.758}, Float2{262.400, 495.156}, Float2{262.166, 497.963}, Float2{260.156, 499.662}, Float2{262.418, 501.436}, Float2{264.682, 503.207}, Float2{262.652, 504.293}, Float2{260.625, 507.652}, Float2{258.877, 511.604}, Float2{254.822, 525.133}, Float2{254.764, 532.664}, Float2{259.299, 537.270}, Float2{266.387, 541.875}, Float2{269.857, 551.830}, Float2{269.775, 558.502}, Float2{307.123, 561.477}, Float2{309.350, 555.070}, Float2{306.033, 548.438}, Float2{292.500, 531.002}, Float2{290.002, 524.457}, Float2{287.350, 513.328}, Float2{286.471, 505.102}, Float2{288.207, 501.426}, Float2{290.412, 496.949}, Float2{292.197, 493.303}, Float2{294.928, 485.602}, Float2{295.318, 475.584}, Float2{303.623, 445.689}, Float2{305.445, 439.234}, Float2{307.500, 432.965}, Float2{308.984, 427.045}, Float2{308.375, 420.938}, Float2{309.844, 409.688}, Float2{312.188, 404.488}, Float2{313.022, 401.422}, Float2{311.664, 387.801}, Float2{313.352, 382.176}, Float2{316.770, 376.969}, Float2{316.844, 349.135}, Float2{319.758, 327.656}, Float2{321.285, 315.209}, Float2{321.523, 309.402}, Float2{321.904, 304.838}, Float2{322.225, 304.922}, Float2{322.225, 300.703}, Float2{322.217, 300.668}, Float2{323.480, 278.338}, Float2{320.926, 256.406}, Float2{317.226, 242.893}, Float2{317.053, 243.012}, Float2{315.723, 239.654}, Float2{318.652, 232.109}, Float2{319.572, 226.529}, Float2{324.394, 199.766}, Float2{315.221, 180.227}, Float2{314.295, 172.637}, Float2{308.230, 139.873}, Float2{306.935, 136.190}, Float2{316.410, 138.113}, Float2{330.078, 140.625}, Float2{339.375, 145.313}, Float2{348.765, 148.576}, Float2{352.894, 147.703}, Float2{357.584, 146.330}, Float2{366.760, 147.025}, Float2{371.310, 146.664}, Float2{381.896, 144.789}, Float2{397.883, 142.107}, Float2{408.558, 141.037}, Float2{414.373, 142.031}, Float2{421.260, 136.322}, Float2{424.031, 133.863}, Float2{424.094, 133.721}, Float2{428.437, 124.893}, Float2{432.890, 118.914}, Float2{440.064, 114.918}, Float2{451.576, 101.508}, Float2{456.814, 93.631}, Float2{462.707, 85.650}, Float2{463.340, 83.666}, Float2{464.457, 81.797}, Float2{465.127, 79.688}, Float2{467.308, 73.947}, Float2{464.209, 72.649}, Float2{465.000, 71.057}, Float2{468.578, 66.293}, Float2{463.392, 64.471}, Float2{457.363, 67.410}, Float2{439.678, 84.844}, Float2{437.172, 91.172}, Float2{436.910, 91.709}, Float2{431.447, 104.865}, Float2{429.416, 105.615}, Float2{426.340, 102.881}, Float2{421.053, 96.563}, Float2{415.312, 101.475}, Float2{413.342, 106.893}, Float2{407.014, 112.887}, Float2{386.678, 113.840}, Float2{382.242, 112.147}, Float2{380.359, 114.432}, Float2{378.068, 115.547}, Float2{376.430, 114.375}, Float2{374.662, 112.969}, Float2{370.312, 112.969}, Float2{365.164, 111.955}, Float2{360.808, 109.992}, Float2{359.062, 109.736}, Float2{356.764, 106.543}, Float2{345.303, 106.172}, Float2{333.390, 109.549}, Float2{313.252, 99.682}, Float2{296.250, 91.443}, Float2{281.250, 84.834}, Float2{264.531, 76.275}, Float2{257.426, 79.619}, Float2{244.617, 74.955}, Float2{237.939, 72.188}, Float2{236.031, 69.609}, Float2{241.537, 62.813}, Float2{243.730, 60.977}, Float2{252.670, 55.307}, Float2{259.250, 49.688}, Float2{259.723, 46.557}, Float2{260.066, 40.412}, Float2{257.369, 39.031}, Float2{255.556, 38.715}, Float2{248.728, 35.639}, Float2{246.349, 32.941}, Float2{247.506, 29.531}, Float2{246.539, 24.857}, Float2{245.713, 24.680}, Float2{243.260, 21.563}, Float2{242.433, 24.141}, Float2{243.973, 26.719}, Float2{242.033, 24.375}, Float2{240.576, 20.086}, Float2{232.074, 16.367}, Float2{226.010, 17.016}, Float2{226.517, 12.492}, Float2{224.373, 4.910}, Float2{216.396, 0.811}
		};
		for (auto& c:dancingMan) c = c * 0.1f - Float2{50, 50};
		std::reverse(dancingMan, &dancingMan[dimof(dancingMan)]);

		// From https://freesvg.org/secretary-bird-silhouette-vector-image (marked with Licence: Public Domain)
		Float2 secretaryBird[] = {
			Float2{467.482, 1030.805}, Float2{466.875, 1026.442}, Float2{464.238, 1022.150}, Float2{453.223, 1007.111}, Float2{440.422, 991.324}, Float2{431.516, 989.029}, Float2{424.688, 987.656}, Float2{420.067, 986.283}, Float2{414.836, 985.182}, Float2{399.311, 988.648}, Float2{389.932, 987.993}, Float2{392.888, 968.800}, Float2{415.781, 962.797}, Float2{426.814, 967.885}, Float2{435.553, 969.584}, Float2{438.778, 969.330}, Float2{439.781, 954.769}, Float2{438.733, 920.625}, Float2{437.767, 873.281}, Float2{437.939, 803.438}, Float2{437.939, 729.260}, Float2{429.896, 686.543}, Float2{425.625, 678.455}, Float2{421.544, 670.427}, Float2{404.607, 627.309}, Float2{399.864, 604.180}, Float2{399.302, 588.516}, Float2{398.590, 584.063}, Float2{388.136, 596.659}, Float2{369.838, 604.684}, Float2{364.519, 601.871}, Float2{360.099, 599.928}, Float2{356.917, 597.988}, Float2{352.125, 602.720}, Float2{342.949, 608.771}, Float2{330.558, 608.141}, Float2{322.551, 606.191}, Float2{321.429, 601.178}, Float2{319.506, 595.669}, Float2{306.598, 601.318}, Float2{288.204, 601.994}, Float2{278.800, 599.063}, Float2{270.816, 604.753}, Float2{264.760, 611.550}, Float2{247.163, 634.396}, Float2{237.788, 646.723}, Float2{209.697, 680.077}, Float2{183.916, 707.807}, Float2{179.809, 704.994}, Float2{176.839, 702.188}, Float2{159.569, 729.515}, Float2{121.842, 795.469}, Float2{60.000, 902.492}, Float2{57.361, 907.611}, Float2{52.683, 909.473}, Float2{49.110, 905.414}, Float2{47.575, 902.446}, Float2{44.412, 905.897}, Float2{41.243, 911.471}, Float2{38.966, 914.646}, Float2{33.818, 912.726}, Float2{30.938, 908.198}, Float2{24.293, 901.340}, Float2{17.648, 896.039}, Float2{11.974, 897.155}, Float2{4.244, 899.053}, Float2{0.952, 899.072}, Float2{9.810, 882.188}, Float2{68.437, 795.469}, Float2{77.804, 779.531}, Float2{92.373, 754.219}, Float2{111.972, 721.693}, Float2{130.704, 689.875}, Float2{121.064, 692.344}, Float2{114.452, 695.625}, Float2{113.438, 690.938}, Float2{113.438, 686.250}, Float2{109.453, 686.254}, Float2{102.651, 687.976}, Float2{93.749, 689.531}, Float2{95.358, 683.803}, Float2{126.083, 646.125}, Float2{133.676, 638.156}, Float2{159.898, 608.456}, Float2{176.250, 587.984}, Float2{164.982, 582.996}, Float2{157.089, 575.431}, Float2{224.629, 494.052}, Float2{255.062, 455.272}, Float2{257.324, 447.019}, Float2{291.403, 414.073}, Float2{327.597, 379.788}, Float2{331.529, 376.875}, Float2{334.389, 374.028}, Float2{341.962, 367.342}, Float2{350.217, 360.694}, Float2{358.400, 354.164}, Float2{367.969, 346.916}, Float2{384.844, 335.571}, Float2{410.156, 319.806}, Float2{452.745, 295.171}, Float2{461.652, 289.844}, Float2{476.719, 283.661}, Float2{497.344, 276.061}, Float2{513.321, 270.559}, Float2{532.071, 263.633}, Float2{549.375, 257.362}, Float2{568.375, 250.029}, Float2{580.343, 244.394}, Float2{587.344, 231.255}, Float2{593.780, 214.293}, Float2{591.838, 209.369}, Float2{592.957, 204.212}, Float2{597.726, 196.971}, Float2{598.125, 187.244}, Float2{597.656, 176.250}, Float2{597.580, 168.382}, Float2{593.830, 167.813}, Float2{591.574, 167.110}, Float2{596.630, 152.037}, Float2{599.728, 142.319}, Float2{598.503, 138.128}, Float2{594.174, 139.268}, Float2{591.043, 141.721}, Float2{585.150, 146.933}, Float2{574.687, 154.602}, Float2{564.050, 157.677}, Float2{583.582, 130.322}, Float2{584.038, 126.563}, Float2{551.719, 148.549}, Float2{532.514, 159.375}, Float2{529.687, 156.261}, Float2{549.943, 129.768}, Float2{563.652, 118.299}, Float2{567.167, 114.970}, Float2{551.913, 117.796}, Float2{539.709, 122.915}, Float2{522.495, 131.308}, Float2{509.135, 136.875}, Float2{505.328, 134.766}, Float2{510.330, 124.688}, Float2{528.829, 107.040}, Float2{548.672, 94.033}, Float2{555.000, 90.032}, Float2{541.406, 88.127}, Float2{499.518, 97.000}, Float2{490.318, 94.219}, Float2{504.375, 83.314}, Float2{538.057, 70.562}, Float2{544.430, 67.804}, Float2{527.257, 62.813}, Float2{517.361, 60.466}, Float2{509.531, 57.097}, Float2{502.500, 51.905}, Float2{516.558, 44.518}, Float2{529.018, 42.497}, Float2{541.350, 41.566}, Float2{546.818, 38.203}, Float2{541.772, 31.875}, Float2{535.880, 19.219}, Float2{532.844, 9.870}, Float2{531.563, 4.542}, Float2{545.867, 7.144}, Float2{564.909, 20.686}, Float2{578.034, 29.261}, Float2{586.875, 33.762}, Float2{598.738, 38.467}, Float2{605.156, 40.806}, Float2{611.898, 43.120}, Float2{615.451, 44.033}, Float2{682.500, 43.002}, Float2{697.427, 43.445}, Float2{706.063, 43.995}, Float2{721.275, 55.620}, Float2{727.211, 63.932}, Float2{756.094, 67.885}, Float2{780.771, 74.146}, Float2{792.063, 85.965}, Float2{795.938, 100.826}, Float2{789.047, 118.946}, Float2{786.658, 121.174}, Float2{786.037, 116.488}, Float2{784.817, 110.932}, Float2{771.081, 107.813}, Float2{716.484, 120.019}, Float2{714.361, 123.496}, Float2{710.344, 137.468}, Float2{709.688, 139.890}, Float2{708.321, 144.298}, Float2{708.734, 154.329}, Float2{714.788, 174.154}, Float2{719.063, 192.709}, Float2{720.570, 211.196}, Float2{726.629, 232.632}, Float2{736.397, 249.787}, Float2{742.482, 272.812}, Float2{739.688, 273.684}, Float2{747.251, 307.694}, Float2{748.141, 318.262}, Float2{744.711, 318.281}, Float2{742.500, 316.832}, Float2{742.500, 323.298}, Float2{743.906, 333.750}, Float2{745.313, 352.504}, Float2{743.454, 368.558}, Float2{740.633, 384.844}, Float2{737.303, 412.292}, Float2{734.762, 411.650}, Float2{727.639, 420.503}, Float2{723.663, 434.259}, Float2{714.027, 444.765}, Float2{710.205, 451.111}, Float2{704.815, 460.849}, Float2{702.188, 466.864}, Float2{687.534, 480.000}, Float2{681.065, 482.311}, Float2{676.601, 484.101}, Float2{669.772, 488.355}, Float2{659.721, 494.138}, Float2{625.386, 517.485}, Float2{608.390, 531.020}, Float2{588.027, 549.645}, Float2{556.519, 595.315}, Float2{534.092, 633.384}, Float2{529.271, 641.017}, Float2{526.875, 645.367}, Float2{522.656, 652.601}, Float2{518.438, 662.484}, Float2{516.666, 670.116}, Float2{511.553, 698.906}, Float2{499.746, 811.417}, Float2{496.010, 855.480}, Float2{491.735, 900.938}, Float2{489.410, 924.375}, Float2{488.074, 944.063}, Float2{491.819, 941.153}, Float2{505.426, 932.009}, Float2{520.116, 919.919}, Float2{524.202, 914.063}, Float2{530.149, 914.063}, Float2{541.315, 918.415}, Float2{542.377, 926.500}, Float2{537.172, 932.412}, Float2{529.332, 937.821}, Float2{526.668, 943.293}, Float2{544.694, 937.406}, Float2{561.094, 933.073}, Float2{567.997, 940.250}, Float2{570.304, 949.088}, Float2{563.806, 951.721}, Float2{558.153, 951.410}, Float2{554.202, 956.642}, Float2{547.786, 961.875}, Float2{540.713, 964.831}, Float2{536.773, 969.753}, Float2{537.705, 984.915}, Float2{534.717, 988.125}, Float2{530.610, 986.016}, Float2{528.888, 983.906}, Float2{522.570, 989.998}, Float2{511.836, 987.729}, Float2{500.755, 985.807}, Float2{503.095, 988.506}, Float2{529.256, 997.727}, Float2{532.878, 1001.869}, Float2{534.588, 1006.003}, Float2{531.051, 1013.942}, Float2{517.029, 1020.920}, Float2{515.245, 1014.806}, Float2{511.163, 1014.726}, Float2{506.905, 1015.722}, Float2{500.836, 1009.305}, Float2{487.349, 999.601}, Float2{470.625, 995.603}, Float2{478.083, 1003.702}, Float2{483.734, 1007.343}, Float2{483.750, 1022.414}, Float2{479.233, 1026.863}
		};
		for (auto& c:secretaryBird) c = c * 0.05f - Float2{50, 50};

		// From https://freesvg.org/swallow-silhouette (marked with Licence: Public Domain)
		Float2 swallow[] = {
			Float2{1097.886, 1103.825},  Float2{1097.857, 1119.543},  Float2{1101.846, 1122.340},  Float2{1119.312, 1113.571},  Float2{1130.002, 1108.763},  Float2{1160.159, 1089.391},  Float2{1186.773, 1071.622},  Float2{1223.445, 1048.001},  Float2{1265.425, 1022.840},  Float2{1296.157, 999.613},  Float2{1328.071, 978.416},  Float2{1371.634, 946.304},  Float2{1418.819, 913.656},  Float2{1433.110, 899.521},  Float2{1491.132, 846.749},  Float2{1525.430, 807.866},  Float2{1567.656, 760.788},  Float2{1587.907, 735.416},  Float2{1607.665, 715.766},  Float2{1618.418, 719.789},  Float2{1615.700, 760.174},  Float2{1581.002, 841.975},  Float2{1540.626, 914.564},  Float2{1489.939, 992.352},  Float2{1458.451, 1034.808},  Float2{1426.127, 1083.195},  Float2{1372.361, 1152.443},  Float2{1332.891, 1201.191},  Float2{1269.300, 1275.858},  Float2{1220.311, 1329.535},  Float2{1176.808, 1374.695},  Float2{1157.494, 1396.966},  Float2{1144.347, 1413.070},  Float2{1107.647, 1450.247},  Float2{1056.748, 1495.359},  Float2{997.118, 1536.503},  Float2{943.732, 1564.044},  Float2{883.096, 1584.904},  Float2{783.752, 1605.373},  Float2{728.968, 1610.160},  Float2{708.724, 1611.451},  Float2{650.419, 1606.283},  Float2{637.851, 1599.473},  Float2{614.934, 1599.187},  Float2{576.025, 1612.513},  Float2{487.085, 1623.027},  Float2{393.203, 1609.658},  Float2{315.995, 1572.628},  Float2{240.282, 1521.404},  Float2{198.853, 1494.137},  Float2{169.976, 1481.839},  Float2{132.667, 1460.324},  Float2{98.523, 1443.274},  Float2{92.727, 1440.274},  Float2{66.503, 1433.302},  Float2{52.755, 1432.724},  Float2{27.944, 1433.134},  Float2{23.509, 1429.566},  Float2{36.130, 1415.589},  Float2{77.258, 1400.257},  Float2{85.164, 1392.212},  Float2{123.997, 1337.681},  Float2{183.424, 1304.307},  Float2{231.565, 1289.373},  Float2{306.243, 1291.476},  Float2{348.436, 1302.416},  Float2{383.992, 1296.636},  Float2{383.044, 1286.802},  Float2{343.742, 1272.213},  Float2{307.953, 1249.311},  Float2{296.295, 1234.359},  Float2{277.141, 1196.689},  Float2{254.686, 1091.855},  Float2{258.900, 1033.721},  Float2{271.886, 945.682},  Float2{311.881, 812.942},  Float2{350.494, 711.304},  Float2{390.529, 611.704},  Float2{420.109, 545.373},  Float2{462.581, 460.542},  Float2{491.207, 403.999},  Float2{529.933, 330.403},  Float2{562.211, 273.064},  Float2{601.069, 205.599},  Float2{633.945, 149.880},  Float2{644.873, 140.683},  Float2{659.561, 148.894},  Float2{659.020, 175.132},  Float2{645.463, 235.087},  Float2{632.819, 283.154},  Float2{606.821, 366.264},  Float2{586.702, 436.687},  Float2{581.533, 502.080},  Float2{577.366, 533.077},  Float2{569.568, 574.849},  Float2{562.611, 601.394},  Float2{559.978, 647.753},  Float2{552.688, 710.057},  Float2{550.189, 742.539},  Float2{548.432, 787.638},  Float2{554.686, 793.891},  Float2{609.205, 782.878},  Float2{641.901, 805.655},  Float2{631.920, 918.078},  Float2{610.479, 984.733},  Float2{609.996, 985.948},  Float2{610.059, 994.047},  Float2{616.525, 990.084},  Float2{735.732, 897.294},  Float2{784.258, 853.635},  Float2{812.677, 822.053},  Float2{844.102, 771.528},  Float2{883.795, 696.249},  Float2{941.277, 572.766},  Float2{966.301, 505.245},  Float2{996.128, 419.229},  Float2{1022.352, 334.125},  Float2{1054.888, 203.250},  Float2{1069.478, 136.759},  Float2{1088.115, 47.764},  Float2{1096.278, 26.475},  Float2{1102.081, 23.329},  Float2{1105.242, 28.526},  Float2{1104.911, 62.528},  Float2{1088.340, 191.825},  Float2{1061.046, 373.233},  Float2{1049.779, 451.040},  Float2{1041.714, 505.273},  Float2{1042.658, 538.613},  Float2{1045.776, 581.697},  Float2{1046.567, 587.510},  Float2{1054.619, 611.012},  Float2{1057.490, 614.813},  Float2{1078.033, 609.338},  Float2{1086.211, 604.155},  Float2{1108.230, 591.947},  Float2{1136.748, 575.397},  Float2{1160.292, 560.198},  Float2{1194.320, 540.883},  Float2{1228.567, 514.820},  Float2{1310.256, 450.385},  Float2{1363.335, 405.916},  Float2{1405.571, 369.120},  Float2{1425.788, 348.779},  Float2{1447.275, 328.841},  Float2{1528.993, 257.194},  Float2{1571.776, 222.679},  Float2{1581.079, 221.924},  Float2{1579.072, 231.053},  Float2{1539.333, 277.831},  Float2{1505.133, 313.983},  Float2{1446.231, 377.019},  Float2{1403.049, 423.215},  Float2{1364.489, 465.040},  Float2{1313.272, 518.346},  Float2{1262.525, 575.544},  Float2{1195.074, 654.616},  Float2{1166.390, 689.533},  Float2{1120.588, 751.697},  Float2{1075.916, 822.350},  Float2{1063.168, 842.099},  Float2{1040.781, 885.581},  Float2{1021.773, 935.024},  Float2{1020.816, 942.069},  Float2{1018.941, 966.187},  Float2{1008.122, 1020.726},  Float2{1013.131, 1023.623},  Float2{1034.981, 1017.389},  Float2{1076.211, 1041.755},  Float2{1088.139, 1062.498}
		};
		for (auto& c:swallow) c = c * 0.05f - Float2{50, 50};

		// From https://freesvg.org/1538160235 (marked with Licence: Public Domain)
		Float2 greenTree[] = {
			Float2{1773.119, 564.923}, Float2{1777.055, 522.664}, Float2{1765.011, 507.183}, Float2{1762.671, 505.157}, Float2{1755.991, 500.123}, Float2{1729.833, 498.073}, Float2{1699.169, 497.536}, Float2{1677.462, 517.128}, Float2{1670.418, 516.401}, Float2{1647.272, 514.016}, Float2{1631.468, 503.265}, Float2{1627.931, 499.727}, Float2{1633.862, 486.722}, Float2{1648.833, 473.946}, Float2{1655.824, 465.281}, Float2{1666.087, 454.215}, Float2{1670.997, 444.774}, Float2{1672.708, 439.981}, Float2{1673.713, 408.231}, Float2{1684.611, 393.676}, Float2{1682.777, 349.680}, Float2{1681.178, 336.614}, Float2{1646.823, 304.981}, Float2{1628.972, 302.579}, Float2{1614.518, 278.849}, Float2{1597.415, 271.277}, Float2{1573.191, 252.639}, Float2{1538.700, 240.307}, Float2{1531.398, 241.715}, Float2{1516.171, 240.837}, Float2{1492.467, 242.752}, Float2{1472.814, 249.663}, Float2{1463.742, 249.280}, Float2{1460.489, 222.614}, Float2{1445.444, 212.833}, Float2{1425.742, 204.514}, Float2{1404.106, 198.281}, Float2{1370.943, 226.994}, Float2{1370.644, 264.050}, Float2{1363.605, 279.852}, Float2{1344.060, 297.163}, Float2{1323.944, 307.862}, Float2{1317.872, 315.442}, Float2{1301.250, 328.715}, Float2{1288.504, 348.017}, Float2{1257.380, 361.599}, Float2{1241.909, 337.935}, Float2{1279.868, 300.422}, Float2{1301.374, 272.378}, Float2{1317.588, 242.948}, Float2{1317.019, 207.186}, Float2{1307.558, 199.163}, Float2{1277.281, 143.700}, Float2{1258.570, 127.068}, Float2{1241.754, 109.633}, Float2{1235.010, 107.619}, Float2{1213.183, 100.577}, Float2{1207.503, 92.905}, Float2{1139.249, 86.334}, Float2{1127.212, 97.519}, Float2{1122.128, 94.661}, Float2{1103.310, 93.395}, Float2{1094.609, 82.111}, Float2{1078.325, 60.819}, Float2{1043.430, 32.461}, Float2{1019.918, 44.315}, Float2{1002.723, 32.820}, Float2{974.898, 20.292}, Float2{949.627, 30.525}, Float2{940.955, 36.789}, Float2{932.433, 30.685}, Float2{927.711, 25.262}, Float2{904.398, 20.356}, Float2{884.296, 27.010}, Float2{873.293, 39.897}, Float2{871.747, 43.287}, Float2{868.479, 60.552}, Float2{840.972, 69.573}, Float2{818.279, 88.294}, Float2{815.102, 95.613}, Float2{813.001, 114.767}, Float2{815.600, 125.903}, Float2{822.355, 136.913}, Float2{834.352, 155.519}, Float2{835.943, 161.125}, Float2{814.383, 167.733}, Float2{805.963, 164.353}, Float2{801.872, 158.508}, Float2{793.524, 148.793}, Float2{776.725, 132.694}, Float2{755.795, 133.914}, Float2{731.611, 140.644}, Float2{710.458, 147.804}, Float2{683.215, 140.679}, Float2{655.110, 151.284}, Float2{643.612, 173.268}, Float2{634.258, 191.755}, Float2{624.438, 203.877}, Float2{606.065, 217.791}, Float2{603.110, 219.712}, Float2{600.265, 209.853}, Float2{594.614, 188.432}, Float2{591.309, 167.610}, Float2{577.576, 108.398}, Float2{568.953, 99.907}, Float2{517.768, 73.579}, Float2{491.359, 70.264}, Float2{453.730, 80.057}, Float2{419.782, 78.561}, Float2{408.344, 80.845}, Float2{393.093, 97.816}, Float2{375.054, 113.823}, Float2{356.746, 102.806}, Float2{337.614, 107.785}, Float2{314.392, 132.519}, Float2{311.648, 139.678}, Float2{312.447, 173.883}, Float2{311.373, 185.843}, Float2{305.184, 183.129}, Float2{253.879, 181.810}, Float2{248.511, 185.350}, Float2{232.695, 195.250}, Float2{223.283, 200.489}, Float2{190.522, 237.518}, Float2{188.326, 241.541}, Float2{181.666, 249.969}, Float2{164.560, 244.359}, Float2{133.904, 267.398}, Float2{128.265, 275.677}, Float2{125.907, 283.648}, Float2{125.105, 308.608}, Float2{130.606, 326.134}, Float2{136.107, 334.940}, Float2{143.816, 342.119}, Float2{148.230, 344.365}, Float2{170.665, 351.255}, Float2{160.989, 362.075}, Float2{162.818, 394.858}, Float2{149.014, 399.933}, Float2{125.412, 422.956}, Float2{106.946, 439.001}, Float2{104.047, 452.941}, Float2{96.473, 460.367}, Float2{79.973, 457.849}, Float2{53.850, 474.324}, Float2{50.828, 481.329}, Float2{28.231, 525.205}, Float2{58.843, 563.302}, Float2{70.090, 569.174}, Float2{81.436, 574.283}, Float2{85.573, 579.367}, Float2{83.454, 611.997}, Float2{90.690, 630.246}, Float2{110.518, 641.946}, Float2{138.835, 654.541}, Float2{129.937, 684.582}, Float2{126.332, 690.236}, Float2{84.305, 706.074}, Float2{75.935, 731.601}, Float2{66.651, 740.496}, Float2{60.462, 749.015}, Float2{50.328, 760.167}, Float2{37.193, 777.487}, Float2{33.805, 782.940}, Float2{28.158, 799.423}, Float2{31.623, 808.363}, Float2{28.343, 817.333}, Float2{23.066, 831.420}, Float2{21.673, 885.323}, Float2{34.276, 896.386}, Float2{35.705, 898.013}, Float2{51.900, 915.487}, Float2{70.089, 914.341}, Float2{74.215, 933.918}, Float2{76.996, 938.548}, Float2{91.152, 958.276}, Float2{102.614, 963.777}, Float2{114.766, 964.752}, Float2{126.452, 959.699}, Float2{137.015, 954.679}, Float2{146.843, 945.469}, Float2{159.020, 939.114}, Float2{168.608, 933.519}, Float2{175.992, 951.097}, Float2{181.493, 959.216}, Float2{190.468, 965.708}, Float2{205.232, 972.756}, Float2{214.029, 981.390}, Float2{223.970, 988.982}, Float2{227.770, 991.697}, Float2{241.338, 1003.661}, Float2{243.680, 1005.231}, Float2{249.640, 1007.213}, Float2{259.227, 1011.960}, Float2{263.045, 1015.798}, Float2{270.894, 1020.871}, Float2{281.545, 1026.220}, Float2{287.320, 1028.272}, Float2{295.294, 1033.575}, Float2{314.932, 1037.327}, Float2{319.052, 1035.989}, Float2{334.715, 1024.160}, Float2{341.697, 1029.882}, Float2{347.946, 1040.833}, Float2{369.866, 1053.337}, Float2{375.350, 1056.657}, Float2{382.304, 1057.985}, Float2{395.937, 1059.399}, Float2{411.181, 1052.228}, Float2{415.536, 1051.455}, Float2{429.525, 1062.016}, 
			Float2{440.888, 1065.356}, Float2{477.398, 1051.998}, Float2{502.509, 1029.256}, Float2{509.267, 1026.766}, Float2{528.704, 1025.443}, Float2{540.268, 1016.580}, Float2{543.362, 1011.516}, Float2{545.964, 1007.881}, Float2{552.255, 1006.497}, Float2{556.461, 1006.668}, Float2{566.853, 1006.935}, Float2{593.461, 988.318}, Float2{598.813, 963.320}, Float2{601.803, 950.849}, Float2{609.230, 956.472}, Float2{619.491, 978.906}, Float2{639.836, 1002.701}, Float2{644.383, 1009.232}, Float2{646.364, 1008.792}, Float2{651.176, 1006.410}, Float2{648.913, 1013.853}, Float2{649.951, 1022.773}, Float2{647.631, 1032.341}, Float2{650.574, 1042.912}, Float2{649.445, 1063.887}, Float2{620.536, 1161.419}, Float2{522.612, 1240.640}, Float2{496.178, 1252.560}, Float2{435.956, 1272.038}, Float2{401.352, 1320.376}, Float2{369.555, 1313.941}, Float2{377.036, 1303.548}, Float2{375.831, 1271.182}, Float2{331.149, 1266.506}, Float2{318.268, 1324.492}, Float2{393.421, 1379.792}, Float2{436.278, 1371.308}, Float2{427.003, 1389.800}, Float2{411.381, 1407.947}, Float2{445.143, 1495.142}, Float2{451.788, 1440.501}, Float2{454.195, 1435.525}, Float2{463.249, 1428.728}, Float2{473.132, 1416.669}, Float2{480.089, 1398.107}, Float2{499.890, 1396.327}, Float2{584.645, 1411.843}, Float2{613.318, 1377.766}, Float2{626.593, 1360.879}, Float2{634.145, 1364.856}, Float2{647.277, 1386.011}, Float2{652.780, 1390.137}, Float2{654.848, 1391.168}, Float2{670.804, 1399.019}, Float2{680.206, 1406.411}, Float2{679.747, 1426.092}, Float2{666.769, 1479.391}, Float2{638.203, 1479.495}, Float2{576.901, 1466.526}, Float2{520.247, 1583.419}, Float2{574.358, 1579.841}, Float2{567.196, 1545.148}, Float2{608.749, 1522.668}, Float2{604.784, 1584.012}, Float2{634.400, 1613.457}, Float2{634.593, 1611.055}, Float2{664.360, 1608.458}, Float2{658.395, 1567.234}, Float2{649.111, 1557.243}, Float2{651.262, 1547.006}, Float2{701.491, 1529.504}, Float2{731.042, 1484.571}, Float2{742.602, 1412.469}, Float2{703.093, 1314.344}, Float2{722.254, 1300.739}, Float2{726.978, 1306.236}, Float2{731.125, 1325.080}, Float2{733.008, 1339.801}, Float2{734.382, 1344.944}, Float2{740.004, 1358.136}, Float2{762.935, 1386.852}, Float2{819.591, 1410.518}, Float2{919.808, 1415.264}, Float2{935.772, 1433.991}, Float2{937.638, 1445.084}, Float2{909.694, 1465.281}, Float2{904.248, 1504.235}, Float2{946.916, 1499.990}, Float2{979.001, 1414.206}, Float2{986.073, 1414.928}, Float2{1003.809, 1433.433}, Float2{1014.120, 1441.578}, Float2{1023.119, 1546.116}, Float2{1046.425, 1552.028}, Float2{1084.389, 1494.786}, Float2{1056.650, 1490.538}, Float2{1052.387, 1492.165}, Float2{1056.512, 1484.533}, Float2{1069.382, 1472.575}, Float2{1060.792, 1402.997}, Float2{1009.101, 1369.228}, Float2{984.299, 1359.122}, Float2{935.184, 1350.079}, Float2{883.119, 1340.539}, Float2{877.164, 1337.875}, Float2{862.339, 1326.964}, Float2{868.674, 1315.302}, Float2{891.697, 1332.652}, Float2{978.106, 1336.653}, Float2{1012.205, 1330.342}, Float2{1043.269, 1327.401}, Float2{1086.921, 1380.255}, Float2{1135.924, 1386.605}, Float2{1177.473, 1350.946}, Float2{1128.480, 1333.044}, Float2{1115.678, 1340.025}, Float2{1116.923, 1321.664}, Float2{1094.305, 1271.995}, Float2{1075.696, 1269.778}, Float2{1040.081, 1263.889}, Float2{893.277, 1255.575}, Float2{897.986, 1205.962}, Float2{925.504, 1144.365}, Float2{969.691, 1079.765}, Float2{977.089, 1070.858}, Float2{980.549, 1066.519}, Float2{990.125, 1046.989}, Float2{999.294, 1036.952}, Float2{1010.098, 1040.765}, Float2{1024.542, 1049.789}, Float2{1026.646, 1053.663}, Float2{1033.236, 1050.083}, Float2{1043.980, 1051.023}, Float2{1045.210, 1055.242}, Float2{1048.446, 1060.737}, Float2{1060.204, 1061.467}, Float2{1076.285, 1058.787}, Float2{1093.675, 1061.430}, Float2{1104.643, 1061.819}, Float2{1107.831, 1059.356}, Float2{1126.591, 1064.613}, Float2{1161.222, 1050.083}, Float2{1170.631, 1044.925}, Float2{1183.877, 1049.908}, Float2{1174.590, 1087.151}, Float2{1191.295, 1114.759}, Float2{1197.828, 1122.021}, Float2{1212.279, 1136.779}, Float2{1230.641, 1135.045}, Float2{1264.822, 1122.274}, Float2{1276.621, 1117.080}, Float2{1309.432, 1111.340}, Float2{1320.289, 1103.287}, Float2{1329.273, 1101.170}, Float2{1336.215, 1108.451}, Float2{1351.923, 1120.739}, Float2{1360.331, 1124.768}, Float2{1394.287, 1122.494}, Float2{1399.495, 1119.929}, Float2{1402.018, 1112.926}, Float2{1411.801, 1105.522}, Float2{1419.541, 1107.233}, Float2{1454.722, 1116.484}, Float2{1466.162, 1109.449}, Float2{1470.843, 1105.441}, Float2{1481.207, 1091.157}, Float2{1485.011, 1075.183}, Float2{1489.016, 1045.565}, Float2{1492.599, 1033.006}, Float2{1520.847, 1013.722}, Float2{1527.523, 1016.126}, Float2{1539.466, 1009.956}, Float2{1551.132, 987.591}, Float2{1553.200, 972.827}, Float2{1565.632, 946.833}, Float2{1562.593, 926.711}, Float2{1611.662, 921.974}, Float2{1619.652, 920.692}, Float2{1623.117, 919.767}, Float2{1635.980, 910.147}, Float2{1647.625, 905.326}, Float2{1656.565, 910.140}, Float2{1681.732, 900.786}, Float2{1690.870, 888.762}, Float2{1708.825, 870.269}, Float2{1710.985, 867.503}, Float2{1710.344, 865.577}, Float2{1709.282, 860.898}, Float2{1717.769, 850.830}, Float2{1738.238, 816.754}, Float2{1770.305, 767.103}, Float2{1767.404, 736.843}, Float2{1766.888, 719.570}, Float2{1760.455, 702.805}, Float2{1757.972, 699.710}, Float2{1753.527, 685.137}, Float2{1744.339, 667.303}, Float2{1760.404, 656.980}, Float2{1763.544, 654.209}, Float2{1770.750, 637.819}, Float2{1777.685, 610.973}, Float2{1770.602, 578.171}
		};
		for (auto& c:greenTree) c = c * 0.1f - Float2{50, 50};
		std::reverse(greenTree, &greenTree[dimof(greenTree)]);

		// From https://freesvg.org/fiddlers-silhouette-vector (marked with Licence: Public Domain)
		Float2 fiddlers[] = {
			Float2{129.840, 201.260}, Float2{133.697, 207.687}, Float2{157.694, 207.687}, Float2{159.408, 193.121}, Float2{159.408, 185.407}, Float2{160.693, 179.405}, Float2{161.979, 161.408}, Float2{148.695, 156.696}, Float2{149.552, 149.841}, Float2{149.980, 137.841}, Float2{151.266, 130.556}, Float2{150.409, 130.127}, Float2{151.694, 126.699}, Float2{153.837, 110.845}, Float2{153.837, 108.700}, Float2{154.265, 107.416}, Float2{148.266, 101.845}, Float2{142.695, 100.987}, Float2{133.697, 89.847}, Float2{134.982, 82.133}, Float2{141.410, 71.420}, Float2{143.552, 62.848}, Float2{145.695, 54.280}, Float2{149.552, 50.852}, Float2{166.692, 39.281}, Float2{163.264, 28.997}, Float2{161.122, 22.141}, Float2{162.836, 19.141}, Float2{171.835, 0.716}, Float2{188.119, 1.143}, Float2{191.547, 5.429}, Float2{193.260, 12.712}, Float2{192.406, 15.712}, Float2{195.834, 23.424}, Float2{194.118, 25.996}, Float2{194.547, 33.710}, Float2{205.689, 35.424}, Float2{218.543, 7.142}, Float2{221.971, 9.283}, Float2{220.259, 13.569}, Float2{213.403, 30.281}, Float2{210.834, 37.566}, Float2{222.400, 36.281}, Float2{232.684, 41.850}, Float2{236.113, 39.280}, Float2{240.398, 37.993}, Float2{247.683, 45.278}, Float2{250.253, 46.565}, Float2{248.540, 48.707}, Float2{257.112, 45.279}, Float2{263.110, 52.135}, Float2{251.110, 59.849}, Float2{248.540, 57.708}, Float2{236.541, 61.136}, Float2{239.540, 67.135}, Float2{236.541, 68.849}, Float2{236.541, 70.992}, Float2{222.829, 91.989}, Float2{210.833, 91.560}, Float2{212.974, 100.131}, Float2{226.686, 101.845}, Float2{238.686, 103.988}, Float2{246.400, 117.701}, Float2{248.970, 131.842}, Float2{254.110, 142.555}, Float2{253.256, 144.697}, Float2{255.398, 140.840}, Float2{264.399, 129.270}, Float2{281.967, 124.129}, Float2{277.252, 108.701}, Float2{276.395, 104.418}, Float2{274.682, 100.561}, Float2{284.966, 85.562}, Float2{293.534, 70.134}, Float2{295.679, 68.849}, Float2{299.107, 61.566}, Float2{315.819, 47.423}, Float2{326.532, 45.280}, Float2{322.675, 35.425}, Float2{319.247, 23.854}, Float2{324.391, 12.712}, Float2{338.103, 9.713}, Float2{350.103, 18.713}, Float2{350.528, 22.997}, Float2{353.531, 25.569}, Float2{353.102, 32.854}, Float2{350.103, 37.567}, Float2{353.102, 46.137}, Float2{365.956, 29.853}, Float2{372.384, 30.709}, Float2{369.385, 34.566}, Float2{357.385, 49.994}, Float2{367.672, 62.848}, Float2{371.954, 61.992}, Float2{377.098, 72.705}, Float2{376.241, 76.133}, Float2{379.669, 74.419}, Float2{372.814, 81.704}, Float2{373.243, 91.132}, Float2{362.530, 100.987}, Float2{363.387, 117.271}, Float2{358.243, 134.840}, Float2{354.387, 153.695}, Float2{349.246, 165.691}, Float2{353.532, 206.402}, Float2{383.955, 206.402}, Float2{383.955, 214.545}, Float2{345.818, 214.545}, Float2{339.816, 215.399}, Float2{295.253, 214.970}, Float2{224.119, 215.399}, Float2{116.989, 215.399}, Float2{45.856, 215.828}, Float2{2.148, 216.257}, Float2{2.148, 208.114}, Float2{28.288, 207.685}, Float2{34.715, 154.121}, Float2{35.144, 148.123}, Float2{32.144, 144.266}, Float2{31.287, 138.268}, Float2{28.288, 132.270}, Float2{30.002, 123.699}, Float2{33.858, 111.699}, Float2{31.716, 97.559}, Float2{27.859, 102.701}, Float2{19.717, 105.273}, Float2{13.718, 108.272}, Float2{10.290, 107.417}, Float2{5.148, 107.417}, Float2{0.005, 102.273}, Float2{3.862, 88.561}, Float2{9.861, 75.278}, Float2{14.146, 67.564}, Float2{15.003, 64.136}, Float2{18.860, 58.994}, Float2{21.003, 57.709}, Float2{23.145, 49.995}, Float2{36.429, 40.996}, Float2{36.001, 35.425}, Float2{29.145, 33.282}, Float2{26.574, 26.855}, Float2{30.430, 9.284}, Float2{41.143, 5.856}, Float2{48.428, 4.571}, Float2{56.570, 11.000}, Float2{57.427, 14.428}, Float2{65.997, 29.427}, Float2{65.140, 33.282}, Float2{72.854, 31.997}, Float2{92.994, 14.428}, Float2{95.565, 17.856}, Float2{92.566, 23.425}, Float2{90.423, 23.854}, Float2{81.853, 31.568}, Float2{89.994, 31.568}, Float2{92.994, 32.855}, Float2{100.707, 33.282}, Float2{104.564, 30.283}, Float2{110.563, 28.998}, Float2{119.562, 37.568}, Float2{122.133, 34.996}, Float2{125.133, 40.996}, Float2{137.988, 40.996}, Float2{126.846, 48.708}, Float2{122.561, 49.566}, Float2{122.561, 46.566}, Float2{119.133, 56.421}, Float2{125.132, 61.565}, Float2{123.846, 66.705}, Float2{120.418, 68.848}, Float2{110.134, 90.274}, Float2{103.706, 94.560}, Float2{109.705, 102.272}, Float2{117.418, 120.270}, Float2{110.990, 133.124}, Float2{107.562, 142.554}, Float2{108.419, 154.979}, Float2{110.133, 155.409}, Float2{108.419, 156.266}, Float2{110.990, 166.550}, Float2{111.847, 166.550}, Float2{114.847, 177.263}, Float2{116.989, 181.978}, Float2{111.847, 188.405}, Float2{117.846, 197.402}, Float2{122.989, 200.830}
		};
		for (auto& c:fiddlers) c = c * 0.5f - Float2{50, 50};

		// From https://freesvg.org/perseus-vector-art (marked with Licence: Public Domain)
		Float2 perseus[] = {
			Float2{550.230, 1030.100}, Float2{550.610, 1023.100}, Float2{551.660, 1017.900}, Float2{548.060, 1004.200}, Float2{542.960, 990.960}, Float2{541.170, 985.980}, Float2{539.340, 982.650}, Float2{536.670, 977.670}, Float2{533.270, 972.180}, Float2{529.780, 966.010}, Float2{525.880, 958.530}, Float2{522.850, 951.340}, Float2{521.620, 948.740}, Float2{520.920, 947.190}, Float2{518.910, 942.620}, Float2{516.890, 937.940}, Float2{515.880, 936.020}, Float2{514.870, 934.130}, Float2{512.080, 927.610}, Float2{511.140, 924.770}, Float2{506.410, 910.060}, Float2{503.030, 899.140}, Float2{500.160, 889.660}, Float2{496.730, 879.220}, Float2{494.790, 873.830}, Float2{493.980, 871.460}, Float2{492.150, 867.710}, Float2{490.670, 865.060}, Float2{489.130, 860.180}, Float2{488.380, 857.680}, Float2{487.590, 855.700}, Float2{487.140, 854.070}, Float2{486.150, 851.110}, Float2{480.270, 831.510}, Float2{479.790, 829.640}, Float2{479.080, 827.330}, Float2{469.290, 792.860}, Float2{468.070, 788.350}, Float2{465.960, 778.630}, Float2{463.430, 769.850}, Float2{462.160, 766.150}, Float2{454.320, 755.810}, Float2{450.660, 751.780}, Float2{449.470, 747.000}, Float2{448.510, 741.290}, Float2{445.810, 742.560}, Float2{430.670, 748.870}, Float2{424.120, 751.150}, Float2{417.560, 752.480}, Float2{412.020, 753.770}, Float2{405.720, 755.070}, Float2{398.660, 756.120}, Float2{378.080, 758.050}, Float2{372.190, 756.060}, Float2{375.640, 753.480}, Float2{378.500, 752.850}, Float2{381.260, 751.570}, Float2{387.310, 747.970}, Float2{389.680, 746.630}, Float2{393.460, 743.380}, Float2{398.720, 738.790}, Float2{401.400, 736.190}, Float2{406.130, 731.760}, Float2{409.490, 728.520}, Float2{411.880, 725.500}, Float2{413.530, 723.430}, Float2{418.890, 717.170}, Float2{427.320, 707.340}, Float2{429.180, 705.320}, Float2{430.780, 703.770}, Float2{433.210, 700.870}, Float2{434.500, 697.830}, Float2{433.440, 695.590}, Float2{432.690, 694.090}, Float2{432.370, 693.070}, Float2{428.360, 683.260}, Float2{423.580, 672.580}, Float2{422.360, 669.970}, Float2{419.290, 661.360}, Float2{418.860, 658.750}, Float2{417.850, 654.650}, Float2{416.560, 651.330}, Float2{415.370, 648.610}, Float2{413.830, 643.180}, Float2{412.500, 638.380}, Float2{411.510, 635.130}, Float2{410.970, 632.420}, Float2{407.760, 622.310}, Float2{405.670, 617.450}, Float2{405.380, 615.630}, Float2{405.340, 603.420}, Float2{401.930, 603.520}, Float2{394.500, 604.030}, Float2{393.210, 601.090}, Float2{391.710, 597.910}, Float2{390.180, 595.830}, Float2{385.580, 598.190}, Float2{381.740, 601.170}, Float2{376.200, 593.430}, Float2{374.970, 581.000}, Float2{376.060, 575.670}, Float2{371.720, 567.960}, Float2{368.700, 568.700}, Float2{365.750, 568.740}, Float2{368.600, 549.990}, Float2{370.420, 545.360}, Float2{368.030, 538.670}, Float2{366.310, 533.610}, Float2{369.280, 517.130}, Float2{368.730, 509.940}, Float2{368.170, 506.960}, Float2{367.580, 498.060}, Float2{367.090, 489.510}, Float2{366.080, 481.200}, Float2{364.580, 477.330}, Float2{362.980, 472.070}, Float2{361.450, 467.090}, Float2{360.660, 463.450}, Float2{361.400, 459.980}, Float2{362.610, 457.190}, Float2{359.870, 451.040}, Float2{356.520, 443.290}, Float2{354.780, 438.660}, Float2{351.980, 431.190}, Float2{348.680, 402.880}, Float2{347.330, 387.220}, Float2{345.090, 385.190}, Float2{343.950, 384.240}, Float2{341.420, 379.190}, Float2{338.320, 372.070}, Float2{338.460, 341.740}, Float2{339.170, 329.050}, Float2{338.190, 313.990}, Float2{336.360, 307.940}, Float2{335.100, 304.310}, Float2{336.560, 288.960}, Float2{338.540, 283.980}, Float2{340.080, 280.660}, Float2{343.920, 273.220}, Float2{348.240, 266.270}, Float2{349.520, 264.220}, Float2{351.140, 262.150}, Float2{353.860, 259.630}, Float2{357.010, 256.550}, Float2{364.620, 247.720}, Float2{367.770, 243.680}, Float2{368.880, 240.890}, Float2{365.010, 234.940}, Float2{357.130, 226.450}, Float2{352.680, 221.120}, Float2{347.900, 217.790}, Float2{346.470, 217.100}, Float2{339.590, 211.620}, Float2{336.450, 209.720}, Float2{333.160, 207.820}, Float2{330.110, 205.940}, Float2{325.670, 203.170}, Float2{321.360, 200.560}, Float2{314.020, 199.180}, Float2{310.610, 200.460}, Float2{303.770, 202.920}, Float2{302.060, 203.400}, Float2{297.690, 204.290}, Float2{292.720, 205.450}, Float2{291.010, 205.930}, Float2{290.190, 206.400}, Float2{283.790, 208.330}, Float2{277.170, 210.290}, Float2{267.930, 212.850}, Float2{259.790, 212.760}, Float2{258.240, 210.960}, Float2{257.560, 209.510}, Float2{253.470, 209.770}, Float2{249.300, 211.980}, Float2{246.710, 213.060}, Float2{243.120, 213.710}, Float2{235.810, 216.120}, Float2{228.750, 218.520}, Float2{224.210, 219.960}, Float2{220.710, 221.180}, Float2{219.310, 221.580}, Float2{217.520, 222.010}, Float2{201.020, 227.230}, Float2{196.480, 228.600}, Float2{180.960, 232.470}, Float2{174.150, 233.870}, Float2{172.540, 234.160}, Float2{169.900, 234.800}, Float2{163.710, 236.290}, Float2{158.070, 237.780}, Float2{154.790, 238.600}, Float2{150.160, 239.330}, Float2{146.570, 239.920}, Float2{144.230, 240.760}, Float2{141.270, 242.120}, Float2{127.910, 245.660}, Float2{119.590, 247.740}, Float2{112.780, 249.090}, Float2{108.250, 250.060}, Float2{97.659, 252.400}, Float2{92.617, 253.400}, Float2{87.500, 254.630}, Float2{82.962, 255.730}, Float2{77.492, 256.940}, Float2{65.139, 258.840}, Float2{61.106, 259.510}, Float2{48.753, 261.700}, Float2{41.947, 262.730}, Float2{38.417, 263.110}, Float2{32.115, 263.710}, Float2{16.252, 259.280}, Float2{14.304, 252.290}, Float2{22.208, 249.890}, Float2{30.350, 246.760}, Float2{35.896, 244.390}, Float2{44.972, 240.800}, Float2{54.551, 237.490}, Float2{62.618, 234.890}, Float2{69.425, 232.500}, Float2{75.223, 230.350}, Float2{87.178, 226.310}, Float2{91.639, 224.900}, Float2{95.817, 223.530}, Float2{103.210, 220.870}, Float2{105.730, 220.180}, Float2{120.160, 214.500}, Float2{123.690, 213.170}, Float2{130.840, 210.200}, Float2{139.650, 206.350}, Float2{143.570, 204.500}, Float2{147.100, 203.020}, Float2{154.260, 199.760}, Float2{156.400, 198.570}, Float2{159.410, 197.380}, Float2{164.880, 195.760}, Float2{170.550, 193.820}, Float2{173.420, 192.440}, Float2{177.260, 190.800}, Float2{182.160, 189.060}, Float2{187.270, 187.360}, Float2{192.190, 185.700}, Float2{195.340, 184.570}, Float2{202.660, 181.690}, Float2{210.600, 178.180}, Float2{219.050, 174.410}, Float2{227.480, 170.980}, Float2{229.550, 170.040}, Float2{231.050, 169.390}, Float2{232.630, 168.540}, Float2{235.490, 167.080}, Float2{238.840, 165.390}, Float2{244.790, 162.530}, Float2{247.980, 161.690}, Float2{250.840, 160.750}, Float2{252.030, 159.940}, Float2{253.380, 158.950}, Float2{255.970, 158.000}, Float2{259.120, 157.060}, Float2{260.760, 156.580}, Float2{262.970, 156.100}, Float2{270.850, 153.210}, Float2{268.830, 150.410}, Float2{266.810, 147.360}, Float2{266.040, 145.150}, Float2{265.660, 143.160}, Float2{266.480, 136.180}, Float2{268.400, 129.860}, Float2{268.580, 128.110}, Float2{269.790, 126.470}, Float2{272.060, 125.740}, Float2{273.870, 122.060}, Float2{275.270, 118.970}, Float2{282.410, 116.250}, Float2{284.280, 115.760}, Float2{286.210, 115.770}, Float2{289.500, 115.210}, Float2{291.140, 115.000}, Float2{295.300, 114.530}, Float2{308.980, 113.090}, Float2{318.830, 113.350}, Float2{325.980, 118.560}, Float2{335.950, 124.890}, Float2{337.760, 125.650}, Float2{340.950, 126.810}, Float2{342.940, 125.410}, Float2{343.740, 124.470}, Float2{344.990, 123.060}, Float2{347.480, 122.370}, Float2{350.290, 122.600}, Float2{352.310, 122.130}, Float2{354.340, 120.950}, Float2{356.400, 119.770}, Float2{357.300, 119.590}, Float2{361.980, 117.880}, Float2{367.150, 116.060}, Float2{370.920, 114.350}, Float2{371.680, 113.680}, Float2{377.160, 112.680}, Float2{380.800, 111.780}, Float2{382.540, 110.560}, Float2{384.940, 108.890}, Float2{388.560, 107.230}, Float2{391.080, 106.140}, Float2{398.660, 102.230}, Float2{403.700, 100.540}, Float2{418.890, 95.394}, Float2{421.600, 94.185}, Float2{424.300, 92.962}, Float2{428.670, 91.079}, Float2{434.350, 88.511}, Float2{439.750, 86.120}, Float2{442.950, 84.932}, Float2{449.050, 82.797}, Float2{454.460, 80.662}, Float2{456.480, 79.476}, Float2{459.320, 78.039}, Float2{462.940, 77.045}, Float2{468.480, 75.400}, Float2{473.440, 73.743}, Float2{478.600, 72.083}, Float2{487.070, 69.088}, Float2{493.770, 66.668}, Float2{497.230, 65.248}, Float2{501.010, 63.570}, Float2{503.530, 62.643}, Float2{508.910, 60.546}, Float2{515.040, 57.863}, Float2{516.840, 56.938}, Float2{519.280, 55.989}, Float2{521.840, 55.040}, Float2{526.380, 53.165}, Float2{540.280, 46.974}, Float2{541.960, 46.025}, Float2{543.980, 45.076}, Float2{556.600, 39.798}, Float2{565.050, 36.061}, Float2{569.960, 33.896}, Float2{579.030, 30.371}, Float2{587.710, 27.079}, Float2{592.940, 25.148}, Float2{600.620, 21.535}, Float2{602.490, 20.877}, Float2{603.800, 22.419}, Float2{604.530, 26.840}, Float2{603.420, 35.316}, Float2{594.850, 43.036}, Float2{589.670, 46.737}, Float2{584.880, 50.181}, Float2{578.800, 54.214}, Float2{572.060, 58.554}, Float2{568.630, 60.835}, Float2{562.300, 65.479}, Float2{555.570, 69.173}, Float2{552.060, 71.078}, Float2{547.830, 73.507}, Float2{545.890, 74.494}, Float2{544.320, 75.495}, Float2{540.390, 77.986}, Float2{533.580, 81.972}, Float2{530.740, 83.509}, Float2{529.430, 84.294}, Float2{527.220, 85.725}, Float2{522.940, 88.273}, Float2{516.610, 91.776}, Float2{511.130, 94.603}, Float2{506.620, 97.074}, Float2{502.770, 98.974}, Float2{500.400, 99.634}, Float2{498.380, 100.510}, Float2{494.960, 102.710}, Float2{491.080, 105.400}, Float2{488.470, 106.760}, Float2{485.040, 108.300}, Float2{477.980, 112.450}, Float2{475.860, 113.440}, Float2{471.740, 115.830}, Float2{467.580, 118.620}, Float2{454.120, 125.930}, Float2{450.590, 127.640}, Float2{442.020, 131.900}, Float2{433.700, 135.770}, Float2{432.190, 136.460}, Float2{425.880, 139.370}, Float2{420.840, 141.620}, 
			Float2{416.050, 143.800}, Float2{412.770, 145.490}, Float2{408.630, 147.920}, Float2{404.850, 149.540}, Float2{400.250, 151.420}, Float2{396.190, 153.030}, Float2{384.620, 158.220}, Float2{384.870, 160.110}, Float2{381.510, 166.030}, Float2{377.450, 171.210}, Float2{388.570, 180.350}, Float2{391.660, 182.470}, Float2{400.800, 186.060}, Float2{405.920, 186.830}, Float2{411.010, 188.400}, Float2{420.200, 191.290}, Float2{421.640, 191.690}, Float2{423.100, 192.750}, Float2{427.220, 194.430}, Float2{431.790, 195.320}, Float2{433.460, 196.490}, Float2{435.120, 197.670}, Float2{445.990, 201.050}, Float2{449.010, 204.390}, Float2{452.100, 207.140}, Float2{454.370, 208.970}, Float2{457.960, 212.090}, Float2{459.950, 201.980}, Float2{458.850, 196.200}, Float2{451.160, 183.950}, Float2{443.460, 172.420}, Float2{443.000, 169.510}, Float2{444.900, 167.500}, Float2{449.180, 163.890}, Float2{454.360, 162.580}, Float2{457.650, 163.220}, Float2{459.790, 163.850}, Float2{462.210, 163.010}, Float2{462.220, 160.430}, Float2{461.740, 154.470}, Float2{463.760, 153.030}, Float2{465.460, 151.070}, Float2{466.300, 148.900}, Float2{469.360, 144.130}, Float2{472.270, 142.820}, Float2{475.560, 142.560}, Float2{476.040, 139.590}, Float2{476.740, 133.240}, Float2{478.170, 132.250}, Float2{481.150, 130.640}, Float2{484.310, 130.390}, Float2{486.130, 130.590}, Float2{488.310, 131.000}, Float2{490.670, 131.670}, Float2{494.130, 131.880}, Float2{497.000, 131.690}, Float2{501.760, 131.660}, Float2{506.860, 132.310}, Float2{512.080, 132.840}, Float2{514.320, 132.030}, Float2{515.530, 128.730}, Float2{515.260, 126.830}, Float2{517.440, 126.500}, Float2{520.450, 127.200}, Float2{523.600, 128.420}, Float2{526.810, 123.900}, Float2{531.880, 119.710}, Float2{535.590, 119.280}, Float2{537.060, 119.300}, Float2{539.330, 120.040}, Float2{542.130, 119.980}, Float2{545.140, 120.520}, Float2{548.320, 121.230}, Float2{551.610, 121.940}, Float2{553.370, 122.650}, Float2{554.480, 123.370}, Float2{559.970, 125.500}, Float2{565.090, 127.650}, Float2{566.800, 128.580}, Float2{568.980, 129.520}, Float2{571.550, 130.720}, Float2{573.610, 131.910}, Float2{577.790, 134.110}, Float2{582.870, 135.600}, Float2{589.280, 135.000}, Float2{594.530, 135.040}, Float2{592.700, 132.710}, Float2{591.200, 129.820}, Float2{595.760, 128.310}, Float2{604.360, 136.060}, Float2{605.880, 138.530}, Float2{608.970, 138.540}, Float2{613.330, 136.980}, Float2{614.330, 132.240}, Float2{613.370, 128.530}, Float2{612.680, 127.090}, Float2{601.590, 117.520}, Float2{591.420, 115.300}, Float2{587.010, 114.050}, Float2{585.960, 111.300}, Float2{574.620, 98.948}, Float2{569.200, 97.055}, Float2{565.290, 93.994}, Float2{569.830, 91.393}, Float2{575.370, 89.924}, Float2{580.920, 88.440}, Float2{596.880, 86.955}, Float2{607.970, 86.656}, Float2{622.010, 88.295}, Float2{628.560, 90.145}, Float2{632.090, 91.297}, Float2{637.390, 93.528}, Float2{640.920, 95.165}, Float2{643.020, 96.019}, Float2{645.800, 97.441}, Float2{648.310, 98.883}, Float2{650.010, 99.897}, Float2{651.730, 100.590}, Float2{655.760, 104.290}, Float2{657.760, 106.350}, Float2{659.710, 108.350}, Float2{661.920, 111.440}, Float2{665.470, 115.470}, Float2{668.750, 119.100}, Float2{672.500, 124.410}, Float2{674.360, 127.820}, Float2{676.590, 131.670}, Float2{681.950, 144.320}, Float2{682.240, 145.930}, Float2{682.630, 149.570}, Float2{683.070, 152.330}, Float2{683.760, 155.490}, Float2{684.800, 157.950}, Float2{685.500, 160.350}, Float2{685.650, 162.610}, Float2{686.210, 165.110}, Float2{686.170, 169.160}, Float2{685.080, 170.220}, Float2{685.210, 172.680}, Float2{686.290, 174.850}, Float2{686.620, 177.400}, Float2{686.120, 181.790}, Float2{685.750, 186.280}, Float2{685.540, 188.360}, Float2{685.260, 192.070}, Float2{685.190, 196.680}, Float2{684.750, 200.620}, Float2{684.030, 202.610}, Float2{683.050, 204.190}, Float2{681.580, 206.480}, Float2{680.760, 210.670}, Float2{678.990, 217.310}, Float2{677.300, 222.770}, Float2{674.620, 230.870}, Float2{673.000, 235.270}, Float2{670.160, 241.520}, Float2{668.090, 246.130}, Float2{662.260, 259.720}, Float2{660.750, 263.020}, Float2{656.340, 276.130}, Float2{654.930, 280.400}, Float2{652.550, 289.040}, Float2{651.460, 292.600}, Float2{650.330, 300.630}, Float2{649.560, 307.150}, Float2{649.250, 308.760}, Float2{649.810, 317.670}, Float2{651.470, 331.430}, Float2{652.930, 336.230}, Float2{653.360, 338.780}, Float2{655.630, 348.960}, Float2{656.110, 350.530}, Float2{656.150, 352.560}, Float2{657.250, 356.030}, Float2{659.990, 363.310}, Float2{662.500, 368.140}, Float2{664.610, 371.500}, Float2{667.660, 376.280}, Float2{670.040, 379.700}, Float2{674.040, 384.160}, Float2{676.600, 391.610}, Float2{671.260, 388.020}, Float2{666.690, 383.180}, Float2{663.390, 378.900}, Float2{658.960, 373.070}, Float2{646.970, 357.520}, Float2{645.270, 354.020}, Float2{643.220, 348.440}, Float2{640.400, 338.540}, Float2{637.380, 320.960}, Float2{636.860, 307.460}, Float2{636.880, 284.990}, Float2{637.930, 273.930}, Float2{638.400, 270.010}, Float2{639.080, 265.410}, Float2{640.070, 259.780}, Float2{641.410, 253.490}, Float2{642.430, 249.450}, Float2{643.220, 244.600}, Float2{643.490, 222.510}, Float2{642.130, 218.240}, Float2{640.410, 213.280}, Float2{636.670, 199.330}, Float2{636.170, 193.710}, Float2{631.430, 194.050}, Float2{623.990, 194.100}, Float2{618.750, 193.590}, Float2{615.190, 192.760}, Float2{611.460, 190.750}, Float2{607.550, 188.880}, Float2{606.130, 187.540}, Float2{605.250, 187.040}, Float2{604.860, 196.440}, Float2{601.550, 211.730}, Float2{601.090, 213.720}, Float2{599.630, 217.160}, Float2{597.300, 222.060}, Float2{594.070, 227.750}, Float2{591.760, 230.320}, Float2{590.260, 232.210}, Float2{581.700, 239.080}, Float2{577.360, 242.450}, Float2{575.600, 243.160}, Float2{573.710, 243.870}, Float2{570.930, 245.240}, Float2{564.280, 247.380}, Float2{555.870, 248.790}, Float2{549.570, 249.230}, Float2{540.840, 249.800}, Float2{531.720, 250.290}, Float2{529.110, 251.310}, Float2{528.250, 254.410}, Float2{526.880, 256.480}, Float2{520.380, 254.130}, Float2{517.590, 253.300}, Float2{514.450, 255.500}, Float2{510.280, 257.900}, Float2{506.810, 262.130}, Float2{508.900, 265.970}, Float2{512.630, 268.840}, Float2{520.660, 275.880}, Float2{523.950, 278.520}, Float2{525.730, 279.160}, Float2{529.120, 280.650}, Float2{531.510, 281.880}, Float2{534.660, 285.270}, Float2{538.720, 289.590}, Float2{540.490, 291.200}, Float2{543.190, 294.230}, Float2{544.450, 295.820}, Float2{548.150, 302.420}, Float2{551.520, 307.400}, Float2{553.460, 310.640}, Float2{554.120, 313.050}, Float2{555.910, 315.670}, Float2{558.250, 317.270}, Float2{559.740, 318.490}, Float2{566.550, 324.630}, Float2{570.700, 327.450}, Float2{573.520, 329.290}, Float2{575.170, 330.290}, Float2{578.460, 332.400}, Float2{581.080, 334.230}, Float2{583.290, 335.940}, Float2{588.870, 339.350}, Float2{590.160, 339.930}, Float2{593.330, 341.970}, Float2{597.990, 346.040}, Float2{600.420, 349.260}, Float2{602.350, 351.960}, Float2{604.590, 354.360}, Float2{606.900, 356.130}, Float2{611.170, 360.500}, Float2{612.480, 362.470}, Float2{615.320, 363.930}, Float2{618.420, 364.830}, Float2{621.740, 366.070}, Float2{627.480, 368.790}, Float2{630.150, 370.100}, Float2{630.830, 371.030}, Float2{631.770, 372.810}, Float2{633.600, 378.290}, Float2{636.690, 382.190}, Float2{639.430, 384.890}, Float2{641.410, 387.890}, Float2{643.940, 394.760}, Float2{646.460, 399.750}, Float2{646.970, 401.320}, Float2{650.260, 406.160}, Float2{651.900, 409.650}, Float2{653.030, 414.470}, Float2{654.030, 417.260}, Float2{655.040, 420.760}, Float2{656.060, 423.370}, Float2{656.800, 425.610}, Float2{657.540, 427.730}, Float2{659.890, 430.670}, Float2{661.890, 433.900}, Float2{664.180, 439.320}, Float2{665.120, 441.750}, Float2{665.820, 443.600}, Float2{666.780, 445.910}, Float2{669.380, 453.830}, Float2{670.450, 457.660}, Float2{671.150, 459.060}, Float2{673.440, 460.810}, Float2{674.630, 464.650}, Float2{676.120, 468.370}, Float2{677.720, 470.570}, Float2{679.110, 472.640}, Float2{684.780, 478.700}, Float2{689.760, 482.070}, Float2{698.250, 489.680}, Float2{700.270, 491.980}, Float2{702.560, 493.780}, Float2{704.790, 490.800}, Float2{708.490, 484.980}, Float2{715.640, 485.750}, Float2{726.640, 494.310}, Float2{728.610, 497.120}, Float2{730.910, 500.080}, Float2{731.710, 501.050}, Float2{731.100, 504.360}, Float2{730.030, 505.720}, Float2{731.100, 507.810}, Float2{729.490, 514.600}, Float2{723.360, 517.300}, Float2{714.110, 516.220}, Float2{713.240, 518.370}, Float2{709.940, 525.340}, Float2{707.970, 528.320}, Float2{704.870, 534.190}, Float2{701.240, 538.090}, Float2{699.890, 540.930}, Float2{693.350, 545.180}, Float2{691.420, 547.250}, Float2{687.680, 550.810}, Float2{683.610, 552.300}, Float2{681.020, 552.850}, Float2{675.820, 550.130}, Float2{669.940, 549.690}, Float2{666.980, 552.130}, Float2{663.610, 554.290}, Float2{660.660, 556.590}, Float2{658.900, 557.520}, Float2{654.060, 561.220}, Float2{649.810, 564.920}, Float2{650.500, 567.270}, Float2{652.870, 571.670}, Float2{654.850, 574.990}, Float2{655.560, 576.730}, Float2{656.860, 579.340}, Float2{659.350, 583.440}, Float2{660.480, 593.240}, Float2{651.180, 597.370}, Float2{647.550, 597.370}, Float2{646.380, 595.360}, Float2{633.850, 592.080}, Float2{625.410, 595.050}, Float2{622.970, 596.670}, Float2{620.530, 598.110}, Float2{616.730, 600.260}, Float2{613.350, 602.120}, Float2{610.270, 603.300}, Float2{607.750, 604.490}, Float2{604.950, 606.510}, Float2{598.810, 611.130}, Float2{590.710, 617.360}, Float2{585.940, 621.570}, Float2{566.440, 640.850}, Float2{564.050, 643.360}, Float2{553.700, 653.030}, Float2{551.300, 655.720}, Float2{544.370, 663.360}, Float2{539.580, 668.740}, Float2{534.790, 673.930}, Float2{528.860, 680.090}, Float2{523.720, 686.390}, Float2{517.010, 692.210}, Float2{514.870, 693.740}, 
			Float2{512.170, 696.930}, Float2{508.970, 700.570}, Float2{508.910, 706.270}, Float2{509.640, 715.850}, Float2{510.630, 721.250}, Float2{511.340, 722.200}, Float2{512.570, 724.290}, Float2{514.130, 727.520}, Float2{515.410, 730.000}, Float2{520.430, 740.220}, Float2{522.990, 745.150}, Float2{524.450, 747.820}, Float2{525.180, 749.190}, Float2{526.500, 751.420}, Float2{528.280, 755.060}, Float2{529.490, 758.730}, Float2{532.820, 768.530}, Float2{534.800, 775.260}, Float2{536.070, 781.180}, Float2{537.040, 786.320}, Float2{538.110, 794.970}, Float2{538.570, 796.960}, Float2{539.010, 798.760}, Float2{538.680, 803.850}, Float2{538.160, 806.960}, Float2{540.130, 812.170}, Float2{539.580, 814.080}, Float2{538.730, 815.730}, Float2{538.470, 818.180}, Float2{539.070, 820.950}, Float2{539.840, 823.890}, Float2{539.920, 826.910}, Float2{539.700, 832.720}, Float2{540.690, 839.360}, Float2{540.730, 872.730}, Float2{540.940, 883.570}, Float2{542.600, 882.130}, Float2{543.230, 880.240}, Float2{543.940, 878.360}, Float2{544.820, 872.570}, Float2{545.620, 863.210}, Float2{547.260, 855.420}, Float2{561.510, 844.300}, Float2{572.510, 838.520}, Float2{577.640, 837.160}, Float2{580.610, 837.070}, Float2{584.430, 836.040}, Float2{588.550, 834.780}, Float2{590.000, 834.140}, Float2{591.510, 833.430}, Float2{593.710, 832.720}, Float2{604.400, 828.220}, Float2{607.230, 827.020}, Float2{609.810, 825.450}, Float2{617.560, 819.750}, Float2{620.080, 817.900}, Float2{622.390, 815.980}, Float2{623.780, 813.950}, Float2{625.790, 809.170}, Float2{627.810, 804.210}, Float2{630.440, 801.140}, Float2{632.450, 801.600}, Float2{634.280, 803.460}, Float2{633.540, 810.470}, Float2{632.850, 811.460}, Float2{631.090, 815.290}, Float2{629.320, 820.240}, Float2{630.710, 821.510}, Float2{633.430, 820.310}, Float2{635.260, 819.430}, Float2{638.430, 817.340}, Float2{647.920, 809.560}, Float2{657.050, 806.150}, Float2{657.610, 811.470}, Float2{653.570, 815.960}, Float2{649.990, 821.240}, Float2{647.040, 826.540}, Float2{642.510, 830.850}, Float2{641.670, 833.190}, Float2{642.430, 834.380}, Float2{637.510, 840.310}, Float2{634.810, 841.610}, Float2{633.130, 844.520}, Float2{632.260, 848.200}, Float2{630.710, 850.270}, Float2{630.330, 851.140}, Float2{627.270, 854.080}, Float2{624.840, 855.430}, Float2{623.970, 857.960}, Float2{622.760, 861.660}, Float2{618.770, 864.470}, Float2{613.740, 868.280}, Float2{612.170, 870.180}, Float2{609.780, 874.640}, Float2{606.590, 876.090}, Float2{604.110, 877.320}, Float2{602.390, 878.060}, Float2{602.400, 879.930}, Float2{598.100, 883.800}, Float2{590.410, 887.830}, Float2{587.980, 890.430}, Float2{585.310, 891.220}, Float2{578.400, 893.460}, Float2{575.120, 895.340}, Float2{570.610, 898.850}, Float2{567.540, 899.480}, Float2{563.920, 900.480}, Float2{561.830, 902.750}, Float2{563.590, 905.710}, Float2{571.360, 910.140}, Float2{574.020, 915.410}, Float2{573.600, 923.080}, Float2{571.070, 939.480}, Float2{569.880, 944.220}, Float2{568.560, 949.680}, Float2{569.410, 959.630}, Float2{572.880, 966.300}, Float2{576.380, 971.990}, Float2{577.390, 973.640}, Float2{578.400, 975.310}, Float2{579.280, 978.490}, Float2{580.160, 988.660}, Float2{580.160, 996.810}, Float2{577.990, 999.770}, Float2{576.340, 1004.900}, Float2{573.980, 1015.400}, Float2{571.920, 1019.900}, Float2{565.940, 1028.500}, Float2{563.250, 1031.100}, Float2{557.340, 1031.000}
		};
		for (auto& c:perseus) c = c * 0.1f - Float2{50, 50};

		// From https://freesvg.org/1529566357 (marked with Licence: Public Domain)
		Float2 eagleFlight[] = {
			Float2{13.814, 94.495}, Float2{13.143, 93.318}, Float2{13.002, 92.927}, Float2{12.894, 93.409}, Float2{12.779, 93.801}, Float2{12.803, 92.095}, Float2{13.105, 89.361}, Float2{13.204, 88.277}, Float2{12.686, 90.478}, Float2{12.602, 90.940}, Float2{11.307, 89.203}, Float2{11.226, 89.648}, Float2{11.130, 90.464}, Float2{11.091, 90.266}, Float2{11.324, 88.063}, Float2{11.493, 87.101}, Float2{11.427, 87.079}, Float2{11.001, 88.437}, Float2{10.503, 90.716}, Float2{10.263, 91.030}, Float2{9.677, 89.843}, Float2{9.315, 89.302}, Float2{9.170, 89.265}, Float2{10.304, 85.524}, Float2{10.576, 84.458}, Float2{10.452, 84.509}, Float2{9.863, 85.644}, Float2{8.299, 88.857}, Float2{6.797, 91.592}, Float2{6.600, 91.845}, Float2{6.320, 92.062}, Float2{6.110, 91.539}, Float2{6.058, 91.279}, Float2{5.830, 91.603}, Float2{6.524, 89.623}, Float2{7.366, 87.967}, Float2{8.827, 85.174}, Float2{11.131, 80.496}, Float2{11.055, 80.401}, Float2{10.689, 80.834}, Float2{8.479, 83.549}, Float2{5.175, 85.376}, Float2{4.402, 85.458}, Float2{4.251, 85.411}, Float2{5.364, 84.894}, Float2{11.823, 78.353}, Float2{12.796, 76.758}, Float2{13.685, 75.275}, Float2{14.543, 73.730}, Float2{15.449, 72.050}, Float2{18.365, 68.169}, Float2{19.241, 67.180}, Float2{20.204, 66.118}, Float2{21.603, 65.084}, Float2{24.209, 63.517}, Float2{27.930, 61.206}, Float2{30.550, 59.564}, Float2{32.477, 58.286}, Float2{34.270, 56.437}, Float2{34.824, 54.836}, Float2{34.439, 54.510}, Float2{34.207, 54.773}, Float2{34.152, 55.020}, Float2{34.066, 54.822}, Float2{34.727, 52.920}, Float2{35.271, 52.556}, Float2{36.655, 51.876}, Float2{38.262, 51.285}, Float2{38.905, 50.950}, Float2{41.165, 49.054}, Float2{43.578, 44.688}, Float2{46.197, 39.349}, Float2{46.728, 38.212}, Float2{47.207, 37.152}, Float2{47.483, 36.504}, Float2{48.362, 35.529}, Float2{49.142, 34.752}, Float2{54.056, 30.116}, Float2{55.424, 29.059}, Float2{57.471, 27.457}, Float2{58.456, 26.718}, Float2{59.469, 25.931}, Float2{65.570, 19.749}, Float2{66.467, 18.859}, Float2{66.335, 19.421}, Float2{65.616, 20.743}, Float2{64.844, 21.602}, Float2{62.843, 23.603}, Float2{62.581, 23.891}, Float2{67.531, 20.163}, Float2{71.210, 17.057}, Float2{73.000, 15.730}, Float2{73.068, 15.887}, Float2{72.675, 16.684}, Float2{70.938, 18.612}, Float2{67.662, 21.267}, Float2{66.686, 21.996}, Float2{65.499, 22.826}, Float2{64.718, 23.554}, Float2{65.154, 23.366}, Float2{66.909, 22.419}, Float2{69.257, 21.180}, Float2{70.064, 20.730}, Float2{70.148, 20.688}, Float2{70.750, 20.422}, Float2{72.354, 19.724}, Float2{72.626, 19.764}, Float2{73.212, 19.871}, Float2{74.040, 20.058}, Float2{73.898, 20.441}, Float2{73.804, 20.491}, Float2{73.987, 20.513}, Float2{75.313, 20.046}, Float2{76.464, 19.758}, Float2{76.677, 19.996}, Float2{76.695, 20.565}, Float2{76.598, 20.668}, Float2{76.524, 20.769}, Float2{76.450, 20.916}, Float2{76.354, 21.034}, Float2{76.231, 21.078}, Float2{75.795, 21.284}, Float2{73.378, 22.336}, Float2{72.463, 22.740}, Float2{71.358, 23.234}, Float2{70.399, 23.675}, Float2{70.127, 23.824}, Float2{71.828, 23.368}, Float2{72.693, 23.110}, Float2{73.607, 22.864}, Float2{76.264, 22.255}, Float2{76.806, 22.307}, Float2{76.851, 22.531}, Float2{76.633, 22.693}, Float2{76.424, 22.928}, Float2{76.319, 23.150}, Float2{76.153, 23.475}, Float2{75.387, 23.925}, Float2{73.307, 24.599}, Float2{72.406, 24.962}, Float2{72.140, 25.247}, Float2{74.052, 24.913}, Float2{74.967, 24.714}, Float2{76.040, 24.698}, Float2{75.935, 25.261}, Float2{75.030, 26.231}, Float2{73.424, 27.158}, Float2{72.185, 27.751}, Float2{72.272, 28.022}, Float2{72.395, 28.415}, Float2{72.023, 29.318}, Float2{70.751, 30.377}, Float2{69.098, 29.808}, Float2{68.190, 29.438}, Float2{68.098, 29.515}, Float2{68.512, 29.819}, Float2{69.257, 30.352}, Float2{69.863, 30.796}, Float2{70.073, 30.964}, Float2{69.125, 31.267}, Float2{68.669, 31.390}, Float2{68.026, 31.514}, Float2{67.447, 31.592}, Float2{66.686, 31.693}, Float2{64.907, 31.981}, Float2{64.363, 32.176}, Float2{63.785, 32.439}, Float2{62.956, 33.200}, Float2{63.202, 33.383}, Float2{63.477, 33.669}, Float2{64.057, 34.064}, Float2{64.830, 35.320}, Float2{64.559, 35.697}, Float2{64.189, 36.087}, Float2{64.109, 36.590}, Float2{64.187, 37.649}, Float2{63.304, 38.578}, Float2{62.940, 38.845}, Float2{62.953, 39.121}, Float2{62.769, 39.969}, Float2{61.913, 40.865}, Float2{61.668, 41.248}, Float2{60.968, 42.582}, Float2{60.662, 42.908}, Float2{60.581, 43.295}, Float2{59.580, 44.809}, Float2{59.395, 45.177}, Float2{58.312, 47.273}, Float2{57.912, 47.797}, Float2{57.209, 49.270}, Float2{56.891, 49.863}, Float2{56.674, 50.480}, Float2{56.332, 51.115}, Float2{55.793, 52.119}, Float2{55.287, 52.747}, Float2{55.088, 52.984}, Float2{54.799, 53.438}, Float2{54.534, 53.842}, Float2{54.161, 54.361}, Float2{53.795, 54.902}, Float2{53.078, 55.439}, Float2{52.768, 55.552}, Float2{51.739, 55.020}, Float2{51.588, 55.164}, Float2{51.405, 55.410}, Float2{50.278, 56.280}, Float2{50.131, 56.490}, Float2{50.150, 56.873}, Float2{50.102, 57.752}, Float2{50.033, 58.032}, Float2{50.810, 58.572}, Float2{51.353, 58.699}, Float2{54.558, 59.850}, Float2{56.627, 60.739}, Float2{57.403, 61.125}, Float2{57.570, 61.274}, Float2{58.876, 61.767}, Float2{62.331, 63.060}, Float2{63.771, 63.447}, Float2{65.624, 64.391}, Float2{65.408, 64.832}, Float2{65.222, 65.111}, Float2{65.157, 65.402}, Float2{64.163, 65.958}, Float2{62.880, 65.548}, Float2{61.872, 65.231}, Float2{60.968, 64.844}, Float2{60.574, 64.653}, Float2{60.385, 65.345}, Float2{61.076, 66.141}, Float2{61.907, 67.212}, Float2{61.696, 67.519}, Float2{61.500, 67.611}, Float2{61.825, 67.877}, Float2{61.722, 68.169}, Float2{61.558, 68.497}, Float2{61.273, 68.733}, Float2{60.504, 68.755}, Float2{60.062, 68.499}, Float2{59.617, 68.235}, Float2{59.305, 68.141}, Float2{59.758, 68.925}, Float2{59.939, 69.233}, Float2{59.753, 69.281}, Float2{59.716, 69.677}, Float2{59.648, 70.389}, Float2{59.146, 71.012}, Float2{58.676, 71.407}, Float2{58.382, 71.704}, Float2{57.096, 72.401}, Float2{56.699, 72.852}, Float2{55.425, 74.287}, Float2{54.773, 74.225}, Float2{54.576, 74.077}, Float2{54.366, 74.386}, Float2{53.272, 75.696}, Float2{53.044, 76.043}, Float2{52.915, 76.474}, Float2{52.397, 77.275}, Float2{51.674, 78.031}, Float2{51.362, 78.157}, Float2{50.857, 78.311}, Float2{50.464, 78.211}, Float2{49.896, 76.928}, Float2{49.411, 75.634}, Float2{47.929, 72.677}, Float2{47.626, 72.112}, Float2{47.503, 71.951}, Float2{46.597, 70.166}, Float2{46.453, 69.910}, Float2{46.251, 69.504}, Float2{45.745, 68.491}, Float2{45.808, 69.325}, Float2{45.280, 68.664}, Float2{44.051, 65.618}, Float2{43.942, 64.876}, Float2{43.809, 64.272}, Float2{43.082, 64.143}, Float2{42.142, 64.230}, Float2{41.412, 64.307}, Float2{41.998, 64.851}, Float2{42.672, 65.371}, Float2{42.836, 66.187}, Float2{42.039, 67.049}, Float2{41.159, 67.960}, Float2{40.698, 68.659}, Float2{40.388, 68.889}, Float2{39.920, 69.220}, Float2{39.564, 69.552}, Float2{39.096, 70.025}, Float2{38.608, 70.297}, Float2{38.033, 70.602}, Float2{37.721, 70.839}, Float2{37.169, 71.404}, Float2{36.750, 71.632}, Float2{36.174, 71.882}, Float2{35.716, 72.272}, Float2{34.028, 73.931}, Float2{33.771, 74.171}, Float2{32.121, 75.506}, Float2{31.597, 75.787}, Float2{31.058, 76.351}, Float2{30.833, 76.466}, Float2{29.927, 76.788}, Float2{29.663, 76.820}, Float2{29.379, 77.251}, Float2{28.932, 77.765}, Float2{28.696, 77.933}, Float2{28.414, 78.123}, Float2{27.292, 78.149}, Float2{26.752, 78.607}, Float2{25.989, 79.731}, Float2{24.658, 79.960}, Float2{24.217, 79.801}, Float2{23.242, 80.872}, Float2{22.544, 81.124}, Float2{21.591, 80.281}, Float2{21.286, 79.934}, Float2{21.000, 80.086}, Float2{20.554, 80.531}, Float2{20.195, 81.601}, Float2{20.249, 81.961}, Float2{20.313, 82.752}, Float2{20.486, 83.914}, Float2{20.663, 85.273}, Float2{20.060, 88.412}, Float2{19.407, 88.405}, Float2{18.927, 87.779}, Float2{18.744, 87.635}, Float2{18.510, 88.684}, Float2{18.037, 90.384}, Float2{17.870, 90.890}, Float2{17.652, 90.829}, Float2{17.085, 90.174}, Float2{16.462, 89.439}, Float2{16.249, 89.180}, Float2{16.060, 89.290}, Float2{15.596, 90.206}, Float2{15.474, 90.506}, Float2{14.486, 93.446}, Float2{14.405, 94.138}, Float2{14.285, 94.721}
		};
		for (auto& c:eagleFlight) c = c * 0.5f - Float2{50, 50};

		// From https://freesvg.org/female-ninja (marked with Licence: Public Domain)
		Float2 ninja[] = {
			Float2{1503.170, 378.850}, Float2{1492.600, 351.390}, Float2{1455.430, 298.530}, Float2{1403.960, 230.000}, Float2{1372.940, 180.070}, Float2{1361.910, 161.780}, Float2{1357.160, 162.750}, Float2{1338.620, 140.340}, Float2{1320.230, 123.780}, Float2{1307.140, 113.920}, Float2{1296.020, 105.420}, Float2{1301.150, 90.560}, Float2{1284.210, 98.220}, Float2{1279.060, 103.200}, Float2{1273.530, 104.570}, Float2{1269.880, 96.130}, Float2{1254.920, 84.810}, Float2{1257.840, 100.360}, Float2{1262.910, 107.100}, Float2{1255.290, 111.670}, Float2{1242.700, 110.360}, Float2{1210.040, 52.950}, Float2{1165.110, 26.720}, Float2{1151.980, 23.720}, Float2{1099.620, 37.630}, Float2{1079.550, 54.530}, Float2{1064.620, 74.710}, Float2{1057.360, 89.160}, Float2{1048.220, 130.960}, Float2{1045.140, 157.960}, Float2{1051.410, 183.500}, Float2{1058.510, 199.360}, Float2{1064.370, 220.930}, Float2{1066.860, 242.980}, Float2{1063.870, 258.860}, Float2{1060.390, 266.090}, Float2{1057.780, 272.790}, Float2{1050.690, 277.690}, Float2{1041.190, 286.290}, Float2{1030.900, 292.780}, Float2{1018.310, 298.220}, Float2{1011.770, 291.110}, Float2{1008.350, 286.800}, Float2{1001.620, 278.370}, Float2{995.590, 270.460}, Float2{988.870, 262.180}, Float2{984.370, 255.630}, Float2{979.050, 250.970}, Float2{975.990, 244.920}, Float2{970.720, 239.500}, Float2{966.740, 234.270}, Float2{961.270, 226.820}, Float2{953.970, 218.030}, Float2{947.970, 210.210}, Float2{941.270, 201.830}, Float2{937.710, 197.150}, Float2{932.970, 192.120}, Float2{928.430, 185.600}, Float2{921.950, 177.280}, Float2{915.670, 169.300}, Float2{906.010, 161.750}, Float2{901.090, 164.180}, Float2{890.610, 174.980}, Float2{897.340, 188.600}, Float2{902.200, 193.620}, Float2{906.030, 198.920}, Float2{912.840, 207.210}, Float2{919.010, 215.040}, Float2{925.510, 223.300}, Float2{931.260, 230.820}, Float2{935.280, 235.690}, Float2{941.570, 243.520}, Float2{948.120, 251.730}, Float2{954.710, 260.140}, Float2{960.650, 266.340}, Float2{964.550, 273.370}, Float2{968.820, 277.080}, Float2{971.150, 281.800}, Float2{976.040, 286.820}, Float2{980.680, 292.370}, Float2{985.150, 298.770}, Float2{989.950, 306.070}, Float2{973.060, 313.130}, Float2{961.360, 319.320}, Float2{957.780, 324.700}, Float2{976.850, 335.230}, Float2{988.310, 341.890}, Float2{978.010, 349.130}, Float2{956.230, 385.130}, Float2{936.970, 453.080}, Float2{930.230, 479.300}, Float2{914.100, 522.190}, Float2{902.510, 545.500}, Float2{896.820, 558.880}, Float2{883.600, 579.590}, Float2{866.440, 597.260}, Float2{822.220, 649.330}, Float2{800.300, 679.310}, Float2{781.470, 704.460}, Float2{754.040, 736.990}, Float2{732.650, 756.720}, Float2{713.980, 765.950}, Float2{700.590, 772.670}, Float2{687.210, 789.360}, Float2{675.580, 784.130}, Float2{669.660, 782.340}, Float2{659.430, 778.080}, Float2{662.490, 761.830}, Float2{655.340, 774.270}, Float2{652.060, 776.670}, Float2{636.100, 768.820}, Float2{542.910, 723.960}, Float2{440.700, 674.290}, Float2{390.370, 650.600}, Float2{342.420, 627.400}, Float2{313.230, 613.010}, Float2{283.220, 598.920}, Float2{248.470, 582.540}, Float2{165.060, 543.990}, Float2{115.760, 521.180}, Float2{41.840, 490.250}, Float2{42.160, 493.740}, Float2{64.710, 507.190}, Float2{94.660, 523.360}, Float2{210.580, 582.450}, Float2{234.280, 594.660}, Float2{285.910, 620.100}, Float2{295.040, 624.950}, Float2{344.830, 649.130}, Float2{363.670, 658.340}, Float2{388.980, 671.030}, Float2{402.370, 677.650}, Float2{435.280, 693.760}, Float2{573.060, 760.880}, Float2{643.250, 794.830}, Float2{643.350, 799.260}, Float2{637.820, 813.350}, Float2{648.270, 799.920}, Float2{653.440, 803.590}, Float2{661.130, 806.840}, Float2{667.020, 809.950}, Float2{676.800, 814.590}, Float2{685.960, 819.620}, Float2{684.910, 837.010}, Float2{692.540, 842.560}, Float2{702.900, 851.230}, Float2{714.960, 852.880}, Float2{725.850, 855.200}, Float2{735.870, 859.860}, Float2{748.920, 848.530}, Float2{757.970, 853.860}, Float2{766.020, 858.140}, Float2{772.010, 860.900}, Float2{781.650, 865.430}, Float2{790.460, 869.530}, Float2{805.040, 876.950}, Float2{815.010, 881.730}, Float2{821.560, 885.490}, Float2{828.220, 887.750}, Float2{834.920, 891.210}, Float2{842.390, 895.490}, Float2{855.780, 898.060}, Float2{864.670, 879.870}, Float2{852.680, 870.380}, Float2{845.040, 867.050}, Float2{837.650, 864.150}, Float2{833.950, 860.950}, Float2{827.070, 858.790}, Float2{822.000, 855.910}, Float2{815.520, 852.490}, Float2{807.840, 849.260}, Float2{801.030, 846.070}, Float2{794.010, 842.070}, Float2{787.150, 838.810}, Float2{778.760, 835.280}, Float2{770.220, 831.790}, Float2{766.610, 828.370}, Float2{760.840, 826.360}, Float2{769.010, 803.970}, Float2{772.980, 791.010}, Float2{779.669, 784.740}, Float2{793.090, 772.090}, Float2{823.860, 740.790}, Float2{865.380, 700.400}, Float2{919.320, 654.360}, Float2{941.970, 622.070}, Float2{961.070, 592.050}, Float2{979.500, 546.350}, Float2{981.520, 546.440}, Float2{1007.870, 574.020}, Float2{1022.730, 603.010}, Float2{1025.930, 624.940}, Float2{1033.410, 634.100}, Float2{1033.640, 640.870}, Float2{1030.650, 646.340}, Float2{1022.230, 672.110}, Float2{1026.680, 682.870}, Float2{1026.910, 697.950}, Float2{1034.140, 724.020}, Float2{1042.090, 726.560}, Float2{1039.500, 740.520}, Float2{1032.770, 757.570}, Float2{1011.750, 795.560}, Float2{997.270, 818.050}, Float2{990.220, 829.240}, Float2{985.150, 834.420}, Float2{991.980, 819.090}, Float2{995.520, 808.600}, Float2{1001.450, 793.380}, Float2{1005.180, 784.440}, Float2{1007.520, 772.390}, Float2{998.330, 765.550}, Float2{981.240, 762.330}, Float2{973.400, 778.380}, Float2{963.110, 805.210}, Float2{961.740, 807.620}, Float2{951.190, 834.490}, Float2{949.470, 842.640}, Float2{952.110, 847.210}, Float2{948.720, 849.610}, Float2{924.410, 855.200}, Float2{906.270, 868.220}, Float2{905.210, 890.040}, Float2{897.750, 917.720}, Float2{893.580, 928.170}, 
			Float2{906.550, 915.550}, Float2{915.210, 903.050}, Float2{917.150, 896.070}, Float2{917.930, 908.820}, Float2{921.800, 917.150}, Float2{919.580, 923.930}, Float2{921.240, 930.990}, Float2{905.020, 972.630}, Float2{897.560, 969.490}, Float2{882.820, 955.810}, Float2{874.170, 961.130}, Float2{874.780, 972.210}, Float2{887.720, 986.270}, Float2{887.820, 996.030}, Float2{893.450, 1002.850}, Float2{888.970, 1014.090}, Float2{875.930, 999.780}, Float2{867.040, 1005.060}, Float2{870.800, 1018.220}, Float2{879.520, 1029.070}, Float2{875.710, 1037.010}, Float2{877.350, 1044.250}, Float2{847.260, 1123.450}, Float2{829.180, 1177.090}, Float2{823.400, 1199.460}, Float2{818.810, 1211.010}, Float2{813.890, 1227.000}, Float2{808.740, 1257.060}, Float2{797.760, 1309.940}, Float2{791.800, 1343.960}, Float2{789.240, 1360.930}, Float2{785.650, 1376.050}, Float2{775.980, 1395.970}, Float2{769.169, 1412.270}, Float2{758.650, 1422.670}, Float2{739.520, 1441.670}, Float2{728.959, 1453.960}, Float2{711.929, 1473.080}, Float2{697.969, 1483.830}, Float2{679.109, 1493.270}, Float2{669.859, 1500.880}, Float2{661.269, 1508.320}, Float2{660.629, 1515.100}, Float2{656.269, 1528.100}, Float2{659.340, 1541.580}, Float2{664.439, 1546.590}, Float2{667.489, 1548.870}, Float2{689.060, 1550.070}, Float2{703.999, 1548.200}, Float2{713.959, 1546.290}, Float2{736.869, 1538.880}, Float2{745.969, 1532.950}, Float2{761.030, 1523.960}, Float2{775.150, 1515.300}, Float2{786.020, 1509.160}, Float2{804.419, 1500.450}, Float2{827.000, 1492.470}, Float2{840.550, 1482.520}, Float2{839.179, 1474.490}, Float2{838.219, 1473.260}, Float2{840.359, 1466.060}, Float2{838.629, 1448.810}, Float2{833.219, 1429.050}, Float2{831.479, 1408.880}, Float2{833.310, 1398.120}, Float2{841.760, 1378.730}, Float2{852.850, 1361.700}, Float2{860.790, 1352.790}, Float2{870.400, 1338.390}, Float2{874.350, 1322.330}, Float2{880.990, 1309.010}, Float2{895.770, 1284.590}, Float2{906.420, 1263.700}, Float2{913.620, 1237.590}, Float2{917.760, 1223.650}, Float2{922.000, 1212.030}, Float2{924.460, 1194.330}, Float2{940.750, 1165.650}, Float2{953.060, 1148.000}, Float2{962.850, 1132.000}, Float2{981.760, 1097.620}, Float2{991.210, 1085.260}, Float2{1000.690, 1073.570}, Float2{1017.330, 1050.300}, Float2{1031.420, 1035.510}, Float2{1037.910, 1024.070}, Float2{1044.820, 1008.770}, Float2{1059.020, 995.120}, Float2{1065.730, 988.690}, Float2{1078.580, 969.510}, Float2{1108.770, 934.790}, Float2{1126.870, 922.860}, Float2{1151.030, 926.220}, Float2{1160.110, 924.640}, Float2{1170.870, 942.140}, Float2{1183.560, 983.560}, Float2{1189.860, 1005.520}, Float2{1195.860, 1020.180}, Float2{1204.890, 1041.200}, Float2{1209.420, 1058.700}, Float2{1211.670, 1070.930}, Float2{1216.500, 1089.390}, Float2{1221.100, 1102.010}, Float2{1231.620, 1143.140}, Float2{1236.240, 1152.020}, Float2{1249.150, 1201.920}, Float2{1258.600, 1231.380}, Float2{1266.400, 1252.650}, Float2{1274.860, 1270.270}, Float2{1279.900, 1287.010}, Float2{1283.310, 1297.520}, Float2{1284.060, 1310.920}, Float2{1287.180, 1328.590}, Float2{1294.720, 1352.270}, Float2{1304.380, 1385.030}, Float2{1313.370, 1423.600}, Float2{1319.760, 1443.200}, Float2{1328.430, 1487.950}, Float2{1328.770, 1499.980}, Float2{1334.940, 1512.250}, Float2{1338.640, 1516.190}, Float2{1342.550, 1533.380}, Float2{1345.650, 1558.050}, Float2{1341.690, 1593.070}, Float2{1336.750, 1624.800}, Float2{1333.580, 1642.030}, Float2{1332.970, 1655.960}, Float2{1338.110, 1659.170}, Float2{1331.150, 1688.130}, Float2{1327.170, 1697.450}, Float2{1325.710, 1707.950}, Float2{1321.780, 1716.810}, Float2{1318.760, 1723.970}, Float2{1321.120, 1738.560}, Float2{1323.850, 1741.610}, Float2{1324.010, 1744.550}, Float2{1334.255, 1751.141}, Float2{1334.062, 1751.188}, Float2{1349.520, 1753.510}, Float2{1355.880, 1754.250}, Float2{1371.700, 1753.670}, Float2{1395.810, 1750.780}, Float2{1415.260, 1745.250}, Float2{1425.290, 1724.040}, Float2{1426.630, 1699.510}, Float2{1424.340, 1665.030}, Float2{1419.920, 1654.960}, Float2{1419.630, 1639.140}, Float2{1414.200, 1617.050}, Float2{1410.740, 1595.190}, Float2{1407.150, 1578.850}, Float2{1406.320, 1565.060}, Float2{1405.510, 1533.590}, Float2{1405.330, 1513.970}, Float2{1408.100, 1490.910}, Float2{1410.080, 1457.010}, Float2{1410.150, 1419.980}, Float2{1413.700, 1386.480}, Float2{1411.200, 1362.100}, Float2{1407.210, 1336.120}, Float2{1400.820, 1316.460}, Float2{1386.330, 1283.280}, Float2{1375.580, 1257.340}, Float2{1370.570, 1233.080}, Float2{1364.940, 1213.000}, Float2{1363.780, 1197.990}, Float2{1362.160, 1186.970}, Float2{1360.270, 1112.970}, Float2{1360.060, 1095.540}, Float2{1372.020, 1096.250}, Float2{1376.230, 1095.290}, Float2{1380.170, 1082.920}, Float2{1371.630, 1072.260}, Float2{1359.680, 1070.180}, Float2{1359.470, 1049.050}, Float2{1358.870, 1037.230}, Float2{1361.040, 1035.290}, Float2{1372.850, 1035.250}, Float2{1378.730, 1025.140}, Float2{1368.680, 1013.190}, Float2{1358.610, 1012.410}, Float2{1358.220, 1001.230}, Float2{1357.790, 983.050}, Float2{1358.390, 968.770}, Float2{1365.590, 958.110}, Float2{1357.630, 947.510}, Float2{1357.230, 916.010}, Float2{1357.260, 877.310}, Float2{1377.740, 864.630}, Float2{1379.330, 850.720}, Float2{1362.810, 843.460}, Float2{1361.620, 835.220}, Float2{1361.640, 828.270}, Float2{1361.350, 819.730}, Float2{1360.900, 810.910}, Float2{1360.150, 799.830}, Float2{1360.770, 795.100}, Float2{1359.740, 773.060}, Float2{1360.630, 767.320}, Float2{1373.420, 781.570}, Float2{1396.620, 809.470}, Float2{1417.810, 836.220}, Float2{1434.390, 854.520}, Float2{1449.070, 867.000}, Float2{1452.890, 864.070}, Float2{1451.772, 860.950}, Float2{1446.526, 851.453}, Float2{1426.469, 823.478}, Float2{1417.480, 811.460}, Float2{1383.690, 766.310}, Float2{1351.160, 723.670}, Float2{1317.590, 680.540}, Float2{1280.560, 632.110}, Float2{1281.240, 628.190}, Float2{1276.730, 628.090}, Float2{1274.390, 617.550}, Float2{1265.230, 612.780}, Float2{1268.160, 598.091}, Float2{1260.390, 589.390}, Float2{1261.960, 584.020}, Float2{1260.090, 575.050}, Float2{1259.170, 565.050}, Float2{1260.460, 549.010}, Float2{1261.750, 533.140}, Float2{1262.560, 519.180}, Float2{1264.340, 456.910}, Float2{1276.300, 417.260}, Float2{1289.510, 400.530}, Float2{1302.030, 398.000}, Float2{1326.030, 405.960}, Float2{1345.420, 410.560}, Float2{1363.060, 413.680}, Float2{1397.540, 422.430}, Float2{1428.350, 431.610}, Float2{1464.080, 430.990}, Float2{1489.810, 421.700}
		};
		for (auto& c:ninja) c = (c - Float2{1000, 1000}) * 0.05;
		std::reverse(ninja, &ninja[dimof(ninja)]);

		REQUIRE(ValidatePolygonLoop<float>(eagle) == true);
		REQUIRE(ValidatePolygonLoop<float>(dancingMan) == true);
		REQUIRE(ValidatePolygonLoop<float>(secretaryBird) == true);
		REQUIRE(ValidatePolygonLoop<float>(swallow) == true);
		REQUIRE(ValidatePolygonLoop<float>(greenTree) == true);
		REQUIRE(ValidatePolygonLoop<float>(fiddlers) == true);
		REQUIRE(ValidatePolygonLoop<float>(perseus) == true);
		REQUIRE(ValidatePolygonLoop<float>(eagleFlight) == true);
		REQUIRE(ValidatePolygonLoop<float>(ninja) == true);

		REQUIRE(ValidatePolygonLoop<float>(womanWithSpear) == false);
		REQUIRE(ValidatePolygonLoop<float>(archer) == false);
		REQUIRE(ValidatePolygonLoop<float>(figure0) == false);		// this one has intersecting edges

		class SwitchStraightSkeletonOverlay : public IInteractiveTestOverlay
		{
		public:
			virtual void Render(
				RenderCore::IThreadContext& threadContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				IInteractiveTestHelper& testHelper) override
			{
				{
					auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(
						threadContext, *testHelper.GetImmediateDrawingApparatus()->_immediateDrawables);
					_preview.Draw(*overlayContext);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			virtual bool OnInputEvent(
				const PlatformRig::InputContext& context,
				const PlatformRig::InputSnapshot& evnt,
				IInteractiveTestHelper& testHelper) override
			{
				if (evnt._pressedChar == 'q' || evnt._pressedChar == 'Q') {
					_maxInset += (evnt._pressedChar == 'Q') ? (20.f * 0.01f) : 0.01f;
					_preview = StraightSkeletonPreview<float>(_inputs[_currentInputIdx], _maxInset);
				} else if (evnt._pressedChar == 'a' || evnt._pressedChar == 'A') {
					_maxInset -= (evnt._pressedChar == 'A') ? (20.f * 0.01f) : 0.01f;
					_preview = StraightSkeletonPreview<float>(_inputs[_currentInputIdx], _maxInset);
				} else if (evnt._pressedChar == ' ') {
					_currentInputIdx = (_currentInputIdx+1)%_inputs.size();
					_preview = StraightSkeletonPreview<float>(_inputs[_currentInputIdx], _maxInset);
				}
				return false;
			}

			StraightSkeletonPreview<float> _preview;
			std::vector<std::vector<Float2>> _inputs;
			size_t _currentInputIdx = 0; 
			float _maxInset = 10.f;
			
			SwitchStraightSkeletonOverlay() {}
		};

		{
			auto tester = std::make_shared<SwitchStraightSkeletonOverlay>();			
			tester->_inputs.emplace_back(std::vector<Float2>(eagle, &eagle[dimof(eagle)]));
			tester->_inputs.emplace_back(std::vector<Float2>(dancingMan, &dancingMan[dimof(dancingMan)]));
			tester->_inputs.emplace_back(std::vector<Float2>(secretaryBird, &secretaryBird[dimof(secretaryBird)]));
			tester->_inputs.emplace_back(std::vector<Float2>(swallow, &swallow[dimof(swallow)]));
			tester->_inputs.emplace_back(std::vector<Float2>(greenTree, &greenTree[dimof(greenTree)]));
			tester->_inputs.emplace_back(std::vector<Float2>(fiddlers, &fiddlers[dimof(fiddlers)]));
			tester->_inputs.emplace_back(std::vector<Float2>(perseus, &perseus[dimof(perseus)]));
			tester->_inputs.emplace_back(std::vector<Float2>(eagleFlight, &eagleFlight[dimof(eagleFlight)]));
			tester->_inputs.emplace_back(std::vector<Float2>(ninja, &ninja[dimof(ninja)]));
			tester->_preview = StraightSkeletonPreview<float>(tester->_inputs[tester->_currentInputIdx], tester->_maxInset);
			testHelper->Run(StartingCamera(), tester);
		}
	}

}

#if 0
	T1(Primitive) std::pair<Vector2T<Primitive>, Vector2T<Primitive>> AdvanceEdge(
		std::pair<Vector2T<Primitive>, Vector2T<Primitive>> input, Primitive time )
	{
		auto t0 = Vector2T<Primitive>(input.second-input.first);
		auto movement = SetMagnitude(EdgeTangentToMovementDir(t0), time);
		return { input.first + movement, input.second + movement };
	}

	T1(Primitive) Primitive TestTriangle(Vector2T<Primitive> p[3])
	{
		using VelocityType = decltype(CalculateVertexVelocity(p[0], p[1], p[2]));
		VelocityType velocities[] = 
		{
			CalculateVertexVelocity(p[2], p[0], p[1]),
			CalculateVertexVelocity(p[0], p[1], p[2]),
			CalculateVertexVelocity(p[1], p[2], p[0])
		};

		Primitive collapses[] = 
		{
			CalculateCollapseTime<Primitive>({p[0], velocities[0]}, {p[1], velocities[1]}),
			CalculateCollapseTime<Primitive>({p[1], velocities[1]}, {p[2], velocities[2]}),
			CalculateCollapseTime<Primitive>({p[2], velocities[2]}, {p[0], velocities[0]})
		};

		// Find the earliest collapse, and calculate the accuracy
		unsigned edgeToCollapse = 0; 
		if (collapses[0] < collapses[1]) {
			if (collapses[0] < collapses[2]) {
				edgeToCollapse = 0;
			} else {
				edgeToCollapse = 2;
			}
		} else if (collapses[1] < collapses[2]) {
			edgeToCollapse = 1;
		} else {
			edgeToCollapse = 2;
		}

		auto triCollapse = CalculateTriangleCollapse(p[0], p[1], p[2]);
		auto collapseTest = collapses[edgeToCollapse];
		auto collapseTest1 = triCollapse[2];
		auto collapseTest2 = CalculateTriangleCollapse_Area(p[0], p[1], p[2], velocities[0], velocities[1], velocities[2]);
		auto collapseTest3 = CalculateTriangleCollapse_Offset(p[0], p[1], p[2]);
		auto collapseTest4 = CalculateEdgeCollapse_Offset(p[2], p[0], p[1], p[2]);
		(void)collapseTest, collapseTest1, collapseTest2, collapseTest3, collapseTest4;

		// Advance forward to time "collapses[edgeToCollapse]" and look
		// at the difference in position of pts edgeToCollapse & (edgeToCollapse+1)%3
		auto zero = edgeToCollapse, one = (edgeToCollapse+1)%3, m1 = (edgeToCollapse+2)%3;
		auto zeroA = PositionAtTime<Primitive>({p[zero], velocities[zero]}, collapses[edgeToCollapse]);
		auto oneA = PositionAtTime<Primitive>({p[one], velocities[one]}, collapses[edgeToCollapse]);

		// Accurately move forward edges m1 -> zero & one -> m1
		// The intersection point of these 2 edges should be the intersection point
		auto e0 = AdvanceEdge({p[m1], p[zero]}, collapses[edgeToCollapse]);
		auto e1 = AdvanceEdge({p[one], p[m1]}, collapses[edgeToCollapse]);
		auto intr = LineIntersection(e0, e1);

		auto intr2 = Truncate(triCollapse);

		auto d0 = zeroA - intr;
		auto d1 = oneA - intr;
		return std::max(MagnitudeSquared(d0), MagnitudeSquared(d1));
	}

	void TestSSAccuracy()
	{
		std::stringstream str;
		// generate random triangles, and 
		std::mt19937 rng;
		const unsigned tests = 10000;
		const Vector2T<double> mins{-10, -10};
		const Vector2T<double> maxs{ 10,  10};
		double iScale = (INT64_MAX >> 8) / (maxs[0] - mins[0]) / 2.0;
		for (unsigned c=0; c<tests; ++c) {
			Vector2T<double> d[3];
			Vector2T<float> f[3];
			Vector2T<int64_t> i[3];
			for (unsigned q=0; q<3; ++q) {
				d[q][0] = std::uniform_real_distribution<double>(mins[0], maxs[0])(rng);
				d[q][1] = std::uniform_real_distribution<double>(mins[1], maxs[1])(rng);
				f[q][0] = (float)d[q][0];
				f[q][1] = (float)d[q][1];
				i[q][0] = int64_t(d[q][0] * iScale);
				i[q][1] = int64_t(d[q][1] * iScale);
			}

			auto windingType = CalculateWindingType(d[0], d[1], d[2], GetEpsilon<double>());
			if (windingType == WindingType::Straight) continue;
			if (windingType == WindingType::Right) {
				std::swap(d[0], d[2]);
				std::swap(f[0], f[2]);
				std::swap(i[0], i[2]);
			}

			auto dq = TestTriangle(d);
			auto fq = TestTriangle(f);
			// auto iq = TestTriangle(i);

			// The difference between the double and float results can give us a sense of
			// the accuracy of the method
			auto triCollapseDouble = CalculateTriangleCollapse_Offset(d[0], d[1], d[2]);
			auto triCollapseFloat = CalculateTriangleCollapse_Offset(f[0], f[1], f[2]);
			auto jitter = triCollapseDouble[2] - double(triCollapseFloat[2]);

			str << "dq: " << dq
				<< ", fq: " << fq
				// << ", iq: " << double(iq) / iScale
				<< ", jitter:" << jitter
				<< std::endl
				;
		}

		auto result = str.str();
		printf("%s", result.c_str());
		(void)result;
	}
#endif

