// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Detail/API.h"
#include "../Core/Prefix.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace Utility
{
	// Core character types can be "UTF" or "UCS" types
	// Note that "UTF" types have variable character sizes.
	// That is, in utf8, a single character might be 8, 16 or 32 bits long
	//
	// In UCS types, characters are fixed length. This is a little easier to work
	// with, but doesn't provide access to every possible unicode character.
	// However, we can't use wchar_t because the standard doesn't clearly define the encoding. The Visual Studio documentation
	// is even unclear about exactly what conversion are performed on L"" literals (maybe it depends on the encoding of the source file?)
	//
    using utf8 = char;          // (could become char8_t in C++20)
	using utf32 = char32_t;		// like char16_t, this is for UTF-32 type strings

	// We're going to do a hack and define ucs2 and UTF-16 as the same thing. This because most string
	// functions should work identically for both. Also, Windows functions tend to mush wchar_t, char16_t
	// USC2 and UTF-16 together confusingly.
	// For Chinese projects, this may be an issue (?), but maybe it won't effect other projects.
	using utf16 = char16_t;		// char16_t is intended by the standard's body to hold UTF-16 type strings	
    using ucs2 = uint16_t;
    using ucs4 = uint32_t;

    // convert UTF-8 data to ucs4
    // return value := >= 0 if valid or 
    XL_UTILITY_API int utf8_2_ucs4(const utf8* src, size_t sl, ucs4* dst, size_t dl);
    XL_UTILITY_API int ucs4_2_utf8(const ucs4* src, size_t sl, utf8* dst, size_t dl);
    XL_UTILITY_API int utf8_2_ucs2(const utf8* src, size_t sl, ucs2* dst, size_t dl);
    XL_UTILITY_API int ucs2_2_utf8(const ucs2* src, size_t sl, utf8* dst, size_t dl);
    XL_UTILITY_API int ucs4_2_ucs2(const ucs4* src, size_t sl, ucs2* dst, size_t dl);
    XL_UTILITY_API int ucs2_2_ucs4(const ucs2* src, size_t sl, ucs4* dst, size_t dl);

    // refer ConvertUTF.h in libtransmission
    // remark_todo("need some safe conversion") 

    // single character conversion
    XL_UTILITY_API int ucs2_2_utf8(ucs2 ch, utf8* dst);
    XL_UTILITY_API int ucs4_2_utf8(ucs4 ch, utf8* dst);

    // find byte offset of n-th character
    XL_UTILITY_API size_t utf8_nth_offset(const utf8* s, size_t N);
    // find ordinal at the given byte offset
    XL_UTILITY_API size_t utf8_offset_ord(const utf8* s, size_t offset);


    // return next character, updating an index variable
    // i := byte offset
    XL_UTILITY_API ucs4 utf8_nextchar(const utf8* s, size_t* i);
	XL_UTILITY_API ucs4 utf8_nextchar(utf8 const*& iterator, utf8 const* end);

    // count the number of characters in a UTF-8 string
    XL_UTILITY_API size_t utf8_strlen(const utf8* s);

    // move to next character
    XL_UTILITY_API void utf8_inc(const utf8* s, size_t* i);
    // move to previous character
    XL_UTILITY_API void utf8_dec(const utf8* s, size_t*i);

    // next utf-8 sequence length
    XL_UTILITY_API int utf8_seqlen(const utf8* s);

    XL_UTILITY_API int utf8_read_escape_sequence(char* src, uint32_t* dst);

    XL_UTILITY_API size_t utf8_escape_wchar(utf8* buf, size_t dim, uint32_t ch);

    // ASCII <--> UTF8 escaping
    XL_UTILITY_API size_t utf8_unescape(utf8* buf, size_t dim, char *src);
    XL_UTILITY_API size_t utf8_escape(utf8* buf, size_t dim, char *src, int escape_quotes);

    // strchr with ordinal
    XL_UTILITY_API const utf8* utf8_strchr(const utf8* s, ucs4 ch, int& ord);

    // memchr with ordinal
    // n := buffer length
    XL_UTILITY_API const utf8* utf8_memchr(const utf8* s, ucs4 ch, size_t n, int& ord);

    XL_UTILITY_API int utf8_is_locale_utf8(char *locale);

    XL_UTILITY_API size_t utf8_vprintf(const char* fmt, va_list ap);
    XL_UTILITY_API size_t utf8_printf(const char* fmt, ...);
}

using namespace Utility;

