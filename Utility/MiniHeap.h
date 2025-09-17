// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace Utility
{

    /// <summary>A simple but efficient mini heap implement<summary>
    /// This class is used to manage arbitrary allocations.
    ///
    /// It avoids fragmentation for small allocations. It's works best
    /// with small short-lived allocations. Originally designed to manage
    /// upload data for constant buffers. Often it's best to dedicate a 
    /// separate mini heap for each allocation type (eg, one might be for
    /// constant buffers, another might be for temporary strings).
    ///
    /// Generally allocation should be fast. However, free can take
    /// a little more time. If allocation/free performance is critical
    /// it might be better to just use a heap that continually grows,
    /// until everything is destroyed at the same time (eg, a thread
    /// locked frame temporaries heap)
    ///
    /// Reference counting is built into the heap (just for convenience)
    /// However, note that AddRef/Release might be a bit slower than usual 
    /// because we have to search for the right block, first! To use reference
    /// counting, you must record and use the "MiniHeap::Allocation" objects.
    /// We need this to track the internal heap block. Reference counting
    /// is too slow without this; so AddRef/Release methods without the 
    /// allocation marker are not provided.
    ///
    /// Note that when allocations are first returned, they start with a 
    /// reference count of 1. That is, the follow code will create and then
    /// destroy a single block:
    /// <code>\code
    ///     auto alloc = heap.Allocate(size);
    ///         // (note, no AddRef here -- we already have ref count of 1)
    ///     heap.Release(alloc);
    /// \endcode</code>
    class MiniHeap
    {
    public:
        class Allocation
        {
        public:
            void*       _allocation;
            uint32_t    _marker;

            operator void*()    { return _allocation; }
            operator bool()     { return _allocation != nullptr; }

            Allocation() : _allocation(nullptr), _marker(~uint32_t(0x0)) {}
            Allocation(void* a, uint32_t marker) : _allocation(a), _marker(marker) {}
        };

        Allocation  Allocate(unsigned size);
        Allocation  AllocateAligned(unsigned size, unsigned alignment);
        void        Free(void* ptr);
        void        AddRef(Allocation marker);
        void        Release(Allocation marker);

        MiniHeap();
        ~MiniHeap();

        MiniHeap(MiniHeap&& moveFrom);
        MiniHeap& operator=(MiniHeap&& moveFrom);
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

}

using namespace Utility;

