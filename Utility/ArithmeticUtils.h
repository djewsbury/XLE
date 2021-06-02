// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include "../Core/Types.h"
#include "Detail/API.h"
#include <vector>
#include <assert.h>

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
    #include <intrin.h>
#endif

namespace Utility
{

    // clz := count leading zeros
    // ctz := count trailing zeros
    // bsf := bit scan forward
    // bsr := bit scan reverse

    // [caveats] in intel architecture, bsr & bsf are coressponds to
    //           machine instruction. so the following implementations are inefficient!

    uint32      xl_clz1(uint8 x);
    uint32      xl_ctz1(uint8 x);
    uint32      xl_clz2(uint16 x);
    uint32      xl_ctz2(uint16 x);
    uint32      xl_ctz4(uint32 x);
    uint32      xl_clz4(uint32 x);
    uint32      xl_ctz8(uint64 x);
    uint32      xl_clz8(uint64 x);

    uint32      xl_bsr1(uint16 x);
    uint32      xl_bsr2(uint16 x);
    uint32      xl_bsr4(uint32 x);
    uint32      xl_bsr8(uint64 x);
    uint32      xl_lg(size_t x);

    XL_UTILITY_API void  printbits(const void* blob, int len);
    XL_UTILITY_API void  printhex32(const void* blob, int len);
    XL_UTILITY_API void  printbytes(const void* blob, int len);
    XL_UTILITY_API void  printbytes2(const void* blob, int len);

    uint32      popcount(uint32 v);
    uint32      parity(uint32 v);
    int         countbits(uint32 v);
    int         countbits(std::vector<uint32>& v);
    int         countbits(const void* blob, int len);
    void        invert(std::vector<uint32>& v);

    uint32      getbit(const void* block, int len, uint32 bit);
    uint32      getbit_wrap(const void* block, int len, uint32 bit);
    template < typename T > inline uint32 getbit(T& blob, uint32 bit);

    void        xl_setbit(void* block, int len, uint32 bit);
    void        xl_setbit(void* block, int len, uint32 bit, uint32 val);
    template < typename T > inline void xl_setbit(T& blob, uint32 bit);

    void        xl_clearbit(void* block, int len, uint32 bit);

    void        flipbit(void* block, int len, uint32 bit);
    template < typename T > inline void flipbit(T& blob, uint32 bit);

    //-----------------------------------------------------------------------------
    // Left and right shift of blobs. The shift(N) versions work on chunks of N
    // bits at a time (faster)

    XL_UTILITY_API void      lshift1(void* blob, int len, int c);
    XL_UTILITY_API void      lshift8(void* blob, int len, int c);
    XL_UTILITY_API void      lshift32(void* blob, int len, int c);
    void                    lshift(void* blob, int len, int c);
    template < typename T > inline void lshift(T& blob, int c);

    XL_UTILITY_API void      rshift1(void* blob, int len, int c);
    XL_UTILITY_API void      rshift8(void* blob, int len, int c);
    XL_UTILITY_API void      rshift32(void* blob, int len, int c);
    void                    rshift(void* blob, int len, int c);
    template < typename T > inline void rshift(T& blob, int c);

    //-----------------------------------------------------------------------------
    // Left and right rotate of blobs. The rot(N) versions work on chunks of N
    // bits at a time (faster)

    XL_UTILITY_API void      lrot1(void* blob, int len, int c);
    XL_UTILITY_API void      lrot8(void* blob, int len, int c);
    XL_UTILITY_API void      lrot32(void* blob, int len, int c);
    void                    lrot(void* blob, int len, int c);
    template < typename T > inline void lrot(T& blob, int c);

    XL_UTILITY_API void      rrot1(void* blob, int len, int c);
    XL_UTILITY_API void      rrot8(void* blob, int len, int c);
    XL_UTILITY_API void      rrot32(void* blob, int len, int c);
    void                    rrot(void* blob, int len, int c);
    template < typename T > inline void rrot(T& blob, int c);

    //-----------------------------------------------------------------------------
    // Bit-windowing functions - select some N-bit subset of the input blob

    XL_UTILITY_API uint32    window1(void* blob, int len, int start, int count);
    XL_UTILITY_API uint32    window8(void* blob, int len, int start, int count);
    XL_UTILITY_API uint32    window32(void* blob, int len, int start, int count);
    uint32                  window(void* blob, int len, int start, int count);
    template < typename T > inline uint32 window(T& blob, int start, int count);

    // bit-scan
    #if COMPILER_ACTIVE == COMPILER_TYPE_MSVC

        #pragma intrinsic(_BitScanForward, _BitScanReverse)

        inline uint32 xl_ctz4(uint32 x)
        {
            unsigned long i = 0;
            if (!_BitScanForward(&i, (unsigned long)x)) {
                return 32;
            }
            return (uint32)i;
        }

        inline uint32 xl_clz4(uint32 x)
        {
            unsigned long i = 0;
            if (!_BitScanReverse(&i, (unsigned long)x)) {
                return 32;
            }
            return (uint32)(31 - i);
        }

        #if SIZEOF_PTR == 8

            #pragma intrinsic(_BitScanForward64, _BitScanReverse64)
            inline uint32 xl_ctz8(uint64 x)
            {
                unsigned long i = 0;
                if (!_BitScanForward64(&i, x)) {
                    return 64;
                }
                return (uint32)i;
            }

            inline uint32 xl_clz8(uint64 x)
            {
                unsigned long i = 0;
                if (!_BitScanReverse64(&i, x)) {
                    return 64;
                }
                return (uint32)(63 - i);
            }

        #else

            namespace Internal
            {
                union Int64U
                {
                    struct Comp
                    {
                        uint32 LowPart;
                        uint32 HighPart;
                    } comp;
                    uint64 QuadPart;
                };
            }

            inline uint32 xl_ctz8(uint64 x)
            {
                Internal::Int64U li;
                li.QuadPart = (uint64)x;
                uint32 i = xl_ctz4((uint32)li.comp.LowPart);
                if (i < 32) {
                    return i;
                }
                return xl_ctz4((uint32)li.comp.HighPart) + 32;
            }

            inline uint32 xl_clz8(uint64 x)
            {
                Internal::Int64U li;
                li.QuadPart = (uint64)x;
                uint32 i = xl_clz4((uint32)li.comp.HighPart);
                if (i < 32) {
                    return i;
                }
                return xl_clz4((uint32)li.comp.LowPart) + 32;
            }

        #endif

    #elif (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)

        inline uint32 xl_ctz4(uint32 x) { return __builtin_ctz(x); }
        inline uint32 xl_clz4(uint32 x) { return __builtin_clz(x); }
        inline uint32 xl_ctz8(uint64 x) { return __builtin_ctzll(x); }
        inline uint32 xl_clz8(uint64 x) { return __builtin_clzll(x); }

    #else

        #error 'Unsupported Compiler!'

    #endif

    inline uint32 xl_clz2(uint16 x)
    {
        uint32 i = xl_clz4(x);
        if (i == 32) {
            return 16;
        }
        //assert (i >= 16);
        return (i - 16);
    }

    inline uint32 xl_ctz2(uint16 x)
    {
        uint32 i = xl_ctz4(x);
        if (i == 32) {
            return 16;
        }
        //assert(i < 16);
        return i;
    }


    inline uint32 xl_clz1(uint8 x)
    {
        uint32 i = xl_clz4(x);
        if (i == 32) {
            return 8;
        }
        //assert (i >= 24);
        return (i - 24);
    }

    inline uint32 xl_ctz1(uint8 x)
    {
        uint32 i = xl_ctz4(x);
        if (i == 32) {
            return 8;
        }
        //assert(i < 8);
        return i;
    }

    #define xl_bsf1 xl_ctz1
    #define xl_bsf2 xl_ctz2
    #define xl_bsf4 xl_ctz4
    #define xl_bsf8 xl_ctz8

    inline uint32 xl_bsr1(uint16 x)
    {
        uint32 i = (uint32)xl_clz2(x);
        if (i == 8) {
            return 8;
        }
        return 7 - i;
    }

    inline uint32 xl_bsr2(uint16 x)
    {
        uint32 i = xl_clz2(x);
        if (i == 16) {
            return 16;
        }
        return 15 - i;
    }

    inline uint32 xl_bsr4(uint32 x)
    {
        uint32 i = xl_clz4(x);
        if (i == 32) {
            return 32;
        }
        return 31 - i;
    }

    inline uint32 xl_bsr8(uint64 x)
    {
        uint32 i = xl_clz8(x);
        if (i == 64) {
            return 64;
        }
        return 63 - i;
    }

    inline uint32 xl_lg(size_t x)
    {
        #if SIZEOF_PTR == 8
            return xl_bsr8(x);
        #else
            return xl_bsr4(x);
        #endif
    }

    //-----------------------------------------------------------------------------
    // Bit-level manipulation
    // These two are from the "Bit Twiddling Hacks" webpage
    inline uint32 popcount(uint32 v)
    {
        v = v - ((v >> 1) & 0x55555555);                    // reuse input as temporary
        v = (v & 0x33333333) + ((v >> 2) & 0x33333333);     // temp
        uint32 c = ((v + ((v >> 4) & 0xF0F0F0F)) * 0x1010101) >> 24; // count

        return c;
    }

    inline uint32 parity(uint32 v)
    {
        v ^= v >> 1;
        v ^= v >> 2;
        v = (v & 0x11111111U) * 0x11111111U;
        return (v >> 28) & 1;
    }

    inline uint32 getbit(const void* block, int len, uint32 bit)
    {
        uint8* b = (uint8*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {return (b[byte] >> bit) & 1;}

        return 0;
    }

    inline uint32 getbit_wrap(const void* block, int len, uint32 bit)
    {
        uint8* b = (uint8*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        byte %= len;

        return (b[byte] >> bit) & 1;
    }


    inline void xl_setbit(void* block, int len, uint32 bit)
    {
        unsigned char* b = (unsigned char*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] |= (1 << bit);}
    }

    inline void xl_clearbit(void* block, int len, uint32 bit)
    {
        uint8* b = (uint8*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] &= ~(1 << bit);}
    }

    inline void xl_setbit(void* block, int len, uint32 bit, uint32 val)
    {
        val ? xl_setbit(block, len, bit) : xl_clearbit(block, len, bit);
    }


    inline void flipbit(void* block, int len, uint32 bit)
    {
        uint8* b = (uint8*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] ^= (1 << bit);}
    }

    inline int countbits(uint32 v)
    {
		// (note -- this is the same as popcount)
        v = v - ((v >> 1) & 0x55555555);                    // reuse input as temporary
        v = (v & 0x33333333) + ((v >> 2) & 0x33333333);     // temp
        int c = ((v + ((v >> 4) & 0xF0F0F0F)) * 0x1010101) >> 24; // count

        return c;
    }

    //----------

    template < typename T > inline uint32 getbit(T& blob, uint32 bit)
    {
        return getbit(&blob, sizeof(blob), bit);
    }

    template <> inline uint32 getbit(uint32& blob, uint32 bit) { return (blob >> (bit & 31)) & 1; }
    template <> inline uint32 getbit(uint64& blob, uint32 bit) { return (blob >> (bit & 63)) & 1; }

    //----------

    template < typename T > inline void xl_setbit(T& blob, uint32 bit)
    {
        return xl_setbit(&blob, sizeof(blob), bit);
    }

    template <> inline void xl_setbit(uint32& blob, uint32 bit) { blob |= uint32(1) << (bit & 31); }
    template <> inline void xl_setbit(uint64& blob, uint32 bit) { blob |= uint64(1) << (bit & 63); }

    //----------

    template < typename T > inline void flipbit(T& blob, uint32 bit)
    {
        flipbit(&blob, sizeof(blob), bit);
    }

    template <> inline void flipbit(uint32& blob, uint32 bit)
    {
        bit &= 31;
        blob ^= (uint32(1) << bit);
    }
    template <> inline void flipbit(uint64& blob, uint32 bit)
    {
        bit &= 63;
        blob ^= (uint64(1) << bit);
    }

    // shift left and right

    inline void lshift(void* blob, int len, int c)
    {
        if ((len & 3) == 0) {
            lshift32(blob, len, c);
        } else {
            lshift8(blob, len, c);
        }
    }

    inline void rshift(void* blob, int len, int c)
    {
        if ((len & 3) == 0) {
            rshift32(blob, len, c);
        } else {
            rshift8(blob, len, c);
        }
    }

    template < typename T > inline void lshift(T& blob, int c)
    {
        if ((sizeof(T) & 3) == 0) {
            lshift32(&blob, sizeof(T), c);
        } else {
            lshift8(&blob, sizeof(T), c);
        }
    }

    template < typename T > inline void rshift(T& blob, int c)
    {
        if ((sizeof(T) & 3) == 0) {
            lshift32(&blob, sizeof(T), c);
        } else {
            lshift8(&blob, sizeof(T), c);
        }
    }

    template <> inline void lshift(uint32& blob, int c) { blob <<= c; }
    template <> inline void lshift(uint64& blob, int c) { blob <<= c; }
    template <> inline void rshift(uint32& blob, int c) { blob >>= c; }
    template <> inline void rshift(uint64& blob, int c) { blob >>= c; }

    // Left and right rotate of blobs

    inline void lrot(void* blob, int len, int c)
    {
        if ((len & 3) == 0) {
            return lrot32(blob, len, c);
        } else {
            return lrot8(blob, len, c);
        }
    }

    inline void rrot(void* blob, int len, int c)
    {
        if ((len & 3) == 0) {
            return rrot32(blob, len, c);
        } else {
            return rrot8(blob, len, c);
        }
    }

    template < typename T > inline void lrot(T& blob, int c)
    {
        if ((sizeof(T) & 3) == 0) {
            return lrot32(&blob, sizeof(T), c);
        } else {
            return lrot8(&blob, sizeof(T), c);
        }
    }

    template < typename T > inline void rrot(T& blob, int c)
    {
        if ((sizeof(T) & 3) == 0) {
            return rrot32(&blob, sizeof(T), c);
        } else {
            return rrot8(&blob, sizeof(T), c);
        }
    }

    #if COMPILER_ACTIVE == COMPILER_TYPE_MSVC

        #define ROTL32(x, y) _rotl(x, y)
        #define ROTL64(x, y) _rotl64(x, y)
        #define ROTR32(x, y) _rotr(x, y)
        #define ROTR64(x, y) _rotr64(x, y)

        #define BIG_CONSTANT(x) (x)

    #else

        inline uint32 rotl32(uint32 x, int8_t r)
        {
            return (x << uint32(r)) | (x >> uint32(32 - r));
        }

        inline uint64 rotl64(uint64 x, int8_t r)
        {
            return (x << uint64(r)) | (x >> uint64(64 - r));
        }

        inline uint32 rotr32(uint32 x, int8_t r)
        {
            return (x >> uint32(r)) | (x << uint32(32 - r));
        }

        inline uint64 rotr64(uint64 x, int8_t r)
        {
            return (x >> uint64(r)) | (x << uint64(64 - r));
        }

        #define ROTL32(x, y) rotl32(x, y)
        #define ROTL64(x, y) rotl64(x, y)
        #define ROTR32(x, y) rotr32(x, y)
        #define ROTR64(x, y) rotr64(x, y)

        #define BIG_CONSTANT(x) (x ## LLU)

    #endif


    template <> inline void lrot(uint32& blob, int c) { blob = ROTL32(blob, c); }
    template <> inline void lrot(uint64& blob, int c) { blob = ROTL64(blob, c); }
    template <> inline void rrot(uint32& blob, int c) { blob = ROTR32(blob, c); }
    template <> inline void rrot(uint64& blob, int c) { blob = ROTR64(blob, c); }


    //-----------------------------------------------------------------------------
    // Bit-windowing functions - select some N-bit subset of the input blob

    inline uint32 window(void* blob, int len, int start, int count)
    {
        if (len & 3) {
            return window8(blob, len, start, count);
        } else {
            return window32(blob, len, start, count);
        }
    }

    template < typename T > inline uint32 window(T& blob, int start, int count)
    {
        if ((sizeof(T) & 3) == 0) {
            return window32(&blob, sizeof(T), start, count);
        } else {
            return window8(&blob, sizeof(T), start, count);
        }
    }

    template <> inline uint32 window(uint32& blob, int start, int count)
    {
        return ROTR32(blob, start) & ((1 << count) - 1);
    }

    template <> inline uint32 window(uint64& blob, int start, int count)
    {
        return (uint32)ROTR64(blob, start) & ((1 << count) - 1);
    }

}

using namespace Utility;
