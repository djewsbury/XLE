// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"		// require for assets exceptions
#include "../Utility/StringUtils.h"
#include "../Core/Exceptions.h"
#include <assert.h>

namespace Utility { template<typename Iterator> class IteratorRange; }

namespace Formatters
{
	struct StreamLocation { unsigned _charIndex, _lineIndex; ::Assets::DependencyValidation _depVal; };

	template<typename CharType>
		class TextStreamMarker;

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
		class TextInputFormatter
	{
	public:
		FormatterBlob PeekNext();

		bool TryBeginElement();
		bool TryEndElement();
		bool TryKeyedItem(StringSection<CharType>& name);
		bool TryStringValue(StringSection<CharType>& value);
		bool TryCharacterData(StringSection<CharType>&);

		StreamLocation GetLocation() const;

		// Create a "child" formatter that acts as if the current element in the stream is the
		// root. Otherwise the formatter will return the same sequence of blobs
		// This means that when the child formatter reaches the end of the current element, it
		// will return FormatterBlob::None instead of FormatterBlob::EndElement
		TextInputFormatter<CharType> CreateChildFormatter();

		using value_type = CharType;
		using InteriorSection = StringSection<CharType>;
		using Blob = FormatterBlob;

		TextInputFormatter(const TextStreamMarker<CharType>& marker);
		TextInputFormatter(StringSection<CharType> source, ::Assets::DependencyValidation = {});
		TextInputFormatter(IteratorRange<const void*> source, ::Assets::DependencyValidation = {});
		~TextInputFormatter();

		TextInputFormatter();
		TextInputFormatter(const TextInputFormatter& cloneFrom);
		TextInputFormatter& operator=(const TextInputFormatter& cloneFrom);
	protected:
		TextStreamMarker<CharType> _marker;
		FormatterBlob _primed;
		signed _activeLineSpaces;
		signed _parentBaseLine;

		signed _baseLineStack[32];
		unsigned _baseLineStackPtr;
		unsigned _terminatingBaseLineStackPtr;

		bool _protectedStringMode;

		unsigned _format;
		unsigned _tabWidth;
		bool _pendingHeader;

		void ReadHeader();
	};

	class FormatException : public ::Assets::Exceptions::ExceptionWithDepVal
	{
	public:
		virtual const ::Assets::DependencyValidation& GetDependencyValidation() const override;
		virtual const char* what() const override;
		FormatException(StringSection<> msg, StreamLocation location);
	private:
		std::string _msg;
		::Assets::DependencyValidation _depVal;
	};

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

		TextStreamMarker(StringSection<CharType> source, ::Assets::DependencyValidation = {});
		TextStreamMarker(IteratorRange<const void*> source, ::Assets::DependencyValidation = {});
		TextStreamMarker();
		~TextStreamMarker();
	protected:
		const CharType* _ptr;
		const CharType* _end;

		unsigned _lineIndex;
		const CharType* _lineStart;

		::Assets::DependencyValidation _depVal;
	};


	template<typename CharType>
		inline TextInputFormatter<CharType>::TextInputFormatter(StringSection<CharType> source, ::Assets::DependencyValidation depVal)
		: TextInputFormatter(TextStreamMarker<CharType>{source, std::move(depVal)}) {}
	template<typename CharType>
		inline TextInputFormatter<CharType>::TextInputFormatter(IteratorRange<const void*> source, ::Assets::DependencyValidation depVal)
		: TextInputFormatter(TextStreamMarker<CharType>{source, std::move(depVal)}) {}
}

using namespace Utility;
