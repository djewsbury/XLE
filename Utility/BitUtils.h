// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ArithmeticUtils.h"
#include "IteratorUtils.h"

namespace Utility
{
    template <typename Type> 
        inline constexpr bool IsPowerOfTwo(Type x)
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

    inline uint32_t IntegerLog2(uint8_t x)
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
        inline constexpr Type CeilToMultiplePow2(Type input, unsigned multiple)
    {
            // returns "input", or the next largest multiple of the number "multiple"
            // Here, we assume "multiple" is a power of 2
        assert(IsPowerOfTwo(multiple) && multiple > 0);
        return (input + multiple - 1) & ~(Type(multiple) - 1);
    }

    template <typename Type>
        inline constexpr Type FloorToMultiplePow2(Type input, unsigned multiple)
    {
        assert(IsPowerOfTwo(multiple) && multiple > 0);
        return input & ~(multiple - 1);
    }

    template <typename Type>
        inline constexpr Type CeilToMultiple(Type input, unsigned multiple)
    {
        assert(multiple > 0);
        auto Q = input + multiple - 1;
        assert(Q >= input);
        return Q - Q % multiple;
    }

    inline uint32_t InterleaveBits(uint16_t ix, uint16_t iy)
    {
        uint32_t x = ix, y = iy;
        x = (x | (x << 8u)) & 0x00FF00FFu;
        x = (x | (x << 4u)) & 0x0F0F0F0Fu;
        x = (x | (x << 2u)) & 0x33333333u;
        x = (x | (x << 1u)) & 0x55555555u;

        y = (y | (y << 8u)) & 0x00FF00FFu;
        y = (y | (y << 4u)) & 0x0F0F0F0Fu;
        y = (y | (y << 2u)) & 0x33333333u;
        y = (y | (y << 1u)) & 0x55555555u;

        return x | (y << 1u);
    }

    inline float AsFloatBits(uint32_t input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; uint32_t i; };
		Converter c; c.i = input; 
		return c.f;
	}

	inline uint32_t AsUInt32Bits(float input)
	{
			// (or just use a reinterpret cast)
		union Converter { float f; uint32_t i; };
		Converter c; c.f = input; 
		return c.i;
	}

	inline double AsFloatBits(uint64_t input)
	{
			// (or just use a reinterpret cast)
		union Converter { double f; uint64_t i; };
		Converter c; c.i = input; 
		return c.f;
	}

	inline uint64_t AsUInt64Bits(double input)
	{
			// (or just use a reinterpret cast)
		union Converter { double f; uint64_t i; };
		Converter c; c.f = input; 
		return c.i;
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
        unsigned AllocatedCount() const;
        void    DeallocateAll();
        IteratorRange<const uint64_t*> InternalArray() const { return _heap; }

        BitHeap(unsigned slotCount = 8 * 64);
        BitHeap(BitHeap&& moveFrom) = default;
        BitHeap& operator=(BitHeap&& moveFrom) = default;
        BitHeap(const BitHeap& cloneFrom) = default;
        BitHeap& operator=(const BitHeap& cloneFrom) = default;;
        ~BitHeap();
    private:
        std::vector<uint64_t>         _heap;
    };
}

using namespace Utility;

