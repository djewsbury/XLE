// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayPrimitives.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"

namespace RenderOverlays
{

	const ColorB ColorB::White(0xff, 0xff, 0xff, 0xff);
	const ColorB ColorB::Black(0x0, 0x0, 0x0, 0xff);
	const ColorB ColorB::Red(0xff, 0x0, 0x0, 0xff);
	const ColorB ColorB::Green(0x0, 0xff, 0x0, 0xff);
	const ColorB ColorB::Blue(0x0, 0x0, 0xff, 0xff);
	const ColorB ColorB::Zero(0x0, 0x0, 0x0, 0x0);

	Float3 AsPixelCoords(Coord2 input)              { return Float3(float(input[0]), float(input[1]), RenderCore::Techniques::g_NDCDepthAtNearClip); }
	Float3 AsPixelCoords(Coord2 input, float depth) { return Float3(float(input[0]), float(input[1]), depth); }
	Float3 AsPixelCoords(Float2 input)              { return Expand(input, RenderCore::Techniques::g_NDCDepthAtNearClip); }
	Float3 AsPixelCoords(Float3 input)              { return input; }
	std::tuple<Float3, Float3> AsPixelCoords(const Rect& rect)
	{
		return std::make_tuple(AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight));
	}
	unsigned  HardwareColor(ColorB input)
	{
		// see duplicate in FontRendering.cpp
		return (uint32_t(input.a) << 24) | (uint32_t(input.b) << 16) | (uint32_t(input.g) << 8) | uint32_t(input.r);
	}

}


