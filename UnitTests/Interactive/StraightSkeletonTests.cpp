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
		// static constexpr unsigned randomCellCount = 32u;
		static constexpr unsigned randomCellCount = 256u;
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

		REQUIRE(ValidatePolygonLoop<float>(eagle) == true);
		REQUIRE(ValidatePolygonLoop<float>(dancingMan) == true);
		REQUIRE(ValidatePolygonLoop<float>(secretaryBird) == true);
		REQUIRE(ValidatePolygonLoop<float>(womanWithSpear) == false);
		REQUIRE(ValidatePolygonLoop<float>(archer) == false);
		REQUIRE(ValidatePolygonLoop<float>(figure0) == false);		// this one has intersecting edges

		{
			auto tester = std::make_shared<BasicDrawStraightSkeleton>();			
			tester->_previews.emplace_back(MakeIteratorRange(secretaryBird), .7);
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

