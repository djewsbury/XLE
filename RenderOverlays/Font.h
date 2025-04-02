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
#include <memory>
#include <utility>

#if !defined(XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS)
	#define XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS 0
#endif

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
			#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
				signed _lsbDelta = 0, _rsbDelta = 0;
			#endif
		};

		struct GlyphProperties
		{
			float _xAdvance = 0.f;
			unsigned _width = 0, _height = 0;
			signed _bitmapOffsetX = 0, _bitmapOffsetY = 0;
			#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
				signed _lsbDelta = 0, _rsbDelta = 0;
			#endif
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

	void RegisterFontLibraryFile(StringSection<> path);
	class FTFontResources;
	std::shared_ptr<FTFontResources> CreateFTFontResources();

	float CharWidth(const Font& font, ucs4 ch, ucs4 prev);

	template<typename CharType>
		float StringWidth(		const Font& font,
								StringSection<CharType> text,
								float spaceExtra     = 0.0f,
								bool outline         = false);

	template<typename CharType>
		std::pair<float, unsigned> StringWidthAndNewLineCount(const Font& font, StringSection<CharType> text, float spaceExtra=0.f, bool outline=false);

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

	struct FontSpan
	{
		constexpr static unsigned s_maxInstancesPerSpan = 128;
		using Glyph = ucs4;
		Glyph _glyphs[s_maxInstancesPerSpan];
		unsigned _glyphsInstanceCounts[s_maxInstancesPerSpan];
		unsigned _glyphCount = 0;
		struct Instance { Float2 _xy; ColorB _colorOverride; };
		Instance _instances[s_maxInstancesPerSpan];

		// The following aren't required for rendering, but may be required for
		// more processing operations (such as word wrapping, etc)
		struct InstanceExtra { uint16_t _wordIndex, _lineIndex; };
		InstanceExtra _instanceExtras[s_maxInstancesPerSpan];
		unsigned _totalInstanceCount = 0;

		std::pair<uint16_t, uint16_t> _originalOrdering[s_maxInstancesPerSpan];		// instance, glyph
	};

	struct CalculateFontSpansControlBlock
	{
		unsigned _currentLineIndex = 0;
		unsigned _nextWordIndex = 0;
		float _maxX = 0.f;
		Float2 _iterator { 0.f, 0.f };
		ColorB _colorOverride { 0x0 };
		unsigned _additionalLineSpacing = 0;
	};

	/// Calculate spans for the given text, until we can't fit any more into the rectangle provided
	/// Callers call follow this up with functions such as WordWrapping() to get efficient word wrapping
	/// on the calculated spans.
	///
	/// Spans are separated based on the maximum number of characters per span (ie, not by lines, etc)
	/// Glyph instances are reordered in the spans based on glyph index (ie, not position in the output)
	/// No font rendering is done while calculating the output (though we do load characters in order to
	/// get advance, etc)
	///
	/// Spans can be modified between frames. Advanced users may wish to change spans after they have been
	/// generated.
	///
	/// This is also intended to support advanced cases where custom text rendering is required. That while,
	/// while there are functions that render from spans in FontRendering.h, it's also possible to write
	/// a customized version while still sharing all of the logic related to character layout, etc
	const char* CalculateFontSpans(
		std::vector<FontSpan>& result,
		CalculateFontSpansControlBlock& ctrlBlock,
		const Font& font, DrawTextFlags::BitField flags,
		StringSection<> text,
		unsigned maxLines = ~0u);

	unsigned WordWrapping(
		IteratorRange<FontSpan*> spans,
		const Font& font, float maxX,
		unsigned additionLineSpacing = 0);

	void WordJustification(
		IteratorRange<FontSpan*> spans,
		const Font& font, float maxX);

	// "SingleLine" variations allow for selecting the maxX on a line by line basis (at the cost of some efficiency)
	bool WordWrapping_SingleLine(
		IteratorRange<FontSpan*> spans,
		const Font& font, unsigned line, float maxX,
		unsigned additionLineSpacing);

	void WordJustification_SingleLine(
		IteratorRange<FontSpan*> spans,
		const Font& font, unsigned line, float maxX);

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

