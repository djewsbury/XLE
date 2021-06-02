// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MiniHeap.h"
#include "BitUtils.h"
#include "HeapUtils.h"
#include "PtrUtils.h"
#include "Threading/ThreadingUtils.h"
#include "Threading/Mutex.h"
#include <assert.h>

namespace Utility
{
    namespace Exceptions
    {
        class HeapCorruption : public std::exception 
        {
        public:
            virtual const char* what() const never_throws { return "Heap corruption"; }
        };
    }

    class FixedSizePage
    {
    public:
        BitHeap                                 _allocationStatus;
        std::unique_ptr<Interlocked::Value[]>   _refCounts;
        std::vector<uint8>                      _pageMemory;

        unsigned            Allocate();

        FixedSizePage(unsigned blockCount, unsigned blockSize);
        FixedSizePage(FixedSizePage&& moveFrom);
        FixedSizePage& operator=(FixedSizePage&& moveFrom);
        ~FixedSizePage();
    private:
        FixedSizePage(const FixedSizePage&);
        FixedSizePage& operator=(const FixedSizePage&);
    };

    unsigned FixedSizePage::Allocate()
    {
        unsigned result = _allocationStatus.AllocateNoExpand();
        if (result != ~unsigned(0x0)) {
                // initialize reference count to 1 on allocate, always
            Interlocked::Store(&_refCounts[result], 1);
        }
        return result;
    }

    FixedSizePage::FixedSizePage(unsigned blockCount, unsigned blockSize) 
        : _allocationStatus(blockCount)
    {
        _allocationStatus.Reserve(blockCount);
        _refCounts = std::make_unique<Interlocked::Value[]>(blockCount);
        for (unsigned c=0; c<blockCount; ++c) {
            Interlocked::Store(&_refCounts[c], 0);
        }
        _pageMemory.resize(blockCount * blockSize, 0xac);
    }

    FixedSizePage::FixedSizePage(FixedSizePage&& moveFrom)
    : _allocationStatus(std::move(moveFrom._allocationStatus))
    , _refCounts(std::move(moveFrom._refCounts))
    , _pageMemory(std::move(moveFrom._pageMemory))
    {}

    FixedSizePage& FixedSizePage::operator=(FixedSizePage&& moveFrom)
    {
        _allocationStatus = std::move(moveFrom._allocationStatus);
        _refCounts = std::move(moveFrom._refCounts);
        _pageMemory = std::move(moveFrom._pageMemory);
        return *this;
    }

    FixedSizePage::~FixedSizePage() {}

    class FreePage
    {
    public:
        std::vector<uint8>  _pageMemory;
        SimpleSpanningHeap  _spanningHeap;

        class Block
        {
        public:
            unsigned    _offset, _size;
            signed      _refCount;
        };
        std::vector<Block>  _blocks;

        void*   Allocate(unsigned size);

        class CompareBlockOffset
        {
        public:
            bool operator()(const Block& lhs, unsigned rhs) { return lhs._offset < rhs; }
            bool operator()(unsigned lhs, const Block& rhs) { return lhs < rhs._offset; }
            bool operator()(const Block& lhs, const Block& rhs) { return lhs._offset < rhs._offset; }
        };

        FreePage(unsigned pageSize);
        FreePage(FreePage&& moveFrom) never_throws;
        FreePage& operator=(FreePage&& moveFrom) never_throws;
        ~FreePage();
    private:
        FreePage(const FreePage&);
        FreePage& operator=(const FreePage&);
    };

    void*   FreePage::Allocate(unsigned size)
    {
        unsigned offset = _spanningHeap.Allocate(size);
        if (offset == ~unsigned(0x0)) {
            return nullptr;
        }
        assert(offset < _pageMemory.size());

            // add a new "Block" in stored order
        auto i = std::lower_bound(_blocks.begin(), _blocks.end(), offset, CompareBlockOffset());
        assert(i == _blocks.end() || i->_offset != offset);
        i = _blocks.insert(i, { offset, size, 1 }); // note -- start with reference count of "1"

        return PtrAdd(AsPointer(_pageMemory.begin()), offset);
    }

    FreePage::FreePage(unsigned pageSize)
    : _spanningHeap(pageSize)
    {
        _pageMemory.resize(pageSize, 0xac);
        _blocks.reserve(64);
    }

    FreePage::FreePage(FreePage&& moveFrom) never_throws
    : _pageMemory(std::move(moveFrom._pageMemory))
    , _spanningHeap(std::move(moveFrom._spanningHeap))
    , _blocks(std::move(moveFrom._blocks))
    {}

    FreePage& FreePage::operator=(FreePage&& moveFrom) never_throws
    {
        _pageMemory = std::move(moveFrom._pageMemory);
        _spanningHeap = std::move(moveFrom._spanningHeap);
        _blocks = std::move(moveFrom._blocks);
        return *this;
    }

    FreePage::~FreePage() {}

        //      We keep separate fixed size pages for allocations
        //      on multiples of 16. This is convenient for constant
        //      buffer allocations (because they are always a multiple
        //      of 8, often at least 16 bytes, and we accept the extra 
        //      overhead when required).
        //
        //      Note that each allocation requires at least 9 bits of 
        //      overhead (probably an average of 10-11 bits). So that's
        //      that's a lot of overhead for 16 byte allocations. Removing
        //      the reference counting would reduce that greatly.
        //
        //  16, 32, 48, 64, 80, 96, 112, 128
    static const unsigned FixedSizeHeapCount = 8;
    static const unsigned FixedSizePageSize = 8*1024;
    static const unsigned FreePageSize = 8*1024;

    class MiniHeap::Pimpl
    {
    public:
        std::vector<FixedSizePage>  _fixedSizePages[FixedSizeHeapCount];
        std::vector<FreePage>       _freePages;
        Threading::Mutex            _lock;
    };

    struct SplitMarker { unsigned _pageCat, _pageIndex, _blockIndex; };
    static SplitMarker Split(uint32 marker)
    {
        //  Our "marker" object should tell us some information about the allocation,
        //  ... particularly which page it belongs to, and which block within that page
        //
        //  Largest block index possible (for fixed size heap) is FixedSizePageSize / 16 - 1 = 511
        //  There are 9 different categories for pages,
        //  There are potentially an infinite number of pages per category; but we
        //  can limit it to some practical maximum
        //
        //  So, let's split up our marker index like this:
        //  [10 bits: block index] [18 bits: page index] [4 bits: page category]
        //
        //  That means the maximum number of pages per category is 262143, which is
        //  far more than needed
        SplitMarker result = { marker & 0xf, (marker >> 4) & 0x3FFFF, (marker >> 22) };
        return result;
    }

    static uint32 MakeMarker(unsigned pageCat, unsigned pageIndex, unsigned blockIndex)
    {
        assert(pageIndex <= 0x3FFFF);
        assert(blockIndex <= 0x3FF);
        return (pageCat & 0xf) | ((pageIndex & 0x3FFFF) << 4) | ((blockIndex & 0x3ff) << 22);
    }

    static uint32 MainHeapMarker = 0xf;     // (category 15)

    class MainHeapExtraData
    {
    public:
        Interlocked::Value _refCount;
        void *_alignedFreePtr;
        unsigned _dummy[3 - sizeof(void *) / 4];     // (padding to try to encourage 16 byte alignment)

        void Initialize(signed refCount, void *alignedFreePtr = nullptr)
        {
            _refCount = refCount;
            _alignedFreePtr = alignedFreePtr;
            for (unsigned c = 0; c < dimof(_dummy); ++c)
                _dummy[c] = 0x0;
        }
    };

    static unsigned BlockSizeForHeap(unsigned heapIndex)
    {
        return (heapIndex + 1) * 16;
    }

    static unsigned HeapIndexForBlockSize(unsigned blockSize)
    {
        assert(blockSize != 0); // returns -1 for blockSize==0
        return (blockSize + 16 - 1) / 16 - 1;
    }

    auto MiniHeap::Allocate(unsigned size) -> Allocation
    {
        if (!size) { return Allocation(); }     // zero size allocation is valid, but wierd

        if (size > FreePageSize) {
            //  there is a limit on the maximum allocation size
            //  we can allocate. Bigger allocations should ideally
            //  the main heap (no need for the mini heap for very
            //  large allocations)
            auto *heapAlloc = malloc(size + sizeof(MainHeapExtraData));
            ((MainHeapExtraData *) heapAlloc)->Initialize(1); // start at ref count of "1"
            return Allocation(PtrAdd(heapAlloc, sizeof(MainHeapExtraData)), MainHeapMarker);
        }

        ScopedLock(_pimpl->_lock);

            //  Attempt to allocate in a fixed size page, if the 
            //  allocation is small;
        auto fixedSizeHeap = HeapIndexForBlockSize(size);
        if (fixedSizeHeap < FixedSizeHeapCount) {
            auto& pages = _pimpl->_fixedSizePages[fixedSizeHeap];
            const auto blockSize = BlockSizeForHeap(fixedSizeHeap);
            for (auto i=pages.begin(); i!=pages.end(); ++i) {
                auto result = i->Allocate();
                if (result != ~unsigned(0x0)) {
                    Allocation alloc;
                    alloc._allocation = PtrAdd(AsPointer(i->_pageMemory.begin()), result * blockSize);
                    alloc._marker = MakeMarker(fixedSizeHeap, (unsigned)std::distance(pages.begin(), i), result);
                    return alloc;
                }
            }

                // need a new page...
            FixedSizePage newPage(FixedSizePageSize / blockSize, blockSize);
            auto resultIdx = newPage.Allocate();
            assert(resultIdx != ~unsigned(0x0));
            Allocation alloc;
            alloc._allocation = PtrAdd(AsPointer(newPage._pageMemory.begin()), resultIdx * blockSize);
            alloc._marker = MakeMarker(fixedSizeHeap, (unsigned)pages.size(), resultIdx);
            pages.push_back(std::move(newPage));
            return alloc;
        }

            //  If we can't put it in a fixed-size heap, we 
            //  need to commit to a "free page"
        for (auto i=_pimpl->_freePages.begin(); i!=_pimpl->_freePages.end(); ++i) {
            auto result = i->Allocate(size);
            if (result) {
                Allocation alloc;
                alloc._allocation = result;
                alloc._marker = MakeMarker(FixedSizeHeapCount, (unsigned)std::distance(_pimpl->_freePages.begin(), i), 0);
                return alloc;
            }
        }

        FreePage newPage(FreePageSize);
        Allocation alloc;
        alloc._allocation = newPage.Allocate(size);
        alloc._marker = MakeMarker(FixedSizeHeapCount, (unsigned)_pimpl->_freePages.size(), 0);
        assert(alloc._allocation != nullptr);
        _pimpl->_freePages.push_back(std::move(newPage));
        return alloc;
    }

    auto MiniHeap::AllocateAligned(unsigned size, unsigned alignment) -> Allocation
    {
        if (!size) { return Allocation(); }     // zero size allocation is valid, but wierd
        if (alignment<=1) return Allocate(size);

        // Aligned allocations always get the slower main heap style allocations

        auto *heapAlloc = malloc(size + sizeof(MainHeapExtraData) + alignment);

        size_t markerAllocation = size_t(heapAlloc) + sizeof(MainHeapExtraData);
        if ((markerAllocation%alignment)!=0)
            markerAllocation += alignment - (markerAllocation%alignment);

        auto* exdata = (MainHeapExtraData*)(markerAllocation - ptrdiff_t(sizeof(MainHeapExtraData)));
        exdata->Initialize(1, heapAlloc); // start at ref count of "1"
        assert((markerAllocation%alignment)==0);
        return Allocation((void*)markerAllocation, MainHeapMarker);
    }

    void        MiniHeap::Free(void* ptr) 
    {
        if (!ptr) return;   // (delete nullptr is valid, but evaluates to nothing)

        ScopedLock(_pimpl->_lock);

            //  Unfortunately, the block-searching behavior here is very slow.
            //  Most alternative implementations would probably be faster
            //  Ideally, the client should use the Allocation marker object,
            //  which helps greatly.

        for (unsigned fixedSizeHeap=0; fixedSizeHeap<FixedSizeHeapCount; ++fixedSizeHeap) {
            auto& pages = _pimpl->_fixedSizePages[fixedSizeHeap];
            const auto blockSize = BlockSizeForHeap(fixedSizeHeap);
            for (auto i=pages.begin(); i!=pages.end(); ++i) {
                if (ptr >= AsPointer(i->_pageMemory.cbegin()) && ptr < AsPointer(i->_pageMemory.cend())) {
                        //  Allocation is within this page. 
                        //  Free it directly, ignoring current status of reference count
                    auto offset = ptrdiff_t(ptr) - ptrdiff_t(AsPointer(i->_pageMemory.cbegin()));
                    assert((offset % blockSize) == 0);
                    i->_allocationStatus.Deallocate(uint32(offset / blockSize));
                    return;
                }
            }
        }

        for (auto i=_pimpl->_freePages.begin(); i!=_pimpl->_freePages.end(); ++i) {
            if (ptr >= AsPointer(i->_pageMemory.cbegin()) && ptr < AsPointer(i->_pageMemory.cend())) {
                    //  allocation is within the page. We should look for a block that
                    //  matches
                auto offset = unsigned(ptrdiff_t(ptr) - ptrdiff_t(AsPointer(i->_pageMemory.cbegin())));
                auto b = std::lower_bound(
                    i->_blocks.cbegin(), i->_blocks.cend(), 
                    offset, FreePage::CompareBlockOffset());
                if (b != i->_blocks.cend() && b->_offset == offset) {
                    auto size = b->_size;
                    i->_spanningHeap.Deallocate(offset, size);
                    i->_blocks.erase(b);
                    return; // successful deallocation
                }

                    // it's in our memory space, but not one of our blocks? Maybe a double-delete
                assert(0);
                throw Exceptions::HeapCorruption();
            }
        }

            // couldn't find this memory in any of our pages. This memory must not have been allocated
            // throw MiniHeap::Allocate. It may have been a block that was too large, and was allocated
            // on the main heap;
        free(ptr);
    }

    void        MiniHeap::AddRef(Allocation marker)
    {
        if (marker._marker == MainHeapMarker) {
            auto* exdata = (MainHeapExtraData*)PtrAdd(marker._allocation, -ptrdiff_t(sizeof(MainHeapExtraData)));
            Interlocked::Increment(&exdata->_refCount);
            return;
        }

        ScopedLock(_pimpl->_lock);

        auto m = Split(marker._marker);
        if (m._pageCat < FixedSizeHeapCount) {
            if (m._pageIndex >= _pimpl->_fixedSizePages[m._pageCat].size()) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }
            if (m._blockIndex >= FixedSizePageSize / BlockSizeForHeap(m._pageCat)) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            auto& page = _pimpl->_fixedSizePages[m._pageCat][m._pageIndex];
            assert(page._allocationStatus.IsAllocated(m._blockIndex));
            Interlocked::Increment(&page._refCounts[m._blockIndex]);
        } else {
            if (m._pageIndex > _pimpl->_freePages.size()) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }
            auto& page = _pimpl->_freePages[m._pageIndex];
                //  in the case of "free pages", we can't use the block index
                //  we have to search for the block, based on the allocation offset
            if (    marker._allocation < AsPointer(page._pageMemory.cbegin()) 
                ||  marker._allocation >= AsPointer(page._pageMemory.cend())) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            auto offset = unsigned(ptrdiff_t(marker._allocation) - ptrdiff_t(AsPointer(page._pageMemory.cbegin())));
            auto b = std::lower_bound(
                page._blocks.begin(), page._blocks.end(), 
                offset, FreePage::CompareBlockOffset());
            if (b == page._blocks.cend() || b->_offset != offset) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            ++b->_refCount;
        }
    }

    void        MiniHeap::Release(Allocation marker)
    {
        assert(marker._allocation); // release on unallocated/null block is invalid!

        if (marker._marker == MainHeapMarker) {
            auto* exdata = (MainHeapExtraData*)PtrAdd(marker._allocation, -ptrdiff_t(sizeof(MainHeapExtraData)));
            auto oldRefCount = Interlocked::Decrement(&exdata->_refCount);
            assert(oldRefCount >= 1);
            if (oldRefCount == 1) {
                if (exdata->_alignedFreePtr) {
                    free(exdata->_alignedFreePtr);
                } else {
                    free(exdata);
                }
            }
            return;
        }

        ScopedLock(_pimpl->_lock);

        auto m = Split(marker._marker);
        if (m._pageCat < FixedSizeHeapCount) {
            if (m._pageIndex >= _pimpl->_fixedSizePages[m._pageCat].size()) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }
            if (m._blockIndex >= FixedSizePageSize / BlockSizeForHeap(m._pageCat)) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            auto& page = _pimpl->_fixedSizePages[m._pageCat][m._pageIndex];
            assert(page._allocationStatus.IsAllocated(m._blockIndex));
            auto oldRefCount = Interlocked::Decrement(&page._refCounts[m._blockIndex]);
            assert(oldRefCount >= 1);
            if (oldRefCount == 1) {
                    // Ref count hit zero. This is a destroy
                page._allocationStatus.Deallocate(m._blockIndex);
            }
        } else {
            if (m._pageIndex > _pimpl->_freePages.size()) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }
            auto& page = _pimpl->_freePages[m._pageIndex];
                //  in the case of "free pages", we can't use the block index
                //  we have to search for the block, based on the allocation offset
            if (    marker._allocation < AsPointer(page._pageMemory.cbegin()) 
                ||  marker._allocation >= AsPointer(page._pageMemory.cend())) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            auto offset = unsigned(ptrdiff_t(marker._allocation) - ptrdiff_t(AsPointer(page._pageMemory.cbegin())));
            auto b = std::lower_bound(
                page._blocks.begin(), page._blocks.end(), 
                offset, FreePage::CompareBlockOffset());
            if (b == page._blocks.cend() || b->_offset != offset) {
                assert(0);
                throw Exceptions::HeapCorruption();
            }

            auto oldRefCount = b->_refCount--;
            assert(oldRefCount >= 1);
            if (oldRefCount == 1) {
                page._spanningHeap.Deallocate(b->_offset, b->_size);
                page._blocks.erase(b);
            }
        }
    }

    MiniHeap::MiniHeap()
    {
        auto pimpl = std::make_unique<Pimpl>();
        _pimpl = std::move(pimpl);
    }

    MiniHeap::~MiniHeap()
    {}

    MiniHeap::MiniHeap(MiniHeap&& moveFrom) : _pimpl(std::move(moveFrom._pimpl)) {}
    MiniHeap& MiniHeap::operator=(MiniHeap&& moveFrom)
    {
        _pimpl = std::move(moveFrom._pimpl);
        return *this;
    }
}

