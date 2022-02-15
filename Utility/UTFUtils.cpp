// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UTFUtils.h"
#include "StringUtils.h"
#include "StringFormat.h"
#include <stdlib.h>

namespace Utility
{
	#define UTF_REPLACEMENT_CHAR    (ucs4)0x0000FFFD
	#define UTF_MAX_BMP             (ucs4)0x0000FFFF
	#define UTF_MAX_UTF16           (ucs4)0x0010FFFF
	#define UTF_MAX_UTF32           (ucs4)0x7FFFFFFF
	#define UTF_MAX_LEGAL_UTF32     (ucs4)0x0010FFFF

	enum ucs_conv_error {
		UCE_OK = 0,
		UCE_SRC_EXHAUSTED = -1,
		UCE_DST_EXHAUSTED = -2,
		UCE_ILLEGAL = -2
	};

// is c the start of a utf8 sequence ?
#define isutf(c) (((uint8_t(c))&0xC0) != 0x80)

// remark_warning("don't use XlFormatString here!")
// begin -- for ucs2 and above
static const int HALF_SHIFT  = 10; // 

static const ucs4 HALF_BASE = 0x00010000UL;
static const ucs4 HALF_MASK = 0x000003FFUL;

#define UNI_SUR_HIGH_START  (ucs4)0xD800
#define UNI_SUR_HIGH_END    (ucs4)0xDBFF
#define UNI_SUR_LOW_START   (ucs4)0xDC00
#define UNI_SUR_LOW_END     (ucs4)0xDFFF

enum ucs_conv_rule {
    UCR_STRICT = 0,
    UCR_LENIENT
};

static inline int octal_digit(utf8 c)
{
    return (c >= '0' && c <= '7');
}

// offsets from utf8
static const uint32 _offsets_magic[6] = 
{
    0x00000000UL, 0x00003080UL, 0x000E2080UL,
    0x03C82080UL, 0xFA082080UL, 0x82082080UL
};

// trailing bytes for utf8
static const uint8_t _trailing_bytes[256] =  
{
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

int utf8_seqlen(const utf8* s)
{
    return _trailing_bytes[(uint8_t)s[0]] + 1;
}

// return trailing bytes + first character (excluding trailing length 4 & 5)
int utf8_step(const char* utf8_str)
{
    static const char utf8_bytes[256] = {
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3, 4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
    };

    return utf8_bytes[static_cast<unsigned char>(*utf8_str)];
}

static bool IsValid(const uint8_t* src, size_t len) 
{
    uint8_t a;
    const uint8_t* srcptr = (const uint8_t*)src + len;
    switch (len) {
    default: return false;
    // everything else falls through when "true"... 
    case 4: if ((a = (uint8_t)(*--srcptr)) < 0x80 || a > 0xBF) return false;
    case 3: if ((a = (uint8_t)(*--srcptr)) < 0x80 || a > 0xBF) return false;
    case 2: if ((a = (uint8_t)(*--srcptr)) > 0xBF) return false;

    switch ((uint8_t)*src) {
        case 0xE0: if (a < 0xA0) return false; 
            break;
        case 0xED: if ((a < 0x80) || (a > 0x9F)) return false; 
            break;
        case 0xF0: if (a < 0x90) return false; 
            break;
        case 0xF4: if (a > 0x8F) return false; 
            break;
        default:   if (a < 0x80) return false;
    }

    case 1: if ((uint8_t)*src >= 0x80 && (uint8_t)*src < 0xC2) return false;
    }
    if ((uint8_t)*src > 0xF4) return false;
    return true;
}

#pragma warning(disable:4127)       // warning C4127: conditional expression is constant

bool IsValid(const uint8_t* s, const uint8_t* se) 
{
    size_t len;
    if (s == se) {
        return true;
    }
    while (true) {
        len = _trailing_bytes[(uint8_t)*s]+1;
        if (s+len > se) {
            return false;
        }
        if (!IsValid(s, len)) {
            return false;
        }
        s += len;
        if (s >= se) {
            return true;
        }
    }
}

int utf8_2_ucs4(const utf8* src, size_t sl, ucs4* dst, size_t dl)
{
    ucs4 ch;
    int nb;

    const uint8_t* s = (const uint8_t*)src;
    const uint8_t* se = (const uint8_t*)src + sl;
    const ucs4* de = dst + dl;
    ucs4* d = dst;

    ucs_conv_error err = UCE_OK;

    while (s < se) {
        nb = _trailing_bytes[*s];
        if (s + nb >= se) {
            err = UCE_SRC_EXHAUSTED;
            break;
        }

        if (!IsValid(s, nb + 1)) {
            err =  UCE_ILLEGAL;
            break;
        }

        ch = 0;
        switch (nb) {
        case 3: ch += *s++; ch <<= 6;
        case 2: ch += *s++; ch <<= 6;
        case 1: ch += *s++; ch <<= 6;
        case 0: ch += *s++;
        }

        ch -= _offsets_magic[nb];

        if (d >= de) {
            s -= (nb + 1);
            err = UCE_DST_EXHAUSTED;
            break;
        }

        if (ch == 0) {
            break;
        }
        *d++ = ch;
    }

    *d = '\0';

    if (err != UCE_OK) {
        return err;
    }

    return (int)(d - dst);
}

int ucs2_2_utf8(const ucs2* src, size_t sl, utf8* dst_, size_t dl)
{
    ucs4 ch;
    size_t i = 0;
    uint8_t* dst = (uint8_t*)dst_;
    const uint8_t* dend = dst + dl;

    while (i < sl) {
        ch = src[i];
        if (ch == 0) {
            break;
        }
        if (ch < 0x80) {
            if (dst >= dend) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = (uint8_t)ch;
        }
        else if (ch < 0x800) {
            if (dst >= dend - 1) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = uint8_t((ch >> 6) | 0xC0);
            *dst++ = uint8_t((ch & 0x3F) | 0x80);
        }
        else if (ch < 0x10000) {
            if (dst >= dend - 2) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = uint8_t((ch>>12) | 0xE0);
            *dst++ = uint8_t(((ch>>6) & 0x3F) | 0x80);
            *dst++ = uint8_t((ch & 0x3F) | 0x80);
        }
        else if (ch < 0x110000) {
            if (dst >= dend - 3) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = uint8_t((ch>>18) | 0xF0);
            *dst++ = uint8_t(((ch>>12) & 0x3F) | 0x80);
            *dst++ = uint8_t(((ch>>6) & 0x3F) | 0x80);
            *dst++ = uint8_t((ch & 0x3F) | 0x80);
        }
        i++;
    }

    if (dst < dend) {
        *dst = '\0';
    }
    return (int)i;
}

int ucs4_2_utf8(const ucs4* src, size_t sl, utf8* dst_, size_t dl)
{
    ucs4 ch;
    size_t i = 0;
    uint8_t* dst = (uint8_t*)dst_;
    const uint8_t* dend = dst + dl;

    while (i < sl) {
        ch = src[i];

        if (ch < 0x80) {
            if (dst >= dend) {
                return UCE_DST_EXHAUSTED;
            }
            if (ch == 0) {
                break;
            }
            *dst++ = uint8_t(ch);
        }
        else if (ch >= 0x80  && ch < 0x800) {
            if (dst >= dend - 1) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = uint8_t((ch >> 6)   | 0xC0);
            *dst++ = uint8_t((ch & 0x3F) | 0x80);
        }
        else if (ch >= 0x800 && ch < 0xFFFF) {
            if (dst >= dend - 2) {
                return UCE_DST_EXHAUSTED;
            }
            *dst++ = uint8_t(((ch >> 12)       ) | 0xE0);
            *dst++ = uint8_t(((ch >> 6 ) & 0x3F) | 0x80);
            *dst++ = uint8_t(((ch      ) & 0x3F) | 0x80);
        }
        i++;
    }

    if (dst < dend) {
        *dst = '\0';
    }
    return (int)i;
}

int utf8_2_ucs2(const utf8* src, size_t sl, ucs2* dst, size_t dl)
{
    const uint8_t* s = (const uint8_t*)src;
    const uint8_t* se = (const uint8_t*)src + sl;
    ucs2* d = dst;
    const ucs2* de = dst + dl;

    ucs_conv_rule flags = UCR_STRICT;
    ucs_conv_error err = UCE_OK;

    while (s < se) {
        ucs4 ch = 0;
        uint8_t nb = _trailing_bytes[*s];
        if (s + nb >= se) {
            err = UCE_SRC_EXHAUSTED;
            break;
        }

        if (!IsValid(s, nb + 1)) {
            err =  UCE_ILLEGAL;
            break;
        }

        switch (nb) {
            case 5: ch += *s++; ch <<= 6;
            case 4: ch += *s++; ch <<= 6;
            case 3: ch += *s++; ch <<= 6;
            case 2: ch += *s++; ch <<= 6;
            case 1: ch += *s++; ch <<= 6;
            case 0: ch += *s++;
        }
        ch -= _offsets_magic[nb];

        if (d >= de) {
            s -= (nb + 1); // Back up source pointer!
            err = UCE_DST_EXHAUSTED;
            break;
        }

        if (ch <= UTF_MAX_BMP) { // Target is a character <= 0xFFFF
            // utf-16 surrogate values are illegal in utf-32
            if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
                if (flags == UCR_STRICT) {
                    s -= (nb+1); // return to the illegal value itself
                    err = UCE_ILLEGAL;
                    break;
                } else {
                    *d++ = UTF_REPLACEMENT_CHAR;
                }
            } else {
                if (ch == 0) {
                    break;
                }
                *d++ = (ucs2)ch;
            }
        } else if (ch > UTF_MAX_UTF16) {
            if (flags == UCR_STRICT) {
                err = UCE_ILLEGAL;
                s -= (nb+1); // return to the start
                break; // Bail out; shouldn't continue
            } else {
                *d++ = UTF_REPLACEMENT_CHAR;
            }
        } else {
            // target is a character in range 0xFFFF - 0x10FFFF.
            if (d + 1 >= de) {
                s -= (nb+1); // Back up source pointer! 
                //result = targetExhausted; break;
                break;
            }
            ch -= HALF_BASE;
            *d++ = (ucs2)((ch >> HALF_SHIFT) + UNI_SUR_HIGH_START);
            *d++ = (ucs2)((ch & HALF_MASK) + UNI_SUR_LOW_START);
        }
    }

    *d = '\0';
    if (err != UCE_OK) {
        return err;
    }
    
    return (int) (d - dst);
}

int ucs4_2_ucs2(const ucs4* src, size_t sl, ucs2* dst, size_t dl)
{
    const ucs4* s = src;
    ucs2* d = dst;

    const ucs4* se = src + sl;
    const ucs2* de = dst + dl;

    ucs_conv_rule flags = UCR_STRICT;
    ucs_conv_error err = UCE_OK;

    while (s < se) {
        ucs4 ch;
        if (d >= de) {
            // target exhausted
            break;
        }
        ch = *s++;
        if (ch <= UTF_MAX_BMP) { // Target is a character <= 0xFFFF 
            // utf-16 surrogate values are illegal in utf-32; 0xffff or 0xfffe are both reserved values
            if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_LOW_END) {
                if (flags == UCR_STRICT) {
                    --s; // return to the illegal value itself
                    err = UCE_ILLEGAL;
                    break;
                } else {
                    *d++ = UTF_REPLACEMENT_CHAR;
                }
            } else {
                if (ch == 0) {
                    break;
                }
                *d++ = (ucs2)ch;
            }
        } else if (ch > UTF_MAX_LEGAL_UTF32) {
            if (flags == UCR_STRICT) {
                err = UCE_ILLEGAL;
                break;
            } else {
                *d++ = UTF_REPLACEMENT_CHAR;
            }
        } else {
            // target is a character in range 0xFFFF - 0x10FFFF.
            if (d + 1 >= de) {
                --s;
                err = UCE_DST_EXHAUSTED;
                break;
            }
            ch -= HALF_BASE;
            *d++ = (ucs2)((ch >> HALF_SHIFT) + UNI_SUR_HIGH_START);
            *d++ = (ucs2)((ch & HALF_MASK) + UNI_SUR_LOW_START);
        }
    }

    *d = '\0';

    if (err != UCE_OK) {
        return err;
    }

    return (int)(d - dst);
}

int ucs2_2_ucs4(const ucs2* src, size_t sl, ucs4* dst, size_t dl)
{
    const ucs2* s = src;
    ucs4* d = dst;

    const ucs2* se = src + sl;
    const ucs4* de = dst + dl;

    ucs4 ch, ch2;

    ucs_conv_rule flags = UCR_STRICT;
    ucs_conv_error err = UCE_OK;

    while (s < se) {
        const ucs2* os = s; //  In case we have to back up because of target overflow.
        ch = *s++;
        // If we have a surrogate pair, convert to UTF32 first.
        if (ch >= UNI_SUR_HIGH_START && ch <= UNI_SUR_HIGH_END) {
            // If the 16 bits following the high surrogate are in the source buffer...
            if (s < se) {
                ch2 = *s;
                // If it's a low surrogate, convert to UTF32. 
                if (ch2 >= UNI_SUR_LOW_START && ch2 <= UNI_SUR_LOW_END) {
                    ch = ((ch - UNI_SUR_HIGH_START) << HALF_SHIFT)
                        + (ch2 - UNI_SUR_LOW_START) + HALF_BASE;
                    ++s;
                } else {
                    if (flags == UCR_STRICT) { // it's an unpaired high surrogate
                        --s;
                        err = UCE_ILLEGAL;
                        break;
                    } /*else {
                        *d++ = UNI_REPLACEMENT_CHAR;
                    }*/
                }
            } else { // We don't have the 16 bits following the high surrogate.
                --s;
                err = UCE_SRC_EXHAUSTED;
                break;
            }
        } else if (flags == UCR_STRICT) {
            // utf-16 surrogate values are illegal in utf-32 
            if (ch >= UNI_SUR_LOW_START && ch <= UNI_SUR_LOW_END) {
                --s;
                err = UCE_ILLEGAL;
                break;
            }
        }

        if (d >= de) {
            s = os;
            err = UCE_DST_EXHAUSTED;
            break;
        }
        if (ch == 0) {
            break;
        }
        *d++ = ch;
    }

    *d = '\0';
    if (err != UCE_OK) {
        return err;
    }

    return (int)(d - dst);
}

int ucs2_2_utf8(ucs2 ch, utf8* dst_)
{
    uint8_t* dst = (uint8_t*)dst_;
    if (ch < 0x80) {
        dst[0] = (ch & 0xFF);
        dst[1] = '\0';
        return 1;
    }
    if (ch >= 0x80  && ch < 0x800) {
        dst[0] = uint8_t((ch >> 6)   | 0xC0);
        dst[1] = uint8_t((ch & 0x3F) | 0x80);
        dst[2] = uint8_t('\0');
        return 2;
    }
    if (ch >= 0x800 && ch < 0xFFFF) {
        dst[0] = uint8_t(((ch >> 12)       ) | 0xE0);
        dst[1] = uint8_t(((ch >> 6 ) & 0x3F) | 0x80);
        dst[2] = uint8_t(((ch      ) & 0x3F) | 0x80);
        dst[3] = uint8_t('\0');
        return 3;
    }
    return -1;
}

int ucs4_2_utf8(ucs4 ch, utf8* dst_)
{
    uint8_t* dst = (uint8_t*)dst_;
    if (ch < 0x80) {
        dst[0] = (uint8_t)ch;
        return 1;
    }
    if (ch < 0x800) {
        dst[0] = uint8_t((ch>>6) | 0xC0);
        dst[1] = uint8_t((ch & 0x3F) | 0x80);
        return 2;
    }
    if (ch < 0x10000) {
        dst[0] = uint8_t((ch>>12) | 0xE0);
        dst[1] = uint8_t(((ch>>6) & 0x3F) | 0x80);
        dst[2] = uint8_t((ch & 0x3F) | 0x80);
        return 3;
    }
    if (ch < 0x110000) {
        dst[0] = uint8_t((ch>>18) | 0xF0);
        dst[1] = uint8_t(((ch>>12) & 0x3F) | 0x80);
        dst[2] = uint8_t(((ch>>6) & 0x3F) | 0x80);
        dst[3] = uint8_t((ch & 0x3F) | 0x80);
        return 4;
    }
    return 0;
}

size_t utf8_nth_offset(const utf8* s, size_t N)
{
    size_t offset = 0;

    while (N > 0 && s[offset]) {
        (void)(isutf(s[++offset]) || isutf(s[++offset]) ||
            isutf(s[++offset]) || ++offset);
        N--;
    }
    return offset;
}

size_t utf8_offset_ord(const utf8* s, size_t offset)
{
    size_t N = 0;
    size_t offs = 0;

    while (offs < offset && s[offs]) {
        (void)(isutf(s[++offs]) || isutf(s[++offs]) ||
            isutf(s[++offs]) || ++offs);
        N++;
    }
    return N;
}

ucs4 utf8_nextchar(const utf8* src, size_t* i)
{
    if (!src[(*i)]) return 0;

    const uint8_t* s = (const uint8_t*)src;
    ucs4 ch = 0;
    size_t l = 0;

    do {
        ch <<= 6;
        ch += s[(*i)++];
        l++;
    } while (s[*i] && !isutf(s[*i]));

    ch -= _offsets_magic[l - 1];

    return ch;
}

ucs4 utf8_nextchar(utf8 const*& iterator, utf8 const* end)
{
	ucs4 ch = 0;
	size_t l = 0;

	do {
		ch <<= 6;
		ch += (uint8_t)*iterator++;
		l++;
	} while (iterator < end && !isutf(*iterator));

	ch -= _offsets_magic[l - 1];

	return ch;
}

size_t utf8_strlen(const utf8* s)
{
    size_t cnt = 0;
    size_t i = 0;

    while (utf8_nextchar(s, &i) != 0)
        cnt++;

    return cnt;
}


void utf8_inc(const utf8* s, size_t* i)
{
    (void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) ||
           isutf(s[++(*i)]) || ++(*i));
}

void utf8_dec(const utf8* s, size_t*i)
{
    (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) ||
           isutf(s[--(*i)]) || --(*i));
}

int utf8_read_escape_sequence(char* str, uint32* dst)
{
    uint32 ch;
    char digs[9]="\0\0\0\0\0\0\0\0";
    int dno=0, i=1;

    ch = (uint32)str[0];    // take literal character
    if (str[0] == 'n')
        ch = L'\n';
    else if (str[0] == 't')
        ch = L'\t';
    else if (str[0] == 'r')
        ch = L'\r';
    else if (str[0] == 'b')
        ch = L'\b';
    else if (str[0] == 'f')
        ch = L'\f';
    else if (str[0] == 'v')
        ch = L'\v';
    else if (str[0] == 'a')
        ch = L'\a';
    else if (octal_digit(str[0])) {
        i = 0;
        do {
            digs[dno++] = str[i++];
        } while (octal_digit(str[i]) && dno < 3);
        ch = (uint32)strtol(digs, NULL, 8);
    }
    else if (str[0] == 'x') {
        while (XlIsHex(str[i]) && dno < 2) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    else if (str[0] == 'u') {
        while (XlIsHex(str[i]) && dno < 4) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    else if (str[0] == 'U') {
        while (XlIsHex(str[i]) && dno < 8) {
            digs[dno++] = str[i++];
        }
        if (dno > 0)
            ch = (uint32)strtol(digs, NULL, 16);
    }
    *dst = ch;

    return i;
}

// convert '\u' prefixed string 
size_t utf8_unescape(utf8* buf, size_t dim, char *src)
{
    size_t c=0, amt;
    uint32 ch;

    utf8 temp[4];

    while (*src && c < dim) {
        if (*src == '\\') {
            src++;
            amt = utf8_read_escape_sequence(src, &ch);
        }
        else {
            ch = (uint32)*src;
            amt = 1;
        }
        src += amt;
        amt = ucs4_2_utf8(ch, temp);
        if (amt > dim-c)
            break;
        memcpy(&buf[c], temp, amt);
        c += amt;
    }

    if (c < dim)
        buf[c] = '\0';
    return c;
}

size_t utf8_escape_wchar(utf8* buf, size_t dim, uint32 ch)
{
    if (ch == L'\n')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\n");
    else if (ch == L'\t')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\t");
    else if (ch == L'\r')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\r");
    else if (ch == L'\b')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\b");
    else if (ch == L'\f')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\f");
    else if (ch == L'\v')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\v");
    else if (ch == L'\a')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\a");
    else if (ch == L'\\')
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\\\");
    else if (ch < 32 || ch == 0x7f)
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\x%hhX", (uint8_t)ch);
    else if (ch > 0xFFFF)
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\U%.8X", (uint32)ch);
    else if (ch >= 0x80 && ch <= 0xFFFF)
        return (size_t)xl_snprintf((char*)buf, (int)dim, "\\u%.4hX", (uint16)ch);

    return (size_t)xl_snprintf((char*)buf, (int)dim, "%c", (char)ch);
}

size_t utf8_escape(utf8* buf, size_t dim, char* src, int escape_quotes)
{
    size_t c=0, i=0, amt;

    while (src[i] && c < dim) {
        if (escape_quotes && src[i] == '"') {
            amt = (size_t)xl_snprintf((char*)buf, int(dim - c), "\\\"");
            i++;
        }
        else {
            amt = (size_t)utf8_escape_wchar(buf, int(dim - c), utf8_nextchar((const utf8*)src, &i));
        }
        c += amt;
        buf += amt;
    }
    if (c < dim)
        *buf = '\0';
    return c;
}

const utf8* utf8_strchr(const utf8* s, ucs4 ch, int& ord)
{
    ucs4 c;
    size_t i = 0;
    size_t last = 0;

    ord = 0;

    while (s[i]) {
        c = utf8_nextchar(s, &i);
        if (c == ch) {
            return &s[last];
        }
        last = i;

        ord++;
    }
    return NULL;
}

const utf8* utf8_memchr(const utf8* s, ucs4 ch, size_t n, int& ord)
{
    ucs4 c;
    size_t i = 0;
    size_t last = 0;
    size_t csz;

    ord = 0;

    while (i < n) {
        c = 0; csz = 0;

        do {
            c <<= 6;
            c += (uint8_t)s[i++];
            csz++;
        } while (i < n && !isutf(s[i]));

        c -= _offsets_magic[csz - 1];

        if (c == ch) {
            return &s[last];
        }

        last = i;
        ord++;
    }

    return NULL;
}

int utf8_is_locale_utf8(char *locale)
{
    const char* cp = locale;

    for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++) {
        if (*cp == '.') {
            const char* encoding = ++cp;
            for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++)
                ;
            if ((cp - encoding == 5 && !strncmp(encoding, "UTF-8", 5))
                || (cp - encoding == 4 && !strncmp(encoding, "utf8", 4)))
                return 1;
            break;
        }
    }

    return 0;
}

size_t utf8_vprintf(const char* fmt, va_list ap)
{
    size_t cnt, sz=0;
    utf8* buf;
    ucs4* wcs;

    sz = 2048; // remark_todo("fixme!")
    buf = (utf8*)alloca(sz);
 try_print:
    cnt = (size_t)xl_vsnprintf((char*)buf, (int)sz, fmt, ap);
    if (cnt >= sz) {
        buf = (utf8*)alloca(cnt - sz + 1);
        sz = cnt + 1;
        goto try_print;
    }
    wcs = (ucs4*)alloca((cnt+1) * sizeof(ucs4));
    cnt = utf8_2_ucs4(buf, cnt, wcs, cnt+1); // right???
    printf("%ls", (wchar_t*)wcs);
    return cnt;
}

size_t utf8_printf(const char* fmt, ...)
{
    size_t cnt;
    va_list args;

    va_start(args, fmt);

    cnt = utf8_vprintf(fmt, args);

    va_end(args);
    return cnt;
}

}

