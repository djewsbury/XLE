// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Streams/StreamTypes.h"
#include "PtrUtils.h"
#include "Detail/API.h"
#include "MemoryUtils.h"
#include "StringUtils.h"
#include "IteratorUtils.h"
#include <string>
#include <streambuf>
#include <ostream>
#include <stdarg.h>

namespace Utility
{

    XL_UTILITY_API int xl_sprintf(char *buf, const char *fmt, ...);
    XL_UTILITY_API int xl_vsprintf(char *buf, const char *fmt, va_list args);
    XL_UTILITY_API int xl_vsnprintf(char* buf, int size, const char* format, va_list args);

    inline int xl_snprintf(char* buf, int count, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int n = xl_vsnprintf(buf, count, fmt, args);
        va_end(args);
        return n;
    }

    class OutputStream;
    XL_UTILITY_API int      PrintFormatV(OutputStream* stream, const char* fmt, va_list args);
    XL_UTILITY_API int      PrintFormat(OutputStream* stream, const char* fmt, ...);

    XL_UTILITY_API int      XlFormatStringV(char* buf, int count, const char* fmt, va_list args) never_throws;
    XL_UTILITY_API int      XlFormatString(char* buf, int count, const char* fmt, ...);

    XL_UTILITY_API std::basic_string<char>   XlDynFormatString(const char format[], ...);

    #if REDIRECT_CLIB_WITH_PREPROCESSOR

        #include <stdio.h>      // (required so we can redefined sprintf)

        #undef sprintf
        #undef vsprintf
        #define sprintf xl_sprintf
        #define vsprintf xl_vsprintf
        #undef vsprintf
        #define vsprintf xl_vsprintf
        #undef vsprintf_s
        #define vsprintf_s xl_vsnprintf

    #endif


    namespace Internal
    {
        template<int SizeInBytes, typename CharType=char>
            struct FixedMemoryBuffer : public std::basic_streambuf<CharType>
        {
            uint8_t _buffer[SizeInBytes];
            FixedMemoryBuffer(size_t charSize)
            {
                this->setp((CharType*)_buffer, (CharType*)PtrAdd(_buffer, sizeof(_buffer) - charSize));
                std::fill_n(_buffer, dimof(_buffer), '\0');
            }

            const void* begin() const { return this->pbase(); }
            const void* end() const { return this->pptr(); }
        };

		template<typename CharType> struct DemoteCharType { using Value = CharType; };
		template<> struct DemoteCharType<uint8_t> { using Value = char; };
		template<> struct DemoteCharType<uint16> { using Value = wchar_t; };
		template<> struct DemoteCharType<char16_t> { using Value = wchar_t; };
		template<> struct DemoteCharType<uint32> { using Value = char; }; 
		template<> struct DemoteCharType<char32_t> { using Value = wchar_t; };
    }

    /// <summary>Dynamic string formatting utility<summary>
    /// StringMeld provides a simple and handy method to build a string using operator<<.
    /// StringMeld never allocates. It just has a fixed size buffer. So this is a handy
    /// way to build a string in a way that is type-safe, convenient and avoids any allocations.
    ///
    /// <example>
    ///     <code>\code
    ///         window.SetTitle(StringMeld<128>() << "XLE sample [RenderCore: " << v.first << ", " << v.second << "]");
    ///     \endcode</code>
    /// </example>
    template<int Count, typename CharType=char> class StringMeld
    {
    public:
		using DemotedType = typename Internal::DemoteCharType<CharType>::Value;
        mutable std::basic_ostream<DemotedType> _stream;
        
        StringMeld() : _stream(&_buffer), _buffer(sizeof(CharType)) {}

        operator const CharType*() const    { return (const CharType*)_buffer._buffer; }
        const CharType* get() const         { return (const CharType*)_buffer._buffer; }
        StringSection<CharType> AsStringSection() const
        {
            return StringSection<CharType>((const CharType*)_buffer.begin(), (const CharType*)_buffer.end());
        }
		std::basic_string<CharType> AsString() const
        {
            return std::basic_string<CharType>((const CharType*)_buffer.begin(), (const CharType*)_buffer.end());
        }
        IteratorRange<CharType*> AsIteratorRange() { return MakeIteratorRange((CharType*)_buffer.begin(), (CharType*)_buffer.end()); }
        operator StringSection<CharType>() const { return AsStringSection(); }

        std::ostream& AsOStream() { return _stream; }

    protected:
        Internal::FixedMemoryBuffer<Count*sizeof(CharType), DemotedType> _buffer;

        StringMeld(const StringMeld&);
        StringMeld& operator=(const StringMeld&);
    };

    template<typename Type, int Count, typename CharType>
        const StringMeld<Count, CharType>& operator<<(const StringMeld<Count, CharType>& meld, const Type& type)
        {
            meld._stream << type;
            return meld;
        }

    template<int Count, typename CharType>
        const StringMeld<Count, CharType>& operator<<(const StringMeld<Count, CharType>& meld, const std::basic_string<CharType>& str)
        {
            // Awkward, but necessary hack!
            // Visual Studio standard libraries only provide implementations of std::basic_ostream (etc) for
            // built-in character types (eg, char, wchar_t). It seems that even char16_t and char32_t run into problems.
            // Furthermore, when using DLL linking of the CRT, we can't even provide our own implementation of the
            // missing parts (due to dll linking declarations within the template classes).
            // We can use custom std::basic_ostream for some things (like piping in a string), but more complex operations
            // tend to introduce problems.
            // However, we can seek to get around this by using only the built-in char types (called the "demoted" char types)
            // with the streams, and doing a rough cast of the inputs here.
            meld._stream << *reinterpret_cast<const std::basic_string<typename Internal::DemoteCharType<CharType>::Value>*>(&str);
            return meld;
        }

    template<int Count, typename CharType>
        const StringMeld<Count, CharType>& operator<<(const StringMeld<Count, CharType>& meld, StringSection<CharType> section)
        {
            using Demoted = typename Internal::DemoteCharType<CharType>::Value;
            meld._stream.write((const Demoted*)section.begin(), section.size());
            return meld;
        }

    namespace Internal
    {
		template<typename CharType = char>
			class StreamBufInPlace : public std::basic_streambuf<CharType>
        {
        public:
            StreamBufInPlace(uint8_t* start, uint8_t* end, size_t charSize) 
            { 
                this->setp((CharType*)start, (CharType*)PtrAdd(end, -ptrdiff_t(charSize)));
                XlSetMemory(start, 0, end-start);
            }
            ~StreamBufInPlace() {}

            StreamBufInPlace(StreamBufInPlace&& moveFrom) never_throws
            : std::streambuf(std::move(moveFrom))
            {}

            StreamBufInPlace& operator=(StreamBufInPlace&& moveFrom)
            {
                std::basic_streambuf<CharType>::operator=(moveFrom);
                return *this;
            }

            uint8_t* Begin() { return (uint8_t*)this->pbase(); }
            const uint8_t* Begin() const { return (const uint8_t*)this->pbase(); }
        };

        template<typename CharType = char>
			class MoveableOStream : public std::basic_ostream<CharType>
        {
        public:
            MoveableOStream() {}
            explicit MoveableOStream(std::basic_streambuf<CharType, typename std::basic_ostream<CharType>::traits_type>* sb) : std::basic_ostream<CharType>(sb) {}

              // note that the move behaviour of std::basic_ostream is wierd, because the rdbuf() value is not moved
            MoveableOStream(MoveableOStream && moveFrom) : std::basic_ostream<CharType>(moveFrom) {}
            MoveableOStream& operator=(MoveableOStream && moveFrom) { std::basic_ostream<CharType>::operator=(std::move(moveFrom)); return *this; }
        };

        /// <summary>Dynamic string formatting utility<summary>
        /// This version of StringMeld can work on an externally allocated buffer. It's useful
        /// when appending some formatted text onto an existing string (often it's an effective
        /// alternative to multiple string cats)
        ///
        /// <example>
        ///     <code>\code
        ///         char filename[MaxPath];
        ///         object.GetFilename(filename, dimof(filename));
        ///         StringMeldAppend(filename) << "-appendedtext";
        ///     \endcode</code>
        /// </example>
        template<typename CharType> class StringMeldInPlace
        {
        public:
			using DemotedType = typename Internal::DemoteCharType<CharType>::Value;
            mutable MoveableOStream<DemotedType> _stream;

            operator const CharType*() const    { return (const CharType*)_buffer.Begin(); }
            const CharType* get() const         { return (const CharType*)_buffer.Begin(); }
            StringSection<CharType> AsStringSection() const { return MakeStringSection(get()); }
            std::basic_string<CharType> AsString() const { return AsStringSection().AsString(); }
            IteratorRange<CharType*> AsIteratorRange() { return MakeIteratorRange(get(), XlStringEnd(get())); }
            operator StringSection<CharType>() const { return AsStringSection(); }

            std::ostream& AsOStream() { return _stream; }

            StringMeldInPlace(CharType* bufferStart, CharType* bufferEnd)
            : _stream(&_buffer)
            , _buffer((uint8_t*)bufferStart, (uint8_t*)bufferEnd, sizeof(CharType))
            {
                if ((bufferEnd - bufferStart) > 0)
                    ((CharType*)_buffer.Begin())[0] = (CharType)'\0';
            }

            ~StringMeldInPlace() {}

            StringMeldInPlace(StringMeldInPlace&& moveFrom) never_throws
            : _stream(&_buffer)
            , _buffer(std::move(moveFrom._buffer))
            {
                // note that the move behaviour of std::basic_ostream is wierd, because the rdbuf() value is not moved
                _stream = std::move(moveFrom._stream);
            }

            StringMeldInPlace& operator=(StringMeldInPlace&& moveFrom)
            {
                _stream = std::move(moveFrom._stream);  // note that the move behaviour of std::basic_ostream is wierd, because the rdbuf() value is not moved
                _buffer = std::move(moveFrom._buffer);
                return *this;
            }

        protected:
            StreamBufInPlace<DemotedType> _buffer;

            StringMeldInPlace(const StringMeldInPlace&) = delete;
            StringMeldInPlace& operator=(const StringMeldInPlace&) = delete;
        };

        template<typename Type, typename CharType> 
            const StringMeldInPlace<CharType>& 
                operator<<(const StringMeldInPlace<CharType>& meld, const Type& type)
            {
                meld._stream << type;
                return meld;
            }

		template<typename CharType>
			const StringMeldInPlace<CharType>& operator<<(const StringMeldInPlace<CharType>& meld, const std::basic_string<CharType>& str)
			{
				// See the StringMeld case for details about what is happening here
				meld._stream << *reinterpret_cast<const std::basic_string<typename DemoteCharType<CharType>::Value>*>(&str);
				return meld;
			}

		template<typename CharType>
			const StringMeldInPlace<CharType>& operator<<(const StringMeldInPlace<CharType>& meld, StringSection<CharType> section)
			{
				using Demoted = typename DemoteCharType<CharType>::Value;
				meld._stream.write((const Demoted*)section.begin(), section.size());
				return meld;
			}
	}
    
    template<typename CharType, int Count>
        Internal::StringMeldInPlace<CharType> StringMeldInPlace(CharType (&buffer)[Count])
    {
        return Internal::StringMeldInPlace<CharType>(buffer, &buffer[Count]);
    }

    template<typename CharType, int Count>
        Internal::StringMeldInPlace<CharType> StringMeldAppend(CharType (&buffer)[Count])
    {
        auto len = XlStringSize(buffer);
        return Internal::StringMeldInPlace<CharType>(&buffer[len], &buffer[Count]);
    }

    template<typename CharType>
        Internal::StringMeldInPlace<CharType> StringMeldInPlace(CharType *bufferStart, CharType* bufferEnd)
    {
        return Internal::StringMeldInPlace<CharType>(bufferStart, bufferEnd);
    }

    template<typename CharType>
        Internal::StringMeldInPlace<CharType> StringMeldAppend(CharType *bufferStart, CharType* bufferEnd)
    {
        auto len = XlStringSize(bufferStart);
        return Internal::StringMeldInPlace<CharType>(&bufferStart[len], bufferEnd);
    }

	template<typename CharType>
		std::basic_ostream<CharType>& operator<<(std::basic_ostream<CharType>& stream, StringSection<CharType> section)
	{
		using Demoted = typename Internal::DemoteCharType<CharType>::Value;
		stream.write((const Demoted*)section.begin(), section.size());
		return stream;
	}
}

using namespace Utility;
