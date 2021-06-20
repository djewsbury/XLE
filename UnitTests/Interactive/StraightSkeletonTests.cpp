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

	template<typename Primitive>
		class StraightSkeletonPreview
	{
	public:
		StraightSkeleton<Primitive> _straightSkeleton;
		std::vector<Vector2T<Primitive>> _orderedBoundaryPts;

		static constexpr unsigned BoundaryVertexFlag = 1u<<31u;

		Float3 GetPt(unsigned ptIdx) const
		{
			if (ptIdx < _orderedBoundaryPts.size()) {
				return Float3 { _orderedBoundaryPts[ptIdx], 0 };
			} else {
				REQUIRE((ptIdx -_orderedBoundaryPts.size()) < _straightSkeleton._steinerVertices.size());
				return _straightSkeleton._steinerVertices[ptIdx-_orderedBoundaryPts.size()];
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

			std::vector<Float3> originalShapeLines;
			originalShapeLines.reserve(_orderedBoundaryPts.size()*2);

			for (size_t c=0; c<_orderedBoundaryPts.size(); ++c) {
				originalShapeLines.push_back(Float3(_orderedBoundaryPts[c], 0));
				originalShapeLines.push_back(Float3(_orderedBoundaryPts[(c+1)%_orderedBoundaryPts.size()], 0));
			}
			overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, originalShapeLines.data(), originalShapeLines.size(), originalShapeColor);

			const float vertexSize = 0.1f;
			for (unsigned c=0; c<_orderedBoundaryPts.size(); ++c)
				overlayContext.DrawQuad(RenderOverlays::ProjectionMode::P2D, Float3{_orderedBoundaryPts[c] - Float2{vertexSize, vertexSize}, 0.f}, Float3{_orderedBoundaryPts[c] + Float2{vertexSize, vertexSize}, 0.f}, RenderOverlays::ColorB(0x7f, c>>8, c&0xff));
			for (unsigned c=0; c<_straightSkeleton._steinerVertices.size(); ++c)
				overlayContext.DrawQuad(RenderOverlays::ProjectionMode::P2D, Float3{Truncate(_straightSkeleton._steinerVertices[c]) - Float2{vertexSize, vertexSize}, 0.f}, Float3{Truncate(_straightSkeleton._steinerVertices[c]) + Float2{vertexSize, vertexSize}, 0.f}, RenderOverlays::ColorB(0x7f, (c+_orderedBoundaryPts.size())>>8, (c+_orderedBoundaryPts.size())&0xff));
		}

		StraightSkeletonPreview(const HexCellField& cellField, float maxInset = std::numeric_limits<float>::max())
		{
			std::vector<Vector2T<Primitive>> boundaryLines;
			const auto& group = cellField._exteriorGroup;
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
			
			_orderedBoundaryPts.reserve(1+boundaryLines.size()/2);
			_orderedBoundaryPts.push_back(*(boundaryLines.end()-2));
			_orderedBoundaryPts.push_back(*(boundaryLines.end()-1));
			boundaryLines.erase(boundaryLines.end()-2, boundaryLines.end());
			while (boundaryLines.size() > 2) {
				auto i = boundaryLines.begin();
				for (;i!=boundaryLines.end(); i+=2)
					if (Equivalent(*i, *(_orderedBoundaryPts.end()-1), 1e-3f))
						break;
				assert(i != boundaryLines.end());
				_orderedBoundaryPts.push_back(*(i+1));
				boundaryLines.erase(i, i+2);
			}

			// last line should wrap around back to the first
			assert(Equivalent(*(boundaryLines.end()-1), _orderedBoundaryPts[0], 1e-3f));

			// reverse to get the ordering that the straight skeleton algorithm is expecting
			std::reverse(_orderedBoundaryPts.begin(), _orderedBoundaryPts.end());

			_straightSkeleton = CalculateStraightSkeleton<Primitive>(MakeIteratorRange(_orderedBoundaryPts), maxInset);
		}

		StraightSkeletonPreview(IteratorRange<const Float2*> inputPts, float maxInset = std::numeric_limits<float>::max())
		: _straightSkeleton(CalculateStraightSkeleton(inputPts, maxInset))
		{
			_orderedBoundaryPts = std::vector<Float2>(inputPts.begin(), inputPts.end());
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
			Float2 {  10.f, -15.f } + Float2 { 25, 25 },
			Float2 { -10.f, -15.f } + Float2 { 25, 25 },
			Float2 { -10.f,  15.f } + Float2 { 25, 25 }
		};
		std::reverse(rectangleCollapse, &rectangleCollapse[dimof(rectangleCollapse)]);

		Float2 singleMotorcycle[] = {
			Float2 {  10.f,  15.f } + Float2 { 25, -25 },
			Float2 {  10.f, -15.f } + Float2 { 25, -25 },
			Float2 { -10.f, -7.5f } + Float2 { 25, -25 },
			Float2 {   0.f,   0.f } + Float2 { 25, -25 },
			Float2 { -10.f,  7.5f } + Float2 { 25, -25 }
		};
		std::reverse(singleMotorcycle, &singleMotorcycle[dimof(singleMotorcycle)]);

		Float2 doubleMotorcycle[] = {
			Float2 {   0.f,  15.f } + Float2 { -25, -25 },
			Float2 {  10.f,  7.5f } + Float2 { -25, -25 },
			Float2 {  2.5f,   0.f } + Float2 { -25, -25 },
			Float2 {  10.f, -7.5f } + Float2 { -25, -25 },
			Float2 {   0.f, -15.f } + Float2 { -25, -25 },
			Float2 { -10.f, -7.5f } + Float2 { -25, -25 },
			Float2 { -2.5f,   0.f } + Float2 { -25, -25 },
			Float2 { -10.f,  7.5f } + Float2 { -25, -25 }
		};
		std::reverse(doubleMotorcycle, &doubleMotorcycle[dimof(doubleMotorcycle)]);

		Float2 colinearCollapse[] = {
			Float2 {   0.f,  15.f } + Float2 { -25,  25 },
			Float2 {  10.f,  2.5f } + Float2 { -25,  25 },
			Float2 {  10.f, -2.5f } + Float2 { -25,  25 },
			Float2 {   0.f, -15.f } + Float2 { -25,  25 },
			Float2 { -10.f, -2.5f } + Float2 { -25,  25 },
			Float2 { -10.f,  2.5f } + Float2 { -25,  25 }
		};
		std::reverse(colinearCollapse, &colinearCollapse[dimof(colinearCollapse)]);

		Float2 colinearEdges[] = {
			Float2 {  15.f,  10.f },
			Float2 {  15.f,   0.f },
			Float2 {  15.f, -10.f },
			Float2 {   0.f, -10.f },
			Float2 { -15.f, -10.f },
			Float2 { -15.f,   0.f },
			Float2 { -15.f,  10.f },
			Float2 {   0.f,  10.f },
		};
		std::reverse(colinearEdges, &colinearEdges[dimof(colinearEdges)]);

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

