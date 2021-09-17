// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HeapUtils.h"
#include "PtrUtils.h"
#include "MemoryUtils.h"
#include <assert.h>

namespace Utility
{
    unsigned LRUQueue::GetOldestValue() const
    {
        return _oldestBlock;
    }

    void    LRUQueue::BringToFront(unsigned linearAddress)
    {
        assert(linearAddress < _lruQueue.size());

        if (_newestBlock == ~unsigned(0x0)) {
            assert(_oldestBlock == ~unsigned(0x0));
            _oldestBlock = _newestBlock = linearAddress;
            return;
        }
        assert(_oldestBlock != ~unsigned(0x0));

        #if defined(_DEBUG)
            auto count = QueueDepth();
        #endif
        
            // remove this item from it's old place in the queue
        auto oldLinks = _lruQueue[linearAddress];
        if (oldLinks.first != ~unsigned(0x0)) {
            _lruQueue[oldLinks.first].second = oldLinks.second;
            assert(_newestBlock != linearAddress);
        } else {
            // no prev means this may be the newest. Nothing to change
            if (_newestBlock == linearAddress)
                return;
        }

        if (oldLinks.second != ~unsigned(0x0)) {
            _lruQueue[oldLinks.second].first = oldLinks.first;
            assert(linearAddress != _oldestBlock);
        } else {
            // no next means this may be the oldest.
            if (linearAddress == _oldestBlock)
                _oldestBlock = oldLinks.first;
        }
        
        assert(_lruQueue[_newestBlock].first == ~unsigned(0x0));
        _lruQueue[_newestBlock].first = linearAddress;
        _lruQueue[linearAddress].first = ~unsigned(0x0);
        _lruQueue[linearAddress].second = _newestBlock;
        _newestBlock = linearAddress;

        #if defined(_DEBUG)
            auto newQueueDepth = count;
            assert(newQueueDepth == count || newQueueDepth == (count+1));
        #endif
    }

    void    LRUQueue::SendToBack(unsigned linearAddress)
    {
        assert(linearAddress < _lruQueue.size());

        if (_newestBlock == ~unsigned(0x0)) {
            assert(_oldestBlock == ~unsigned(0x0));
            _oldestBlock = _newestBlock = linearAddress;
            return;
        }
        assert(_oldestBlock != ~unsigned(0x0));

        #if defined(_DEBUG)
            auto count = QueueDepth();
        #endif
        
            // remove this item from it's old place in the queue
		auto oldLinks = _lruQueue[linearAddress];
        if (oldLinks.second != ~unsigned(0x0)) {
            _lruQueue[oldLinks.second].first = oldLinks.first;
            assert(linearAddress != _oldestBlock);
        } else {
            // no next means this may be the oldest. Nothing to change
            if (linearAddress == _oldestBlock)
                return;
        }

        if (oldLinks.first != ~unsigned(0x0)) {
            _lruQueue[oldLinks.first].second = oldLinks.second;
            assert(_newestBlock != linearAddress);
        } else {
            // no prev means this may be the newest. Nothing to change
            if (_newestBlock == linearAddress)
                _newestBlock = oldLinks.second;
        }

        assert(_lruQueue[_oldestBlock].second == ~unsigned(0x0));
        _lruQueue[_oldestBlock].second = linearAddress;
        _lruQueue[linearAddress].first = _oldestBlock;
        _lruQueue[linearAddress].second = ~unsigned(0x0);
        _oldestBlock = linearAddress;

        #if defined(_DEBUG)
            auto newQueueDepth = count;
            assert(newQueueDepth == count || newQueueDepth == (count+1));
        #endif
    }

    void LRUQueue::DisconnectOldest()
    {
            // Disconnect the oldest block from the linked
            // list.
            // It will no longer be returned from GetOldestValue()
            // until it is added back with BringToFront()
        if (_oldestBlock != ~unsigned(0x0)) {
            auto blockToRemove = _oldestBlock;
            _oldestBlock = _lruQueue[blockToRemove].first;
            if (_oldestBlock != ~unsigned(0x0)) {
                assert(_newestBlock != blockToRemove);
                assert(_lruQueue[_oldestBlock].second == blockToRemove);
                _lruQueue[_oldestBlock].second = ~unsigned(0x0);
            } else if (_newestBlock == blockToRemove) {    
                    // when disconnecting the last block, both _oldestBlock
                    // and _newestBlock should end up invalid
                    // This is the only case in which we should touch _newestBlock
                assert(_oldestBlock == ~unsigned(0x0));
                _newestBlock = ~unsigned(0x0);
            }

            _lruQueue[blockToRemove] = std::make_pair(~unsigned(0x0), ~unsigned(0x0));
        }
    }

    unsigned LRUQueue::QueueDepth() const
    {
        if (_newestBlock == ~unsigned(0x0)) {
            assert(_oldestBlock == ~unsigned(0x0));
            return 0;
        }

        unsigned count = 1;
        unsigned idx = _lruQueue[_oldestBlock].first;
        while (idx != ~0x0u) {
            ++count;
            idx = _lruQueue[idx].first;
        }
        return count;
    }

    bool LRUQueue::HasValue(unsigned value) const
    {
        return _oldestBlock == value || _newestBlock == value || (value < _lruQueue.size() && _lruQueue[value] != std::make_pair(~unsigned(0x0), ~unsigned(0x0)));
    }

    LRUQueue::LRUQueue(unsigned maxValues)
    {
        _oldestBlock = _newestBlock = ~unsigned(0x0);
        std::vector<std::pair<unsigned, unsigned>> lruQueue;
        lruQueue.resize(maxValues, std::make_pair(~unsigned(0x0), ~unsigned(0x0)));
        _lruQueue = std::move(lruQueue);
    }

    LRUQueue::LRUQueue(LRUQueue&& moveFrom) never_throws
    : _lruQueue(std::move(moveFrom._lruQueue))
    {
        _oldestBlock = moveFrom._oldestBlock;
        _newestBlock = moveFrom._newestBlock;
    }

    LRUQueue& LRUQueue::operator=(LRUQueue&& moveFrom) never_throws
    {
        _lruQueue = std::move(moveFrom._lruQueue);
        _oldestBlock = moveFrom._oldestBlock;
        _newestBlock = moveFrom._newestBlock;
        return *this;
    }

    LRUQueue::LRUQueue(const LRUQueue& copyFrom)
    : _lruQueue(copyFrom._lruQueue)
    {
        _oldestBlock = copyFrom._oldestBlock;
        _newestBlock = copyFrom._newestBlock;
    }

    LRUQueue& LRUQueue::operator=(const LRUQueue& copyFrom)
    {
        _lruQueue = copyFrom._lruQueue;
        _oldestBlock = copyFrom._oldestBlock;
        _newestBlock = copyFrom._newestBlock;
        return *this;
    }

    LRUQueue::LRUQueue()
    {
        _oldestBlock = _newestBlock = ~unsigned(0x0);
    }
    LRUQueue::~LRUQueue() {}


        /////////////////////////////////////////////////////////////////////////////////
            //////   S I M P L E   S P A N N I N G   H E A P   //////
        /////////////////////////////////////////////////////////////////////////////////

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::Allocate(unsigned size)
    {
        const Marker sentinel = Marker(~0x0);
        Marker bestSize = sentinel;
        Marker internalSize = MarkerHeap<Marker>::ToInternalSize(MarkerHeap<Marker>::AlignSize(size));
        assert(MarkerHeap<Marker>::ToExternalSize(internalSize)>=size);

        if ((_largestFreeBlockValid && _largestFreeBlock<internalSize) || !_markers.size()) {
            return ~unsigned(0x0);  // threading can cause false return here -- but that shouldn't be a major issue
        }

        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif

            //  Marker array is simple -- just a list of positions. It will alternate between
            //  allocated and unallocated
        Marker largestFreeBlock[2] = {0,0};
        Marker largestFreeBlockPosition = 0;

        typename std::vector<Marker>::iterator best = _markers.end();
        for (auto i=_markers.begin(); i<(_markers.end()-1);i+=2) {
            Marker blockSize = *(i+1) - *i;
            if (blockSize >= internalSize && blockSize < bestSize) {
                bestSize = blockSize;
                best = i;
            }
            if (blockSize >= largestFreeBlock[0]) {
                largestFreeBlock[1] = largestFreeBlock[0];
                largestFreeBlock[0] = blockSize;
                largestFreeBlockPosition = *i;
            } else if (blockSize > largestFreeBlock[1]) {
                largestFreeBlock[1] = blockSize;
            }
        }

        if (bestSize == sentinel) {
            _largestFreeBlock = largestFreeBlock[0];
            _largestFreeBlockValid = true;
            assert(largestFreeBlock[0] < size);
            return ~unsigned(0x0);
        } 

        {
            if (largestFreeBlockPosition==*best) {
                _largestFreeBlock = std::max(Marker(largestFreeBlock[0]-internalSize), largestFreeBlock[1]);
            } else {
                _largestFreeBlock = largestFreeBlock[0];
            }
            _largestFreeBlockValid = true;
        }
        
        if (bestSize == internalSize) {
                //  Got an exact match. In this case we remove 2 markers, because the entire span has become 
                //  allocated, and should just merge into the spans around it.
            unsigned result = MarkerHeap<Marker>::ToExternalSize(*best);
            if (best == _markers.begin()) {
                if (_markers.size()==2) {   // special case for unallocated heap to fully allocated heap (0,0) -> (0,0,size)
                    _markers.insert(_markers.begin(), 0);
                } else {
                    *(best+1) = 0;
                }
            } else {
                if (best+2 >= _markers.end()) {
                    _markers.erase(best);
                } else {
                    _markers.erase(best, best+2);
                }
            }
            assert(_markers[0]==0);
            assert(_largestFreeBlock==CalculateLargestFreeBlock_Internal());
            if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
            return result;
        } else {
                //  We'll allocate from the start of the span space. 
            unsigned result = MarkerHeap<Marker>::ToExternalSize(*best);
            if (best == _markers.begin()) {
                    //      We're allocating from the start of the heap. But we can't move the marker
                    //      at the start of the heap, so we have to insert 2 more...
                Marker insertion[] = {0, internalSize};
                _markers.insert(_markers.begin()+1, insertion, &insertion[dimof(insertion)]);
            } else {
                *best += internalSize;
            }
            if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
            assert(_largestFreeBlock==CalculateLargestFreeBlock_Internal());
            return result;
        }
    }

    template <typename Marker>
        bool        SpanningHeap<Marker>::Allocate(unsigned ptr, unsigned size)
    {
        return BlockAdjust_Internal(ptr, size, true);
    }
    
    template <typename Marker>
        bool        SpanningHeap<Marker>::Deallocate(unsigned ptr, unsigned size)
    {
        return BlockAdjust_Internal(ptr, size, false);
    }

    template <typename Marker>
        bool        SpanningHeap<Marker>::BlockAdjust_Internal(unsigned ptr, unsigned size, bool allocateOperation)
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        Marker internalOffset = MarkerHeap<Marker>::ToInternalSize(ptr);
        Marker internalSize = MarkerHeap<Marker>::ToInternalSize(MarkerHeap<Marker>::AlignSize(size));
        if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}

        _largestFreeBlockValid = false; // have to recalculate largest free after deallocate. We could update based on local changes, but...

            // find the span in which this belongs, and mark the space deallocated
        typename std::vector<Marker>::iterator i = _markers.begin()+(allocateOperation?0:1);
        for (; i<(_markers.end()-1);i+=2) {
            Marker start = *i;
            Marker end = *(i+1);
            if (internalOffset >= start && internalOffset < end) {
                assert((internalOffset+internalSize) <= end);
                if (start == internalOffset) {
                    if (end == (internalOffset+internalSize)) {
                            // the entire span is begin destroyed.
                        if (i == _markers.begin() && allocateOperation) {
                            *(i+1) = 0;
                        } else if (i+2 >= _markers.end()) {
                            _markers.erase(i);
                        } else {
                            _markers.erase(i, i+2);
                        }
                        assert(_markers[0]==0);
                        if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
                        return true;
                    }

                        // we can just move the start marker up to cover the unallocated space
                    if (i == _markers.begin() && allocateOperation) {
                        Marker insertion[] = {internalOffset, Marker(internalOffset+internalSize)};
                        _markers.insert(i+1, insertion, &insertion[dimof(insertion)]);
                    } else {
                        *i = internalOffset+internalSize;
                    }
                    assert(_markers[0]==0);
                    if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
                    return true;
                } else if (end == (internalOffset+internalSize)) {
                        // move the end marker back to cover the space (but not if it's the end sentinel)
                    if (i+2 >= _markers.end()) {
                        _markers.insert(i+1, internalOffset);
                    } else {
                        *(i+1) = internalOffset;
                    }
                    assert(_markers[0]==0);
                    if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
                    return true;
                } else {
                        // create new markers to match the deallocated space
                    Marker insertion[] = {internalOffset, Marker(internalOffset+internalSize)};
                    _markers.insert(i+1, insertion, &insertion[dimof(insertion)]);
                    if (_markers.size()==2) {assert(_markers[0]==0 && _markers[1]!=0);}
                    return true;
                }
            }
        }

        assert(0);      // couldn't find it within our heap
        return false;
    }

    template <typename Marker>
        auto SpanningHeap<Marker>::CalculateLargestFreeBlock_Internal() const -> Marker
    {
        Marker largestBlock = 0;
        assert(!_markers.empty());
        typename std::vector<Marker>::const_iterator i = _markers.begin();
        for (; i<(_markers.end()-1);i+=2) {
            Marker start = *i;
            Marker end = *(i+1);
            largestBlock = std::max(largestBlock, Marker(end-start));
        }
        return largestBlock;
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateAvailableSpace_AlreadyLocked() const
    {
        unsigned result = 0;
        auto i = _markers.begin();
        for (; i<(_markers.end()-1);i+=2) {
            Marker start = *i;
            Marker end = *(i+1);
            result += end-start;
        }
        return MarkerHeap<Marker>::ToExternalSize(Marker(result));
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateLargestFreeBlock_AlreadyLocked() const
    {
        if (!_largestFreeBlockValid) {
            _largestFreeBlock = CalculateLargestFreeBlock_Internal();
            _largestFreeBlockValid = true;
        }
        return MarkerHeap<Marker>::ToExternalSize(_largestFreeBlock);
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateAvailableSpace() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        return CalculateAvailableSpace_AlreadyLocked();
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateLargestFreeBlock() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        return CalculateLargestFreeBlock_AlreadyLocked();
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateAllocatedSpace() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        if (_markers.empty()) return 0;

        unsigned result = 0;
        typename std::vector<Marker>::const_iterator i = _markers.begin()+1;
        for (; i<(_markers.end()-1);i+=2) {
            Marker start = *i;
            Marker end = *(i+1);
            result += end-start;
        }
        return MarkerHeap<Marker>::ToExternalSize(Marker(result));
    }

    template <typename Marker>
        unsigned    SpanningHeap<Marker>::CalculateHeapSize() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        if (_markers.empty()) {
            return 0;
        }
        return MarkerHeap<Marker>::ToExternalSize(_markers[_markers.size()-1]);
    }

    template <typename Marker>
        unsigned        SpanningHeap<Marker>::AppendNewBlock(unsigned size)
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        if (!_markers.size()) {
            _markers.push_back(0);
            _markers.push_back(0);
            _markers.push_back(MarkerHeap<Marker>::ToInternalSize(MarkerHeap<Marker>::AlignSize(size)));
            return 0;
        }

            // append a new block in an allocated status

        const bool endsInAllocatedBlock = _markers.size()&1;  // odd markers == ends in allocated block
        auto finalMarker = _markers[_markers.size()-1];
        auto newBlockInternalSize = MarkerHeap<Marker>::ToInternalSize(MarkerHeap<Marker>::AlignSize(size));
        assert((unsigned(finalMarker) + unsigned(newBlockInternalSize)) <= std::numeric_limits<Marker>::max());
        auto newEnd = Marker(finalMarker + newBlockInternalSize);
        if (endsInAllocatedBlock) {
            _markers[_markers.size()-1] = newEnd;     // just shift the end marker back
        } else {
            _markers.push_back(newEnd);               // add a final allocated block
        }

        return MarkerHeap<Marker>::ToExternalSize(finalMarker);
    }
    
    template <typename Marker>
        uint64_t      SpanningHeap<Marker>::CalculateHash() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        return Hash64(AsPointer(_markers.begin()), AsPointer(_markers.end()));
    }

    template <typename Marker>
        bool        SpanningHeap<Marker>::IsEmpty() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        return _markers.size() <= 2;
    }

    template <typename Marker>
        std::vector<unsigned> SpanningHeap<Marker>::CalculateMetrics() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif
        std::vector<unsigned> result;
        result.reserve(_markers.size());
        typename std::vector<Marker>::const_iterator i = _markers.begin();
        for (; i!=_markers.end();++i) {
            result.push_back(MarkerHeap<Marker>::ToExternalSize(*i));
        }
        #if defined(XL_DEBUG)
            assert(!_largestFreeBlockValid || _largestFreeBlock==CalculateLargestFreeBlock_Internal());
        #endif
        return result;
    }

    // static bool SortAllocatedBlocks_LargestToSmallest(const std::pair<MarkerHeap::Marker, MarkerHeap::Marker>& lhs, const std::pair<MarkerHeap::Marker, MarkerHeap::Marker>& rhs)
    // {
    //     return (lhs.second-lhs.first)>(rhs.second-rhs.first);
    // }

    template<typename Marker>
        static bool SortAllocatedBlocks_SmallestToLargest(
            const std::pair<Marker, Marker>& lhs, 
            const std::pair<Marker, Marker>& rhs)
    {
        return (lhs.second-lhs.first)<(rhs.second-rhs.first);
    }

    static bool SortDefragStep_SourceStart(const DefragStep& lhs, const DefragStep& rhs)
    {
        return lhs._sourceStart < rhs._sourceEnd;
    }

    static bool SortDefragStep_Destination(const DefragStep& lhs, const DefragStep& rhs)
    {
        return lhs._destination < rhs._destination;
    }

    template <typename Marker>
        std::vector<DefragStep> SpanningHeap<Marker>::CalculateDefragSteps() const
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif

        std::vector<std::pair<Marker, Marker> > allocatedBlocks;
        allocatedBlocks.reserve(_markers.size()/2);
        typename std::vector<Marker>::const_iterator i2 = _markers.begin()+1;
        for (; i2<(_markers.end()-1);i2+=2) {
            Marker start = *i2;
            Marker end   = *(i2+1);
            assert(start < end);
            allocatedBlocks.push_back(std::make_pair(start, end));
        }

            //
            //      Very simple heap compression method...
            //          we're going to be writing to a new buffer, so we don't have to worry about 
            //          writing over data that we'll read later on. So we can safely re-order the 
            //          blocks how we like.
            //
            //      We could write a method that only moves smaller blocks, without creating mirror
            //      resources... But that would require a convenient method to copy data from one
            //      area in a resource to another -- and that might not be possible efficiently in D3D.
            //

        std::sort(allocatedBlocks.begin(), allocatedBlocks.end(), SortAllocatedBlocks_SmallestToLargest<Marker>);

        std::vector<DefragStep> result;
        result.reserve(allocatedBlocks.size());

        Marker compressedPosition = 0;
        for (auto i=allocatedBlocks.begin(); i!=allocatedBlocks.end(); ++i) {
            assert(i->first < i->second);
            DefragStep step;
            step._sourceStart    = MarkerHeap<Marker>::ToExternalSize(i->first);
            step._sourceEnd      = MarkerHeap<Marker>::ToExternalSize(i->second);
            step._destination    = MarkerHeap<Marker>::ToExternalSize(compressedPosition);
            assert(step._destination < 512*1024);
            assert((step._destination + step._sourceEnd - step._sourceStart) <= MarkerHeap<Marker>::ToExternalSize(_markers[_markers.size()-1]));
            assert(step._sourceStart < step._sourceEnd);
            compressedPosition += i->second - i->first;
            result.push_back(step);
        }

        std::sort(result.begin(), result.end(), SortDefragStep_SourceStart);

            // check for sane boundary
        #if defined(XL_DEBUG)
            for (std::vector<DefragStep>::iterator i=result.begin(); i!=result.end(); ++i) {
                assert(i->_destination < 512*1024);
            }
        #endif

        return result;
    }

    template <typename Marker>
        void        SpanningHeap<Marker>::PerformDefrag(const std::vector<DefragStep>& defrag)
    {
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif

            //
            //      All of the spans in the heap have moved about we have to recalculate the
            //      allocated spans from scratch, based on the positions of the new blocks
            //
        #if defined(_DEBUG)
            unsigned startingAvailableSize = CalculateAvailableSpace_AlreadyLocked(); (void)startingAvailableSize;
            unsigned startingLargestBlock = CalculateLargestFreeBlock_AlreadyLocked(); (void)startingLargestBlock;
        #endif

        Marker heapEnd = _markers[_markers.size()-1];
        _markers.erase(_markers.begin(), _markers.end());
        _markers.push_back(0);
        if (!defrag.empty()) {
            std::vector<DefragStep> defragByDestination(defrag);
            std::sort(defragByDestination.begin(), defragByDestination.end(), SortDefragStep_Destination);

            Marker currentAllocatedBlockBegin    = MarkerHeap<Marker>::ToInternalSize(defragByDestination.begin()->_destination);
            Marker currentAllocatedBlockEnd      = MarkerHeap<Marker>::ToInternalSize(defragByDestination.begin()->_destination + MarkerHeap<Marker>::AlignSize(defragByDestination.begin()->_sourceEnd-defragByDestination.begin()->_sourceStart));

            for (std::vector<DefragStep>::const_iterator i=defragByDestination.begin()+1; i!=defragByDestination.end(); ++i) {
                Marker blockBegin    = MarkerHeap<Marker>::ToInternalSize(i->_destination);
                Marker blockEnd      = MarkerHeap<Marker>::ToInternalSize(i->_destination+MarkerHeap<Marker>::AlignSize(i->_sourceEnd-i->_sourceStart));

                if (blockBegin == currentAllocatedBlockEnd) {
                    currentAllocatedBlockEnd = blockEnd;
                } else {
                    _markers.push_back(currentAllocatedBlockBegin);
                    _markers.push_back(currentAllocatedBlockEnd);
                    currentAllocatedBlockBegin = blockBegin;
                    currentAllocatedBlockEnd = blockEnd;
                }
            }

            _markers.push_back(currentAllocatedBlockBegin);
            _markers.push_back(currentAllocatedBlockEnd);
        }
        _markers.push_back(heapEnd);
        _largestFreeBlockValid = false;

        #if defined(_DEBUG)
            unsigned newAvailableSpace = CalculateAvailableSpace_AlreadyLocked(); (void)newAvailableSpace;
            unsigned newLargestBlock = CalculateLargestFreeBlock_AlreadyLocked(); (void)newLargestBlock;
            assert(newAvailableSpace == startingAvailableSize);
            assert(newLargestBlock >= startingLargestBlock);        // sometimes the tests will run a defrag that doesn't reduce the largest block
        #endif
    }

    template <typename Marker>
        std::pair<std::unique_ptr<uint8_t[]>, size_t> SpanningHeap<Marker>::Flatten() const
    {
        // return a "serialized" / flattened representation of this heap
        //  -- useful to write it out to disk, or store in a compact form
        #if defined(_DEBUG)
            std::unique_lock lockTest(_lock, std::try_to_lock);
            assert(lockTest);
        #endif

        if (_markers.size() >= 2) {
            for (auto i=_markers.cbegin()+1; i!=_markers.cend(); ++i) {
                assert(*(i-1) <= *i);
            }
        }

        size_t resultSize = sizeof(Marker) * _markers.size();
        auto result = std::make_unique<uint8_t[]>(resultSize);
        XlCopyMemory(result.get(), AsPointer(_markers.begin()), resultSize);
        return std::make_pair(std::move(result), resultSize);
    }

    template <typename Marker>
        SpanningHeap<Marker>::SpanningHeap(unsigned size)
    {
        _markers.reserve(64);
        _markers.push_back(0);
        _markers.push_back(MarkerHeap<Marker>::ToInternalSize(MarkerHeap<Marker>::AlignSize(size)));
        _largestFreeBlockValid = false;
        _largestFreeBlock = 0;
    }

    template <typename Marker>
        SpanningHeap<Marker>::SpanningHeap(const uint8_t flattened[], size_t flattenedSize)
    {
        _largestFreeBlockValid = false;
        _largestFreeBlock = 0;
            // flattened rep is just a copy of the markers array... we can copy it straight in...
        auto markerCount = flattenedSize / sizeof(Marker);
        _markers.resize(markerCount);
        std::copy(
            (const Marker*)flattened, (const Marker*)PtrAdd(flattened, markerCount * sizeof(Marker)), 
            _markers.begin());

            // make sure things are in the right order
        if (_markers.size() >= 2) {
            for (auto i=_markers.cbegin()+1; i!=_markers.cend(); ++i) {
                assert(*(i-1) <= *i);
            }
        }
    }

    template <typename Marker>
        SpanningHeap<Marker>::SpanningHeap(SpanningHeap&& moveFrom) never_throws
    : _markers(std::move(moveFrom._markers))
    , _largestFreeBlock(moveFrom._largestFreeBlock)
    , _largestFreeBlockValid(moveFrom._largestFreeBlockValid) 
    {
    }

    template <typename Marker>
        auto SpanningHeap<Marker>::operator=(SpanningHeap&& moveFrom) never_throws -> const SpanningHeap&
    {
        _markers = std::move(moveFrom._markers);
        _largestFreeBlock = moveFrom._largestFreeBlock;
        _largestFreeBlockValid = moveFrom._largestFreeBlockValid;
        return *this;
    }

    template <typename Marker>
        SpanningHeap<Marker>::SpanningHeap(const SpanningHeap<Marker>& cloneFrom) 
    : _markers(cloneFrom._markers)
    , _largestFreeBlock(cloneFrom._largestFreeBlock)
    , _largestFreeBlockValid(cloneFrom._largestFreeBlockValid) 
    {}

    template <typename Marker>
        const SpanningHeap<Marker>& SpanningHeap<Marker>::operator=(const SpanningHeap<Marker>& cloneFrom)
    {
        _markers = cloneFrom._markers;
        _largestFreeBlock = cloneFrom._largestFreeBlock;
        _largestFreeBlockValid = cloneFrom._largestFreeBlockValid;
        return *this;
    }

    template <typename Marker>
        SpanningHeap<Marker>::SpanningHeap() : _largestFreeBlock(0), _largestFreeBlockValid(false) {}

    template <typename Marker>
        SpanningHeap<Marker>::~SpanningHeap()
    {}

    template class SpanningHeap<uint16_t>;
    template class SpanningHeap<uint32_t>;
}

