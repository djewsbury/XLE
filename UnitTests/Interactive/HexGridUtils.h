// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderOverlays/OverlayContext.h"
#include "../../Math/Vector.h"
#include <random>

namespace UnitTests
{

	class HexCellField
	{
	public:
		std::vector<Int2> _enabledCells;

		struct BoundaryGroup
		{
			std::vector<Float2> _boundaryLineLoop;
		};
		std::vector<BoundaryGroup> _interiorGroups;
		BoundaryGroup _exteriorGroup;
	};

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

	HexCellField CreateRegularHexField(unsigned radius);
	HexCellField CreateRandomHexCellField(unsigned cellCount, std::mt19937_64& rng);
	HexCellField CreateHexField(std::vector<Int2>&& enabledCellList);
	std::vector<std::pair<HexCellField, Int2>> CreateFromMultipleCellIslands(IteratorRange<const Int2*> enabledCells);
	Float2 CellCenter(Int2);

	void DrawBoundary(
		RenderOverlays::IOverlayContext& overlayContext,
		const HexCellField& cellField, const HexCellField::BoundaryGroup& group,
		const Float3x3& localToWorld,
		RenderOverlays::ColorB color);

	void FillHexGrid(
		RenderOverlays::IOverlayContext& overlayContext,
		IteratorRange<const Int2*> enabledCells,
		const Float3x3& localToWorld,
		RenderOverlays::ColorB color);

}