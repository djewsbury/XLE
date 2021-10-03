// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Font.h"
#include "../Utility/UTFUtils.h"
#include <assert.h>

namespace RenderOverlays
{

	Font::~Font() {}

	template<typename CharType>
		static ucs4 NextCharacter(StringSection<CharType>& text)
		{
			if (text.IsEmpty()) return 0;
			return (ucs4)*text._start++;
		}

	template<>
		ucs4 NextCharacter(StringSection<utf8>& text)
		{
			return utf8_nextchar(text._start, text._end);
		}

	template<typename CharType>
		float StringWidth(const Font& font, StringSection<CharType> text, float spaceExtra, bool outline)
	{
		int prevGlyph = 0;
		float x = 0.0f, prevMaxX = 0.0f;
		while (!text.IsEmpty()) {
			ucs4 ch = NextCharacter(text);
			if (ch == '\n' || ch == '\r') {
				if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
				prevMaxX = std::max(x, prevMaxX);
				prevGlyph = 0;
				x = 0;
				continue;
			}

			// note -- tags like {color:xxxx} not considered

			int curGlyph;
			x += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
			prevGlyph = curGlyph;
			x += font.GetGlyphProperties(ch)._xAdvance;

			if(outline) x += 2.0f;
			if(ch == ' ') x += spaceExtra;
		}

		return std::max(x, prevMaxX);
	}

	template<typename CharType>
		std::pair<float, unsigned> StringWidthAndNewLineCount(const Font& font, StringSection<CharType> text, float spaceExtra=0.f, bool outline=false)
	{
		int prevGlyph = 0;
		float x = 0.0f, prevMaxX = 0.0f;
		unsigned newLineCount = 0;
		while (!text.IsEmpty()) {
			ucs4 ch = NextCharacter(text);
			if (ch == '\n' || ch == '\r') {
				if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
				prevMaxX = std::max(x, prevMaxX);
				prevGlyph = 0;
				x = 0;
				++newLineCount;
				continue;
			}

			// note -- tags like {color:xxxx} not considered

			int curGlyph;
			x += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
			prevGlyph = curGlyph;
			x += font.GetGlyphProperties(ch)._xAdvance;

			if(outline) x += 2.0f;
			if(ch == ' ') x += spaceExtra;
		}

		return {std::max(x, prevMaxX), newLineCount};
	}

	template<typename CharType>
		unsigned NewLineCount(const Font& font, StringSection<CharType> text)
	{
		unsigned newLineCount = 0;
		while (!text.IsEmpty()) {
			ucs4 ch = NextCharacter(text);
			if (ch == '\n' || ch == '\r') {
				if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
				++newLineCount;
			}
		}
		return newLineCount;
	}

	template<typename CharType>
		int CharCountFromWidth(const Font& font, StringSection<CharType> text, float width, float spaceExtra, bool outline)
	{
		int prevGlyph = 0;
		int charCount = 0;

		float x = 0.0f;
		while (!text.IsEmpty()) {
			ucs4 ch = NextCharacter(text);
			if (ch == '\n') {
				prevGlyph = 0;
				x = 0;
				continue;
			}

			int curGlyph;
			x += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
			prevGlyph = curGlyph;
			x += font.GetGlyphProperties(ch)._xAdvance;

			if(outline) x += 2.0f;
			if(ch == ' ') x += spaceExtra;

			if (width < x) {
				return charCount;
			}

			++charCount;
		}

		return charCount;
	}

	#pragma warning(disable:4706)   // C4706: assignment within conditional expression

	template<typename CharType>
		static void CopyString(CharType* dst, int count, const CharType* src)
	{
		if (!count)
			return;

		if (!src) {
			*dst = 0;
			return;
		}

		while (--count && (*dst++ = *src++))
			;
		*dst = 0;
	}

	template<typename CharType>
		float StringEllipsis(const Font& font, StringSection<CharType> inText, CharType* outText, size_t outTextSize, float width, float spaceExtra, bool outline)
	{
		if (width <= 0.0f)
			return 0.0f;

		int prevGlyph = 0;
		float x = 0.0f;
		auto text = inText;
		while (!text.IsEmpty()) {
			auto i = text.begin();
			ucs4 ch = NextCharacter(text);
			if (ch == '\n') {
				prevGlyph = 0;
				x = 0.0f;
				continue;
			}

			int curGlyph;
			x += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
			prevGlyph = curGlyph;
			x += font.GetGlyphProperties(ch)._xAdvance;

			if(outline) x += 2.0f;
			if(ch == ' ') x += spaceExtra;

			if (x > width) {
				size_t count = size_t(i - inText.begin());
				if (count > outTextSize - 2) {
					return x;
				}

				CopyString(outText, (int)count, inText.begin());
				outText[count - 1] = '.';
				outText[count] = '.';
				outText[count + 1] = 0;

				return StringWidth(font, MakeStringSection(outText, &outText[count + 1]), spaceExtra, outline);
			}
		}

		return x;
	}

	float CharWidth(const Font& font, ucs4 ch, ucs4 prev)
	{
		float x = 0.0f;
		if (prev) {
			x += font.GetKerning(prev, ch);
		}

		x += font.GetGlyphProperties(ch)._xAdvance;

		return x;
	}

	static Float2 GetAlignPos(const Quad& q, const Float2& extent, TextAlignment align)
	{
		Float2 pos;
		pos[0] = q.min[0];
		pos[1] = q.min[1];
		switch (align) {
		case TextAlignment::TopLeft:
			pos[0] = q.min[0];
			pos[1] = q.min[1];
			break;
		case TextAlignment::Top:
			pos[0] = 0.5f * (q.min[0] + q.max[0] - extent[0]);
			pos[1] = q.min[1];
			break;
		case TextAlignment::TopRight:
			pos[0] = q.max[0] - extent[0];
			pos[1] = q.min[1];
			break;
		case TextAlignment::Left:
			pos[0] = q.min[0];
			pos[1] = 0.5f * (q.min[1] + q.max[1] - extent[1]);
			break;
		case TextAlignment::Center:
			pos[0] = 0.5f * (q.min[0] + q.max[0] - extent[0]);
			pos[1] = 0.5f * (q.min[1] + q.max[1] - extent[1]);
			break;
		case TextAlignment::Right:
			pos[0] = q.max[0] - extent[0];
			pos[1] = 0.5f * (q.min[1] + q.max[1] - extent[1]);
			break;
		case TextAlignment::BottomLeft:
			pos[0] = q.min[0];
			pos[1] = q.max[1] - extent[1];
			break;
		case TextAlignment::Bottom:
			pos[0] = 0.5f * (q.min[0] + q.max[0] - extent[0]);
			pos[1] = q.max[1] - extent[1];
			break;
		case TextAlignment::BottomRight:
			pos[0] = q.max[0] - extent[0];
			pos[1] = q.max[1] - extent[1];
			break;
		}
		return pos;
	}

	template<typename CharType>
		static Float2 AlignText(const Quad& q, const Font& font, StringSection<CharType> text, float indent, TextAlignment align)
	{
		auto fontProps = font.GetFontProperties();
		Float2 extent{0,0};

		// do we need the width, height, or both?
		if (align == TextAlignment::Top || align == TextAlignment::TopRight) {
			extent[0] = StringWidth(font, text);
		} else if (align == TextAlignment::Left || align == TextAlignment::BottomLeft) {
			extent[1] = NewLineCount(font, text) * fontProps._lineHeight + fontProps._ascenderExcludingAccent;
		} else if (align == TextAlignment::Center || align == TextAlignment::Right || align == TextAlignment::Bottom || align == TextAlignment::BottomRight) {
			auto measurements = StringWidthAndNewLineCount(font, text);
			extent[0] = measurements.first;
			extent[1] = measurements.second * fontProps._lineHeight + fontProps._ascenderExcludingAccent;
		}

		Float2 pos = GetAlignPos(q, extent, align);
		pos[0] += indent;

		// reposition "pos" to be on the base line for the first line
		pos[1] += fontProps._ascenderExcludingAccent;
		switch (align) {
		case TextAlignment::TopLeft:
		case TextAlignment::Top:
		case TextAlignment::TopRight:
			pos[1] += fontProps._ascender - fontProps._ascenderExcludingAccent;
			break;
		case TextAlignment::BottomLeft:
		case TextAlignment::Bottom:
		case TextAlignment::BottomRight:
			pos[1] -= fontProps._descender;
			break;
		default:
			break;
		}
		return pos;
	}

	Float2 AlignText(const Font& font, const Quad& q, TextAlignment align, StringSection<ucs4> text)
	{
		return AlignText(q, font, text, 0, align);
	}

	Float2 AlignText(const Font& font, const Quad& q, TextAlignment align, StringSection<> text)
	{
		return AlignText(q, font, text, 0, align);
	}

	template float StringWidth(const Font&, StringSection<utf8>, float, bool);
	template float StringWidth(const Font&, StringSection<char>, float, bool);
	template float StringWidth(const Font&, StringSection<ucs2>, float, bool);
	template float StringWidth(const Font&, StringSection<ucs4>, float, bool);

	template int CharCountFromWidth(const Font&, StringSection<utf8> text, float width, float spaceExtra, bool outline);
	template int CharCountFromWidth(const Font&, StringSection<char> text, float width, float spaceExtra, bool outline);
	template int CharCountFromWidth(const Font&, StringSection<ucs2> text, float width, float spaceExtra, bool outline);
	template int CharCountFromWidth(const Font&, StringSection<ucs4> text, float width, float spaceExtra, bool outline);

	template float StringEllipsis(const Font&, StringSection<utf8>, utf8*, size_t, float, float, bool);
	template float StringEllipsis(const Font&, StringSection<char>, char*, size_t, float, float, bool);
	template float StringEllipsis(const Font&, StringSection<ucs2>, ucs2*, size_t, float, float, bool);
	template float StringEllipsis(const Font&, StringSection<ucs4>, ucs4*, size_t, float, float, bool);

	// --------------------------------------------------------------------------
	// Quad
	// --------------------------------------------------------------------------

	inline float FloatBits(uint32 i) { return *(float*)&i; }
	static const float FP_INFINITY = FloatBits(0x7F800000);
	static const float FP_NEG_INFINITY = FloatBits(0xFF800000);

	Quad Quad::Empty()
	{
		Quad q;
		q.min[0] = FP_INFINITY;
		q.min[1] = FP_INFINITY;
		q.max[0] = FP_NEG_INFINITY;
		q.max[1] = FP_NEG_INFINITY;
		return q;
	}

	Quad Quad::MinMax(float minX, float minY, float maxX, float maxY)
	{
		Quad q;
		q.min[0] = minX;
		q.min[1] = minY;
		q.max[0] = maxX;
		q.max[1] = maxY;
		return q;
	}


	Quad Quad::MinMax(const Float2& min, const Float2& max)
	{
		Quad q;
		q.min = min;
		q.max = max;
		return q;
	}

	Quad Quad::CenterExtent(const Float2& center, const Float2& extent)
	{
		Quad q;
		q.min = center - extent;
		q.max = center + extent;
		return q;
	}

	bool Quad::operator == (const Quad& v) const
	{
		return min == v.min && max == v.max;
	}

	bool Quad::operator != (const Quad& v) const
	{
		return min != v.min || max != v.max;
	}

	Float2 Quad::Center() const
	{
		return Float2(0.5f * (min[0] + max[0]), 0.5f * (min[1] + max[1]));
	}

	Float2 Quad::Extent() const
	{
		return max - Center();
	}
}

