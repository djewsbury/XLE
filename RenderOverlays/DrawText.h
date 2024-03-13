// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Font.h"
#include "OverlayPrimitives.h"
#include "Math/Matrix.h"        // for Float3x4
#include "Math/Vector.h"

namespace RenderCore { namespace Assets { class RenderStateSet; }}

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

    Float2 DrawTextHelper(
        IOverlayContext& context,
        const std::tuple<Float3, Float3>& quad,
        const Font& font, DrawTextFlags::BitField,
        ColorB col, TextAlignment alignment, StringSection<char> text);

    void DrawTextHelper(
        IOverlayContext& context,
        const Float3x4& localToWorld,
        const Font& font, DrawTextFlags::BitField,
        ColorB col, RenderCore::Assets::RenderStateSet stateSet,
        bool center, StringSection<char> text);

    using FontPtrAndFlags = std::pair<Font*, DrawTextFlags::BitField>;
    void  DrawTextWithTableHelper(
        IOverlayContext& context,
        const std::tuple<Float3, Float3>& quad,
        FontPtrAndFlags fontTable[256],
        TextAlignment alignment,
        StringSection<char> text,
        IteratorRange<const uint32_t*> colors = {},
        IteratorRange<const uint8_t*> fontSelectors = {},
        ColorB shadowColor = ColorB::Black);

}

