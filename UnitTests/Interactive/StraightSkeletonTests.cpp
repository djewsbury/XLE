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
		std::vector<Int2> _boundaryCells;
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
		for (auto c:adjacent) result._boundaryCells.push_back(c);

		while (result._enabledCells.size() < cellCount) {
			assert(result._boundaryCells.size());
			auto idx = std::uniform_int_distribution<size_t>(0, result._boundaryCells.size()-1)(rng);
			auto i = result._boundaryCells.begin() + idx;
			assert(std::find(result._enabledCells.begin(), result._enabledCells.end(), *i) == result._enabledCells.end());
			GetAdjacentCells(adjacent, *i);
			result._enabledCells.push_back(*i);
			result._boundaryCells.erase(i);

			for (auto c:adjacent) {
				if (std::find(result._enabledCells.begin(), result._enabledCells.end(), c) != result._enabledCells.end()) continue;
				if (std::find(result._boundaryCells.begin(), result._boundaryCells.end(), c) != result._boundaryCells.end()) continue;
				result._boundaryCells.push_back(c);
			}
		}

		return result;
	}

	static void DrawBoundary(RenderOverlays::IOverlayContext& overlayContext, const HexCellField& cellField)
	{
		std::vector<Float3> boundaryLines;
		boundaryLines.reserve(cellField._boundaryCells.size() * 2 * 6);

		// Super primitive; but.. for each boundary cell, check which neighbours are enabled and draw
		// a line between them
		for (auto cell:cellField._boundaryCells) {
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

		overlayContext.DrawLines(RenderOverlays::ProjectionMode::P2D, boundaryLines.data(), boundaryLines.size(), RenderOverlays::ColorB{100, 190, 190});
	}

	TEST_CASE( "StraightSkeletonTests", "[math]" )
	{
		using namespace RenderCore;

		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques);

		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, 0.0f, -1.0f}), Normalize(Float3{0.0f, 1.0f, 0.0f}), Float3{0.0f, 0.0f, 200.0f});
		visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = -50.f;
		visCamera._right = 50.f;
		visCamera._top = 50.f;
		visCamera._bottom = -50.f;

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
					DrawBoundary(*overlayContext, _cellField);
				}

				auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, parserContext, LoadStore::Clear);
				testHelper.GetImmediateDrawingApparatus()->_immediateDrawables->ExecuteDraws(threadContext, parserContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
			}

			HexCellField _cellField;

			HexGridStraightSkeleton()
			{
				std::mt19937_64 rng(619047819);
				_cellField = CreateRandomHexCellField(256, rng);
			}
		};

		{
			auto tester = std::make_shared<HexGridStraightSkeleton>();
			testHelper->Run(visCamera, tester);
		}
	}
}
