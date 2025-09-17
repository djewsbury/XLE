// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Font.h"
#include "FontRendering.h"		// for FontRenderingControlStatement
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
			assert(!text.IsEmpty());
			return utf8_nextchar(text._start, text._end);
		}

	template<typename CharType>
		float StringWidth(const Font& font, StringSection<CharType> text, float spaceExtra, bool outline)
	{
		int prevGlyph = 0;
		float x = 0.0f, prevMaxX = 0.0f;
		while (true) {
			if (!text.IsEmpty())
				text = FontRenderingControlStatement{}.TryParse(text);		// skip any control statements
			if (text.IsEmpty()) break;

			ucs4 ch = NextCharacter(text);
			if (ch == '\n' || ch == '\r') {
				if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
				prevMaxX = std::max(x, prevMaxX);
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
		}

		return std::max(x, prevMaxX);
	}

	template<typename CharType>
		std::pair<float, unsigned> StringWidthAndNewLineCount(const Font& font, StringSection<CharType> text, float spaceExtra, bool outline)
	{
		int prevGlyph = 0;
		float x = 0.0f, prevMaxX = 0.0f;
		unsigned newLineCount = 0;
		while (true) {
			if (!text.IsEmpty())
				text = FontRenderingControlStatement{}.TryParse(text);		// skip any control statements
			if (text.IsEmpty()) break;

			ucs4 ch = NextCharacter(text);
			if (ch == '\n' || ch == '\r') {
				if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
				prevMaxX = std::max(x, prevMaxX);
				prevGlyph = 0;
				x = 0;
				++newLineCount;
				continue;
			}

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
		while (true) {
			if (!text.IsEmpty())
				text = FontRenderingControlStatement{}.TryParse(text);		// skip any control statements
			if (text.IsEmpty()) break;

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
		while (true) {
			if (!text.IsEmpty())
				text = FontRenderingControlStatement{}.TryParse(text);		// skip any control statements
			if (text.IsEmpty()) break;

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
		static void CopyString(CharType* dst, size_t dstSize, StringSection<CharType> src)
	{
		if (!dstSize)
			return;

		auto finalCount = std::min(dstSize-1, src.size());
		std::copy(src.begin(), src.begin()+finalCount, dst);
		dst[finalCount] = '\0';
	}

	template<typename CharType>
		float StringEllipsis(CharType* outText, size_t outTextSize, const Font& font, StringSection<CharType> inText, float width, float spaceExtra, bool outline)
	{
		if (width <= 0.0f || outTextSize <= 1) {
			if (outTextSize != 0)
				outText[0] = 0;
			return 0.0f;
		}

		int prevGlyph = 0;
		float x = 0.0f;
		auto text = inText;
		while (true) {
			if (!text.IsEmpty())
				text = FontRenderingControlStatement{}.TryParse(text);		// skip any control statements
			if (text.IsEmpty()) break;
			auto i = text.begin();
			ucs4 ch = NextCharacter(text);
			assert(ch != '\n');		// new lines within this string not supported; separate lines before calling

			int curGlyph;
			x += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
			prevGlyph = curGlyph;
			x += font.GetGlyphProperties(ch)._xAdvance;

			if(outline) x += 2.0f;
			if(ch == ' ') x += spaceExtra;

			if (x > width) {
				size_t count = size_t(i - inText.begin());
				if (outTextSize > 5) {	// at least one character, plus ellipsis, plus null terminator
					if (count) --count;	// go back one character to ensure we have room for the ellipsis itself
					count = std::min(count, outTextSize-4);

					std::copy(inText.begin(), inText.begin()+count, outText);
					outText[count + 0] = '.';
					outText[count + 1] = '.';
					outText[count + 2] = '.';
					outText[count + 3] = 0;
				} else {
					CopyString(outText, outTextSize, {inText.begin(), inText.begin()+count});
				}

				return StringWidth(font, MakeStringSection(outText, &outText[count + 1]), spaceExtra, outline);
			}
		}

		if (inText.size() > size_t(outTextSize-1)) {
			size_t count = inText.size();
			if (outTextSize > 5) {	// at least one character, plus ellipsis, plus null terminator
				if (count) --count;
				count = std::min(count, outTextSize-4);

				std::copy(inText.begin(), inText.begin()+count, outText);
				outText[count + 0] = '.';
				outText[count + 1] = '.';
				outText[count + 2] = '.';
				outText[count + 3] = 0;
			} else {
				CopyString(outText, outTextSize, {inText.begin(), inText.begin()+count});
			}

			return StringWidth(font, MakeStringSectionNullTerm(outText), spaceExtra, outline);
		} else {
			CopyString(outText, outTextSize, inText);
			return x;
		}
	}

	template<typename CharType>
		static const CharType* SkipControlStatements_Reverse(StringSection<CharType> text)
	{
		assert(!text.IsEmpty());
		if (expect_evaluation(*(text.end()-1) != '}', true))
			return text.end();

		// since we're iterating backwards, we need to reverse back to find the '{'
		auto i = text.end()-1;
		while (i != text.begin() && *(i-1) != '{') --i;
		if (i == text.begin()) return text.end();

		--i;
		auto skipped = FontRenderingControlStatement{}.TryParse(MakeStringSection(i, text.end()));
		if (skipped.begin() != i)
			return i;	// it is a control statement, return skipped iterator
		return text.end();
	}

	template<typename CharType>
		static StringSection<CharType> FindLastControlStatement(StringSection<CharType> text)
	{
		auto originalEnd = text.end();
		for (;;) {
			auto i = text.end();
			while (i != text.begin() && *(i-1) != '{') --i;
			if (i == text.begin()) return {};

			--i;
			auto skipped = FontRenderingControlStatement{}.TryParse(MakeStringSection(i, originalEnd));
			if (skipped.begin() != i)
				return {i, skipped.begin()};

			// continue for prior '{'
			text._end = i;
		}
	}

	template<typename CharType>
		float StringEllipsisDoubleEnded(
			CharType* outText, size_t outTextSize,
			const Font& font,
			StringSection<CharType> inText,
			StringSection<CharType> separatorList,
			float width, float spaceExtra, bool outline)
	{
		if (width <= 0.0f || outTextSize <= 1) {
			if (outTextSize != 0)
				outText[0] = 0;
			return 0.0f;
		}

		int prevGlyphLeft = 0, prevGlyphRight = 0;
		float leftx = 0.0f, rightx = 0.f;

		// use 4 dots to estimate "..." + some kerning each side
		auto ellipsisWidth = StringWidth(font, MakeStringSectionLiteral("...."), spaceExtra, outline);

		auto text = inText;
		unsigned direction = 0;		// this will prioritize keeping the last token
		unsigned directionBlocked = 0;
		while (!text.IsEmpty() && directionBlocked != 3) {

			direction ^= 1;

			if (direction == 0) {

				if (directionBlocked & 1) continue;

				auto start = text.begin();
				auto i = start;
				float additionalX = 0;
				while (true) {
					if (i != text.end())
						i = FontRenderingControlStatement{}.TryParse(MakeStringSection(i, text.end()))._start;		// skip any control statements
					if (i == text.end()) break;

					StringSection<CharType> t { i, text.end() };
					ucs4 ch = NextCharacter(t);
					i = t.begin();
					assert(ch != '\n');		// new lines within this string not supported; separate lines before calling

					int curGlyph;
					additionalX += font.GetKerning(prevGlyphLeft, ch, &curGlyph)[0];
					prevGlyphLeft = curGlyph;
					additionalX += font.GetGlyphProperties(ch)._xAdvance;

					if(outline) additionalX += 2.0f;
					if(ch == ' ') additionalX += spaceExtra;

					if (std::find(separatorList.begin(), separatorList.end(), (CharType)ch) != separatorList.end())
						break;
				}

				float finalEllipsisBuffer = 0.f;
				size_t outBufferRequired = (i-inText.begin()) + (inText.end()-text.end());
				if (i != text._end) {
					finalEllipsisBuffer = ellipsisWidth;
					outBufferRequired += 3;
				}
				outBufferRequired++;		// null terminator
				if ((leftx+additionalX-rightx+finalEllipsisBuffer) <= width && outBufferRequired < outTextSize) {
					// it fits, accept this text
					text._start = i;
					leftx += additionalX;
				} else {
					// we can't fit it
					directionBlocked |= 1;
				}

			} else {

				if (directionBlocked & 2) continue;

				auto start = text.end();
				auto i = start;
				float additionalX = 0;
				while (true) {
					if (i != text.begin())
						i = SkipControlStatements_Reverse(MakeStringSection(text.begin(), i));
					if (i == text.begin()) break;

					ucs4 ch;
					if constexpr (std::is_same_v<CharType, utf8>) {
						size_t idx = int(i-text.begin());
						utf8_dec(text.begin(), &idx);
						i = text.begin()+idx;
						ch = utf8_nextchar(text.begin(), &idx);
					} else
						ch = (ucs4)*--i;
					assert(ch != '\n');		// new lines within this string not supported; separate lines before calling

					int curGlyph;
					additionalX += font.GetKerningReverse(prevGlyphRight, ch, &curGlyph)[0];
					prevGlyphRight = curGlyph;
					additionalX += font.GetGlyphProperties(ch)._xAdvance;

					if(outline) additionalX += 2.0f;
					if(ch == ' ') additionalX += spaceExtra;

					if (std::find(separatorList.begin(), separatorList.end(), (CharType)ch) != separatorList.end())
						break;
				}

				float finalEllipsisBuffer = 0.f;
				size_t outBufferRequired = (text.begin()-inText.begin()) + (inText.end()-i);
				if (i != text._start) {
					finalEllipsisBuffer = ellipsisWidth;
					outBufferRequired += 3;
				}
				outBufferRequired++;		// null terminator
				if ((leftx+additionalX-rightx+finalEllipsisBuffer) <= width && outBufferRequired < outTextSize) {
					// it fits, accept this text
					text._end = i;
					rightx -= additionalX;
				} else {
					// we can't fit it
					directionBlocked |= 2;
				}

			}
		}

		if (text.IsEmpty()) {
			CopyString(outText, outTextSize, inText);
			return leftx-rightx;
		}

		if (text.size() == inText.size()) {
			// nothing was accepted -- fallback to just getting as much of the left most token as possible
			return StringEllipsis(outText, outTextSize, font, inText, width, spaceExtra, outline);
		}

		// We didn't fit everything -- we need
		// {inText.begin(), text.begin()} ... {text.end(), inText.end}
		assert((size_t(text.begin() - inText.begin()) + 4 + size_t(inText.end() - text.end())) <= outTextSize);
		auto outTextIterator = outText;
		CopyString(outTextIterator, outTextSize, {inText.begin(), text.begin()});
		outTextIterator += text.begin() - inText.begin();
		*outTextIterator++ = '.';
		*outTextIterator++ = '.';
		*outTextIterator++ = '.';
		// if there is a control statement in the space that we cut out (and we can copy that control statement
		// into the buffer safely), let's grab that. This avoids most cases where cutting out parts changes the 
		// color of later parts of the string entirely
		// but note that we take only a single control statement, so if there are statements controlling different
		// properties, we'll end up ignoring all except one
		auto removedControlStatement = FindLastControlStatement(text);
		if (!removedControlStatement.IsEmpty() && (outTextIterator + removedControlStatement.size() + (inText.end() - text.end()) + 1) <= &outText[outTextSize]) {
			CopyString(outTextIterator, outText+outTextSize-outTextIterator, removedControlStatement);
			outTextIterator += removedControlStatement.size();
		}
		CopyString(outTextIterator, outText+outTextSize-outTextIterator, {text.end(), inText.end()});
		outTextIterator += inText.end() - text.end();
		*outTextIterator = 0;

		return StringWidth(font, MakeStringSectionNullTerm(outText), spaceExtra, outline);
	}

	template<typename CharType>
		StringSplitByWidthResult<CharType> StringSplitByWidth(
			const Font& font,
			StringSection<CharType> text,
			float maxWidth,
			StringSection<CharType> whitespaceDividers,
			StringSection<CharType> nonWhitespaceDividers,
			float spaceExtra,
			bool outline)
	{
		StringSplitByWidthResult<CharType> result;

		float currentLineWidth = 0.f;
		int currentLinePrevGlyph = 0;
		StringSection<CharType> currentLine { text.begin(), text.begin() };

		while (true) {
			// find the next token (but start by finding a pre-whitespace block if it exists)
			auto preWhitespaceBegin = text.begin();
			float preWhitespaceWidth = 0.f;
			int prevGlyph = 0;
			Utility::ucs4 ch = 0;
			for (;;) {
				auto t = text;
				if (!t.IsEmpty())
					t._start = FontRenderingControlStatement{}.TryParse(t)._start;		// skip any control statements
				if (t.IsEmpty()) { ch = 0; break; }
				ch = NextCharacter(t);
				bool isWhitespace = std::find(whitespaceDividers.begin(), whitespaceDividers.end(), ch) != whitespaceDividers.end();
				bool isExplicitNewLine = ch == '\r' || ch == '\n';
				if (!isWhitespace || isExplicitNewLine) break;		// not that when we break here, we don't absorb any final control statements

				int curGlyph;
				preWhitespaceWidth += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
				prevGlyph = curGlyph;
				preWhitespaceWidth += font.GetGlyphProperties(ch)._xAdvance;

				if(outline) preWhitespaceWidth += 2.0f;
				if(ch == ' ') preWhitespaceWidth += spaceExtra;
				text = t;
			}

			if (text.IsEmpty()) break;		// reject trailing whitespace

			// if we end on a newline, we need to handle that specifically
			if (!text.IsEmpty() && (ch == '\r' || ch == '\n')) {
				text._start = FontRenderingControlStatement{}.TryParse(text)._start;		// skip any control statements
				ch = NextCharacter(text);
				// newline types supported -- "\r", "\r\n", "\n"
				if (ch == '\r' && !text.IsEmpty()) {
					auto t = text;
					ch = NextCharacter(t);
					if (ch == '\n')
						text = t;
				}

				// break the current line (excluding any whitespace that we just scanned past in the preWhitespace block)
				result._maxLineWidth = std::max(result._maxLineWidth, currentLineWidth);
				result._sections.emplace_back(currentLine);
				currentLinePrevGlyph = 0;
				currentLine = { text.begin(), text.begin() };
				currentLineWidth = 0;
				continue;
			}

			// now the main token part
			auto tokenBegin = text.begin();
			float nextTokenWidth = 0.f;
			prevGlyph = 0;	// (restart kerning)
			for (;;) {
				if (!text.IsEmpty())
					text._start = FontRenderingControlStatement{}.TryParse(text)._start;		// skip any control statements
				if (text.IsEmpty()) break;
				auto t = text;
				ch = NextCharacter(t);

				bool isWhitespace = std::find(whitespaceDividers.begin(), whitespaceDividers.end(), ch) != whitespaceDividers.end();
				bool isNonWhitespaceDivider = std::find(nonWhitespaceDividers.begin(), nonWhitespaceDividers.end(), ch) != nonWhitespaceDividers.end();
				bool isExplicitNewLine = ch == '\r' || ch == '\n';

				if ((isWhitespace || isNonWhitespaceDivider) && text._start != tokenBegin) break;
				if (isExplicitNewLine) break;

				int curGlyph;
				nextTokenWidth += font.GetKerning(prevGlyph, ch, &curGlyph)[0];
				prevGlyph = curGlyph;
				nextTokenWidth += font.GetGlyphProperties(ch)._xAdvance;

				if(outline) nextTokenWidth += 2.0f;
				if(ch == ' ') nextTokenWidth += spaceExtra;
				text = t;

				if (isNonWhitespaceDivider) break;	// dividers are treated as single character tokens
			}

			// find the kerning required to append the new token onto the current line
			float appendKerning = 0, prewhitespaceKerning = 0;
			if (tokenBegin != text.end()) {
				StringSection t { tokenBegin, text.end() };
				auto ch = NextCharacter(t);
				int curGlyph;
				appendKerning = font.GetKerning(currentLinePrevGlyph, ch, &curGlyph)[0];

				if (preWhitespaceBegin != tokenBegin) {
					StringSection t { preWhitespaceBegin, tokenBegin };
					ch = NextCharacter(t);
					int curGlyph2;
					prewhitespaceKerning = font.GetKerning(curGlyph, ch, &curGlyph2)[0];
				}
			}

			if ((currentLineWidth + preWhitespaceWidth + nextTokenWidth + appendKerning + prewhitespaceKerning) <= maxWidth) {
				// append this to the current line, and continue on
				currentLineWidth += preWhitespaceWidth + nextTokenWidth + appendKerning + prewhitespaceKerning;
				currentLine._end = text.begin();
				currentLinePrevGlyph = prevGlyph;
				continue;
			} else {
				// line break...
				if (!currentLine.IsEmpty()) {
					result._maxLineWidth = std::max(result._maxLineWidth, currentLineWidth);
					result._sections.emplace_back(currentLine);
				}
				currentLinePrevGlyph = prevGlyph;
				currentLine = { tokenBegin, text.begin() };		// note that we don't include "prewhitespace" block here -- this can sometimes exclude control statements
				currentLineWidth = nextTokenWidth;
			}
		}

		if (!currentLine.IsEmpty()) {
			result._maxLineWidth = std::max(result._maxLineWidth, currentLineWidth);
			result._sections.emplace_back(currentLine);
		}

		return result;
	}

	template<typename CharType>
		std::basic_string<CharType> StringSplitByWidthResult<CharType>::Concatenate() const
	{
		size_t size = 0;
		bool first = true;
		for (auto s:_sections) {
			if (!first) ++size;
			first = false;
			size += s.size();
		}
		std::basic_string<CharType> result;
		result.reserve(size);
		first = true;
		for (auto s:_sections) {
			if (!first) result.push_back((CharType)'\r');
			first = false;
			result.insert(result.end(), s.begin(), s.end());
		}
		return result;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<bool CheckMaxXY, bool SnapCoords>
		const char* CalculateFontSpans_Internal(
			FontSpan& result, CalculateFontSpansControlBlock& ctrlBlock,
			const Font& font, DrawTextFlags::BitField flags,
			StringSection<> text, float maxY)
	{
		using namespace RenderCore;
		assert(result._glyphCount == 0);
		if (text.IsEmpty()) return text._start;

		////////
		struct Instance
		{
			ucs4 _chr;
			Float2 _xy;
			ColorB _color;
			unsigned _lineIdx = 0;
			const char* _textPtr;
			unsigned _glyphIdx = ~0u;
			unsigned _wordIdx = ~0u;
		};
		VLA_UNSAFE_FORCE(Instance, instances, std::min(FontSpan::s_maxInstancesPerSpan, (unsigned)text.size()));
		unsigned instanceCount = 0;
		////////

		float x = ctrlBlock._iterator[0], y = ctrlBlock._iterator[1];
		const float xScale = 1.f, yScale = 1.f;
		const float xAtLineStart = 0.f;
		auto scaledLineHeight = yScale * (font.GetFontProperties()._lineHeight + ctrlBlock._additionalLineSpacing);
		if (!CheckMaxXY || (y + scaledLineHeight) <= maxY) {
			int prevGlyph = 0;
			float yAtLineStart = y;

			if constexpr (SnapCoords) {
				x = xScale * (int)(0.5f + x / xScale);
				y = yScale * (int)(0.5f + y / yScale);
			}
			while (instanceCount < FontSpan::s_maxInstancesPerSpan) {
				if (!text.IsEmpty() && expect_evaluation(*text._start == '{', false)) {
					FontRenderingControlStatement ctrl;
					text = ctrl.TryParse(text);
					if (ctrl._type == FontRenderingControlStatement::Type::ColorOverride) {
						ctrlBlock._colorOverride = ctrl._newColorOverride;
						continue;
					}
				}
				if (text.IsEmpty()) break;

				auto ptr = text.begin();
				auto ch = NextCharacter(text);

				// \n, \r\n, \r all considered new lines
				if (ch == '\n' || ch == '\r') {
					if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
					x = xAtLineStart;
					prevGlyph = 0;
					y = yAtLineStart = yAtLineStart + scaledLineHeight;
					if constexpr (SnapCoords) {
						x = xScale * (int)(0.5f + x / xScale);
						y = yScale * (int)(0.5f + y / yScale);
					}
					++ctrlBlock._currentLineIndex;

					if (CheckMaxXY && (y + scaledLineHeight) > maxY) {
						text._end = text._start;		// end iteration
						break;
					}

					continue;
				}

				int curGlyph;
				Float2 v = font.GetKerning(prevGlyph, ch, &curGlyph);
				x += xScale * v[0];
				y += yScale * v[1];
				prevGlyph = curGlyph;

				instances[instanceCount++] = { ch, Float2{x, y}, ctrlBlock._colorOverride, ctrlBlock._currentLineIndex, ptr };

				if (flags & DrawTextFlags::Outline) x += 2 * xScale;
			}

		} else {
			text._end = text._start;		// end iteration
		}

		if (!instanceCount) {
			ctrlBlock._iterator = {x, y};
			return text._start;
		}

		VLA(Instance*, sortedInstances, instanceCount);
		for (unsigned c=0; c<instanceCount; ++c) sortedInstances[c] = &instances[c];
		std::sort(sortedInstances, &sortedInstances[instanceCount], [](auto* lhs, auto* rhs) { return lhs->_chr < rhs->_chr; });

		VLA(ucs4, chrsToLookup, instanceCount);
		unsigned chrsToLookupCount = 0;
		ucs4 lastChar = ~ucs4(0);
		for (auto* i=sortedInstances; i!=&sortedInstances[instanceCount]; ++i) {
			if ((*i)->_chr != lastChar)
				chrsToLookup[chrsToLookupCount++] = lastChar = (*i)->_chr;		// get unique chars
			(*i)->_glyphIdx = chrsToLookupCount-1;
		}

		assert(chrsToLookupCount);
		VLA_UNSAFE_FORCE(Font::GlyphProperties, glyphProps, chrsToLookupCount);
		font.GetGlyphPropertiesSorted(
			MakeIteratorRange(glyphProps, &glyphProps[chrsToLookupCount]),
			MakeIteratorRange(chrsToLookup, &chrsToLookup[chrsToLookupCount]));

		// divider chars hard coded for now
		// vscode allows &()+,./;?[]| to break (note curious inconsistency here -- things like !*- don't break)
		// const auto dividers = MakeStringSectionLiteral("\t ");
		constexpr auto dividers = MakeStringSectionLiteral(U"\t &()+,./;?[]|\xffffffff");
		bool dividersBreakBefore[] = {true, true, true, true, true, true, false, false, true, false, false, true, true, true, true};
		static_assert(dimof(dividersBreakBefore) == dividers.size());
		VLA(bool, glyphDividesBefore, chrsToLookupCount);
		VLA(bool, glyphDividesAfter, chrsToLookupCount);
		VLA(bool, glyphSuppressDivideAfter, chrsToLookupCount);
		{
			auto i = dividers.begin();
			assert(std::is_sorted(dividers.begin(), dividers.end()));
			for (unsigned c=0; c<chrsToLookupCount; ++c) {
				i = std::lower_bound(i, dividers.end(), chrsToLookup[c]);
				if (*i == chrsToLookup[c]) { // require sentinel
					glyphDividesBefore[c] = dividersBreakBefore[i-dividers.begin()];
					glyphDividesAfter[c] = true;
				} else {
					glyphDividesBefore[c] = glyphDividesAfter[c] = false;
				}
				// Prevent "divide before" behaviour just before a quote. This is to deal with situations like -- "Something," he said.
				glyphSuppressDivideAfter[c] = (chrsToLookup[c] == '\"') || (chrsToLookup[c] == '\'');
			}
		}

		unsigned instanceCountPostClip = instanceCount;

		// Update the x values for each instance, now that we've queried the glyph properties
		{
			float xIterator = 0, yIterator = 0;

			// unsigned prev_rsb_delta = 0;
			unsigned lineIdx = 0, additionalLines = 0;
			auto i = instances;
			while (i != &instances[instanceCountPostClip]) {
				auto starti = i;

				// We've already processed newlines and font rendering control statements. Just look for word wrap issues here
				// Some dividers allow us to wrap before, other only after. For example ',' only allows us to wrap after
				++i;
				while (i != &instances[instanceCountPostClip] && i->_lineIdx == starti->_lineIdx && !glyphDividesBefore[i->_glyphIdx] && (!glyphDividesAfter[(i-1)->_glyphIdx] || glyphSuppressDivideAfter[i->_glyphIdx])) ++i;

				// first, check if we need to reset the xIterator
				if (starti->_lineIdx != lineIdx) {
					if ((starti->_xy[1] + yIterator + scaledLineHeight) > maxY) {
						// abort early because we can't fit this in
						text._start = text._end = starti->_textPtr;
						instanceCountPostClip = starti - instances;
						break;
					}

					lineIdx = starti->_lineIdx;
					xIterator = 0;		// reset because we just had a line break
				}

				if ((i-starti) == 1 && !(glyphProps[starti->_glyphIdx]._width * glyphProps[starti->_glyphIdx]._height)) {
					// Simplified version for the common case of just hitting a whitespace character (don't increase word index for the spaces)
					auto& glyph = glyphProps[starti->_glyphIdx];
					xIterator += glyph._xAdvance * xScale;
					#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
						xIterator += float(glyph._lsbDelta - glyph._rsbDelta) / 64.f;
					#endif
					continue;
				}

				// whole word does fit, let's commit it
				for (auto inst=starti; inst!=i; ++inst) {
					auto& glyph = glyphProps[inst->_glyphIdx];

					/*
					The freetype library suggests 2 different ways to use the lsb & rsb delta values. This method is
					sounds like it is intended when for maintaining pixel alignment is needed
					if (prev_rsb_delta - glyph._lsbDelta > 32)
						x -= 1.0f;
					else if (prev_rsb_delta - glyph._lsbDelta < -31)
						x += 1.0f;
					prev_rsb_delta = glyph._rsbDelta;
					*/

					inst->_xy[0] += xIterator;
					inst->_xy[1] += yIterator;
					inst->_wordIdx = ctrlBlock._nextWordIndex;

					xIterator += glyph._xAdvance * xScale;
					#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
						xIterator += float(glyph._lsbDelta - glyph._rsbDelta) / 64.f;
					#endif
				}

				// attempt to prevent increasing the world index if we come across a word split between multiple spans
				if (i != &instances[instanceCountPostClip] || glyphDividesAfter[(i-1)->_glyphIdx])
					++ctrlBlock._nextWordIndex;
			}

			ctrlBlock._iterator = { x + xIterator, y + yIterator };
		}

		// Write out everything to the span. Note that we're going back to glyph ordering now
		assert(result._totalInstanceCount == 0);
		std::fill(result._originalOrdering, ArrayEnd(result._originalOrdering), std::make_pair(0xffffu, 0xffffu));
		for (auto* i=sortedInstances; i!=&sortedInstances[instanceCount];) {
			auto starti = i++;
			while (i!=&sortedInstances[instanceCount] && (*i)->_glyphIdx == (*starti)->_glyphIdx) ++i;

			auto& glyph = glyphProps[(*starti)->_glyphIdx];
			if (!(glyph._width * glyph._height)) continue;

			auto startOutInstanceCount = result._totalInstanceCount;
			for  (auto q=starti; q!=i; ++q) {
				auto idx = (*q) - instances;
				if (idx >= instanceCountPostClip) continue;		// skip glyphs that failed the word wrap thing
				result._instances[result._totalInstanceCount]._xy = (*q)->_xy;
				result._instances[result._totalInstanceCount]._colorOverride = (*q)->_color;
				result._instanceExtras[result._totalInstanceCount]._wordIndex = (*q)->_wordIdx;
				result._instanceExtras[result._totalInstanceCount]._lineIndex = (*q)->_lineIdx;

				ctrlBlock._maxX = std::max(ctrlBlock._maxX, (*q)->_xy[0] + (glyph._bitmapOffsetX+glyph._width)*xScale);		// (alternatively add advance?)
				result._originalOrdering[idx] = { result._totalInstanceCount, result._glyphCount };
				result._totalInstanceCount++;
			}

			if (result._totalInstanceCount == startOutInstanceCount) continue;
			result._glyphs[result._glyphCount] = (*starti)->_chr;
			result._glyphsInstanceCounts[result._glyphCount] = result._totalInstanceCount - startOutInstanceCount;
			++result._glyphCount;
		}
		(void)std::remove(result._originalOrdering, ArrayEnd(result._originalOrdering), std::make_pair(0xffffu, 0xffffu));
		
		return text._start;
	}

	namespace Internal
	{
		struct FontSpanOriginalOrderingIterator
		{
			struct Value
			{
				FontSpan* _span;
				unsigned _idx;

				uint16_t GlyphIndex() const { return _span->_originalOrdering[_idx].second; }
				ucs4 Glyph() const { return _span->_glyphs[_span->_originalOrdering[_idx].second]; }
				FontSpan::Instance& Instance()  { return _span->_instances[_span->_originalOrdering[_idx].first]; }
				FontSpan::InstanceExtra& InstanceExtra()  { return _span->_instanceExtras[_span->_originalOrdering[_idx].first]; }
			};

			Value& operator*() { return _curValue; }
			Value* operator->() { return &_curValue; }
			unsigned GetSpanIdx() const { return _curValue._span - _originalRange.begin(); }

			bool operator==(const FontSpanOriginalOrderingIterator& other) { return _curValue._span == other._curValue._span && _curValue._idx == other._curValue._idx; }
			bool operator!=(const FontSpanOriginalOrderingIterator& other) { return !operator==(other); }
			FontSpanOriginalOrderingIterator& operator++()
			{
				++_curValue._idx;
				while  (_curValue._idx == _curValue._span->_totalInstanceCount) {
					_curValue._idx = 0;
					++_curValue._span;
					if (_curValue._span == _originalRange.end() || _curValue._span->_totalInstanceCount) break;
				}
				return *this;
			}

			friend FontSpanOriginalOrderingIterator operator+(const FontSpanOriginalOrderingIterator& lhs, ptrdiff_t offset)
			{
				assert(offset >= 0);
				FontSpanOriginalOrderingIterator result = lhs;
				while  (offset) {
					auto maxAdd = std::min((unsigned)offset, result._curValue._span->_totalInstanceCount - result._curValue._idx);
					result._curValue._idx += maxAdd;
					offset -= maxAdd;
					if (result._curValue._idx == result._curValue._span->_totalInstanceCount) {
						result._curValue._idx = 0;
						++result._curValue._span;
						if (result._curValue._span == result._originalRange.end() || result._curValue._span->_totalInstanceCount) break;
					}
				}
				return result;
			}

			static FontSpanOriginalOrderingIterator Begin(IteratorRange<FontSpan*> spans)
			{
				FontSpanOriginalOrderingIterator result;
				result._curValue._span = spans.begin();
				result._curValue._idx = 0;
				result._originalRange = spans;
				return result;
			}

			static FontSpanOriginalOrderingIterator End(IteratorRange<FontSpan*> spans)
			{
				FontSpanOriginalOrderingIterator result;
				result._curValue._span = spans.end();
				result._curValue._idx = 0;
				result._originalRange = spans;
				return result;
			}

		private:
			Value _curValue;
			IteratorRange<FontSpan*> _originalRange;
		};
	};

	unsigned WordWrapping(
		IteratorRange<FontSpan*> spans,
		const Font& font, float maxX,
		unsigned additionLineSpacing)
	{
		if (spans.empty()) return 0;

		unsigned additionalLines = 0;
		Float2 additionalOffset = { 0, 0 };
		auto lineHeight = font.GetFontProperties()._lineHeight + additionLineSpacing;

		// only needed for advance for the final glyph of each word... but still need to query them
		VLA_UNSAFE_FORCE(Font::GlyphProperties, glyphProps, spans.size() * FontSpan::s_maxInstancesPerSpan);
		for (unsigned c=0; c<spans.size(); ++c)
			font.GetGlyphPropertiesSorted(
				MakeIteratorRange(glyphProps+c*FontSpan::s_maxInstancesPerSpan, glyphProps+(c+1)*FontSpan::s_maxInstancesPerSpan),
				MakeIteratorRange(spans[c]._glyphs, &spans[c]._glyphs[spans[c]._glyphCount]));

		auto instancesBegin = Internal::FontSpanOriginalOrderingIterator::Begin(spans);
		auto instanceEnd = Internal::FontSpanOriginalOrderingIterator::End(spans);

		auto i = instancesBegin;
		unsigned currentLineIdx = 0;
		while (i != instanceEnd) {
			auto starti = i;

			if (starti->InstanceExtra()._lineIndex != currentLineIdx) {
				currentLineIdx = starti->InstanceExtra()._lineIndex;
				additionalOffset[0] = 0;
			}

			for (;;) {
				auto next = i+1;
				if (next == instanceEnd || next->InstanceExtra()._wordIndex != starti->InstanceExtra()._wordIndex) break;
				i = next;
			}

			auto lastChr = i;
			++i;

			auto glyphIdx = lastChr->GlyphIndex();
			// finalGlyphWidth calculation must agree with our calculations for _maxX in CalculateFontSpans_Internal, otherwise will we get excessive wrapping
			// float finalGlyphWidth = glyphProps[glyphIdx+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan]._xAdvance;
			float finalGlyphWidth = glyphProps[glyphIdx+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan]._bitmapOffsetX+glyphProps[glyphIdx+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan]._width;
			if ((lastChr->Instance()._xy[0] + finalGlyphWidth + additionalOffset[0]) <= maxX) {
				// fits in
				for (auto q=starti; q!=i; ++q) {
					q->Instance()._xy += additionalOffset;
					q->InstanceExtra()._lineIndex += additionalLines;
				}
				continue;
			}

			// Have to move this word to the start of the next line
			// Note there shouldn't be any kerning issues because we're subtracting that also
			// (the last character would have been a whitespace or non-whitespace divider, anyway)
			++additionalLines;
			additionalOffset[0] = -starti->Instance()._xy[0];
			additionalOffset[1] += lineHeight;
			for (auto q=starti; q!=i; ++q) {
				q->Instance()._xy += additionalOffset;
				q->InstanceExtra()._lineIndex += additionalLines;
			}
		}

		return additionalLines;
	}

	bool WordWrapping_SingleLine(
		IteratorRange<FontSpan*> spans,
		const Font& font, unsigned line, float maxX,
		unsigned additionLineSpacing)
	{
		if (spans.empty()) return false;

		auto lineHeight = font.GetFontProperties()._lineHeight + additionLineSpacing;

		// only needed for advance for the final glyph of each word... but still need to query them
		VLA_UNSAFE_FORCE(Font::GlyphProperties, glyphProps, spans.size() * FontSpan::s_maxInstancesPerSpan);
		for (unsigned c=0; c<spans.size(); ++c)
			font.GetGlyphPropertiesSorted(
				MakeIteratorRange(glyphProps+c*FontSpan::s_maxInstancesPerSpan, glyphProps+(c+1)*FontSpan::s_maxInstancesPerSpan),
				MakeIteratorRange(spans[c]._glyphs, &spans[c]._glyphs[spans[c]._glyphCount]));

		auto instancesBegin = Internal::FontSpanOriginalOrderingIterator::Begin(spans);
		auto instanceEnd = Internal::FontSpanOriginalOrderingIterator::End(spans);

		auto i = instancesBegin;
		while (i != instanceEnd && i->InstanceExtra()._lineIndex < line) ++i;

		while (i != instanceEnd) {
			auto starti = i;

			if (starti->InstanceExtra()._lineIndex != line)
				return true;

			for (;;) {
				auto next = i+1;
				if (next == instanceEnd || next->InstanceExtra()._wordIndex != starti->InstanceExtra()._wordIndex) break;
				i = next;
			}

			auto lastChr = i;
			++i;

			auto glyphIdx = lastChr->GlyphIndex();
			float finalGlyphWidth = glyphProps[glyphIdx+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan]._bitmapOffsetX+glyphProps[glyphIdx+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan]._width;
			if ((lastChr->Instance()._xy[0] + finalGlyphWidth) <= maxX) continue;		// fits in

			// Have to move this word to the start of the next line
			// Note there shouldn't be any kerning issues because we're subtracting that also
			// (the last character would have been a whitespace or non-whitespace divider, anyway)
			Coord2 additionalOffset;
			additionalOffset[0] = -starti->Instance()._xy[0];
			additionalOffset[1] = lineHeight;
			for (auto q=starti; q!=instanceEnd; ++q) {
				q->Instance()._xy += additionalOffset;
				q->InstanceExtra()._lineIndex += 1;
			}
			return true;
		}

		return false;
	}

	const char* CalculateFontSpans(
		std::vector<FontSpan>& result,
		CalculateFontSpansControlBlock& ctrlBlock,
		const Font& font, DrawTextFlags::BitField flags,
		StringSection<> text,
		unsigned maxLines)
	{
		auto tempOffset = font.GetFontProperties()._ascenderExcludingAccent; 		// drop down to where the first line should start
		ctrlBlock._iterator[1] += tempOffset;
		float maxY = ctrlBlock._iterator[1] + font.GetFontProperties()._lineHeight * maxLines;
		while (!text.IsEmpty()) {
			result.emplace_back();
			auto adv = CalculateFontSpans_Internal<true, false>(result.back(), ctrlBlock, font, flags, text, maxY);
			if (!result.back()._glyphCount) {
				text._start = adv;		// still advance this, because we might have parsed a control word at the end of the string
				result.pop_back();
				break;
			}
			if (text._start == adv) break;
			text._start = adv;
		}
		ctrlBlock._iterator[1] -= tempOffset;
		return text._start;
	}

	void WordJustification(
		IteratorRange<FontSpan*> spans,
		const Font& font, float maxX)
	{
		// For each line, identify the words and space them out so that full width is used

		// only needed for advance for the final glyph of each word... but still need to query them
		VLA_UNSAFE_FORCE(Font::GlyphProperties, glyphProps, spans.size() * FontSpan::s_maxInstancesPerSpan);
		for (unsigned c=0; c<spans.size(); ++c)
			font.GetGlyphPropertiesSorted(
				MakeIteratorRange(glyphProps+c*FontSpan::s_maxInstancesPerSpan, glyphProps+(c+1)*FontSpan::s_maxInstancesPerSpan),
				MakeIteratorRange(spans[c]._glyphs, &spans[c]._glyphs[spans[c]._glyphCount]));

		auto instancesBegin = Internal::FontSpanOriginalOrderingIterator::Begin(spans);
		auto instanceEnd = Internal::FontSpanOriginalOrderingIterator::End(spans);
		const float maxJustify = maxX * 0.2f;

		auto i = instancesBegin;
		while (i != instanceEnd) {
			auto starti = i;
			for (;;) {
				auto next = i+1;
				if (next == instanceEnd || next->InstanceExtra()._lineIndex != starti->InstanceExtra()._lineIndex) break;
				i = next;
			}

			auto lastChr = i;
			++i;

			auto& g = glyphProps[lastChr->GlyphIndex()+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan];
			float additionalSpace = maxX - (lastChr->Instance()._xy[0] + (g._bitmapOffsetX + g._width));
			if (additionalSpace <= 0.f) continue;
			if (additionalSpace >= maxJustify) continue;
			
			auto initialWord = starti->InstanceExtra()._wordIndex;
			auto wordCount = lastChr->InstanceExtra()._wordIndex - initialWord + 1;
			if (wordCount <= 1) continue;

			float spacePerWord = additionalSpace / float(wordCount - 1);

			for (auto q=starti; q!=i; ++q)
				q->Instance()._xy[0] += std::floor(spacePerWord * (q->InstanceExtra()._wordIndex - initialWord));
		}
	}

	void WordJustification_SingleLine(
		IteratorRange<FontSpan*> spans,
		const Font& font, unsigned line, float maxX)
	{
		// For each line, identify the words and space them out so that full width is used

		// only needed for advance for the final glyph of each word... but still need to query them
		VLA_UNSAFE_FORCE(Font::GlyphProperties, glyphProps, spans.size() * FontSpan::s_maxInstancesPerSpan);
		for (unsigned c=0; c<spans.size(); ++c)
			font.GetGlyphPropertiesSorted(
				MakeIteratorRange(glyphProps+c*FontSpan::s_maxInstancesPerSpan, glyphProps+(c+1)*FontSpan::s_maxInstancesPerSpan),
				MakeIteratorRange(spans[c]._glyphs, &spans[c]._glyphs[spans[c]._glyphCount]));

		auto instancesBegin = Internal::FontSpanOriginalOrderingIterator::Begin(spans);
		auto instanceEnd = Internal::FontSpanOriginalOrderingIterator::End(spans);
		const float maxJustify = maxX * 0.2f;

		auto i = instancesBegin;
		while (i != instanceEnd && i->InstanceExtra()._lineIndex < line) ++i;

		if (i == instanceEnd || i->InstanceExtra()._lineIndex != line) return;

		auto starti = i;
		for (;;) {
			auto next = i+1;
			if (next == instanceEnd || next->InstanceExtra()._lineIndex != line) break;
			i = next;
		}

		auto lastChr = i;
		++i;

		auto& g = glyphProps[lastChr->GlyphIndex()+lastChr.GetSpanIdx()*FontSpan::s_maxInstancesPerSpan];
		float additionalSpace = maxX - (lastChr->Instance()._xy[0] + (g._bitmapOffsetX + g._width));
		if (additionalSpace <= 0.f) return;
		if (additionalSpace >= maxJustify) return;
		
		auto initialWord = starti->InstanceExtra()._wordIndex;
		auto wordCount = lastChr->InstanceExtra()._wordIndex - initialWord + 1;
		if (wordCount <= 1) return;

		float spacePerWord = additionalSpace / float(wordCount - 1);

		for (auto q=starti; q!=i; ++q)
			q->Instance()._xy[0] += std::floor(spacePerWord * (q->InstanceExtra()._wordIndex - initialWord));
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
		if (align != TextAlignment::BottomLeft && align != TextAlignment::Bottom && align != TextAlignment::BottomRight)
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

	template float StringEllipsis(utf8*, size_t, const Font&, StringSection<utf8>, float, float, bool);
	template float StringEllipsis(char*, size_t, const Font&, StringSection<char>, float, float, bool);
	template float StringEllipsis(ucs2*, size_t, const Font&, StringSection<ucs2>, float, float, bool);
	template float StringEllipsis(ucs4*, size_t, const Font&, StringSection<ucs4>, float, float, bool);

	template float StringEllipsisDoubleEnded(utf8*, size_t, const Font&, StringSection<utf8>, StringSection<utf8>, float, float, bool);
	template float StringEllipsisDoubleEnded(char*, size_t, const Font&, StringSection<char>, StringSection<char>, float, float, bool);
	template float StringEllipsisDoubleEnded(ucs2*, size_t, const Font&, StringSection<ucs2>, StringSection<ucs2>, float, float, bool);
	template float StringEllipsisDoubleEnded(ucs4*, size_t, const Font&, StringSection<ucs4>, StringSection<ucs4>, float, float, bool);

	template StringSplitByWidthResult<utf8> StringSplitByWidth(const Font&, StringSection<utf8>, float, StringSection<utf8>, StringSection<utf8>, float, bool);
	template StringSplitByWidthResult<char> StringSplitByWidth(const Font&, StringSection<char>, float, StringSection<char>, StringSection<char>, float, bool);
	template StringSplitByWidthResult<ucs2> StringSplitByWidth(const Font&, StringSection<ucs2>, float, StringSection<ucs2>, StringSection<ucs2>, float, bool);
	template StringSplitByWidthResult<ucs4> StringSplitByWidth(const Font&, StringSection<ucs4>, float, StringSection<ucs4>, StringSection<ucs4>, float, bool);

	template struct StringSplitByWidthResult<utf8>;
	template struct StringSplitByWidthResult<ucs2>;
	template struct StringSplitByWidthResult<ucs4>;

	// --------------------------------------------------------------------------
	// Quad
	// --------------------------------------------------------------------------

	inline float FloatBits(uint32_t i) { return *(float*)&i; }
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

	std::shared_ptr<Font> MakeDummyFont()
	{
		class DummyFont : public Font
		{
		public:
			FontProperties		GetFontProperties() const override { return {}; }
			Bitmap				GetBitmap(ucs4 ch) const override { return {}; }
			Float2		GetKerning(int prevGlyph, ucs4 ch, int* curGlyph) const override { return {0,0}; }
			Float2		GetKerningReverse(int prevGlyph, ucs4 ch, int* curGlyph) const override { return {0,0}; }
			float		GetKerning(ucs4 prev, ucs4 ch) const override { return 0; }
			GlyphProperties		GetGlyphProperties(ucs4 ch) const override { return {}; }
			void GetGlyphPropertiesSorted(
				IteratorRange<GlyphProperties*> result,
				IteratorRange<const ucs4*> glyphs) const override
			{
				for (auto& r:result) r = {};
			}
			DummyFont() { _hashCode = 0; }
		};
		static auto result = std::make_shared<DummyFont>();		// note -- relying on compiler to make this thread safe
		return result;
	}
}

