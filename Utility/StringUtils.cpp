// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "StringUtils.h"
#include "MemoryUtils.h"
#include "PtrUtils.h"   // for AsPointer
#include <string.h>
#include <wchar.h>
#include <locale>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <limits>
#include <climits>

#if OS_OSX
    #include <xlocale.h>
#endif

#if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
    #include <strsafe.h>
    #include <mbstring.h>
#endif

namespace Utility
{

static const uint8_t __alphanum_table[] = {
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,
	0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,
	0x00,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
	0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
static const uint8_t __hex_table[] = {
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
};
static const uint8_t __lower_table[] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
	0x40,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x5B,0x5C,0x5D,0x5E,0x5F,
	0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,
	0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x7B,0x7C,0x7D,0x7E,0x7F,
	0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
	0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
	0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
	0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
	0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
	0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
	0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
	0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
};
static const uint8_t __upper_table[] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,
	0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,
	0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F,
	0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F,
	0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x5B,0x5C,0x5D,0x5E,0x5F,
	0x60,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,
	0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x7B,0x7C,0x7D,0x7E,0x7F,
	0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,
	0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,
	0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,
	0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF,
	0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,
	0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF,
	0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,
	0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF,
};


size_t XlStringSizeSafe(const char* str, const char* end)
{
    const char* p;
    for (p = str; p < end && *p; ++p)
        continue;

    return p - str;
}

size_t XlStringSize(const ucs4* str)
{
    auto* i = str;
	while (*i) ++i;
	return i - str;
}

size_t XlStringSizeSafe(const ucs4* str, const ucs4* end)
{
    const ucs4* p;
    for (p = str; p < end && *p; ++p)
        continue;

    return p - str;
}

inline bool IsUtfMultibyte(utf8 c)
{
    return ((c & 0xc0) == 0x80);
}

inline bool CheckUtfMultibytes(size_t count, const utf8* s)
{
    while (count && IsUtfMultibyte(*s)) {
        ++s;
        --count;
    }
    return (count == 0);
}

size_t XlGlyphCount(const utf8* s)
{
    return utf8_strlen(s);
}

size_t XlGlyphCount(const utf16* s)
{
    // note -- we're assuming the 16 bit characters are encoded in the
    // byte order of the machine here. Also expecting high surrogates to be
    // in earlier memory addresses than low surrogates
    size_t l = 0;
    while (utf16 c = (utf16)*s) {
        ++l;
        if (c >= 0xd800 && c <= 0xdbff) { 
            // this is a "high surrogate". We're expecting a "low surrogate" to follow
            assert(*(s+1) >= 0xDC00 && *(s+1) <= 0xDFFF);
            s += 2;
        } else {
            // we should not encounter a "low surrogate" at this point -- they should
            // only appear immediately after a high surrogate
            assert(!(*(s+1) >= 0xDC00 && *(s+1) <= 0xDFFF));
            ++s;
        }
    }

    return l;
}

size_t XlStringSize(const utf8* s)
{
    // This should return the size in bytes of the string (not the number of
    // characters)
    return std::strlen((const char*)s);
}

size_t XlStringSize(const utf16* str)
{
    // string size returns the number of fixed sized "utf16" elements in the string
	// (even if this is not the same as the number of characters)
    auto* i = str;
	while (*i) ++i;
	return i - str;
}

size_t XlStringSize(const ucs2* str)
{
	// string size returns the number of fixed sized "ucs2" elements in the string
	// (even if this is not the same as the number of characters)
	auto* i = str;
	while (*i) ++i;
	return i - str;
}

size_t XlGlyphCount(const ucs4* str)
{
    // TODO: enhance
    if (!str)
        return 0;

    const ucs4* e = str;
    while (*e++)
        ;
    return e - str - 1;
}

// count is buffer size in char
static void XlCopyString_primitive(char* dst, size_t count, const char* src)
{
#ifdef _MSC_VER
    StringCbCopyA(dst, count, src);
#elif OSX
    strncpy(dst, src, count);
    dst[count - 1] = 0;
#else
    if (!count)
        return;

    while (--count && (*dst++ = *src++))
        ;
    *dst = 0;
#endif
}

// count is buffer size in char
void XlCopyNString_primitive(char* dst, size_t count, const char* src, size_t length)
{
    if (!length) {
        if (count >= 1) dst[0] = '\0';
        return;
    }

    ++length;

    if (length > count) {
        length = count;
    }

    XlCopyString(dst, length, src);
}

#pragma warning(disable:4706)       // warning C4706: assignment within conditional expression
void XlCopyString_SafeUtf(utf8* dst, size_t size_, const utf8* src)
{
    if (!size_)
        return;
    
    ptrdiff_t size = size_;

    --size; // reserve null

    utf8 c;
    while ((c = *src)) {
        int seq = XlCountUtfSequence(c);
        if (seq == 0) {
            break;
        }
        if (!CheckUtfMultibytes(seq - 1, src + 1)) {
            break;
        }
        size -= seq;
        if (size < 0) {
            break;
        }
        XlCopyMemory(dst, src, seq);
        dst += seq;
        src += seq;
    }
    *dst = 0;
}

void XlCopyString_SafeUtfN(utf8* dst, size_t size_, const utf8* src, const uint32_t numSeq)
{
    if (!size_)
        return;
    
    ptrdiff_t size = size_;

    --size; // reserve null

	utf8 c;
    uint32_t n = 0;
    while ((c = *src)) {
        if (n >= numSeq) {
            break;
        }

        int seq = XlCountUtfSequence(c);
        if (seq == 0) {
            break;
        }
        if (!CheckUtfMultibytes(seq - 1, src + 1)) {
            break;
        }
        size -= seq;
        if (size < 0) {
            break;
        }
        XlCopyMemory(dst, src, seq);
        dst += seq;
        src += seq;

        ++n;
    }
    *dst = 0;
}

// TODO -- can we find better implementations of these?
//	UTF8 implementations should be just the same as basic char implementations, except
//	when a multi byte character is chopped off by the buffer size!
//
// see UTFCPP; potentially a good replacement for our UTF implementation
//	http://sourceforge.net/projects/utfcpp/

void     XlCopyString        (utf8* dst, size_t size, const utf8* src)
{
    XlCopyString_primitive(dst, size, src);
}

void     XlCopyNString        (utf8* dst, size_t count, const utf8*src, size_t length)
{
    XlCopyNString_primitive(dst, count, src, length);
}

static void XlCatString_primitive(char* dst, size_t size, const char* src);
void     XlCatString(utf8* dst, size_t size, const utf8* src)
{
	XlCatString_primitive(dst, size, src);
}

// todo -- a lot of these functions need revision to ensure they will work correctly for
//      all utf16 strings on all platforms
#if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC

    #pragma warning(disable: 4995)

    void XlCopyString(wchar_t* dst, size_t size, const wchar_t* src)
    {
        wcscpy_s(dst, size, src);
    }

    void XlCopyNString(wchar_t* dst, size_t count, const wchar_t*src, size_t length)
    {
        wcsncpy_s(dst, count, src, length);
    }

    void XlCopyString(ucs2* dst, size_t count, const ucs2* src)
    {
        wcscpy_s((wchar_t*)dst, count, (const wchar_t*)src);
    }

    void XlCopyNString(ucs2* dst, size_t count, const ucs2*src, size_t length)
    {
        wcsncpy_s((wchar_t*)dst, count, (const wchar_t*)src, length);
    }

    void XlCatString(ucs2* dst, size_t size, const ucs2* src)
    {
        wcscat_s((wchar_t*)dst, size, (const wchar_t*)src);
    }

    void XlCatNString(ucs2* dst, size_t size, const ucs2* src, size_t length)
    {
        wcsncat_s((wchar_t*)dst, size, (const wchar_t*)src, length);
    }

    size_t XlGlyphCount(const ucs2* str)
    {
        return std::wcslen((const wchar_t*)str);
    }

    size_t XlCompareString(const ucs2* x, const ucs2* y)
    {
        return std::wcscmp((const wchar_t*)x, (const wchar_t*)y);
    }

    size_t XlCompareStringI(const ucs2* x, const ucs2* y)
    {
        return _wcsicmp((const wchar_t*)x, (const wchar_t*)y);
    }
    
#else
    
    void XlCopyString(wchar_t* dst, size_t size, const wchar_t* src) { assert(0); }
    
    void XlCopyNString(wchar_t* dst, size_t count, const wchar_t*src, size_t length) { assert(0); }
    
    void XlCopyString(ucs2* dst, size_t count, const ucs2* src) { assert(0); }
    
    void XlCopyNString(ucs2* dst, size_t count, const ucs2*src, size_t length) { assert(0); }
    
    void XlCatString(ucs2* dst, size_t size, const ucs2* src) { assert(0); }
    
    void XlCatNString(ucs2* dst, size_t size, const ucs2* src, size_t length) { assert(0); }
    
    size_t XlGlyphCount(const ucs2* str)
    {
        assert(0);
        return 0;
    }
    
    size_t XlCompareString(const ucs2* x, const ucs2* y)
    {
        assert(0);
        return 0;
    }
    
    size_t XlCompareStringI(const ucs2* x, const ucs2* y)
    {
        assert(0);
        return 0;
    }
    
#endif

XL_UTILITY_API void     XlCopyString(utf16* dst, size_t size, const utf16* src)
{
    assert(0);
}
XL_UTILITY_API void     XlCopyNString(utf16* dst, size_t count, const utf16*src, size_t length)
{
    assert(0);
}

// count is buffer size in ucs4
void XlCopyString(ucs4* dst, size_t count, const ucs4* src)
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

void XlCopyNString(ucs4* dst, size_t count, const ucs4*src, size_t length)
{
    ++length;

    if (length > count) {
        length = count;
    }

    XlCopyString(dst, length, src);
}

static void XlCatString_primitive(char* dst, size_t size, const char* src) 
{
#ifdef _MSC_VER
    StringCbCatA(dst, size, src);
#else
    for (size_t i = 0; i < size - 1; ++i) {
        if (dst[i] == 0) {
            for (; i < size - 1; ++i) {
                dst[i] = *src;
                if (*src == 0) return;
                src++;
            }
            break;
        }
    }
    dst[size - 1] = 0;
#endif
}

void XlCatNString(utf8* dst, size_t size, const utf8* src, size_t length)
{
    for (size_t i = 0; i < size - 1; ++i) {
        if (dst[i] == 0) {
            size_t c=0;
            for (; (i < size - 1); ++i, ++c) {
                if (c >= length) { dst[i] = '\0'; return; }
                dst[i] = *src;
                if (*src == 0) return;
                src++;
            }
            break;
        }
    }
    dst[size - 1] = 0;
}

void XlCatSafeUtf(utf8* dst, size_t size, const utf8* src)
{
    for (size_t i = 0; i < size - 1; ++i) {
        if (dst[i] == 0) {
            XlCopyString_SafeUtf(dst + i, size - i, src);
            break;
        }
    }
}

void XlCatString(ucs4* dst, size_t count, const ucs4* src)
{
    for (size_t i = 0; i < count - 1; ++i) {
        if (dst[i] == 0) {
            for (; i < count - 1; ++i) {
                dst[i] = *src;
                if (*src == 0) return;
                src++;
            }
            break;
        }
    }
    dst[count - 1] = 0;
}


int XlComparePrefix(const utf8* s1, const utf8* s2, size_t size)
{
    return strncmp(s1, s2, size);
}

int XlComparePrefixI(const utf8* s1, const utf8* s2, size_t size)
{
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
        return _strnicmp(s1, s2, size);
    #else
        return strncasecmp(s1, s2, size);
    #endif
}

int XlComparePrefix(const ucs2* x, const ucs2* y, size_t len)
{
    if (!len)
        return 0;

    while (--len && *x && *x == *y) {
        ++x;
        ++y;
    }

    return (int)(*x - *y);
}

int XlComparePrefixI(const ucs2* x, const ucs2* y, size_t len)
{
    if (!len)
        return 0;

    while (--len && *x && XlToLower(*x) == XlToLower(*y)) {
        ++x;
        ++y;
    }

    return (int)(XlToLower(*x) - XlToLower(*y));
}

int XlComparePrefix(const ucs4* x, const ucs4* y, size_t len)
{
    if (!len)
        return 0;

    while (--len && *x && *x == *y) {
        ++x;
        ++y;
    }

    return (int)(*x - *y);
}

int XlComparePrefixI(const ucs4* x, const ucs4* y, size_t len)
{
    if (!len)
        return 0;

    while (--len && *x && XlToLower(*x) == XlToLower(*y)) {
        ++x;
        ++y;
    }

    return (int)(XlToLower(*x) - XlToLower(*y));
}

int XlCompareString(const utf8* x, const utf8* y)
{
    return strcmp(x, y);
}

int XlCompareStringI(const utf8* x, const utf8* y)
{
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
        return _stricmp(x, y);
    #else
        return strcasecmp(x, y);
    #endif
}

int XlCompareString(const ucs4* x, const ucs4* y)
{
    while (*x && *x == *y) {
        ++x;
        ++y;
    }

    return (int)(*x - *y);
}

int XlCompareStringI(const ucs4* x, const ucs4* y) 
{
    while (*x && XlToLower(*x) == XlToLower(*y)) {
        ++x;
        ++y;
    }

    return (int)(XlToLower(*x) - XlToLower(*y));
}

const char* XlFindChar(const char* s, const char ch)
{
    return strchr(s, ch);
}

char* XlFindChar(char* s, const char ch)
{
    return strchr(s, ch);
}

const char*  XlFindChar(StringSection<char> s, char ch)
{
	for (const auto& chr:s)
		if (chr == ch) return &chr;
	return nullptr;
}

const char*  XlFindAnyChar(const char s[], const char delims[])
{
    for (const char* i = s; *i; ++i) {
        for (const char *d = delims; *d; ++d) {
            if (*i == *d) return i;
        }
    }
    return nullptr;
}

char*  XlFindAnyChar(char s[], const char delims[])
{
    for (char* i = s; *i; ++i) {
        for (const char *d = delims; *d; ++d) {
            if (*i == *d) return i;
        }
    }
    return nullptr;
}

const char*  XlFindNot(const char s[], const char delims[])
{
    for (const char* i = s; *i; ++i) {
        const char *d = delims;
        for (; *d; ++d) {
            if (*i == *d) break;
        }
        if (*d == '\0') return i;
    }
    return nullptr;
}

char*  XlFindNot(char s[], const char delims[])
{
    for (char* i = s; *i; ++i) {
        const char *d = delims;
        for (; *d; ++d) {
            if (*i == *d) break;
        }
        if (*d == '\0') return i;
    }
    return nullptr;
}

const char* XlFindCharReverse(const char* s, char ch)
{
    return strrchr(s, ch);
}

const char* XlFindString(const char* s, const char* x)
{
    return strstr(s, x);
}

char* XlFindString(char* s, const char* x)
{
    return strstr(s, x);
}

const char* XlFindStringI(const char* s, const char* x)
{
    size_t sb = XlStringSize(s), xb = XlStringSize(x);
    if (sb < xb) return nullptr;
    for (size_t i = 0; i <= sb - xb; ++i)
        if (XlComparePrefixI(s + i, x, xb) == 0)
            return s + i;
    return nullptr;
}

const char*  XlFindString(StringSection<char> s, StringSection<char> x)
{
	size_t sb = s.size(), xb = x.size();
	if (sb < xb) return nullptr;
	for (size_t i = 0; i <= sb - xb; ++i)
		if (XlEqString(MakeStringSection(s.begin() + i, s.begin() + i + xb), x))
			return s.begin() + i;
	return nullptr;
}

const char*  XlFindStringI(StringSection<char> s, StringSection<char> x)
{
	size_t sb = s.size(), xb = x.size();
	if (sb < xb) return nullptr;
	for (size_t i = 0; i <= sb - xb; ++i)
		if (XlEqStringI(MakeStringSection(s.begin() + i, s.begin() + i + xb), x))
			return s.begin() + i;
	return nullptr;
}

const char* XlFindStringSafe(const char* s, const char* x, size_t size)
{
    size_t xb = XlStringSize(x);

    for (size_t i = 0; s[i] && i < size - xb; ++i) {
        if (XlComparePrefixI(s + i, x, xb) == 0) {
            return s + i;
        }
    }
    return 0;
}

const ucs4* XlFindString(const ucs4* s, const ucs4* x)
{
    if (!*x)
        return s;

    const ucs4 *cs, *cx;
    while (*s) {
        cs = s;
        cx = x;

        while (*cs && *cx && !(*cs - *cx)) {
            cs++;
            cx++;
        }

        if (!*cx)
            return s;

        ++s;
    }
    return 0;
}

const ucs4* XlFindStringI(const ucs4* s, const ucs4* x)
{
    if (!*x) 
        return s;

    size_t sn = XlStringSize(s);
    size_t xn = XlStringSize(x);
    if (sn < xn) return 0;

    for (size_t i = 0; i <= sn - xn; ++i) {
        if (XlComparePrefixI(s + i, x, xn) == 0) {
            return s + i;
        }
    }
    return 0;
}

const ucs4* XlFindStringSafe(const ucs4* s, const ucs4* x, size_t len)
{
    if (!*x) 
        return s;

    size_t xn = XlStringSize(x);

    for (size_t i = 0; s[i] && i < len - xn; ++i) {
        if (XlComparePrefixI(s + i, x, xn) == 0) {
            return s + i;
        }
    }
    return 0;
}

template <class T>
size_t tokenize_string(T* buf, size_t count, const T* delimiters, T** tokens, size_t numMaxToken)
{
    assert(numMaxToken > 1);

    size_t numDelimeters = XlStringSize(delimiters);
    size_t numToken = 0;

    tokens[numToken++] = buf;
    for (size_t i = 0; i < count; ++i) {
        if (buf[i] == '\0') {
            break;
        }
        for (size_t j = 0; j < numDelimeters; ++j) {
            if (buf[i] == delimiters[j]) {
                buf[i] = '\0';
                tokens[numToken++] = &buf[i + 1];
                if (numToken == numMaxToken) {
                    return numToken;
                }
            }
        }
    }

    return numToken;
}

size_t XlTokenizeString(char* buf, size_t count, const char* delimiters, char** tokens, size_t numMaxToken)
{
    return tokenize_string<char>(buf, count, delimiters, tokens, numMaxToken);
}

char* XlStrTok(char* token, const char* delimit, char** context)
{
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
        return strtok_s(token, delimit, context);
    #else
        return strtok_r(token, delimit, context);
    #endif
}

int XlExtractInt(const char* buf, int* arr, size_t length)
{
    assert(length > 1);

    size_t index = 0;
    const char* s = buf;

    while (*s && index < length) {
        bool hasDigit = false;
        int negative = 1;
        int result = 0;
        if (*s == '-') {
            negative = -1;
            ++s;
        }
        while (XlIsDigit(*s)) {
            result = (result * 10) + (*s - '0');
            ++s;
            hasDigit = true;
        }

        if (hasDigit) {
            arr[index++] = negative * result;
        } else {
            ++s;
        }
    }

    return int(index);
}

bool XlSafeAtoi(const char* str, int* n)
{
    errno = 0;
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
        *n = atoi(str);
        if (*n == MAX_INT32 || *n == MIN_INT32) {
            if (errno == ERANGE) {
                return false;
            }
        }
    #else
        char* end = const_cast<char*>(XlStringEnd(str));
        long result = strtol(str, &end, 10);   // GCC atoi doesn't handle ERANGE as expected... But! Sometimes strtol returns a 64 bit number, not a 32 bit number... trouble...?
        if (result == LONG_MAX || result == LONG_MIN) {
            if (errno == ERANGE) {
                return false;
            }
        }
        if (result > std::numeric_limits<int>::max() || result < std::numeric_limits<int>::min()) {
            return false;
        }
        if (!end || *end != '\0') {
            return false;
        }
        *n = int(result);
    #endif

    if ((*n) == 0 && errno != 0) {
        return false;
    }
    return true;
}

bool XlSafeAtoi64(const char* str, int64* n)
{
    errno = 0;
    #if CLIBRARIES_ACTIVE == CLIBRARIES_MSVC
        *n = _atoi64(str);
    #else
        char* end = const_cast<char*>(XlStringEnd(str));
        *n = strtoll(str, &end, 10);
    #endif
    if (*n == std::numeric_limits<int64>::max() || *n == std::numeric_limits<int64>::min()) {
        if (errno == ERANGE) {
            return false;
        }
    }
    if (*n == 0 && errno != 0) {
        return false;
    }
    return true;
}

const char* XlReplaceString(char* dst, size_t size, const char* src, const char* strOld, const char* strNew)
{
    assert(size > 0);

    size_t strOldLen = XlStringSize(strOld);
    size_t strNewLen = XlStringSize(strNew);

    size_t count = 0;
    size_t remain = 0;

    const char* p = src;
    while (const char* find = XlFindString(p, strOld)) {
        ptrdiff_t copySize = find - p;

        if (copySize > 0) {
            remain = size - count;

            XlCopyNString(&dst[count], remain, p, copySize);

            if (copySize >= ptrdiff_t(remain)) {
                return dst;
            }

            count += copySize;
        }

        p = find + strOldLen;

        remain = size - count;

        XlCopyNString(&dst[count], remain, strNew, strNewLen);

        if (strNewLen >= remain) {
            return dst;
        }

        count += strNewLen;
    }

    if (count) {
        XlCopyNString(&dst[count], size - count, p, XlStringSize(p));
    } else {
        XlCopyString(dst, size, src);
    }

    return dst;
}

// count is buffer size of dst, not the string size
XL_UTILITY_API void XlCompactString(char* dst, size_t count, const char* src)
{
    if (!count)
        return;

    if (!src) {
        *dst = 0;
        return;
    }

    char ch;
    while ( --count && (ch = *src++)) {
        if (!XlIsSpace(ch)) {
            *dst++ = ch;
        }
    }
    *dst = 0;
}

void XlCompactString(ucs4* dst, size_t count, const ucs4* src)
{
    if (!count)
        return;

    if (!src) {
        *dst = 0;
        return;
    }

    ucs4 ch;
    while ( --count && (ch = *src++)) {
        if (!XlIsSpace(ch)) {
            *dst++ = ch;
        }
    }
    *dst = 0;
}

bool XlIsValidAscii(const char* str)
{
    while (uint8_t c = (uint8_t)*str++) {
        if (c >= 0x80)
            return false;
    }
    return true;
}

bool XlIsValidUtf8(const utf8* str, size_t count)
{
    const auto* s = str;

    if (XlHasUtf8Bom(s))
        s += 3;

    uint32_t c;
    while ((count == size_t(-1) || size_t(s - str) < count) && (c = (uint8_t)*s++) != 0) {
        if (c < 0x80) {
        } else if (c == 0xc0 || c == 0xc1 || (c >= 0xf5 && c <= 0xff)) {
            return false;
        } else if (c < 0xe0) {
            if ((*s++ & 0xc0) != 0x80) return false;
        } else if (c < 0xf0) {
            if ((*s++ & 0xc0) != 0x80) return false;
            if ((*s++ & 0xc0) != 0x80) return false;
        } else {
            if ((*s++ & 0xc0) != 0x80) return false;
            if ((*s++ & 0xc0) != 0x80) return false;
            if ((*s++ & 0xc0) != 0x80) return false;
        }
    }
    return true;
}

bool XlHasUtf8Bom(const utf8* str)
{
    return (uint8_t)str[0] == 0xef && (uint8_t)str[1] == 0xbb && (uint8_t)str[2] == 0xbf;
}

size_t XlGetOffset(const char* s, size_t index)
{
    size_t l = 0;

    for (size_t i = 0; i < index; ++i) {
        uint32_t c = (uint8_t)s[l];
        if (!c) break;

        if (c < 0x80) {
            l += 1;
        } else if (c < 0xe0) {
            l += 2;
        } else if (c < 0xf0) {
            l += 3;
        } else if (c < 0xf8) {
            l += 4;
        //} else if (c < 0xfc) {
        //    l += 5;
        //} else if (c < 0xfe) {
        //    l += 6;
        } else {
            // 2009-02-25 umean, temporary disabled
            //assert(0);
            break;
        }
    }

    return l;
}


// casing (null terminated string)
const char* XlLowerCase(char* str)
{
    char* c = str;
    while (*c) {
        *c = XlToLower(*c);
        ++c;
    }
    return str;
}
const ucs4* XlLowerCase(ucs4* str)
{
    ucs4* c = str;
    while (*c) {
        *c = XlToLower(*c);
        ++c;
    }
    return str;
}

const char* XlUpperCase(char* str)
{
    char* c = str;
    while (*c) {
        *c = XlToUpper(*c);
        ++c;
    }
    return str;
}

const ucs4* XlUpperCase(ucs4* str)
{
    ucs4* c = str;
    while (*c) {
        *c = XlToUpper(*c);
        ++c;
    }
    return str;
}

size_t XlDecodeUrl(char* dst, size_t count, const char* encoded)
{
    const char* s = encoded;
    char* d = dst;
    while (*s) {
        if (size_t(d - dst) >= size_t(count - 1)) {
            return size_t(-1);
        }

        if (*s == '+') {
            *d = ' ';
            ++d;
            ++s;
        } else if (*s == '%') {
            ++s;

            if (!*s || !*(s + 1)) {
                return size_t(-1);
            }

            if (*s >= '0' && *s <= '9') {
                *d = (*s - '0') << 4;
            } else if (*s >= 'A' && *s <= 'F') {
                *d = (*s - 'A' + 10) << 4;
            } else {
                return size_t(-1);
            }

            ++s;
            if (*s >= '0' && *s <= '9') {
                *d |= (*s - '0');
            } else if (*s >= 'A' && *s <= 'F') {
                *d |= (*s - 'A' + 10);
            } else {
                return size_t(-1);
            }
            ++s;
            ++d;
        } else {
            *d = *s;
            ++d;
            ++s;
        }
    }

    *d = 0;
    return d - dst;
}

bool XlIsAlnum(char c)
{
	return __alphanum_table[(uint8_t)c] != 0x00;
}

bool XlIsEngAlpha(char c)
{
	return __alphanum_table[(uint8_t)c] > 0x01;
}

bool XlIsAlNumSpace(char c)
{
    return (__alphanum_table[(uint8_t)c] != 0x00) || (c == ' ');
}

bool XlIsDigit(utf8 c)
{
	return __alphanum_table[(uint8_t)c] == 0x01;
}

bool XlIsHex(char c)
{
	return __hex_table[(uint8_t)c] != 0xFF;
}

bool XlIsLower(char c)
{
	return __alphanum_table[(uint8_t)c] == 0x02;
}

bool XlIsUpper(char c)
{
	return __alphanum_table[(uint8_t)c] == 0x03;
}

bool XlIsPrint(char c)
{
    return (uint8_t)c >= ' ';
}

bool XlIsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

utf8 XlToLower(utf8 c)
{
	return (utf8)__lower_table[(uint8_t)c];
}

utf8 XlToUpper(utf8 c)
{
	return (utf8)__upper_table[(uint8_t)c];
}

wchar_t XlToLower(wchar_t c) { return std::tolower(c, std::locale()); }
wchar_t XlToUpper(wchar_t c) { return std::toupper(c, std::locale()); }

// remark_todo("remove branching!")
ucs2 XlToLower(ucs2 c)
{
    if (c <= 0x7F) {
        return (ucs2)__lower_table[(uint8_t)c];
    } 
    return c;
}

ucs2 XlToUpper(ucs2 c)
{
    if (c <= 0x7F) {
        return (ucs2)__upper_table[(uint8_t)c];
    }
    return c;
}

utf16 XlToLower(utf16 c)
{
    if (c <= 0x7F) {
        return (utf16)__lower_table[(uint8_t)c];
    } 
    return c;
}

utf16 XlToUpper(utf16 c)
{
    if (c <= 0x7F) {
        return (utf16)__upper_table[(uint8_t)c];
    }
    return c;
}

struct UnicharEng {
	UnicharEng(ucs4 c_) : c(c_) {}
    union {
        struct { 
            uint8_t l1, l2, l3;
            char ascii;
        } e;
        ucs4 c;
    };

	bool c0() const { return (c <= 0x7F); }
	bool c1() const { return (c > 0x7F) && (c <= 0xFF); }
	bool c01() const { return (c <= 0xFF); }
    bool c15() const { return (c <= 0x7FF); }
};

bool XlIsDigit(ucs4 c)
{
	UnicharEng eng(c);
	if (!eng.c0()) {
		return false;
	}
	return XlIsDigit((char) eng.e.l1);
}

bool XlIsAlpha(ucs4 c)
{
    UnicharEng eng(c);
    if (eng.c0()) {    // single byte ucs4
        return XlIsEngAlpha((char) eng.e.l1);
    }

    if (eng.c15()) {   // 2 byte ucs4
        return (iswalpha((wchar_t)c) > 0);
    }

    return false;
}

bool XlIsEngAlpha(ucs4 c)
{
    UnicharEng eng(c);
    if (eng.c0()) {    // single byte ucs4
        return XlIsEngAlpha((char) eng.e.l1);
    }

    return false;
}

bool XlIsUpper(ucs4 c)
{
    UnicharEng eng(c);
    if (!eng.c15()) {   // 3 - 4 byte ucs4
        return false;
    }
    else if (eng.c0()) {    // single byte ucs4
        return XlIsUpper((char) eng.e.l1);
    } 
    
    // 2byte ucs4
    return (iswupper((wchar_t) c) != 0);
}

bool XlIsLower(ucs4 c)
{
    UnicharEng eng(c);
    if (!eng.c15()) {   // 3 - 4 byte ucs4
        return false;
    }
    else if (eng.c0()) {    // single byte ucs4
        return XlIsLower((char) eng.e.l1);
    } 

    // 2byte ucs4
    return (iswlower((wchar_t) c) != 0);
}

bool XlIsSpace(ucs4 c)
{
    UnicharEng eng(c);
    if (!eng.c0()) {
        return false;
    }
    return XlIsSpace((char) eng.e.l1);
}


ucs4 XlToLower(ucs4 c)
{
    UnicharEng eng(c);
    if (!eng.c15()) {   // 3 - 4 byte ucs4
        return c;
    }
    if (eng.c0()) {    // single byte ucs4
        return (ucs4)XlToLower((char) eng.e.l1);
    }

    // 2byte ucs4
    wchar_t ret = towlower((wchar_t) c);

    return (ucs4) ret;
}

ucs4 XlToUpper(ucs4 c)
{
	UnicharEng eng(c);
	if (!eng.c15()) {   // 3 - 4 byte ucs4
		return c;
	}
    if (eng.c0()) {    // single byte ucs4
        return (ucs4)XlToUpper((char) eng.e.l1);
    }

    // 2byte ucs4
    wchar_t ret = towupper((wchar_t) c);

    return (ucs4) ret;
}

int XlFromHex(char c)
{
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return c - '0';
}

char XlToHex(int n)
{   
    uint32_t u = (uint32_t)n;
    if (u < 10) return '0' + char(u);
    else return 'a' + char(u - 10);
}

/*static inline bool XlIsLocaleSyllable(int c, bool allowSpace)
{
    if (c == 0x0020 && allowSpace) {
        return true;
    }

    switch (XlGetLocale()) {
    case LOCALE_ZH_CN:
        return c >= 0x4E00 && c <= 0x9FFF;
    case LOCALE_JA:
        return true;
    case LOCALE_RU:
        return (c >= 0x0400 && c <= 0x052F);
    default:
        return c >= 0xAC00 && c <= 0xD7AF;
    }
}*/

bool XlAtoBool(const char* str, const char** end_ptr)
{ 

	if (XlEqString(str, "true")) {
		if (end_ptr) {
			*end_ptr += 4;
		}
		return true;
	} else if (XlEqString(str, "false")) {
		if (end_ptr) {
			*end_ptr += 5;
		}
		return false;
	} else {
		if (end_ptr) {
			*end_ptr += XlStringSize(str);
		}
		return false;
	}
}

#define RADIX_MIN 2
#define RADIX_MAX 16

template <typename T> // T: must be "signed" type!
static inline T tpl_atoi(const char* buffer, const char** end_ptr, int radix)
{
	const char* s = buffer;

	if (radix < RADIX_MIN || radix > RADIX_MAX) {
		return (T)0;
	}

	// Skip whitespace
	while (XlIsSpace(*s))
		++s;

	// Check for sign
	bool neg = false;

	if (radix == 10) {
		if ((neg = *s == '-') || *s == '+') {
			++s;
		}
	} else if (radix == 16) {
		if (s[0] == '0' && __lower_table[s[1]] == 'x') {
			s += 2;
		}
	}

    // Accumulate digits
    T result = 0;
    while (*s && __hex_table[*s] != 255 && __hex_table[*s] < radix) {
        result = (result * radix) + __hex_table[*s];
        ++s;
    }

	if (end_ptr)
		*end_ptr = s;
	
	return (neg) ? -result : result;
}

#if 0       // deprecated in favour of FastParseValue / std::from_chars (where available)

template<typename Type>
	std::optional<Type> ParseInteger(StringSection<> input, int radix)
{
	const char* s = input.begin(), *se = input.end();

	assert(radix >= RADIX_MIN && radix <= RADIX_MAX);

	// Check for sign
	bool neg = false;
	(void)neg;
	if (constant_expression<std::is_signed<Type>::value>::result()) {
		if ((neg = *s == '-') || *s == '+') {
			++s;
		}
	} else if (*s == '+') {
		++s;
	}
	
	if (radix == 16) {
		if (((se-s)>=2) && s[0] == '0' && __lower_table[s[1]] == 'x') {
			s += 2;
		}
	}

    // Accumulate digits
    Type result = 0;
    while (s<se && __hex_table[*s] != 255 && __hex_table[*s] < radix) {
        result = (result * (Type)radix) + (Type)__hex_table[*s];
        ++s;
    }

	if (s != se)
		return {};		// did not parse all the way to the end

#pragma warning(push)
#pragma warning(disable:4146) // unary minus operator applied to unsigned type, result still unsigned
	if (constant_expression<std::is_signed<Type>::value>::result())
		if (neg)
			return (Type)-result;
#pragma warning(pop)
	return result;
}

template std::optional<uint8_t> ParseInteger(StringSection<> input, int radix);
template std::optional<uint16_t> ParseInteger(StringSection<> input, int radix);
template std::optional<uint32_t> ParseInteger(StringSection<> input, int radix);
template std::optional<uint64_t> ParseInteger(StringSection<> input, int radix);
template std::optional<int8_t> ParseInteger(StringSection<> input, int radix);
template std::optional<int16_t> ParseInteger(StringSection<> input, int radix);
template std::optional<int32_t> ParseInteger(StringSection<> input, int radix);
template std::optional<int64_t> ParseInteger(StringSection<> input, int radix);

#endif

int XlAtoI32(const char* str, const char** end_ptr, int radix)
{
	return tpl_atoi<int32>(str, end_ptr, radix);
}

int64 XlAtoI64(const char* str, const char** end_ptr, int radix)
{
	return tpl_atoi<int64>(str, end_ptr, radix);
}

uint32_t XlAtoUI32(const char* str, const char** end_ptr, int radix)
{
	return (uint32_t)tpl_atoi<int32>(str, end_ptr, radix);
}

uint64_t XlAtoUI64(const char* str, const char** end_ptr, int radix)
{
	return (uint64_t)tpl_atoi<int64>(str, end_ptr, radix);
}

float XlAtoF32(const char* str, const char** end_ptr)
{
	return (float)XlAtoF64(str, end_ptr);
}

double XlAtoF64(const char* str, const char** endptr)
{
	const char* s = str;

	// Skip whitespace
	while (XlIsSpace(*s))
		++s;

	// Check for sign
	int neg_val;
	if ((neg_val = (*s == '-')) || *s == '+') {
		++s;
	}

	// Accumulate digits, using exponent to track the number of digits in the 
	// fraction, and also if there are more digits than can be represented.
	double result = 0.0;
	int exponent = 0;
	while (XlIsDigit(*s)) {
        if (result > std::numeric_limits<double>::max() * 0.1)
			++exponent;
		else
			result = (result * 10.0) + (*s - '0');
		++s;
	}
	if (*s == '.') {
		++s;
		while (XlIsDigit(*s)) {
			--exponent;
			if (result > std::numeric_limits<double>::max() * 0.1)
				++exponent;
			else
				result = (result * 10.0) + (*s - '0');

			++s;
		}
	}

	// Check for exponent
	if (*s == 'e' || *s == 'E') {
		++s;

		// Get exponent and add to previous value computed above
		int neg_exp;
		if ((neg_exp = (*s == '-')) || *s == '+') {
			++s;
		}
		int n = 0;
		while (XlIsDigit(*s)) {
			n = n * 10 + (*s - '0');
			++s;
		}
		exponent += neg_exp ? -n : n;
	}

	if (endptr)
		*endptr = s;

	result *= pow(10.0, exponent);
	return neg_val ? -result : result;
}

static bool IsValidDimForI32(size_t dim, int radix)
{
    switch(radix) {
        case 8:
            return dim > 12;        // "-20000000000"
        case 10:
            return dim > 11;        // "-2147483648" longest possible. need 11 chars + null terminator
        case 16:
            return dim > 8;
        //expand when need.
    }

    assert(0);
    return false;
}

static bool IsValidDimForI64(size_t dim, int radix)
{
    switch(radix) {
        case 8:
            return dim > 23;    // "-1000000000000000000000"
        case 10:
            return dim > 20;    // "-9223372036854775808" or "18446744073709551615" longest possible. need 20 chars + null terminator
        case 16:
            return dim > 16;
        //expand when need.
    }

    assert(0);
    return false;
}

template <typename T>
static inline char* tpl_itoa(T value, char* buf, int radix)
{
	T i;
	char* p = buf;
	char* q = buf;

	if (radix < RADIX_MIN || radix > RADIX_MAX)	{
		*buf = 0;
		return buf;
	}

	if (value == 0) {
		*p++ = '0';
		*p = 0;
		return buf;
	}

	while (value > 0) {
		i = value % radix;

		if (i > 9)
			i += 39;

		*p++ = char('0' + i);
		value /= radix;
	}

	*p-- = 0;
	q = buf;

	while (p > q) {
		i = *q;
		*q++ = *p;
		*p-- = char(i);
	}

	return buf;
}

char* XlI32toA(int32 value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI32(dim, radix)) {
		return 0;
	}

	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint32_t>((uint32_t)(-value), buffer + 1, 10);
		return buffer;
	}

	return tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);	
}

char* XlI64toA(int64 value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI64(dim, radix)) {
		return 0;
	}

	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint64_t>((uint64_t)(-value), buffer + 1, 10);
		return buffer;
	}

	return tpl_itoa<uint64_t>(value, buffer, radix);
}

char* XlUI32toA(uint32_t value, char* buffer, size_t dim, int radix)
{
    if (!IsValidDimForI32(dim, radix)) {
        return 0;
    }

	return tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);	
}

char* XlUI64toA(uint64_t value, char* buffer, size_t dim, int radix)
{
    if (!IsValidDimForI64(dim, radix)) {
        return 0;
    }

    return tpl_itoa<uint64_t>(value, buffer, radix);
}

int XlI32toA_s(int32 value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI32(dim, radix)) {
		return EINVAL;
	}

	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint32_t>((uint32_t)(-value), buffer + 1, 10);
		return 0;
	}

	tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);	
	return 0;
}

int XlI64toA_s(int64 value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI64(dim, radix)) {
		return EINVAL;
	}

	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint64_t>((uint64_t)(-value), buffer + 1, 10);
		return 0;
	}

	tpl_itoa<uint64_t>(value, buffer, radix);
	return 0;
}

int XlUI32toA_s(uint32_t value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI32(dim, radix)) {
		return EINVAL;
	}

	tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);	
	return 0;
}

int XlUI64toA_s(uint64_t value, char* buffer, size_t dim, int radix)
{
	if (!IsValidDimForI64(dim, radix)) {
		return EINVAL;
	}

	tpl_itoa<uint64_t>(value, buffer, radix);
	return 0;
}

char* XlI32toA_ns(int32 value, char* buffer, int radix)
{
	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint32_t>((uint32_t)(-value), buffer + 1, 10);
		return buffer;
	}

	return tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);	
}

char* XlI64toA_ns(int64 value, char* buffer, int radix)
{
	if (radix == 10 && value < 0) {
		*buffer = '-';
		tpl_itoa<uint64_t>((uint64_t)(-value), buffer + 1, 10);
		return buffer;
	}

	return tpl_itoa<uint64_t>((uint64_t)value, buffer, radix);	
}

char* XlUI32toA_ns(uint32_t value, char* buffer, int radix)
{
	return tpl_itoa<uint32_t>((uint32_t)value, buffer, radix);
}

char* XlUI64toA_ns(uint64_t value, char* buffer, int radix)
{
	return tpl_itoa<uint64_t>(value, buffer, radix);
}

bool XlMatchWildcard(const char* str, const char* pat, bool nocase)
{
    const char* s;
    const char* p;
    bool star = false;
    
start:
    for (s = str, p = pat; *s; ++s, ++p) {
        switch (*p) {
            case '?':
                if (*s == '.') goto check;
                break;
            case '*':
                star = true;
                str = s, pat = p;
                if (!*++pat) return true;
                goto start;
            default:
                if (nocase) {
                    if (__lower_table[*s] != __lower_table[*p]) goto check;
                } else {
                    if (*s != *p) goto check;
                }
                break;
        }
    }
    
    if (*p == '*') { 
        ++p;
    }
    
    return (!*p);
    
check:
    if (!star) {
        return false;
    }
    str++;
    goto start;
}

bool XlToHexStr(const char* x, size_t xlen, char* y, size_t ylen)
{
    static const char* hexchars = "0123456789abcdef";

    if (ylen < 2 * xlen + 1) {
        return false;
    }

    for (size_t i = 0; i < xlen; i++) {
        *y++ = hexchars[(uint8_t)x[i] >> 4];
        *y++ = hexchars[(uint8_t)x[i] & 0x0f];
    }
    *y = '\0';

    return true;
}

// limitations: without checking in/out length & input validity!
bool XlHexStrToBin(const char* x, char* y)
{
    const char* c = x;

    char b = 0;

    while (*c) {
        b = (__hex_table[(uint8_t)*c++]) << 4;
        if (*c == '\0') {
            return false;
        }
        b |= (__hex_table[(uint8_t)*c++]) & 0x0f;
        *y++ = b;
    }
    return true;
}

#if 0
size_t XlMultiToWide(ucs4* dst, size_t count, const utf8* src)
{
    // Assume UTF-8, but check for byte-order-mark
    if (XlHasUtf8Bom(src))
        src += 3;

    size_t i = 0;
    while (i < count-1) {
        uint32_t c = (uint8_t)*src++;
        if (c == 0)
            break;

        ucs4 x;
        if (c < 0x80) {
            x = c;
        } else if (c < 0xe0) {
            x = c & 0x1f;
            x = (x << 6) | *src++ & 0x3f;
        } else if (c < 0xf0) {
            x = c & 0x0f;
            x = (x << 6) | *src++ & 0x3f;
            x = (x << 6) | *src++ & 0x3f;
        } else {
            x = c & 0x07;
            x = (x << 6) | *src++ & 0x3f;
            x = (x << 6) | *src++ & 0x3f;
            x = (x << 6) | *src++ & 0x3f;
        }
        dst[i++] = x;
    }
    dst[i++] = '\0'; 
    return i;
}

size_t XlWideToMulti(utf8* dst, size_t count, const ucs4* src)
{
    auto* p = dst;
    auto* end = dst + count - 1;

    ucs4 c;
    while (p < end && (c = *src++) != 0) {

        if (c < 0x80) {
            *p++ = (char)c;

        } else if (c < 0x800) {
            *p++ = 0xc0 | ((c >> 6) & 0x1f);
            if (p == end) break;
            *p++ = 0x80 | (c & 0x3f);

        } else if (c < 0x10000) {
            *p++ = 0xe0 | ((c >> 12) & 0x0f);
            if (p == end) break;
            *p++ = 0x80 | ((c >> 6) & 0x3f);
            if (p == end) break;
            *p++ = 0x80 | (c & 0x3f);

        } else if (c < 0x110000) {
            *p++ = 0xf0 | ((c >> 18) & 0x07);
            if (p == end) break;
            *p++ = 0x80 | ((c >> 12) & 0x3f);
            if (p == end) break;
            *p++ = 0x80 | ((c >> 6) & 0x3f);
            if (p == end) break;
            *p++ = 0x80 | (c & 0x3f);
        }
    }

    *p++ = 0;
    return p - dst;
}

#endif

    std::string Concatenate(StringSection<> zero, StringSection<> one)
    {
        std::string result;
        result.reserve(zero.size() + one.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        return result;
    }
    
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two)
    {
        std::string result;
        result.reserve(zero.size() + one.size() + two.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        result.insert(result.end(), two.begin(), two.end());
        return result;
    }

    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three)
    {
        std::string result;
        result.reserve(zero.size() + one.size() + two.size() + three.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        result.insert(result.end(), two.begin(), two.end());
        result.insert(result.end(), three.begin(), three.end());
        return result;
    }
    
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four)
    {
        std::string result;
        result.reserve(zero.size() + one.size() + two.size() + three.size() + four.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        result.insert(result.end(), two.begin(), two.end());
        result.insert(result.end(), three.begin(), three.end());
        result.insert(result.end(), four.begin(), four.end());
        return result;
    }

    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four, StringSection<> five)
    {
        std::string result;
        result.reserve(zero.size() + one.size() + two.size() + three.size() + four.size() + five.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        result.insert(result.end(), two.begin(), two.end());
        result.insert(result.end(), three.begin(), three.end());
        result.insert(result.end(), four.begin(), four.end());
        result.insert(result.end(), five.begin(), five.end());
        return result;
    }
    
    std::string Concatenate(StringSection<> zero, StringSection<> one, StringSection<> two, StringSection<> three, StringSection<> four, StringSection<> five, StringSection<> six)
    {
        std::string result;
        result.reserve(zero.size() + one.size() + two.size() + three.size() + four.size() + five.size() + six.size());
        result.insert(result.end(), zero.begin(), zero.end());
        result.insert(result.end(), one.begin(), one.end());
        result.insert(result.end(), two.begin(), two.end());
        result.insert(result.end(), three.begin(), three.end());
        result.insert(result.end(), four.begin(), four.end());
        result.insert(result.end(), five.begin(), five.end());
        result.insert(result.end(), six.begin(), six.end());
        return result;
    }
}
