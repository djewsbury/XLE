// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Font.h"
#include "OverlayPrimitives.h"

namespace RenderOverlays
{
	class IOverlayContext;

	///////////////////////////////////////////////////////////////////////////////////
	//          D R A W   T E X T

	struct DrawText
	{
		mutable DrawTextFlags::BitField _flags = DrawTextFlags::Shadow;
		mutable Font* _font = nullptr;
		mutable ColorB _color = ColorB::White;
		mutable TextAlignment _alignment = TextAlignment::Left;

		Coord2 Draw(IOverlayContext& context, const Rect& rect, StringSection<>) const;
		Coord2 FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], ...) const;
		Coord2 FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], va_list args) const;
		
		Coord2 operator()(IOverlayContext& context, const Rect& rect, StringSection<> text) { return Draw(context, rect, text); }

		const DrawText& Alignment(TextAlignment alignment) const { _alignment = alignment; return *this; }
		const DrawText& Flags(DrawTextFlags::BitField flags) const { _flags = flags; return *this; }
		const DrawText& Color(ColorB color) const { _color = color; return *this; }
		const DrawText& Font(Font& font) const { _font = &font; return *this; }
	};

	class Font;
	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> path, int size);

}

