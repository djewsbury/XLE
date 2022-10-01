// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StringFormat.h"
#include "../Core/Types.h"
#include "Streams/Stream.h"
#include "Streams/StreamTypes.h"
#include "PtrUtils.h"
#include "StringUtils.h"
#include <stdio.h>
#include <wchar.h>
#include <assert.h>
#include <algorithm>

#pragma warning(disable:4127)       // conditional expression is constant

namespace Utility
{

// Conversion specification:
//
//   %[Index] [Flags] [Width] [.Precision] [Modifier] Type
//
// Index:
//   <unsigned integer> '$'
// 
//   Access an argument by position.
//
// Flags:
//  '-'    Left align instead of right align within the conversion width.
//
//  '+'    Always prefix with a sign (+ or -) for signed types.
//
//  '0'    Pad with zeros instead of spaces.  Ignored if '-' is specified,
//         or when used with an integer type and a precision is given.
//
//  ' '    Prefix a positive value with a blank for signed types.  Ignored
//         if '+' is specified.
//
//  '#'    Convert to an alternate form:
//           - o, x, X         Prefix nonzero value with 0, 0x, or 0X.
//           - e, E, f, g, G   Always insert a decimal point.
//           - g, G            Do not truncated trailing zeros.
//           - s, S            Convert to the multi-byte string of ACP(active code page)
//
//  ','    Comma separate by thousands using the non-monetary-separator
//         for these types (d, f, g, G, i, u).
//
// Width: 
//   <unsigned integer>           Specified in format string
//   '*'                          Specified as an additional 'int' argument
//   '*' <unsigned integer> '$'   Specified as a positional 'int' argument
//
//   Pads converted values to occupy at least a minimum width (no
//   truncation). 
//
//   Negative widths specified through arguments imply the '-' flag for
//   left alignment.
//
// Precision:
//   <unsigned integer>           Specified in format string
//   '*'                          Specified as an additional 'int' argument
//   '*' <unsigned integer> '$'   Specified as a positional 'int' argument
//
//   Modifies the number of digits in the converted value:
//
//    d,i,o,u,x,X         maximum number of digits
//    e,E,f               maximum number of digits after the decimal point
//    g,G                 maximum number of significant digits
//    s,S                 maximum number of characters
//
//
//   Negative precisions are ignored. Precision is interpreted as follows:
//
// Types:
//   b     (bool) bool
//   c     (int) ASCII character
//   C     (ucs4) UCS-4 character
//   d,i   (int) signed decimal
//   o     (uint) unsigned octal
//   u     (uint) unsigned decimal
//   x     (uint) unsigned lowercase hex
//   X     (uint) unsigned uppercase hex
//   e,E   (double) floating point: [-]d.dddd[e/E]+-ddd
//   f     (double) floating point: [-]dddd.dddd
//   g,G   (double) floating point: 'e' or 'f'
//   p     (void*) pointer
//   s     (char*) mbcs or UTF8 string
//   S     (ucs4*) UCS-4 string
//
// Integer types: d, i, o, u, x, X
// Signed types: d, e, E, f, g, G, i
//
// Modifier   d,i      o,u,x,X         s
// -----------------------------------------------------------
// l / ll    int32/int64    uint64     ucs2*
// -- in 32-bits
// -- 'l' stands for long
// -- 'll' stands for long long
// -- in 64-bits
// -- either 'l' or 'll' mean long long
// -- 'I32' & 'I64' are no longer supported!
// -- e.g., the following code snippet is valid both for 32bits & 64bits
//    size_t someval = your-value;
//    XlFormatString(buf, dimof(buf), "this works fine for all environment!: %ld", someval);

// Modifiers on floating point types are not supported.
//
//
// Current Status:
//   - Positional arguments not implemented
//   - ',' flag not implemented
//   - 'l' modifier with 's' is not implemented

union FormatVal {
    bool f_bool;
    ucs4 f_unichar;
    int f_int;
    uint32 f_uint;
    int64 f_int64;
    uint64 f_uint64;
    double f_double;
    const utf8* f_string;
    const ucs2* f_wstring;
    const ucs4* f_ustring;
    void* f_pointer;
};

enum {
    FLAG_NONE    = 0,
    FLAG_MINUS   = (1 << 0),
    FLAG_PLUS    = (1 << 1),
    FLAG_ZERO    = (1 << 2),
    FLAG_SPACE   = (1 << 3),
    FLAG_SHARP   = (1 << 4),
    FLAG_COMMA   = (1 << 5)
};

enum {
    MOD_NONE,
    MOD_LONG,
    MOD_LONGLONG
};

static int FlagChar(int ch)
{
    switch (ch) {
    case '-': return FLAG_MINUS;
    case '+': return FLAG_PLUS;
    case '0': return FLAG_ZERO;
    case ' ': return FLAG_SPACE;
    case '#': return FLAG_SHARP;
    case ',': return FLAG_COMMA;
    default: return FLAG_NONE;
    }
}

// [TODO] remove xl_snprintf dependency!
template<typename T>
    int _PrintFormatV(OutputStream* stream, const T* fmt, va_list args)
{
    size_t nchars = 0;
    const T* start = fmt;
    bool left = false;

    while (*fmt) {
        // output non-format characters directly
        if (*fmt != '%') {
            ++fmt;
            continue;
        }
        int len2 = (int)(fmt - start);
        stream->Write(start, len2 * sizeof(T));
        nchars += len2;
        ++fmt;
        if (*fmt == '%') {
            start = fmt;
            ++fmt;
            continue;
        }

        // storage for conversion specifier
        int flags = FLAG_NONE;
        int width = -1;
        int prec = -1;
        int modifier = MOD_NONE;
        int type = 0;

        // parse flags
        int flag;
        while ((flag = FlagChar(*fmt)) != FLAG_NONE) {
            ++fmt;
            flags |= flag;
        }

        // parse width
        if (XlIsDigit(*fmt)) {
            width = 0;
            while (XlIsDigit(*fmt)) {
                width = (width * 10) + (*fmt - '0');
                ++fmt;
            }
        } else if (*fmt == '*') {
            ++fmt;
            width = va_arg(args, int);
            if (width < 0) {
                width = -width;
                left = true;
            }
        }

        // parse precision
        if (*fmt == '.') {
            ++fmt;
            if (XlIsDigit(*fmt)) {
                prec = 0;
                while (XlIsDigit(*fmt)) {
                    prec = (prec * 10) + (*fmt - '0');
                    ++fmt;
                }
            } else if (*fmt == '*') {
                ++fmt;
                prec = va_arg(args, int);
            }
        }

        // parse type prefix
#ifdef WIN64
        if (*fmt == 'l') {
            ++fmt;
            modifier = MOD_LONGLONG;
            // for windows compatibility(ll modifier)
            if (*fmt == 'l') {
                ++fmt;
                modifier = MOD_LONGLONG;
            }
        }
#else
        if (*fmt == 'l') {
            ++fmt;
            modifier = MOD_LONG;
            // for windows compatibility(ll modifier)
            if (*fmt == 'l') {
                ++fmt;
                modifier = MOD_LONGLONG;
            }
        }
#endif
        // parse type
        type = *fmt++;
        switch (type) {
        case 'c':
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
        case 's':
            break;
        case 'b':
        case 'C':
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
        case 'p':
        case 'S':
            if (modifier == MOD_LONG) {
                //LOG_ERROR("Error in format string: 'l' modifier unsupported for type '%c'", type);
                assert(0);
                return 0;
            }
            break;
        default:
            //LOG_ERROR("Error in format string: unsupported type '%c'", type);
            assert(0);
            return 0;
        }

        // process value
        FormatVal val;
        if (type == 's') {
            // UTF8 string
            assert(prec < 0);

            if (modifier == MOD_NONE) {
                val.f_string = va_arg(args, const utf8*);
                if (flags == FLAG_MINUS) {
                    flags = FLAG_NONE;
                    left = true;
                }

                if (sizeof(char) != sizeof(T)) {
                    assert(flags == FLAG_NONE);
                    size_t len = XlGlyphCount(val.f_string) + 1;
                    ucs4 tmp[4096];

                    if (len > dimof(tmp)) {
                        return 0;
                    }

                    ptrdiff_t fixed_length = utf8_2_ucs4(val.f_string, XlGlyphCount(val.f_string), tmp, len);
                    nchars += std::max<ptrdiff_t>(fixed_length, width);

                    fixed_length = width - fixed_length;

                    assert(0);  // following no longer supported
                    /*if (fixed_length <= 0) {
                        stream->Write(tmp);
                    } else if (left) {
                        stream->Write(tmp);
                        while (fixed_length-- > 0) {
                            stream->WriteChar(' ');
                        }

                    } else { // right
                        while (fixed_length-- > 0) {
                            stream->WriteChar(' ');
                        }
                        stream->Write(tmp);

                    }*/

                } else {

                    assert(flags == FLAG_NONE || flags == FLAG_SHARP);
                    if (val.f_string) {

                        auto tmp = val.f_string;
                        ptrdiff_t fixed_length = XlStringSize(tmp);
                        nchars += std::max<ptrdiff_t>(fixed_length, width);
                        fixed_length = width - fixed_length;

                        if (fixed_length <= 0) {
                            stream->Write(tmp);
                        } else if (left) {
                            stream->Write(tmp);
                            while (fixed_length-- > 0) {
                                stream->WriteChar(' ');
                            }
                        } else { // right
                            while (fixed_length-- > 0) {
                                stream->WriteChar(' ');
                            }
                            stream->Write(tmp);
                        }
                    } else {
                        stream->Write((const utf8*)"<null>");
                        nchars += XlGlyphCount((const utf8*)"<null>");
                    }
                }
            } else if (modifier == MOD_LONG || modifier == MOD_LONGLONG) {
                val.f_wstring = va_arg(args, const ucs2*);
                if (flags == FLAG_MINUS) {
                    flags = FLAG_NONE;
                    left = true;
                }

                if (sizeof(char) != sizeof(T)) {
                    assert(flags == FLAG_NONE);
                    size_t len = XlGlyphCount(val.f_wstring) + 1;
                    nchars += std::max<size_t>(len, width);

                    len = width - len;

                    assert(0);  // following no longer supported
                    /*
                    auto tmp = val.f_wstring;
                    if (len <= 0) {
                        stream->Write(tmp);
                    } else if (left) {
                        stream->Write(tmp);
                        while (len-- > 0) {
                            stream->WriteChar(' ');
                        }

                    } else { // right
                        while (len-- > 0) {
                            stream->WriteChar(' ');
                        }
                        stream->Write(tmp);

                    }*/

                } else {

                    assert(flags == FLAG_NONE || flags == FLAG_SHARP);
                    if (val.f_wstring) {

                        utf8 tmp[4096] = {0};
                        int len = (int)XlGlyphCount(val.f_wstring);
                        ucs2_2_utf8(val.f_wstring, len, tmp, dimof(tmp));
                        ptrdiff_t fixed_length = XlStringSize(tmp);
                        nchars += std::max<ptrdiff_t>(fixed_length, width);
                        fixed_length = width - fixed_length;

                        if (fixed_length <= 0) {
                            stream->Write(tmp);
                        } else if (left) {
                            stream->Write(tmp);
                            while (fixed_length-- > 0) {
                                stream->WriteChar(' ');
                            }
                        } else { // right
                            while (fixed_length-- > 0) {
                                stream->WriteChar(' ');
                            }
                            stream->Write(tmp);
                        }
                    } else {
                        stream->Write((const utf8*)"<null>");
                        nchars += XlGlyphCount((const utf8*)"<null>");
                    }
                }
            }

        } else if (type == 'S') {
            // UCS4 string
            val.f_ustring = va_arg(args, const ucs4*);
            // TODO support width!

            assert(0);  // following no longer supported
            // stream->Write(val.f_ustring);
            nchars += XlGlyphCount(val.f_ustring);

        } else {
            // build format spec and use system provided sprintf
            T fmt2Buf[128];
            T* fmt2 = fmt2Buf;

            *fmt2++ = '%';
            if (flags & FLAG_MINUS)
                *fmt2++ = '-';
            if (flags & FLAG_PLUS)
                *fmt2++ = '+';
            if (flags & FLAG_ZERO)
                *fmt2++ = '0';
            if (flags & FLAG_SPACE)
                *fmt2++ = ' ';
            if (flags & FLAG_SHARP)
                *fmt2++ = '#';
            if (flags & FLAG_COMMA) // NOTE - not supported on MSVC
                *fmt2++ = ',';
            if (width >= 0) {
                if (type != 's'&& type != 'S') {
                    T buf[16];
                    if (sizeof(char) == sizeof(T)) {
                        xl_snprintf(buf, dimof(buf), (T*)"%d", width);
                    } else {
                        assert(0); // line below is questionable
                        xl_snprintf(buf, dimof(buf), (T*)L"%d", width);
                    }
                    const T* text = buf;
                    while (*text)
                        *fmt2++ = *text++;
                } else {
                    int _width = width;
                    while (_width-- > 0) {
                        *fmt2++ = ' ';
                    }
                }
            }
            if (prec >= 0) {
                *fmt2++ = '.';
                T buf[16];
                if (sizeof(char) == sizeof(T)) {
                    xl_snprintf(buf, dimof(buf), (T*)"%d", prec);
                } else {
                    assert(0); // line below is questionable
                    xl_snprintf(buf, dimof(buf), (T*)L"%d", prec);
                }
                const T* text = buf;
                while (*text)
                    *fmt2++ = *text++;
            }

            if (type == 'c') {
                if (modifier == MOD_LONG)
                    *fmt2++ = 'h';
                else
                    *fmt2++ = 'l';
            } else if (type == 'C') {
                *fmt2++ = 'l';
            } else if (modifier == MOD_LONG) {
                *fmt2++ = 'l';
            } else if (modifier == MOD_LONGLONG) {
                *fmt2++ = 'l';
                *fmt2++ = 'l';
            }
            *fmt2++ = (char)type;
            *fmt2++ = '\0';
            fmt2 = fmt2Buf;

            T buf[512];
            if (modifier == MOD_LONGLONG) {
                switch (type) {
                case 'c':
                    val.f_unichar = va_arg(args, ucs4);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_unichar);
                    break;
                case 'd':
                case 'i':
                    val.f_int64 = va_arg(args, int64);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_int64);
                    break;
                case 'o':
                case 'u':
                case 'x':
                case 'X':
                    val.f_uint64 = va_arg(args, uint64);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_uint64);
                    break;
                }
            } else {
                switch (type) {
                case 'b':
                    val.f_bool = va_arg(args, int) != 0;
                    if (sizeof(char) == sizeof(T)) {
                        XlCopyString(buf, dimof(buf), val.f_bool ? (T*)"true" : (T*)"false");
                    } else {
                        assert(0); // line below is questionable
                        XlCopyString(buf, dimof(buf), val.f_bool ? (T*)L"true" : (T*)L"false");
                    }
                    break;

                case 'd':
                case 'i':
                    val.f_int = va_arg(args, int);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_int);
                    break;
                case 'c':
                    val.f_int = va_arg(args, int/*const char*/);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_int);
                    break;
                case 'C':
                    val.f_unichar = va_arg(args, int/*const ucs4*/);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_unichar);
                    break;
                case 'o':
                case 'u':
                case 'x':
                case 'X':
                    val.f_uint = va_arg(args, uint32);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_uint);
                    break;
                case 'e':
                case 'E':
                case 'f':
                case 'g':
                case 'G':
                    val.f_double = va_arg(args, double);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_double);
                    break;
                case 'p':
                    val.f_pointer = va_arg(args, void*);
                    xl_snprintf(buf, dimof(buf), fmt2, val.f_pointer);
                    break;
                }
            }

            stream->Write(buf);
            nchars += XlGlyphCount(buf);
        }
        start = fmt;
    }

    int len = (int)(fmt - start);
    stream->Write(start, len * sizeof(T));
    nchars += len;
    return (int)nchars;
}

int PrintFormatV(OutputStream* stream, const char* fmt, va_list args)
{
    return _PrintFormatV<utf8>(stream, (utf8*)fmt, args);
}

int PrintFormat(OutputStream* stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = PrintFormatV(stream, fmt, args);
    va_end(args);
    return n;
}

int XlFormatStringV(char* buf, int count, const char* fmt, va_list args) never_throws
{
    FixedMemoryOutputStream<utf8> stream((utf8*)buf, (size_t)count);
    int n = PrintFormatV(&stream, fmt, args);
    stream.WriteChar(0);
    if (n >= count) {
        n = std::max<int>(count - 1, 0);
    }
    return n;
}

int XlFormatString(char* buf, int count, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = XlFormatStringV(buf, count, fmt, args);
    va_end(args);
    return n;
}

std::string XlDynFormatString(const char format[], ...)
{
    va_list args;
    va_start(args, format);
    char buffer[2048];
    xl_vsnprintf(buffer, dimof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}

}

