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
		return (uint32_t(input.a) << 24) | (uint32_t(input.b) << 16) | (uint32_t(input.g) << 8) | uint32_t(input.r);
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

	std::string ColouriseFilename(StringSection<> filename)
	{
		auto split = MakeFileNameSplitter(filename);
		std::stringstream str;
		if (!split.DriveAndPath().IsEmpty()) {
			const bool gradualBrightnessChange = true;
			if (!gradualBrightnessChange) {
				str << "{color:9f9f9f}" << split.DriveAndPath();
			} else {
				auto splitPath = MakeSplitPath(split.DriveAndPath());
				if (splitPath.BeginsWithSeparator()) str << "/";
				for (unsigned c=0; c<splitPath.GetSectionCount(); ++c) {
					auto brightness = LinearInterpolate(0x5f, 0xcf, c/float(splitPath.GetSectionCount()));
					if (c != 0) str << "/";
					str << "{color:" << std::hex << brightness << brightness << brightness << std::dec << "}" << splitPath.GetSection(c);
				}
				if (splitPath.EndsWithSeparator()) str << "/";
			}
		}
		if (!split.File().IsEmpty())
			str << "{color:7f8fdf}" << split.File();
		if (!split.ExtensionWithPeriod().IsEmpty())
			str << "{color:df8f7f}" << split.ExtensionWithPeriod();
		if (!split.ParametersWithDivider().IsEmpty())
			str << "{color:7fdf8f}" << split.ParametersWithDivider();
		return str.str();
	}

}


