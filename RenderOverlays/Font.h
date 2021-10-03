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
		};

		virtual FontProperties		GetFontProperties() const = 0;
		virtual Bitmap				GetBitmap(ucs4 ch) const = 0;

		virtual Float2		GetKerning(int prevGlyph, ucs4 ch, int* curGlyph) const = 0;
		virtual float       GetKerning(ucs4 prev, ucs4 ch) const = 0;

		virtual GlyphProperties		GetGlyphProperties(ucs4 ch) const = 0;

		uint64_t			GetHash() const { return _hashCode; }

		virtual ~Font();

	protected:
		uint64_t _hashCode;
    };

	::Assets::PtrToFuturePtr<Font> MakeFont(StringSection<> path, int size);

	float CharWidth(const Font& font, ucs4 ch, ucs4 prev);

	template<typename CharType>
		float StringWidth(      const Font& font,
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
		float StringEllipsis(   const Font& font,
								StringSection<CharType> inText,
								CharType* outText, 
								size_t outTextSize,
								float width,
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

