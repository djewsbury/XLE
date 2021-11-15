// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BitUtils.h"
#include <assert.h>

namespace Utility
{
    uint32_t  BitHeap::Allocate()
    {
        for (auto i=_heap.begin(); i!=_heap.end(); ++i) {
            if (*i != 0) {
                auto bitIndex = LeastSignificantBitSet(*i);
                (*i) &= ~(1ull<<uint64_t(bitIndex));
                return ((uint32_t)std::distance(_heap.begin(), i))*64 + bitIndex;
            }
        }

        _heap.push_back(~uint64_t(1));
        return uint32_t(_heap.size()-1)*64;
    }

    uint32_t  BitHeap::AllocateNoExpand()
    {
        for (auto i=_heap.begin(); i!=_heap.end(); ++i) {
            if (*i != 0) {
                auto bitIndex = LeastSignificantBitSet(*i);
                (*i) &= ~(1ull<<uint64_t(bitIndex));
                return ((uint32_t)std::distance(_heap.begin(), i))*64 + bitIndex;
            }
        }

        return ~uint32_t(0x0);
    }

    void    BitHeap::Deallocate(uint32_t value)
    {
        uint32_t bitIndex = value&(64-1);
        uint32_t arrayIndex = value>>6;
        if (arrayIndex < _heap.size()) {
            assert((_heap[arrayIndex] & (1ull<<uint64_t(bitIndex))) == 0);
            _heap[arrayIndex] |= 1ull<<uint64_t(bitIndex);
        }
    }

    bool    BitHeap::IsAllocated(uint32_t value) const
    {
        uint32_t bitIndex = value&(64-1);
        uint32_t arrayIndex = value>>6;
        if (arrayIndex < _heap.size()) {
            return (_heap[arrayIndex] & (1ull<<uint64_t(bitIndex))) == 0;
        }
        return false;
    }
    
    void    BitHeap::Reserve(uint32_t count)
    {
        unsigned elementCount = (count + 64 - 1) / 64;
        if (_heap.size() < elementCount) {
            _heap.resize(elementCount, ~uint64_t(0x0));
        }
    }

    void    BitHeap::Allocate(uint32_t value)
    {
        assert(!IsAllocated(value));
        Reserve(value+1);
        uint32_t bitIndex = value&(64-1);
        uint32_t arrayIndex = value>>6;
        assert(arrayIndex < _heap.size());
        _heap[arrayIndex] &= ~(1ull<<uint64_t(bitIndex));
    }

    unsigned BitHeap::FirstUnallocated() const
    {
        unsigned idx = 0;
        for (auto i=_heap.begin(); i!=_heap.end(); ++i, idx+=64) {
            auto trailingZeroes = xl_ctz8(*i);      // zeroes are allocated
            if (*i == 0) continue;
            assert(!IsAllocated(idx + trailingZeroes));
            return idx + trailingZeroes;
        }
        return ~0u;
    }

    unsigned BitHeap::AllocatedCount() const
    {
        unsigned result = 0;
        for (auto i:_heap) result += 64u-popcount(i);
        return result;
    }

    BitHeap::BitHeap(unsigned slotCount)
    {
        unsigned longLongCount = (slotCount + 64 - 1) / 64;
        _heap.resize(longLongCount, ~uint64_t(0x0));
        if ((slotCount % 64) != 0) {
                // prevent top bits from being allocated
            _heap[longLongCount-1] = ((1ull << (slotCount % 64ull)) - 1ull);
        }
    }

    BitHeap::~BitHeap()
    {}

}

