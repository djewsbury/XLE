// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Detail/API.h"
#include "../UTFUtils.h"
#include "../StringUtils.h"
#include "../PtrUtils.h"
#include "../IteratorUtils.h"
#include "../../Core/Exceptions.h"
#include <assert.h>

namespace Utility
{
	class StreamLocation { public: unsigned _charIndex, _lineIndex; };

	template<typename CharType>
		class TextStreamMarker
	{
	public:
		CharType operator*() const                      { return *_ptr; }
		CharType operator[](size_t offset) const        { assert((_ptr+offset) < _end); return *(_ptr+offset); }
		ptrdiff_t Remaining() const                     { return (_end - _ptr); }
		const TextStreamMarker<CharType>& operator++()  { _ptr++; assert(_ptr<=_end); return *this; }
		const TextStreamMarker<CharType>& operator+=(size_t advancement)  { _ptr+=advancement; assert(_ptr<=_end); return *this; }
		const CharType* Pointer() const                 { return _ptr; }
		const CharType* End() const                     { return _end; }
		void SetPointer(const CharType* newPtr)         { assert(newPtr <= _end); _ptr = newPtr; }

		StreamLocation GetLocation() const;
		void AdvanceCheckNewLine();

		TextStreamMarker(StringSection<CharType> source);
		TextStreamMarker(IteratorRange<const void*> source);
		TextStreamMarker();
		~TextStreamMarker();
	protected:
		const CharType* _ptr;
		const CharType* _end;

		unsigned _lineIndex;
		const CharType* _lineStart;
	};

	class FormatException : public ::Exceptions::BasicLabel
	{
	public:
		FormatException(const char message[], StreamLocation location);
	};

	enum class FormatterBlob
	{
		KeyedItem,
		Value,
		BeginElement,
		EndElement,
		CharacterData,
		None 
	};

	template<typename CharType=char>
		class XL_UTILITY_API InputStreamFormatter
	{
	public:
		FormatterBlob PeekNext();

		bool TryBeginElement();
		bool TryEndElement();
		bool TryKeyedItem(StringSection<CharType>& name);
		bool TryValue(StringSection<CharType>& value);
		bool TryCharacterData(StringSection<CharType>&);

		StreamLocation GetLocation() const;

		// Create a "child" formatter that acts as if the current element in the stream is the
		// root. Otherwise the formatter will return the same sequence of blobs
		// This means that when the child formatter reaches the end of the current element, it
		// will return FormatterBlob::None instead of FormatterBlob::EndElement
		InputStreamFormatter<CharType> CreateChildFormatter();		

		using value_type = CharType;
		using InteriorSection = StringSection<CharType>;
		using Blob = FormatterBlob;

		InputStreamFormatter(const TextStreamMarker<CharType>& marker);
		~InputStreamFormatter();

		InputStreamFormatter();
		InputStreamFormatter(const InputStreamFormatter& cloneFrom);
		InputStreamFormatter& operator=(const InputStreamFormatter& cloneFrom);
	protected:
		TextStreamMarker<CharType> _marker;
		FormatterBlob _primed;
		signed _activeLineSpaces;
		signed _parentBaseLine;

		signed _baseLineStack[32];
		unsigned _baseLineStackPtr;

		bool _protectedStringMode;

		unsigned _format;
		unsigned _tabWidth;
		bool _pendingHeader;

		void ReadHeader();
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Formatter>
		void SkipElement(Formatter& formatter)
	{
		unsigned subtreeEle = 0;
		typename Formatter::InteriorSection dummy0;
		for (;;) {
			switch(formatter.PeekNext()) {
			case FormatterBlob::BeginElement:
				if (!formatter.TryBeginElement())
					Throw(FormatException(
						"Malformed begin element while skipping forward", formatter.GetLocation()));
				++subtreeEle;
				break;

			case FormatterBlob::EndElement:
				if (!subtreeEle) return;    // end now, while the EndElement is primed

				if (!formatter.TryEndElement())
					Throw(FormatException(
						"Malformed end element while skipping forward", formatter.GetLocation()));
				--subtreeEle;
				break;

			case FormatterBlob::KeyedItem:
				if (!formatter.TryKeyedItem(dummy0))
					Throw(FormatException(
						"Malformed keyed item while skipping forward", formatter.GetLocation()));
				break;

			case FormatterBlob::Value:
				if (!formatter.TryValue(dummy0))
					Throw(FormatException(
						"Malformed value while skipping forward", formatter.GetLocation()));
				break;

			case FormatterBlob::CharacterData:
				if (!formatter.TryCharacterData(dummy0))
					Throw(FormatException(
						"Malformed value while skipping forward", formatter.GetLocation()));
				break;

			default:
				Throw(FormatException(
					"Unexpected blob or end of stream hit while skipping forward", formatter.GetLocation()));
			}
		}
	}

	template<typename Formatter>
		void SkipValueOrElement(Formatter& formatter)
	{
		typename Formatter::InteriorSection dummy0;
		if (formatter.PeekNext() == FormatterBlob::Value) {
			if (!formatter.TryValue(dummy0))
				Throw(FormatException(
					"Malformed value while skipping forward", formatter.GetLocation()));
		} else {
			if (!formatter.TryBeginElement())
				Throw(FormatException(
					"Expected begin element while skipping forward", formatter.GetLocation()));
			SkipElement(formatter);
			if (!formatter.TryEndElement())
				Throw(FormatException(
					"Malformed end element while skipping forward", formatter.GetLocation()));
		}
	}

	template<typename Formatter>
		void RequireBeginElement(Formatter& formatter)
	{
		if (!formatter.TryBeginElement())
			Throw(Utility::FormatException("Expecting begin element", formatter.GetLocation()));
	}

	template<typename Formatter>
		void RequireEndElement(Formatter& formatter)
	{
		if (!formatter.TryEndElement())
			Throw(Utility::FormatException("Expecting end element", formatter.GetLocation()));
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireKeyedItem(Formatter& formatter)
	{
		typename Formatter::InteriorSection name;
		if (!formatter.TryKeyedItem(name))
			Throw(Utility::FormatException("Expecting keyed item", formatter.GetLocation()));
		return name;
	}

	template<typename Formatter>
		typename Formatter::InteriorSection RequireValue(Formatter& formatter)
	{
		typename Formatter::InteriorSection value;
		if (!formatter.TryValue(value))
			Throw(Utility::FormatException("Expecting value", formatter.GetLocation()));
		return value;
	}
}

using namespace Utility;
