// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayPrimitives.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Types.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringFormat.h"
#include <sstream>

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
		return (uint32_t(input.a) << 24u) | (uint32_t(input.b) << 16u) | (uint32_t(input.g) << 8u) | uint32_t(input.r);
	}

	static RenderCore::MiniInputElementDesc Vertex_PCT_inputElements2D[] =
	{
		{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
	};

	static RenderCore::MiniInputElementDesc Vertex_PCT_inputElements3D[] = 
	{
		{ RenderCore::Techniques::CommonSemantics::POSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
	};
	
	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PCT::s_inputElements2D = Vertex_PCT_inputElements2D;
	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PCT::s_inputElements3D = Vertex_PCT_inputElements3D;

	static RenderCore::MiniInputElementDesc Vertex_PC_inputElements2D[] =
	{
		{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM }
	};

	static RenderCore::MiniInputElementDesc Vertex_PC_inputElements3D[] = 
	{
		{ RenderCore::Techniques::CommonSemantics::POSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM }
	};

	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PC::s_inputElements2D = Vertex_PC_inputElements2D;
	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PC::s_inputElements3D = Vertex_PC_inputElements3D;

	static RenderCore::MiniInputElementDesc Vertex_PCCTT_inputElements2D[] =
	{
		{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::COLOR+1, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD+1, RenderCore::Format::R32G32_FLOAT }
	};

	static RenderCore::MiniInputElementDesc Vertex_PCCTT_inputElements3D[] = 
	{
		{ RenderCore::Techniques::CommonSemantics::POSITION, RenderCore::Format::R32G32B32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::COLOR+1, RenderCore::Format::R8G8B8A8_UNORM },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT },
		{ RenderCore::Techniques::CommonSemantics::TEXCOORD+1, RenderCore::Format::R32G32_FLOAT }
	};
	
	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PCCTT::s_inputElements2D = Vertex_PCCTT_inputElements2D;
	IteratorRange<const RenderCore::MiniInputElementDesc*> Vertex_PCCTT::s_inputElements3D = Vertex_PCCTT_inputElements3D;

	std::optional<TextAlignment> AsTextAlignment(StringSection<> str)
	{
		if (XlEqString(str, "TopLeft")) return TextAlignment::TopLeft;
		if (XlEqString(str, "Top")) return TextAlignment::Top;
		if (XlEqString(str, "TopRight")) return TextAlignment::TopRight;
		if (XlEqString(str, "Left")) return TextAlignment::Left;
		if (XlEqString(str, "Center")) return TextAlignment::Center;
		if (XlEqString(str, "Right")) return TextAlignment::Right;
		if (XlEqString(str, "BottomLeft")) return TextAlignment::BottomLeft;
		if (XlEqString(str, "Bottom")) return TextAlignment::Bottom;
		if (XlEqString(str, "BottomRight")) return TextAlignment::BottomRight;
		return {};

	}
}


