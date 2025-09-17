// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ArithmeticUtils.h"
#include <stdio.h>

namespace Utility
{

    void printbits(const void* blob, int len)
    {
        const uint8_t* data = (const uint8_t*)blob;

        printf("[");

        for (int i = 0; i < len; i++) {
            unsigned char byte = data[i];

            int hi = (byte >> 4);
            int lo = (byte & 0xF);

            if (hi) {printf("%01x", hi);} else {printf(".");}

            if (lo) {printf("%01x", lo);} else {printf(".");}

            if (i != len - 1) {printf(" ");}
        }
        printf("]");
    }

    void printbits2(const uint8_t* k, int nbytes)
    {
        printf("[");

        for (int i = nbytes - 1; i >= 0; i--) {
            uint8_t b = k[i];

            for (int j = 7; j >= 0; j--) {
                uint8_t c = (b & (1 << j)) ? '#' : ' ';

                putc(c, stdout);
            }
        }
        printf("]");
    }

    void printhex32(const void* blob, int len)
    {
        assert((len & 3) == 0);

        uint32_t* d = (uint32_t*)blob;

        printf("{ ");

        for (int i = 0; i < len / 4; i++) {
            printf("0x%08x, ", d[i]);
        }

        printf("}");
    }

    void printbytes(const void* blob, int len)
    {
        uint8_t* d = (uint8_t*)blob;

        printf("{ ");

        for (int i = 0; i < len; i++) {
            printf("0x%02x, ", d[i]);
        }

        printf(" };");
    }

    void printbytes2(const void* blob, int len)
    {
        uint8_t* d = (uint8_t*)blob;

        for (int i = 0; i < len; i++) {
            printf("%02x ", d[i]);
        }
    }

    void lshift1(void* blob, int len, int c)
    {
        int nbits = len * 8;

        for (int i = nbits - 1; i >= 0; i--) {
            xl_setbit(blob, len, i, getbit(blob, len, i - c));
        }
    }


    void lshift8(void* blob, int nbytes, int c)
    {
        uint8_t* k = (uint8_t*)blob;

        if (c == 0) {return;}

        int b2 = c >> 3;
        c &= 7;

        for (int i = nbytes - 1; i >= b2; i--) {
            k[i] = k[i - b2];
        }

        for (int i = b2 - 1; i >= 0; i--) {
            k[i] = 0;
        }

        if (c == 0) {return;}

        for (int i = nbytes - 1; i >= 0; i--) {
            uint8_t a = k[i];
            uint8_t b = (i == 0) ? 0 : k[i - 1];

            k[i] = (a << c) | (b >> (8 - c));
        }
    }

    void lshift32(void* blob, int len, int c)
    {
        assert((len & 3) == 0);

        int nbytes = len;
        int ndwords = nbytes / 4;

        uint32_t* k = reinterpret_cast<uint32_t*>(blob);

        if (c == 0) {return;}

        //----------

        int b2 = c / 32;
        c &= (32 - 1);

        for (int i = ndwords - 1; i >= b2; i--) {
            k[i] = k[i - b2];
        }

        for (int i = b2 - 1; i >= 0; i--) {
            k[i] = 0;
        }

        if (c == 0) {return;}

        for (int i = ndwords - 1; i >= 0; i--) {
            uint32_t a = k[i];
            uint32_t b = (i == 0) ? 0 : k[i - 1];

            k[i] = (a << c) | (b >> (32 - c));
        }
    }

    //-----------------------------------------------------------------------------

    void rshift1(void* blob, int len, int c)
    {
        int nbits = len * 8;

        for (int i = 0; i < nbits; i++) {
            xl_setbit(blob, len, i, getbit(blob, len, i + c));
        }
    }

    void rshift8(void* blob, int nbytes, int c)
    {
        uint8_t* k = (uint8_t*)blob;

        if (c == 0) {return;}

        int b2 = c >> 3;
        c &= 7;

        for (int i = 0; i < nbytes - b2; i++) {
            k[i] = k[i + b2];
        }

        for (int i = nbytes - b2; i < nbytes; i++) {
            k[i] = 0;
        }

        if (c == 0) {return;}

        for (int i = 0; i < nbytes; i++) {
            uint8_t a = (i == nbytes - 1) ? 0 : k[i + 1];
            uint8_t b = k[i];

            k[i] = (a << (8 - c)) | (b >> c);
        }
    }

    void rshift32(void* blob, int len, int c)
    {
        assert((len & 3) == 0);

        int nbytes = len;
        int ndwords = nbytes / 4;

        uint32_t* k = (uint32_t*)blob;

        //----------

        if (c == 0) {return;}

        int b2 = c / 32;
        c &= (32 - 1);

        for (int i = 0; i < ndwords - b2; i++) {
            k[i] = k[i + b2];
        }

        for (int i = ndwords - b2; i < ndwords; i++) {
            k[i] = 0;
        }

        if (c == 0) {return;}

        for (int i = 0; i < ndwords; i++) {
            uint32_t a = (i == ndwords - 1) ? 0 : k[i + 1];
            uint32_t b = k[i];

            k[i] = (a << (32 - c)) | (b >> c);
        }
    }

    //-----------------------------------------------------------------------------

    void lrot1(void* blob, int len, int c)
    {
        int nbits = len * 8;

        for (int i = 0; i < c; i++) {
            uint32_t bit = getbit(blob, len, nbits - 1);

            lshift1(blob, len, 1);

            xl_setbit(blob, len, 0, bit);
        }
    }

    void lrot8(void* blob, int len, int c)
    {
        int nbytes = len;

        uint8_t* k = (uint8_t*)blob;

        if (c == 0) {return;}

        //----------

        int b2 = c / 8;
        c &= (8 - 1);

        for (int j = 0; j < b2; j++) {
            uint8_t t = k[nbytes - 1];

            for (int i = nbytes - 1; i > 0; i--) {
                k[i] = k[i - 1];
            }

            k[0] = t;
        }

        uint8_t t = k[nbytes - 1];

        if (c == 0) {return;}

        for (int i = nbytes - 1; i >= 0; i--) {
            uint8_t a = k[i];
            uint8_t b = (i == 0) ? t : k[i - 1];

            k[i] = (a << c) | (b >> (8 - c));
        }
    }

    void lrot32(void* blob, int len, int c)
    {
        assert((len & 3) == 0);

        int nbytes = len;
        int ndwords = nbytes / 4;

        uint32_t* k = (uint32_t*)blob;

        if (c == 0) {return;}

        //----------

        int b2 = c / 32;
        c &= (32 - 1);

        for (int j = 0; j < b2; j++) {
            uint32_t t = k[ndwords - 1];

            for (int i = ndwords - 1; i > 0; i--) {
                k[i] = k[i - 1];
            }

            k[0] = t;
        }

        uint32_t t = k[ndwords - 1];

        if (c == 0) {return;}

        for (int i = ndwords - 1; i >= 0; i--) {
            uint32_t a = k[i];
            uint32_t b = (i == 0) ? t : k[i - 1];

            k[i] = (a << c) | (b >> (32 - c));
        }
    }

    //-----------------------------------------------------------------------------

    void rrot1(void* blob, int len, int c)
    {
        int nbits = len * 8;

        for (int i = 0; i < c; i++) {
            uint32_t bit = getbit(blob, len, 0);

            rshift1(blob, len, 1);

            xl_setbit(blob, len, nbits - 1, bit);
        }
    }

    void rrot8(void* blob, int len, int c)
    {
        int nbytes = len;

        uint8_t* k = (uint8_t*)blob;

        if (c == 0) {return;}

        //----------

        int b2 = c / 8;
        c &= (8 - 1);

        for (int j = 0; j < b2; j++) {
            uint8_t t = k[0];

            for (int i = 0; i < nbytes - 1; i++) {
                k[i] = k[i + 1];
            }

            k[nbytes - 1] = t;
        }

        if (c == 0) {return;}

        //----------

        uint8_t t = k[0];

        for (int i = 0; i < nbytes; i++) {
            uint8_t a = (i == nbytes - 1) ? t : k[i + 1];
            uint8_t b = k[i];

            k[i] = (a << (8 - c)) | (b >> c);
        }
    }

    void rrot32(void* blob, int len, int c)
    {
        assert((len & 3) == 0);

        int nbytes = len;
        int ndwords = nbytes / 4;

        uint32_t* k = (uint32_t*)blob;

        if (c == 0) {return;}

        //----------

        int b2 = c / 32;
        c &= (32 - 1);

        for (int j = 0; j < b2; j++) {
            uint32_t t = k[0];

            for (int i = 0; i < ndwords - 1; i++) {
                k[i] = k[i + 1];
            }

            k[ndwords - 1] = t;
        }

        if (c == 0) {return;}

        //----------

        uint32_t t = k[0];

        for (int i = 0; i < ndwords; i++) {
            uint32_t a = (i == ndwords - 1) ? t : k[i + 1];
            uint32_t b = k[i];

            k[i] = (a << (32 - c)) | (b >> c);
        }
    }

    uint32_t window1(void* blob, int len, int start, int count)
    {
        int nbits = len * 8;
        start %= nbits;

        uint32_t t = 0;

        for (int i = 0; i < count; i++) {
            xl_setbit(&t, sizeof(t), i, getbit_wrap(blob, len, start + i));
        }

        return t;
    }

    uint32_t window8(void* blob, int len, int start, int count)
    {
        int nbits = len * 8;
        start %= nbits;

        uint32_t t = 0;
        uint8_t* k = (uint8_t*)blob;

        if (count == 0) {return 0;}

        int c = start & (8 - 1);
        int d = start / 8;

        for (int i = 0; i < 4; i++) {
            int ia = (i + d + 1) % len;
            int ib = (i + d + 0) % len;

            uint32_t a = k[ia];
            uint32_t b = k[ib];

            uint32_t m = (a << (8 - c)) | (b >> c);

            t |= (m << (8 * i));

        }

        t &= ((1 << count) - 1);

        return t;
    }

    uint32_t window32(void* blob, int len, int start, int count)
    {
        int nbits = len * 8;
        start %= nbits;

        assert((len & 3) == 0);

        int ndwords = len / 4;

        uint32_t* k = (uint32_t*)blob;

        if (count == 0) {return 0;}

        int c = start & (32 - 1);
        int d = start / 32;

        if (c == 0) {return (k[d] & ((1 << count) - 1));}

        int ia = (d + 1) % ndwords;
        int ib = (d + 0) % ndwords;

        uint32_t a = k[ia];
        uint32_t b = k[ib];

        uint32_t t = (a << (32 - c)) | (b >> c);

        t &= ((1 << count) - 1);

        return t;
    }

}

