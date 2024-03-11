// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "OverlayPrimitives.h"
#include "../Assets/AssetsCore.h"
#include "../Math/Vector.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Core/Prefix.h"
#include "../Core/Types.h"
#include <memory>
#include <utility>

namespace RenderOverlays
{
    class Font
    {
    public:
		struct FontProperties
		{
			float _descender = 0.f, _ascender = 0.f;
			float _ascenderExcludingAccent = 0.f;
			float _lineHeight = 0.f;
			float _maxAdvance = 0.f;
			float _fixedWidthAdvance = 0.f;		// will be zero for non-fixed-width fonts
		};

		struct Bitmap
		{
			unsigned _width = 0, _height = 0;
			IteratorRange<const void*> _data;

			float _xAdvance = 0.f;
			signed _bitmapOffsetX = 0, _bitmapOffsetY = 0;
			unsigned _lsbDelta = 0, _rsbDelta = 0;
		};

		struct GlyphProperties
		{
			float _xAdvance = 0.f;
			unsigned _lsbDelta = 0, _rsbDelta = 0;
		};

		virtual FontProperties		GetFontProperties() const = 0;
		virtual Bitmap				GetBitmap(ucs4 ch) const = 0;

		virtual Float2		GetKerning(int prevGlyph, ucs4 ch, int* curGlyph) const = 0;
		virtual Float2 		GetKerningReverse(int prevGlyph, ucs4 ch, int* curGlyph) const = 0;
		virtual float       GetKerning(ucs4 prev, ucs4 ch) const = 0;

		virtual GlyphProperties		GetGlyphProperties(ucs4 ch) const = 0;
		virtual void GetGlyphPropertiesSorted(
			IteratorRange<GlyphProperties*> result,
			IteratorRange<const ucs4*> glyphs) const = 0;

		uint64_t			GetHash() const { return _hashCode; }

		virtual ~Font();

	protected:
		uint64_t _hashCode;
    };

	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> path, int size);
	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> pathAndSize);		// use "<fontname>:<size>"
	std::shared_ptr<Font> MakeDummyFont();

	float CharWidth(const Font& font, ucs4 ch, ucs4 prev);

	template<typename CharType>
		float StringWidth(		const Font& font,
								StringSection<CharType> text,
								float spaceExtra     = 0.0f,
								bool outline         = false);

	template<typename CharType>
		int CharCountFromWidth( const Font& font,
								StringSection<CharType> text, 
								float width, 
								float spaceExtra     = 0.0f,
								bool outline         = false);

	template<typename CharType>
		float StringEllipsis(   CharType* outText, size_t outTextSize,
								const Font& font,
								StringSection<CharType> inText,
								float width,
								float spaceExtra     = 0.0f,
								bool outline         = false);

	template<typename CharType>
		float StringEllipsisDoubleEnded(
			CharType* outText, size_t outTextSize,
			const Font& font,
			StringSection<CharType> inText,
			StringSection<CharType> separatorList,
			float width,
			float spaceExtra     = 0.0f,
			bool outline         = false);

	template<typename CharType>
		struct StringSplitByWidthResult
	{
		std::vector<StringSection<CharType>> _sections;
		float _maxLineWidth = 0.f;

		std::basic_string<CharType> Concatenate() const;
	};

	/// <summary>Split a string on token boundaries to try to avoid exceeding any line exceeding a given width in pixels</summary>
	/// Used to word-wrap text. Returns internal pointers into the input string buffer representing separate lines.
	///
	/// If an individual token is longer than the given width, this function will not attempt to split it. Instead that token will
	/// end up on a line of it's own. To detect when this occurs, check "_maxLineWidth" in the result.
	///
	/// This can also be used to find the maximum width of a block of text with line breaks already included. To do this, set
	/// maxWidth to some very large value and check count of in "_sections" and "_maxLineWidth" in the result
	///
	/// Don't include '\n' or '\n' in either whitespaceDividers or nonWhitespaceDividers, since newline handling is built into the
	/// algorithm
	template<typename CharType>
		StringSplitByWidthResult<CharType> StringSplitByWidth(
			const Font& font,
			StringSection<CharType> text,
			float maxWidth,
			StringSection<CharType> whitespaceDividers,		// when a line is split by whitespace, the whitespace is removed entirely
			StringSection<CharType> nonWhitespaceDividers,
			float spaceExtra     = 0.0f,
			bool outline         = false);

	class Quad
	{
	public:
		Float2 min, max;

		Quad() : min(0.f, 0.f), max(0.f, 0.f) {}
		static Quad Empty();
		static Quad MinMax(float minX, float minY, float maxX, float maxY);
		static Quad MinMax(const Float2& min, const Float2& max);
		static Quad CenterExtent(const Float2& center, const Float2& extent);

		bool operator == (const Quad& v) const;
		bool operator != (const Quad& v) const;

		Float2 Center() const;
		Float2 Extent() const;
		float Length() const { return max[0] - min[0]; }
		float Height() const { return max[1] - min[1]; }
	};

    Float2		AlignText(const Font& font, const Quad& q, TextAlignment align, StringSection<ucs4> text);
	Float2		AlignText(const Font& font, const Quad& q, TextAlignment align, StringSection<> text);
}

