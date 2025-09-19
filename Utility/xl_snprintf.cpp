// [ORIGIN] := http://www.jbox.dk/sanos/

// vsprintf.c
//
// Print formatting routines
//
// Copyright (C) 2002 Michael Ringgaard. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.  
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.  
// 3. Neither the name of the project nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
// SUCH DAMAGE.
// 

#include "../Core/Prefix.h"
#include "Streams/StreamTypes.h"
#include "StringUtils.h"

#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <math.h>

namespace Utility
{

#define ZEROPAD 1               // Pad with zero
#define SIGN    2               // Unsigned/signed long
#define PLUS    4               // Show plus
#define SPACE   8               // Space if plus
#define LEFT    16              // Left justified
#define SPECIAL 32              // 0x
#define LARGE   64              // Use 'ABCDEF' instead of 'abcdef'

#define is_digit(c) ((c) >= '0' && (c) <= '9')

static const char *digits = "0123456789abcdefghijklmnopqrstuvwxyz";
static const char *upper_digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

#define CVTBUFSIZE        (309 + 43)

static char null_str[] = { '<', 'N','U', 'L', 'L', '>' };

namespace fmt 
{
int skip_atoi(const char **s);
char *number(char *str, long num, int base, int size, int precision, int type);
char *number64(char *str, uint64_t num, int base, int size, int precision, int type);
char *eaddr(char *str, unsigned char *addr, int size, int precision, int type);
char *ecvtbuf(double arg, int ndigits, int *decpt, int *sign, char *buf);
char *fcvtbuf(double arg, int ndigits, int *decpt, int *sign, char *buf);
char *iaddr(char *str, unsigned char *addr, int size, int precision, int type);
void cfltcvt(double value, char *buffer, char fmt, int precision);
void forcdecpt(char *buffer);
void cropzeros(char *buffer);
char *flt(char *str, double num, int size, int precision, char fmt, int flags);
char *cvt(double arg, int ndigits, int *decpt, int *sign, char *buf, int eflag);
}

namespace fmt
{

int skip_atoi(const char **s)
{
    int i = 0;
    while (is_digit(**s)) i = i*10 + *((*s)++) - '0';
    return i;
}

char *number(char *str, long num, int base, int size, int precision, int type)
{
    char c, sign, tmp[66];
    const char *dig = digits;
    int i;

    if (type & LARGE)  dig = upper_digits;
    if (type & LEFT) type &= ~ZEROPAD;
    if (base < 2 || base > 36) return 0;

    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN)
    {
        if (num < 0)
        {
            sign = '-';
            num = -num;
            size--;
        }
        else if (type & PLUS)
        {
            sign = '+';
            size--;
        }
        else if (type & SPACE)
        {
            sign = ' ';
            size--;
        }
    }

    if (type & SPECIAL)
    {
        if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
    }

    i = 0;

    if (num == 0)
        tmp[i++] = '0';
    else
    {
        while (num != 0)
        {
            tmp[i++] = dig[((unsigned long) num) % (unsigned) base];
            num = ((unsigned long) num) / (unsigned) base;
        }
    }

    if (i > precision) precision = i;
    size -= precision;
    if (!(type & (ZEROPAD | LEFT))) while (size-- > 0) *str++ = ' ';
    if (sign) *str++ = sign;

    if (type & SPECIAL)
    {
        if (base == 8)
            *str++ = '0';
        else if (base == 16)
        {
            *str++ = '0';
            *str++ = digits[33];
        }
    }

    if (!(type & LEFT)) while (size-- > 0) *str++ = c;
    while (i < precision--) *str++ = '0';
    while (i-- > 0) *str++ = tmp[i];
    while (size-- > 0) *str++ = ' ';

    return str;
}

char *number64(char *str, uint64_t num, int base, int size, int precision, int type)
{
    char c, sign, tmp[66];
    const char *dig = digits;
    int i;

    if (type & LARGE)  dig = upper_digits;
    if (type & LEFT) type &= ~ZEROPAD;
    if (base < 2 || base > 36) return 0;

    c = (type & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (type & SIGN)
    {
        int64_t n0 = (int64_t)num;
        if (n0 < 0)
        {
            sign = '-';
            num = -n0;
            size--;
        }
        else if (type & PLUS)
        {
            sign = '+';
            size--;
        }
        else if (type & SPACE)
        {
            sign = ' ';
            size--;
        }
    }

    if (type & SPECIAL)
    {
        if (base == 16)
            size -= 2;
        else if (base == 8)
            size--;
    }

    i = 0;

    if (num == 0)
        tmp[i++] = '0';
    else
    {
        while (num != 0)
        {
            tmp[i++] = dig[((uint64_t) num) % (unsigned) base];
            num = ((uint64_t) num) / (unsigned) base;
        }
    }

    if (i > precision) precision = i;
    size -= precision;
    if (!(type & (ZEROPAD | LEFT))) while (size-- > 0) *str++ = ' ';
    if (sign) *str++ = sign;

    if (type & SPECIAL)
    {
        if (base == 8)
            *str++ = '0';
        else if (base == 16)
        {
            *str++ = '0';
            *str++ = digits[33];
        }
    }

    if (!(type & LEFT)) while (size-- > 0) *str++ = c;
    while (i < precision--) *str++ = '0';
    while (i-- > 0) *str++ = tmp[i];
    while (size-- > 0) *str++ = ' ';

    return str;
}


char *eaddr(char *str, unsigned char *addr, int size, int precision, int type)
{
    char tmp[24];
    const char *dig = digits;
    int i, len;

    if (type & LARGE)  dig = upper_digits;
    len = 0;
    for (i = 0; i < 6; i++)
    {
        if (i != 0) tmp[len++] = ':';
        tmp[len++] = dig[addr[i] >> 4];
        tmp[len++] = dig[addr[i] & 0x0F];
    }

    if (!(type & LEFT)) while (len < size--) *str++ = ' ';
    for (i = 0; i < len; ++i) *str++ = tmp[i];
    while (len < size--) *str++ = ' ';

    return str;
}

char *iaddr(char *str, unsigned char *addr, int size, int precision, int type)
{
    char tmp[24];
    int i, n, len;

    len = 0;
    for (i = 0; i < 4; i++)
    {
        if (i != 0) tmp[len++] = '.';
        n = addr[i];

        if (n == 0)
            tmp[len++] = digits[0];
        else
        {
            if (n >= 100) 
            {
                tmp[len++] = digits[n / 100];
                n = n % 100;
                tmp[len++] = digits[n / 10];
                n = n % 10;
            }
            else if (n >= 10) 
            {
                tmp[len++] = digits[n / 10];
                n = n % 10;
            }

            tmp[len++] = digits[n];
        }
    }

    if (!(type & LEFT)) while (len < size--) *str++ = ' ';
    for (i = 0; i < len; ++i) *str++ = tmp[i];
    while (len < size--) *str++ = ' ';

    return str;
}

void cfltcvt(double value, char *buffer, char fmt, int precision)
{
    int decpt, sign, exp, pos;
    char *valuedigits = NULL;
    char cvtbuf[CVTBUFSIZE];
    int capexp = 0;
    int magnitude;

    if (fmt == 'G' || fmt == 'E')
    {
        capexp = 1;
        fmt += 'a' - 'A';
    }

    if (fmt == 'g')
    {
        valuedigits = ecvtbuf(value, precision, &decpt, &sign, cvtbuf);
        magnitude = decpt - 1;
        if (magnitude < -4  ||  magnitude > precision - 1)
        {
            fmt = 'e';
            precision -= 1;
        }
        else
        {
            fmt = 'f';
            precision -= decpt;
        }
    }

    if (fmt == 'e')
    {
        valuedigits = ecvtbuf(value, precision + 1, &decpt, &sign, cvtbuf);

        if (sign) *buffer++ = '-';
        *buffer++ = *valuedigits;
        if (precision > 0) *buffer++ = '.';
        memcpy(buffer, valuedigits + 1, precision);
        buffer += precision;
        *buffer++ = capexp ? 'E' : 'e';

        if (decpt == 0)
        {
            if (value == 0.0)
                exp = 0;
            else
                exp = -1;
        }
        else
            exp = decpt - 1;

        if (exp < 0)
        {
            *buffer++ = '-';
            exp = -exp;
        }
        else
            *buffer++ = '+';

        buffer[2] = (exp % 10) + '0';
        exp = exp / 10;
        buffer[1] = (exp % 10) + '0';
        exp = exp / 10;
        buffer[0] = (exp % 10) + '0';
        buffer += 3;
    }
    else if (fmt == 'f')
    {
        valuedigits = fcvtbuf(value, precision, &decpt, &sign, cvtbuf);
        if (sign) *buffer++ = '-';
        if (*valuedigits)
        {
            if (decpt <= 0)
            {
                *buffer++ = '0';
                *buffer++ = '.';
                for (pos = 0; pos < -decpt; pos++) *buffer++ = '0';
                while (*valuedigits) *buffer++ = *valuedigits++;
            }
            else
            {
                pos = 0;
                while (*valuedigits)
                {
                    if (pos++ == decpt) *buffer++ = '.';
                    *buffer++ = *valuedigits++;
                }
            }
        }
        else
        {
            *buffer++ = '0';
            if (precision > 0)
            {
                *buffer++ = '.';
                for (pos = 0; pos < precision; pos++) *buffer++ = '0';
            }
        }
    }

    *buffer = '\0';
}

void forcdecpt(char *buffer)
{
    while (*buffer)
    {
        if (*buffer == '.') return;
        if (*buffer == 'e' || *buffer == 'E') break;
        buffer++;
    }

    if (*buffer)
    {
        size_t n = strlen(buffer);
        while (n > 0) 
        {
            buffer[n + 1] = buffer[n];
            n--;
        }

        *buffer = '.';
    }
    else
    {
        *buffer++ = '.';
        *buffer = '\0';
    }
}

#pragma warning(disable:4706)   // C4706: assignment within conditional expression

void cropzeros(char *buffer)
{
    char *stop;

    while (*buffer && *buffer != '.') buffer++;
    if (*buffer++)
    {
        while (*buffer && *buffer != 'e' && *buffer != 'E') buffer++;
        stop = buffer--;
        while (*buffer == '0') buffer--;
        if (*buffer == '.') buffer--;
        while ((*++buffer = *stop++));
    }
}

#define EXPAND(x) {x, (int)sizeof(x) }

#if !defined(DoubleU64)
#define DoubleU64(x)                (*( (uint64_t*) &(x) ))
#define DoubleU64ExpMask            ((uint64_t)0x7FF << 52)
#define DoubleU64FracMask           (((uint64_t)1 << 52) - (uint64_t)1)
#define DoubleU64SignMask           ((uint64_t)1 << 63)
#endif

namespace ScalarType
{
    typedef enum  { 
        VALID = 0,
        DEN,
        INF_PLUS,
        INF_MINUS,
        NAN_PLUS,
        NAN_MINUS,
        MAX
    } Enum;
}

inline ScalarType::Enum GetScalarType(double v)
{
    uint64_t u = DoubleU64(v);
    uint64_t me = (u & DoubleU64ExpMask);

    if (me == DoubleU64ExpMask) {
        if ((u & DoubleU64FracMask) == 0) {
            return (u & DoubleU64SignMask) ? ScalarType::INF_MINUS : ScalarType::INF_PLUS;
        } 
        return (u & DoubleU64SignMask) ?  ScalarType::NAN_MINUS : ScalarType::NAN_PLUS;
    } else if (me == 0) {
        return ScalarType::DEN;
    }

    return ScalarType::VALID;
}

struct NumberTypeSignature {
    const char* name;
    int len; // including null termination
} g_signatures[] = {
    EXPAND("valid"),
    EXPAND("#DEN"),
    EXPAND("1.#INF00"),
    EXPAND("-1.#INF00"),
    EXPAND("1.#NAN00"),
    EXPAND("-1.#NAN00"),
};

static_assert(dimof(g_signatures) == ScalarType::MAX, "g_signatures array out of sync with enum");

char *flt(char *str, double num, int size, int precision, char fmt, int flags)
{
    char tmp[CVTBUFSIZE];
    char c, sign;
    int n, i;

    ScalarType::Enum type = GetScalarType(num);
    if (type > ScalarType::DEN) {
        memcpy(str, g_signatures[type].name, g_signatures[type].len);
        return str + (g_signatures[type].len - 1);
    }

    // Left align means no zero padding
    if (flags & LEFT) flags &= ~ZEROPAD;

    // Determine padding and sign char
    c = (flags & ZEROPAD) ? '0' : ' ';
    sign = 0;
    if (flags & SIGN)
    {
        if (num < 0.0)
        {
            sign = '-';
            num = -num;
            size--;
        }
        else if (flags & PLUS)
        {
            sign = '+';
            size--;
        }
        else if (flags & SPACE)
        {
            sign = ' ';
            size--;
        }
    }

    // Compute the precision value
    if (precision < 0)
        precision = 6; // Default precision: 6
    else if (precision == 0 && fmt == 'g')
        precision = 1; // ANSI specified

    // Convert floating point number to text
    cfltcvt(num, tmp, fmt, precision);

    // '#' and precision == 0 means force a decimal point
    if ((flags & SPECIAL) && precision == 0) forcdecpt(tmp);

    // 'g' format means crop zero unless '#' given
    if (fmt == 'g' && !(flags & SPECIAL)) cropzeros(tmp);

    n = (int)strlen(tmp);

    // Output number with alignment and padding
    size -= n;
    if (!(flags & (ZEROPAD | LEFT))) while (size-- > 0) *str++ = ' ';
    if (sign) *str++ = sign;
    if (!(flags & LEFT)) while (size-- > 0) *str++ = c;
    for (i = 0; i < n; i++) *str++ = tmp[i];
    while (size-- > 0) *str++ = ' ';

    return str;
}



//
// fcvt.c
//
// Floating point to string conversion routines
//
// Copyright (C) 2002 Michael Ringgaard. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 
// 1. Redistributions of source code must retain the above copyright 
//    notice, this list of conditions and the following disclaimer.  
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.  
// 3. Neither the name of the project nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
// SUCH DAMAGE.
// 

//
// cvt.c - IEEE floating point formatting routines for FreeBSD
// from GNU libc-4.6.27
//

char *cvt(double arg, int ndigits, int *decpt, int *sign, char *buf, int eflag)
{
    int r2;
    double fi, fj;
    char *p, *p1;

    if (ndigits < 0) ndigits = 0;
    if (ndigits >= CVTBUFSIZE - 1) ndigits = CVTBUFSIZE - 2;
    r2 = 0;
    *sign = 0;
    p = &buf[0];
    if (arg < 0)
    {
        *sign = 1;
        arg = -arg;
    }
    arg = modf(arg, &fi);
    p1 = &buf[CVTBUFSIZE];

    if (fi != 0) 
    {
        p1 = &buf[CVTBUFSIZE];
        while (fi != 0) 
        {
            fj = modf(fi / 10, &fi);
            assert(p1 >= buf + 1);
            *--p1 = char((int)((fj + .03) * 10) + '0');
            r2++;
        }
        while (p1 < &buf[CVTBUFSIZE]) *p++ = *p1++;
    } 
    else if (arg > 0)
    {
        while ((fj = arg * 10) < 1) 
        {
            arg = fj;
            r2--;
        }
    }
    p1 = &buf[ndigits];
    if (eflag == 0) p1 += r2;
    *decpt = r2;
    if (p1 < &buf[0]) 
    {
        buf[0] = '\0';
        return buf;
    }
    while (p <= p1 && p < &buf[CVTBUFSIZE])
    {
        arg *= 10;
        arg = modf(arg, &fj);
        *p++ = char((int) fj + '0');
    }
    if (p1 >= &buf[CVTBUFSIZE]) 
    {
        buf[CVTBUFSIZE - 1] = '\0';
        return buf;
    }
    p = p1;
    *p1 += 5;
    while (*p1 > '9') 
    {
        *p1 = '0';
        if (p1 > buf)
            ++*--p1;
        else 
        {
            *p1 = '1';
            (*decpt)++;
            if (eflag == 0) 
            {
                if (p > buf) *p = '0';
                p++;
            }
        }
    }
    *p = '\0';
    return buf;
}

//char *ecvt(double arg, int ndigits, int *decpt, int *sign)
//{
//    return cvt(arg, ndigits, decpt, sign, gettib()->cvtbuf, 1);
//}

char *ecvtbuf(double arg, int ndigits, int *decpt, int *sign, char *buf)
{
    return cvt(arg, ndigits, decpt, sign, buf, 1);
}

//char *fcvt(double arg, int ndigits, int *decpt, int *sign)
//{
//    return cvt(arg, ndigits, decpt, sign, gettib()->cvtbuf, 0);
//}

char *fcvtbuf(double arg, int ndigits, int *decpt, int *sign, char *buf)
{
    return cvt(arg, ndigits, decpt, sign, buf, 0);
}

}// end of namespace fmt

using namespace fmt;

int xl_vsprintf(char *buf, const char *fmt, va_list args)
{
    size_t len;
    unsigned long num;
    int i, base;
    char *str;
    char *s;

    int flags;            // Flags to number()

    int field_width;      // Width of output field
    int precision;        // Min. # of digits for integers; max number of chars for from string
    int qualifier;        // 'h', 'l', or 'L' for integer fields
    
    for (str = buf; *fmt; fmt++)
    {
        if (*fmt != '%')
        {
            *str++ = *fmt;
            continue;
        }

        // Process flags
        flags = 0;
repeat:
        fmt++; // This also skips first '%'
        switch (*fmt)
        {
        case '-': flags |= LEFT; goto repeat;
        case '+': flags |= PLUS; goto repeat;
        case ' ': flags |= SPACE; goto repeat;
        case '#': flags |= SPECIAL; goto repeat;
        case '0': flags |= ZEROPAD; goto repeat;
        }

        // Get field width
        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*')
        {
            fmt++;
            field_width = va_arg(args, int);
            if (field_width < 0)
            {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        // Get the precision
        precision = -1;
        if (*fmt == '.')
        {
            ++fmt;    
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*')
            {
                ++fmt;
                precision = va_arg(args, int);
            }
            if (precision < 0) precision = 0;
        }

        // Get the conversion qualifier
        qualifier = -1;

        if (*fmt == 'l') {
            /*if (*++fmt == 'l') {
                qualifier = 'll';
                ++fmt;
            } else*/ {
                qualifier = 'l';
            }
        } else if (*fmt == 'h' ||  *fmt == 'L') {
            qualifier = *fmt;
            fmt++;
        }

        // Default base
        base = 10;

        switch (*fmt)
        {
        case 'c':
            if (!(flags & LEFT)) while (--field_width > 0) *str++ = ' ';
            *str++ = (unsigned char) va_arg(args, int);
            while (--field_width > 0) *str++ = ' ';
            continue;

        case 's':
        {
            s = va_arg(args, char *);
            if (!s) s = null_str;
            len = strnlen(s, precision);
            if (!(flags & LEFT)) while (int(len) < field_width--) *str++ = ' ';
            for (i = 0; i < int(len); ++i) *str++ = *s++;
            while (int(len) < field_width--) *str++ = ' ';
            continue;
        }    
        case 'p':
            if (field_width == -1)
            {
                field_width = 2 * sizeof(void *);
                flags |= ZEROPAD;
            }
            str = number(str, (long)(size_t)va_arg(args, void *), 16, field_width, precision, flags);
            continue;

        case 'n':
            if (qualifier == 'l')
            {
                long *ip = va_arg(args, long *);
                *ip = (long)(str - buf);
            }
            else
            {
                int *ip = va_arg(args, int *);
                *ip = (int)(str - buf);
            }
            continue;

        case 'A':
            flags |= LARGE;

        case 'a':
            if (qualifier == 'l')
                str = eaddr(str, va_arg(args, unsigned char *), field_width, precision, flags);
            else
                str = iaddr(str, va_arg(args, unsigned char *), field_width, precision, flags);
            continue;

            // Integer number formats - set up the flags and "break"
        case 'o':
            base = 8;
            break;

        case 'X':
            flags |= LARGE;

        case 'x':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;

        case 'u':
            break;

        case 'E':
        case 'G':
        case 'e':
        case 'f':
        case 'g':
            str = flt(str, va_arg(args, double), field_width, precision, *fmt, flags | SIGN);
            continue;

        default:
            if (*fmt != '%') *str++ = '%';
            if (*fmt)
                *str++ = *fmt;
            else
                --fmt;
            continue;
        }

        if (qualifier == 'l')
            num = va_arg(args, unsigned long);
        else if (qualifier == 'h')
        {
            if (flags & SIGN)
                num = va_arg(args, int);
            else
                num = va_arg(args, unsigned int);
        } /*else if (qualifier == 'll') {
            uint64_t num64 = va_arg(args, uint64_t);
            s = number64(str, num64, base, field_width, precision, flags);
            *s = '\0';

            continue;
        }*/
        else if (flags & SIGN)
            num = va_arg(args, int);
        else
            num = va_arg(args, unsigned int);

        str = number(str, num, base, field_width, precision, flags);
    }

    *str = '\0';
    return int(str - buf);
}


int xl_sprintf(char *buf, const char *fmt, ...)
{
    va_list args;
    int n;

    va_start(args, fmt);
    n = xl_vsprintf(buf, fmt, args);
    va_end(args);

    return n;
}

int xl_vsnprintf(char* buf, int count, const char* fmt, va_list args)
{
    int len;
    unsigned long num;
    // uint64_t num64;
    int i, base;
    char *s;

    int flags;            // Flags to number()

    int field_width;      // Width of output field
    int precision;        // Min. # of digits for integers; max number of chars for from string
    int qualifier;        // 'h', 'l', or 'L' for integer fields

    FixedMemoryOutputStream<char> stream(buf, (size_t)count);

    char numbuf[CVTBUFSIZE]; // number buffer

    for (; *fmt && !stream.IsFull() ; fmt++)
    {
        if (*fmt != '%')
        {
            stream.WriteChar(*fmt);
            continue;
        }

        // Process flags
        flags = 0;
repeat:
        fmt++; // This also skips first '%'
        switch (*fmt)
        {
        case '-': flags |= LEFT; goto repeat;
        case '+': flags |= PLUS; goto repeat;
        case ' ': flags |= SPACE; goto repeat;
        case '#': flags |= SPECIAL; goto repeat;
        case '0': flags |= ZEROPAD; goto repeat;
        }

        // Get field width
        field_width = -1;
        if (is_digit(*fmt))
            field_width = skip_atoi(&fmt);
        else if (*fmt == '*')
        {
            fmt++;
            field_width = va_arg(args, int);
            if (field_width < 0)
            {
                field_width = -field_width;
                flags |= LEFT;
            }
        }

        // Get the precision
        precision = -1;
        if (*fmt == '.')
        {
            ++fmt;    
            if (is_digit(*fmt))
                precision = skip_atoi(&fmt);
            else if (*fmt == '*')
            {
                ++fmt;
                precision = va_arg(args, int);
            }
            if (precision < 0) precision = 0;
        }

        // Get the conversion qualifier
        qualifier = -1;

        if (*fmt == 'l') {
            /*if (*++fmt == 'l') {
                qualifier = 'll';
                ++fmt;
            } else*/ {
                qualifier = 'l';
            }
        } else if (*fmt == 'h' ||  *fmt == 'L') {
            qualifier = *fmt;
            fmt++;
        }

        // Default base
        base = 10;

        switch (*fmt)
        {
        case 'c':
            if (!(flags & LEFT)) while (--field_width > 0) { stream.WriteChar(' '); }
            stream.WriteChar((char)va_arg(args, int));
            while (--field_width > 0) { stream.WriteChar(' '); }
            continue;

        case 's':
            s = va_arg(args, char *);
            if (!s) s = null_str;
            len = (int)strnlen(s, precision);
            if (!(flags & LEFT)) while (len < field_width--) { stream.WriteChar(' '); }
            for (i = 0; i < len; ++i) { stream.WriteChar(*s++); }
            while (len < field_width--) { stream.WriteChar(' '); }
            continue;

        case 'p':
            if (field_width == -1)
            {
                field_width = 2 * sizeof(void *);
                flags |= ZEROPAD;
            }
            s = number(numbuf, (long)(size_t)va_arg(args, void *), 16, field_width, precision, flags);
            *s = '\0';
            stream.Write((const utf8*)numbuf);
            continue;

        case 'n':
            if (qualifier == 'l')
            {
                long *ip = va_arg(args, long *);
                *ip = (long)stream.Tell();
            }
            else
            {
                int *ip = va_arg(args, int *);
                *ip = (int)stream.Tell();
            }
            continue;

        case 'A':
            flags |= LARGE;

        case 'a':
            if (qualifier == 'l') {
                s = eaddr(numbuf, va_arg(args, unsigned char *), field_width, precision, flags);
                *s = '\0';
                stream.Write((const utf8*)numbuf);
            } else {
                s = iaddr(numbuf, va_arg(args, unsigned char *), field_width, precision, flags);
                *s = '\0';
                stream.Write((const utf8*)numbuf);
            }
            continue;

            // Integer number formats - set up the flags and "break"
        case 'o':
            base = 8;
            break;

        case 'X':
            flags |= LARGE;

        case 'x':
            base = 16;
            break;

        case 'd':
        case 'i':
            flags |= SIGN;

        case 'u':
            break;

        case 'E':
        case 'G':
        case 'e':
        case 'f':
        case 'g':
            s = flt(numbuf, va_arg(args, double), field_width, precision, *fmt, flags | SIGN);
            *s = '\0';
            stream.Write((const utf8*)numbuf); 
            continue;

        default:
            if (*fmt != '%') {stream.WriteChar('%'); stream.WriteChar('s');}  // stream.WriteChar((char)'%s'); // DavidJ -- bug...?
            if (*fmt) {
                stream.WriteChar(*fmt);
            }
            else
                --fmt;
            continue;
        }

        if (qualifier == 'l')
            num = va_arg(args, unsigned long);
        else if (qualifier == 'h')
        {
            if (flags & SIGN)
                num = va_arg(args, int);
            else
                num = va_arg(args, unsigned int);
        } /*else if (qualifier == 'll') {
            num64 = va_arg(args, uint64_t);
            s = number64(numbuf, num64, base, field_width, precision, flags);
            *s = '\0';
            stream.Write((const utf8*)numbuf); 
            continue;
        } */else if (flags & SIGN)
            num = va_arg(args, int);
        else
            num = va_arg(args, unsigned int);

        s = number(numbuf, num, base, field_width, precision, flags);
        *s = '\0';
        stream.Write((const utf8*)numbuf); 
    }

    stream.WriteChar(0);

    return (int)stream.Tell();
}

}

