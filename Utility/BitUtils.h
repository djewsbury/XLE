// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ArithmeticUtils.h"
#include "../Core/Types.h"

namespace Utility
{
    template <typename Type> 
        inline bool IsPowerOfTwo(Type x)
    {
            //  powers of two should have only 1 bit set... We can check using standard
            //  bit twiddling check...
        return (x & (x - 1)) == 0;
    }

    //      We can use the "bsr" instruction (if it's available) for 
    //      calculating the integer log 2. Let's use the abstractions for
    //      bsr in ArithmeticUtils.h.
    //
    //      Note that currently the Win32 implementation of bsr has a 
    //      number of conditions, which should optimise out if inlining
    //      works correctly.
    //
    //      See alternative methods for calculating this via well-known
    //      bit twiddling web site:
    //          https://graphics.stanford.edu/~seander/bithacks.html

    inline uint32_t IntegerLog2(uint8 x)
    {
        return xl_bsr1(x);
    }

    inline uint32_t IntegerLog2(uint16_t x)
    {
        return xl_bsr2(x);
    }

    inline uint32_t IntegerLog2(uint32_t x)
    {
        return xl_bsr4(x);
    }

    inline uint32_t IntegerLog2(uint64_t x)
    {
        return xl_bsr8(x);
    }

    inline uint32_t LeastSignificantBitSet(uint64_t input)
    {
            // (same as count-trailing-zeroes)
        return xl_ctz8(input);
    }

    template <typename Type>
        inline Type CeilToMultiplePow2(Type input, unsigned multiple)
    {
            // returns "input", or the next largest multiple of the number "multiple"
            // Here, we assume "multiple" is a power of 2
        assert(IsPowerOfTwo(multiple));
        return (input + multiple - 1) & ~(multiple - 1);
    }

    template <typename Type>
        inline Type FloorToMultiplePow2(Type input, unsigned multiple)
    {
        assert(IsPowerOfTwo(multiple));
        return input & ~(multiple - 1);
    }

    template <typename Type>
        inline Type CeilToMultiple(Type input, unsigned multiple)
    {
        assert(multiple > 0);
        return input + multiple - 1 - (input - 1) % multiple;
    }

    class BitHeap
    {
    public:
        uint32_t  Allocate();
        uint32_t  AllocateNoExpand();
        void    Allocate(uint32_t value);       ///< allocate a specific entry
        void    Deallocate(uint32_t value);
        bool    IsAllocated(uint32_t value) const;
        void    Reserve(uint32_t count);
        unsigned FirstUnallocated() const;

        BitHeap(unsigned slotCount = 8 * 64);
        BitHeap(BitHeap&& moveFrom) = default;
        BitHeap& operator=(BitHeap&& moveFrom) = default;
        BitHeap(const BitHeap& cloneFrom) = default;
        BitHeap& operator=(const BitHeap& cloneFrom) = default;;
        ~BitHeap();
    private:
        std::vector<uint64>         _heap;
    };
}

using namespace Utility;

