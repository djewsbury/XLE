// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HeapUtils.h"
#include "PtrUtils.h"
#include "MemoryUtils.h"
#include "BitUtils.h"
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
        #if defined(_DEBUG)
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

    static bool SortDefragStep_SourceStart(const RepositionStep& lhs, const RepositionStep& rhs)
    {
        return lhs._sourceStart < rhs._sourceEnd;
    }

    static bool SortDefragStep_Destination(const RepositionStep& lhs, const RepositionStep& rhs)
    {
        return lhs._destination < rhs._destination;
    }

    template <typename Marker>
        std::vector<RepositionStep> SpanningHeap<Marker>::CalculateHeapCompression() const
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

        std::vector<RepositionStep> result;
        result.reserve(allocatedBlocks.size());

        Marker compressedPosition = 0;
        for (auto i=allocatedBlocks.begin(); i!=allocatedBlocks.end(); ++i) {
            assert(i->first < i->second);
            RepositionStep step;
            step._sourceStart    = MarkerHeap<Marker>::ToExternalSize(i->first);
            step._sourceEnd      = MarkerHeap<Marker>::ToExternalSize(i->second);
            step._destination    = MarkerHeap<Marker>::ToExternalSize(compressedPosition);
            assert((step._destination + step._sourceEnd - step._sourceStart) <= MarkerHeap<Marker>::ToExternalSize(_markers[_markers.size()-1]));
            assert(step._sourceStart < step._sourceEnd);
            compressedPosition += i->second - i->first;
            result.push_back(step);
        }

        std::sort(result.begin(), result.end(), SortDefragStep_SourceStart);

        return result;
    }

    template <typename Marker>
        auto SpanningHeap<Marker>::CalculateIncrementalDefragCandidate() const -> IncrementalDefragCandidate
    {
        // Look for cases where we can move some data, and in doing so merge 2 unallocated blocks
        // to form a larger unallocated block
        // We're assuming there that the moved data will go to somewhere outside of this heap -- ie
        // the returned "destinations" can overlap with non-repositioned data in this heap
        if (_markers.size() <= 3) return {};

        IncrementalDefragCandidate result;
        result._newLargestFreeBlock = 0;
        unsigned destinationIterator = 0;
        auto meaningfulSizeThreshold = *(_markers.end()-1)/8;
        auto jumpOverGapThreshold = MarkerHeap<Marker>::ToInternalSize(8*1024);

        auto i = _markers.begin();
        unsigned preceedingUnallocatedSpace = *(i+1)-*i;
        ++i;
        if (!preceedingUnallocatedSpace) {
            ++i; // we never move a block allocated at 0 so skip directly over that and the next unallocated space
            if ((i+1) == _markers.end()) return {};
            preceedingUnallocatedSpace = *(i+1)-*i;
            ++i;
        }
        for (;i<_markers.end()-2;) {
            auto endRun = i+1;
            auto successiveUnallocatedSpace = *(endRun+1) - *endRun;
            while (successiveUnallocatedSpace < jumpOverGapThreshold && endRun < _markers.end()-3) {
                // skip over tiny empty spaces
                endRun += 2;
                successiveUnallocatedSpace = *(endRun+1) - *endRun;
            }
            auto allocatedSpace = *endRun - *i;

            // If moving this block will expand the contiguous unallocated space sufficiently,
            // then let's do it
            // In certain situations, this can even move all allocations from the heap
            if (allocatedSpace < preceedingUnallocatedSpace && allocatedSpace < successiveUnallocatedSpace
                && preceedingUnallocatedSpace+allocatedSpace+successiveUnallocatedSpace >= meaningfulSizeThreshold) {
                result._steps.push_back(RepositionStep{*i, *endRun, destinationIterator});
                destinationIterator += allocatedSpace;
                preceedingUnallocatedSpace += allocatedSpace + successiveUnallocatedSpace;
                result._newLargestFreeBlock = std::max(result._newLargestFreeBlock, preceedingUnallocatedSpace);
            } else {
                preceedingUnallocatedSpace = successiveUnallocatedSpace;
            }
            i = endRun+1;
        }
        return result;
    }

    template <typename Marker>
        void        SpanningHeap<Marker>::PerformReposition(IteratorRange<const RepositionStep*> defrag)
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
            std::vector<RepositionStep> defragByDestination(defrag.begin(), defrag.end());
            std::sort(defragByDestination.begin(), defragByDestination.end(), SortDefragStep_Destination);

            Marker currentAllocatedBlockBegin    = MarkerHeap<Marker>::ToInternalSize(defragByDestination.begin()->_destination);
            Marker currentAllocatedBlockEnd      = MarkerHeap<Marker>::ToInternalSize(defragByDestination.begin()->_destination + MarkerHeap<Marker>::AlignSize(defragByDestination.begin()->_sourceEnd-defragByDestination.begin()->_sourceStart));

            for (auto i=defragByDestination.begin()+1; i!=defragByDestination.end(); ++i) {
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

    unsigned	CircularHeap::AllocateBack(unsigned size)
	{
		if (_start == _end) return ~0u;
		if (_start > _end) {
			if ((_start - _end) >= size) {
				auto result = _end;
				_end += size;
				return result;
			}
		} else if ((_end + size) <= _heapSize) {
			auto result = _end;
			_end += size;
			return result;
		} else if (_start >= size) { // this is the wrap around case
			_end = size;
			return 0u;
		}

		return ~0u;
	}

    unsigned	CircularHeap::AllocateBack(unsigned size, unsigned alignment)
    {
        assert(alignment);
        if (_start == _end) return ~0u;
		auto alignedEnd = CeilToMultiple(_end, alignment);
        if (_start > _end) {
			if ((_start - alignedEnd) >= size) {
				auto result = alignedEnd;
				_end = alignedEnd + size;
				return result;
			}
		} else if ((alignedEnd + size) <= _heapSize) {
			auto result = alignedEnd;
			_end = alignedEnd + size;
			return result;
		} else if (_start >= size) {
			_end = size;
			return 0u;
		}

		return ~0u;
    }

    void		CircularHeap::UndoLastAllocation(unsigned size)
    {
        // Roll back the last allocation we performed with AllocateBack
        // This can also be used to shrink the size of the last allocation
        // (for example, if it was an estimate) by giving the number of bytes
        // we want to give back to the heap
        assert(_end >= size);
        _end -= size;
    }

    auto CircularHeap::GetQuickMetrics() const -> QuickMetrics
    {
        QuickMetrics result;
        if (_start == _end) {
            result._bytesAllocated = _heapSize;
            result._maxNextBlockBytes = 0;
        } else if (_start > _end) {
            result._maxNextBlockBytes = _start - _end;
            result._bytesAllocated = _heapSize - result._maxNextBlockBytes;
        } else {
            result._maxNextBlockBytes = std::max(_heapSize - _end, _start);
            result._bytesAllocated = _end - _start;
        }
        result._front = _start;
        result._back = _end;
        return result;
    }

	void		CircularHeap::ResetFront(unsigned newFront)
	{
		_start = newFront;
		if (_start == _end) {
			_start = _heapSize;
			_end = 0;
		}
	}

	CircularHeap::CircularHeap(unsigned heapSize)
	{
		_start = heapSize;
		_end = 0;
		_heapSize = heapSize;
	}

	CircularHeap::CircularHeap()
    {
        _start = _end = _heapSize = 0;
    }

	CircularHeap::~CircularHeap() {}


        /////////////////////////////////////////////////////////////////////////////////
            //////   R E F E R E N C E   C O U N T I N G   L A Y E R   //////
        /////////////////////////////////////////////////////////////////////////////////

    std::pair<signed,signed> ReferenceCountingLayer::AddRef(unsigned start, unsigned size, const char name[])
    {
        Marker internalStart = ToInternalSize(start);
        Marker internalSize = ToInternalSize(AlignSize(size));

        if (_entries.empty()) {
            Entry newBlock;
            newBlock._start = internalStart;
            newBlock._end = internalStart+internalSize;
            newBlock._refCount = 1;
            DEBUG_ONLY(if (name) newBlock._name = name);
            _entries.insert(_entries.end(), newBlock);
            return std::make_pair(newBlock._refCount, newBlock._refCount);
        }

        std::vector<Entry>::iterator i = std::lower_bound(_entries.begin(), _entries.end(), internalStart, CompareStart());
        if (i != _entries.begin() && ((i-1)->_end > internalStart)) {
            --i;
        }

        Marker currentStart = internalStart;
        Marker internalEnd = internalStart+internalSize;
        signed refMin = INT_MAX, refMax = INT_MIN;
        for (;;++i) {
            if (i >= _entries.end() || currentStart < i->_start) {
                    //      this this is past the end of any other blocks -- add new a block
                Entry newBlock;
                newBlock._start = currentStart;
                newBlock._end = std::min(internalEnd, Marker((i<_entries.end())?i->_start:INT_MAX));
                newBlock._refCount = 1;
                DEBUG_ONLY(if (name) newBlock._name = name);
                assert(newBlock._start < newBlock._end);
                assert(newBlock._end != 0xbaad);
                bool end = i >= _entries.end() || internalEnd <= i->_start;
                i = _entries.insert(i, newBlock)+1;
                refMin = std::min(refMin, 1); refMax = std::max(refMax,1);
                if (end) {
                    break;  // it's the end
                }
                currentStart = i->_start;
            }

            if (i->_start == currentStart) {
                if (internalEnd >= i->_end) {
                        // we've hit a block identical to the one we're looking for. Just increase the ref count
                    signed newRefCount = ++i->_refCount;
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    assert(i->_start < i->_end);
                    assert(i->_end != 0xbaad);
                    currentStart = i->_end;
                    if (name && name[0]) {
                        DEBUG_ONLY(i->_name = name);     // we have to take on the new name here. Sometimes we'll get a number of sub blocks inside of a super block. The last sub block will allocate the entirely remaining part of the super block. When this happens, rename to the sub block name.
                    }
                    if (internalEnd == i->_end) {
                        break;  // it's the end
                    }
                } else {
                        // split the block and add a new one in front
                    Entry newBlock;
                    newBlock._start = i->_start;
                    newBlock._end = internalEnd;
                    DEBUG_ONLY(if (name) newBlock._name = name);
                    signed newRefCount = newBlock._refCount = i->_refCount+1;
                    i->_start = internalEnd;
                    assert(newBlock._start < newBlock._end && i->_start < i->_end);
                    assert(i->_end != 0xbaad && newBlock._end != 0xbaad);
                    _entries.insert(i, newBlock);
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    break;  // it's the end
                }
            } else {
                if (internalEnd < i->_end) {
                        //  This is a block that falls entirely within the old block. We need to create a new block, splitting the old one if necessary
                        // we need do split the end part of the old block off too, and then insert 2 blocks
                    Entry newBlock[2];
                    newBlock[0]._start = i->_start;
                    newBlock[0]._end = currentStart;
                    newBlock[0]._refCount = i->_refCount;
                    DEBUG_ONLY(newBlock[0]._name = i->_name);
                    newBlock[1]._start = currentStart;
                    newBlock[1]._end = internalEnd;
                    DEBUG_ONLY(if (name) newBlock[1]._name = name);
                    signed newRefCount = newBlock[1]._refCount = i->_refCount+1;
                    i->_start = internalEnd;
                    assert(newBlock[0]._start < newBlock[0]._end && newBlock[1]._start < newBlock[1]._end&& i->_start < i->_end);
                    assert(i->_end != 0xbaad && newBlock[0]._end != 0xbaad && newBlock[1]._end != 0xbaad);
                    _entries.insert(i, newBlock, &newBlock[2]);
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    break;
                } else {
                    Marker iEnd = i->_end;
                    Entry newBlock;
                    newBlock._start = i->_start;
                    newBlock._end = currentStart;
                    newBlock._refCount = i->_refCount;
                    DEBUG_ONLY(newBlock._name.swap(i->_name));
                    i->_start = currentStart;
                    DEBUG_ONLY(if (name) i->_name = name);
                    signed newRefCount = ++i->_refCount;
                    assert(newBlock._start < newBlock._end && i->_start < i->_end);
                    assert(i->_end != 0xbaad && newBlock._end != 0xbaad);
                    i = _entries.insert(i, newBlock)+1;
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);

                    if (internalEnd == iEnd) {
                        break;
                    }

                    currentStart = iEnd;
                        // this block extends into the next area -- so continue around the loop again
                }
            }
        }

        return std::make_pair(refMin, refMax);
    }

    size_t ReferenceCountingLayer::Validate()
    {
        size_t result = 0;
        for (std::vector<Entry>::iterator i=_entries.begin(); i<_entries.end(); ++i) {
            assert(i->_start < i->_end);
            if ((i+1)<_entries.end()) {
                assert(i->_end <= (i+1)->_start);
            }
            assert(i->_start <= 0x800 && i->_end <= 0x800);
            result += i->_refCount*size_t(i->_end-i->_start);
        }
        return result;
    }

    unsigned ReferenceCountingLayer::CalculatedReferencedSpace() const
    {
        unsigned result = 0;
        for (std::vector<Entry>::const_iterator i=_entries.begin(); i<_entries.end(); ++i) {
            result += ToExternalSize(i->_end-i->_start);
        }
        return result;
    }

    void ReferenceCountingLayer::PerformDefrag(const std::vector<RepositionStep>& defrag)
    {
        std::vector<Entry>::iterator entryIterator = _entries.begin();
        for (   std::vector<RepositionStep>::const_iterator s=defrag.begin(); 
                s!=defrag.end() && entryIterator!=_entries.end();) {
            unsigned entryStart  = ToExternalSize(entryIterator->_start);
            unsigned entryEnd    = ToExternalSize(entryIterator->_end);
            if (s->_sourceEnd <= entryStart) {
                ++s;
                continue;
            }

            if (s->_sourceStart >= entryEnd) {
                //      This deallocate iterator doesn't have an adjustment
                ++entryIterator;
                continue;
            }

                //
                //      We shouldn't have any blocks that are stretched between multiple 
                //      steps. If we've got a match it must match the entire deallocation block
                //
            assert(entryStart >= s->_sourceStart && entryStart < s->_sourceEnd);
            assert(entryEnd > s->_sourceStart && entryEnd <= s->_sourceEnd);

            signed offset = s->_destination - signed(s->_sourceStart);
            entryIterator->_start    = ToInternalSize(entryStart+offset);
            entryIterator->_end      = ToInternalSize(entryEnd+offset);
            ++entryIterator;
        }

            //  The defrag process may have modified the order of the entries (in the heap space)
            //  We need to resort by start
        std::sort(_entries.begin(), _entries.end(), CompareStart());
    }

    std::pair<signed,signed> ReferenceCountingLayer::Release(unsigned start, unsigned size)
    {
        Marker internalStart = ToInternalSize(start);
        Marker internalSize = ToInternalSize(AlignSize(size));

        if (_entries.empty()) {
            return std::make_pair(INT_MIN, INT_MIN);
        }

        std::vector<Entry>::iterator i = std::lower_bound(_entries.begin(), _entries.end(), internalStart, CompareStart());
        if (i != _entries.begin() && ((i-1)->_end > internalStart)) {
            --i;
        }

        Marker currentStart = internalStart;
        Marker internalEnd = internalStart+internalSize;
        signed refMin = INT_MAX, refMax = INT_MIN;
        for (;;) {
            if (i >= _entries.end() || currentStart < i->_start) {
                if (i >= _entries.end() || internalEnd <= i->_start)
                    break;
                currentStart = i->_start;
            }
            assert(i>=_entries.begin() && i<_entries.end());

            #if defined(_DEBUG)
                if (i->_start == currentStart) {
                    assert(internalEnd >= i->_end);
                } else {
                    assert(currentStart >= i->_end);
                }
            #endif

            if (i->_start == currentStart) {
                if (internalEnd >= i->_end) {
                    signed newRefCount = --i->_refCount;
                    Marker iEnd = i->_end;
                    if (!newRefCount) {
                        i = _entries.erase(i);
                    }
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    if (internalEnd == iEnd) {
                        break;  // it's the end
                    }
                    currentStart = iEnd;
                    if (!newRefCount)
                        continue; // continue to skip the next ++i (because we've just done an erase)
                } else {
                        // split the block and add a new one in front
                    signed newRefCount = i->_refCount-1;
                    if (newRefCount == 0) {
                        i->_start = internalEnd;
                    } else {
                        Entry newBlock;
                        newBlock._start = currentStart;
                        newBlock._end = internalEnd;
                        newBlock._refCount = newRefCount;
                        DEBUG_ONLY(newBlock._name = i->_name);
                        i->_start = internalEnd;
                        assert(newBlock._start < newBlock._end && i->_start < i->_end);
                        assert(i->_end != 0xbaad && newBlock._end != 0xbaad);
                        i = _entries.insert(i, newBlock);
                    }
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    break;  // it's the end
                }
            } else {
                if (internalEnd < i->_end) {
                    signed newRefCount = i->_refCount-1;
                    if (newRefCount==0) {
                        Entry newBlock;
                        newBlock._start = i->_start;
                        newBlock._end = currentStart;
                        newBlock._refCount = i->_refCount;
                        DEBUG_ONLY(newBlock._name = i->_name);
                        i->_start = internalEnd;
                        assert(newBlock._start < newBlock._end && i->_start < i->_end);
                        assert(i->_end != 0xbaad && newBlock._end != 0xbaad);
                        i = _entries.insert(i, newBlock);
                    } else {
                            //  This is a block that falls entirely within the old block. We need to create a new block, splitting the old one if necessary
                            // we need do split the end part of the old block off too, and then insert 2 blocks
                        Entry newBlock[2];
                        newBlock[0]._start = i->_start;
                        newBlock[0]._end = currentStart;
                        newBlock[0]._refCount = i->_refCount;
                        DEBUG_ONLY(newBlock[0]._name = i->_name);
                        newBlock[1]._start = currentStart;
                        newBlock[1]._end = internalEnd;
                        newBlock[1]._refCount = newRefCount;
                        DEBUG_ONLY(newBlock[1]._name = i->_name);
                        i->_start = internalEnd;
                        assert(newBlock[0]._start < newBlock[0]._end && newBlock[1]._start < newBlock[1]._end && i->_start < i->_end);
                        assert(i->_end != 0xbaad && newBlock[0]._end != 0xbaad && newBlock[1]._end != 0xbaad);
                        size_t offset = std::distance(_entries.begin(),i);
                        _entries.insert(i, newBlock, &newBlock[2]);
                        i = _entries.begin()+offset+2;
                    }
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);
                    break;
                } else {
                    Marker iEnd = i->_end;
                    signed newRefCount = i->_refCount-1;
                    if (newRefCount==0) {
                        i->_end = currentStart;
                    } else {
                        Entry newBlock;
                        newBlock._start = i->_start;
                        newBlock._end = currentStart;
                        newBlock._refCount = i->_refCount;
                        DEBUG_ONLY(newBlock._name = i->_name);
                        i->_start = currentStart;
                        i->_refCount = newRefCount;
                        assert(i>=_entries.begin() && i<_entries.end());
                        assert(newBlock._start < newBlock._end && i->_start < i->_end);
                        assert(i->_end != 0xbaad && newBlock._end != 0xbaad);
                        i = _entries.insert(i, newBlock)+1;
                    }
                    refMin = std::min(refMin, newRefCount); refMax = std::max(refMax, newRefCount);

                    if (internalEnd == iEnd) {
                        break;
                    }

                    currentStart = iEnd;
                        // this block extends into the next area -- so continue around the loop again
                }
            }

            ++i;
        }

        return std::make_pair(refMin, refMax);
    }

    bool        ReferenceCountingLayer::ValidateBlock(unsigned start, unsigned size) const
    {
        Marker internalStart = ToInternalSize(start);
        Marker internalEnd = internalStart+ToInternalSize(AlignSize(size));
        std::vector<Entry>::const_iterator i = std::lower_bound(_entries.begin(), _entries.end(), internalStart, CompareStart());
        return (i != _entries.end() && i->_start == internalStart && i->_end == internalEnd);
    }

    ReferenceCountingLayer::ReferenceCountingLayer(size_t size)
    {
        _entries.reserve(64);
    }

    ReferenceCountingLayer::ReferenceCountingLayer(const ReferenceCountingLayer& cloneFrom)
    : _entries(cloneFrom._entries)
    {}

}

