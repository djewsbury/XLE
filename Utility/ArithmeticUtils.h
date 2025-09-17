// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Core/Prefix.h"
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

    uint32_t      xl_clz1(uint8_t x);
    uint32_t      xl_ctz1(uint8_t x);
    uint32_t      xl_clz2(uint16_t x);
    uint32_t      xl_ctz2(uint16_t x);
    uint32_t      xl_ctz4(uint32_t x);
    uint32_t      xl_clz4(uint32_t x);
    uint32_t      xl_ctz8(uint64_t x);
    uint32_t      xl_clz8(uint64_t x);

    uint32_t      xl_bsr1(uint16_t x);
    uint32_t      xl_bsr2(uint16_t x);
    uint32_t      xl_bsr4(uint32_t x);
    uint32_t      xl_bsr8(uint64_t x);
    uint32_t      xl_lg(size_t x);

    XL_UTILITY_API void  printbits(const void* blob, int len);
    XL_UTILITY_API void  printhex32(const void* blob, int len);
    XL_UTILITY_API void  printbytes(const void* blob, int len);
    XL_UTILITY_API void  printbytes2(const void* blob, int len);

    int         popcount(uint32_t v);
    int         popcount(uint64_t v);
    uint32_t      parity(uint32_t v);
    int         countbits(uint32_t v);
    int         countbits(uint64_t v);
    int         countbits(std::vector<uint32_t>& v);
    int         countbits(const void* blob, int len);
    void        invert(std::vector<uint32_t>& v);

    uint32_t      getbit(const void* block, int len, uint32_t bit);
    uint32_t      getbit_wrap(const void* block, int len, uint32_t bit);
    template < typename T > inline uint32_t getbit(T& blob, uint32_t bit);

    void        xl_setbit(void* block, int len, uint32_t bit);
    void        xl_setbit(void* block, int len, uint32_t bit, uint32_t val);
    template < typename T > inline void xl_setbit(T& blob, uint32_t bit);

    void        xl_clearbit(void* block, int len, uint32_t bit);

    void        flipbit(void* block, int len, uint32_t bit);
    template < typename T > inline void flipbit(T& blob, uint32_t bit);

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

    XL_UTILITY_API uint32_t    window1(void* blob, int len, int start, int count);
    XL_UTILITY_API uint32_t    window8(void* blob, int len, int start, int count);
    XL_UTILITY_API uint32_t    window32(void* blob, int len, int start, int count);
    uint32_t                  window(void* blob, int len, int start, int count);
    template < typename T > inline uint32_t window(T& blob, int start, int count);

    // bit-scan
    #if COMPILER_ACTIVE == COMPILER_TYPE_MSVC

        #pragma intrinsic(_BitScanForward, _BitScanReverse)

        inline uint32_t xl_ctz4(uint32_t x)
        {
            unsigned long i = 0;
            if (!_BitScanForward(&i, (unsigned long)x)) {
                return 32;
            }
            return (uint32_t)i;
        }

        inline uint32_t xl_clz4(uint32_t x)
        {
            unsigned long i = 0;
            if (!_BitScanReverse(&i, (unsigned long)x)) {
                return 32;
            }
            return (uint32_t)(31 - i);
        }

        #if SIZEOF_PTR == 8

            #pragma intrinsic(_BitScanForward64, _BitScanReverse64)
            inline uint32_t xl_ctz8(uint64_t x)
            {
                unsigned long i = 0;
                if (!_BitScanForward64(&i, x)) {
                    return 64;
                }
                return (uint32_t)i;
            }

            inline uint32_t xl_clz8(uint64_t x)
            {
                unsigned long i = 0;
                if (!_BitScanReverse64(&i, x)) {
                    return 64;
                }
                return (uint32_t)(63 - i);
            }

        #else

            namespace Internal
            {
                union Int64U
                {
                    struct Comp
                    {
                        uint32_t LowPart;
                        uint32_t HighPart;
                    } comp;
                    uint64_t QuadPart;
                };
            }

            inline uint32_t xl_ctz8(uint64_t x)
            {
                Internal::Int64U li;
                li.QuadPart = (uint64_t)x;
                uint32_t i = xl_ctz4((uint32_t)li.comp.LowPart);
                if (i < 32) {
                    return i;
                }
                return xl_ctz4((uint32_t)li.comp.HighPart) + 32;
            }

            inline uint32_t xl_clz8(uint64_t x)
            {
                Internal::Int64U li;
                li.QuadPart = (uint64_t)x;
                uint32_t i = xl_clz4((uint32_t)li.comp.HighPart);
                if (i < 32) {
                    return i;
                }
                return xl_clz4((uint32_t)li.comp.LowPart) + 32;
            }

        #endif

    #elif (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)

        inline uint32_t xl_ctz4(uint32_t x) { return __builtin_ctz(x); }
        inline uint32_t xl_clz4(uint32_t x) { return __builtin_clz(x); }
        inline uint32_t xl_ctz8(uint64_t x) { return __builtin_ctzll(x); }
        inline uint32_t xl_clz8(uint64_t x) { return __builtin_clzll(x); }

    #else

        #error 'Unsupported Compiler!'

    #endif

    inline uint32_t xl_clz2(uint16_t x)
    {
        uint32_t i = xl_clz4(x);
        if (i == 32) {
            return 16;
        }
        //assert (i >= 16);
        return (i - 16);
    }

    inline uint32_t xl_ctz2(uint16_t x)
    {
        uint32_t i = xl_ctz4(x);
        if (i == 32) {
            return 16;
        }
        //assert(i < 16);
        return i;
    }


    inline uint32_t xl_clz1(uint8_t x)
    {
        uint32_t i = xl_clz4(x);
        if (i == 32) {
            return 8;
        }
        //assert (i >= 24);
        return (i - 24);
    }

    inline uint32_t xl_ctz1(uint8_t x)
    {
        uint32_t i = xl_ctz4(x);
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

    inline uint32_t xl_bsr1(uint16_t x)
    {
        uint32_t i = (uint32_t)xl_clz2(x);
        if (i == 8) {
            return 8;
        }
        return 7 - i;
    }

    inline uint32_t xl_bsr2(uint16_t x)
    {
        uint32_t i = xl_clz2(x);
        if (i == 16) {
            return 16;
        }
        return 15 - i;
    }

    inline uint32_t xl_bsr4(uint32_t x)
    {
        uint32_t i = xl_clz4(x);
        if (i == 32) {
            return 32;
        }
        return 31 - i;
    }

    inline uint32_t xl_bsr8(uint64_t x)
    {
        uint32_t i = xl_clz8(x);
        if (i == 64) {
            return 64;
        }
        return 63 - i;
    }

    inline uint32_t xl_lg(size_t x)
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
    #if (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)
        inline int popcount(uint32_t v)
        {
            return __builtin_popcount(v);
        }
        inline int popcount(uint64_t v)
        {
            return __builtin_popcountll(v);
        }
    #else
        inline int popcount(uint32_t v)
        {
            /*v = v - ((v >> 1) & 0x55555555);                    // reuse input as temporary
            v = (v & 0x33333333) + ((v >> 2) & 0x33333333);     // temp
            uint32_t c = ((v + ((v >> 4) & 0xF0F0F0F)) * 0x1010101) >> 24; // count

            return c;*/
            return (int)_mm_popcnt_u32(v);       // requires SSE4
        }
        inline int popcount(uint64_t v)
        {
            return (int)_mm_popcnt_u64(v);       // requires SSE4
        }
    #endif

    inline uint32_t parity(uint32_t v)
    {
        v ^= v >> 1;
        v ^= v >> 2;
        v = (v & 0x11111111U) * 0x11111111U;
        return (v >> 28) & 1;
    }

    inline uint32_t getbit(const void* block, int len, uint32_t bit)
    {
        uint8_t* b = (uint8_t*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {return (b[byte] >> bit) & 1;}

        return 0;
    }

    inline uint32_t getbit_wrap(const void* block, int len, uint32_t bit)
    {
        uint8_t* b = (uint8_t*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        byte %= len;

        return (b[byte] >> bit) & 1;
    }


    inline void xl_setbit(void* block, int len, uint32_t bit)
    {
        unsigned char* b = (unsigned char*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] |= (1 << bit);}
    }

    inline void xl_clearbit(void* block, int len, uint32_t bit)
    {
        uint8_t* b = (uint8_t*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] &= ~(1 << bit);}
    }

    inline void xl_setbit(void* block, int len, uint32_t bit, uint32_t val)
    {
        val ? xl_setbit(block, len, bit) : xl_clearbit(block, len, bit);
    }


    inline void flipbit(void* block, int len, uint32_t bit)
    {
        uint8_t* b = (uint8_t*)block;

        int byte = bit >> 3;
        bit = bit & 0x7;

        if (byte < len) {b[byte] ^= (1 << bit);}
    }

    inline int countbits(uint32_t v) { return popcount(v); }
    inline int countbits(uint64_t v) { return popcount(v); }

    //----------

    template < typename T > inline uint32_t getbit(T& blob, uint32_t bit)
    {
        return getbit(&blob, sizeof(blob), bit);
    }

    template <> inline uint32_t getbit(uint32_t& blob, uint32_t bit) { return (blob >> (bit & 31)) & 1; }
    template <> inline uint32_t getbit(uint64_t& blob, uint32_t bit) { return (blob >> (bit & 63)) & 1; }

    //----------

    template < typename T > inline void xl_setbit(T& blob, uint32_t bit)
    {
        return xl_setbit(&blob, sizeof(blob), bit);
    }

    template <> inline void xl_setbit(uint32_t& blob, uint32_t bit) { blob |= uint32_t(1) << (bit & 31); }
    template <> inline void xl_setbit(uint64_t& blob, uint32_t bit) { blob |= uint64_t(1) << (bit & 63); }

    //----------

    template < typename T > inline void flipbit(T& blob, uint32_t bit)
    {
        flipbit(&blob, sizeof(blob), bit);
    }

    template <> inline void flipbit(uint32_t& blob, uint32_t bit)
    {
        bit &= 31;
        blob ^= (uint32_t(1) << bit);
    }
    template <> inline void flipbit(uint64_t& blob, uint32_t bit)
    {
        bit &= 63;
        blob ^= (uint64_t(1) << bit);
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

    template <> inline void lshift(uint32_t& blob, int c) { blob <<= c; }
    template <> inline void lshift(uint64_t& blob, int c) { blob <<= c; }
    template <> inline void rshift(uint32_t& blob, int c) { blob >>= c; }
    template <> inline void rshift(uint64_t& blob, int c) { blob >>= c; }

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

        inline uint32_t rotl32(uint32_t x, int8_t r) { return _rotl(x, r); }
        inline uint64_t rotl64(uint64_t x, int8_t r) { return _rotl64(x, r); }
        inline uint32_t rotr32(uint32_t x, int8_t r) { return _rotr(x, r); }
        inline uint64_t rotr64(uint64_t x, int8_t r) { return _rotr64(x, r); }

        #define ROTL32(x, y) _rotl(x, y)
        #define ROTL64(x, y) _rotl64(x, y)
        #define ROTR32(x, y) _rotr(x, y)
        #define ROTR64(x, y) _rotr64(x, y)

        #define BIG_CONSTANT(x) (x)

    #else

        inline uint32_t rotl32(uint32_t x, int8_t r)
        {
            return (x << uint32_t(r)) | (x >> uint32_t(32 - r));
        }

        inline uint64_t rotl64(uint64_t x, int8_t r)
        {
            return (x << uint64_t(r)) | (x >> uint64_t(64 - r));
        }

        inline uint32_t rotr32(uint32_t x, int8_t r)
        {
            return (x >> uint32_t(r)) | (x << uint32_t(32 - r));
        }

        inline uint64_t rotr64(uint64_t x, int8_t r)
        {
            return (x >> uint64_t(r)) | (x << uint64_t(64 - r));
        }

        #define ROTL32(x, y) rotl32(x, y)
        #define ROTL64(x, y) rotl64(x, y)
        #define ROTR32(x, y) rotr32(x, y)
        #define ROTR64(x, y) rotr64(x, y)

        #define BIG_CONSTANT(x) (x ## LLU)

    #endif


    template <> inline void lrot(uint32_t& blob, int c) { blob = ROTL32(blob, c); }
    template <> inline void lrot(uint64_t& blob, int c) { blob = ROTL64(blob, c); }
    template <> inline void rrot(uint32_t& blob, int c) { blob = ROTR32(blob, c); }
    template <> inline void rrot(uint64_t& blob, int c) { blob = ROTR64(blob, c); }


    //-----------------------------------------------------------------------------
    // Bit-windowing functions - select some N-bit subset of the input blob

    inline uint32_t window(void* blob, int len, int start, int count)
    {
        if (len & 3) {
            return window8(blob, len, start, count);
        } else {
            return window32(blob, len, start, count);
        }
    }

    template < typename T > inline uint32_t window(T& blob, int start, int count)
    {
        if ((sizeof(T) & 3) == 0) {
            return window32(&blob, sizeof(T), start, count);
        } else {
            return window8(&blob, sizeof(T), start, count);
        }
    }

    template <> inline uint32_t window(uint32_t& blob, int start, int count)
    {
        return ROTR32(blob, start) & ((1 << count) - 1);
    }

    template <> inline uint32_t window(uint64_t& blob, int start, int count)
    {
        return (uint32_t)ROTR64(blob, start) & ((1 << count) - 1);
    }

}

using namespace Utility;
