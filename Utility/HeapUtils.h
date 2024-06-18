// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "ArithmeticUtils.h"
#include "StringUtils.h"
#include "BitUtils.h"
#include "Threading/Mutex.h"
#include <vector>
#include <algorithm>
#include <memory>
#include <limits>
#include <assert.h>

namespace Utility
{
    class LRUQueue
    {
    public:
        unsigned GetOldestValue() const;
        void BringToFront(unsigned value);
        void SendToBack(unsigned linearAddress);
        void DisconnectOldest();
        unsigned QueueDepth() const;
        bool HasValue(unsigned value) const;

        LRUQueue(unsigned maxValues);
        LRUQueue();
        ~LRUQueue();

        LRUQueue(LRUQueue&& moveFrom) never_throws;
        LRUQueue& operator=(LRUQueue&& moveFrom) never_throws;
        LRUQueue(const LRUQueue& copyFrom);
        LRUQueue& operator=(const LRUQueue& copyFrom);
    protected:
        std::vector<std::pair<unsigned, unsigned>>  _lruQueue;
        unsigned    _oldestBlock;
        unsigned    _newestBlock;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    enum class LRUCacheInsertType { Add, Update, EvictAndReplace, Fail };
    template<typename Type> class LRUCache
    {
    public:
        LRUCacheInsertType Insert(uint64_t hashName, Type&& object);
        const Type* Get(uint64_t hashName);

        IteratorRange<const Type*> GetObjects() const { return MakeIteratorRange(_objects); }
        unsigned GetCacheSize() const { return _cacheSize; }

        LRUCache(unsigned cacheSize);
        ~LRUCache();

        LRUCache(LRUCache&& moveFrom) never_throws;
        LRUCache& operator=(LRUCache&& moveFrom) never_throws;
    protected:
        std::vector<Type> _objects;
        std::vector<std::pair<uint64_t, unsigned>> _lookupTable;
        LRUQueue _queue;
        unsigned _cacheSize;
    };

    template<typename Type>
        using LRUCachePtr = LRUCache<std::shared_ptr<Type>>;

    template<typename Type>
        LRUCacheInsertType LRUCache<Type>::Insert(uint64_t hashName, Type&& object)
    {
            // try to insert this object into the cache (if it's not already here)
        auto i = std::lower_bound(_lookupTable.cbegin(), _lookupTable.cend(), hashName, CompareFirst<uint64_t, unsigned>());
        if (i != _lookupTable.cend() && i->first == hashName) {
                // already here! But we should replace, this might be an update operation
            _objects[i->second] = std::move(object);
            _queue.BringToFront(i->second);
            return LRUCacheInsertType::Update;
        }

        if (_objects.size() < _cacheSize) {
            _objects.push_back(std::move(object));
            _lookupTable.insert(i, std::make_pair(hashName, unsigned(_objects.size()-1)));
            _queue.BringToFront(unsigned(_objects.size()-1));
            return LRUCacheInsertType::Add;
        }

            // we need to evict an existing object.
        unsigned eviction = _queue.GetOldestValue();
        if (eviction == ~unsigned(0x0)) {
            return LRUCacheInsertType::Fail;
        }

        _objects[eviction] = std::move(object);
        auto oldLookup = std::find_if(_lookupTable.cbegin(), _lookupTable.cend(), 
            [=](const std::pair<uint64_t, unsigned>& p) { return p.second == eviction; });
        assert(oldLookup != _lookupTable.cend());
        _lookupTable.erase(oldLookup);

            // have to search again after the erase above
        i = std::lower_bound(_lookupTable.cbegin(), _lookupTable.cend(), hashName, CompareFirst<uint64_t, unsigned>());
        _lookupTable.insert(i, std::make_pair(hashName, eviction));

        _queue.BringToFront(eviction);
        return LRUCacheInsertType::EvictAndReplace;
    }

    template<typename Type>
        const Type* LRUCache<Type>::Get(uint64_t hashName)
    {
            // find the given object, and move it to the front of the queue
        auto i = std::lower_bound(_lookupTable.cbegin(), _lookupTable.cend(), hashName, CompareFirst<uint64_t, unsigned>());
        if (i != _lookupTable.cend() && i->first == hashName) {
            _queue.BringToFront(i->second);
            return &_objects[i->second];
        }
        return nullptr;
    }

    template<typename Type>
        LRUCache<Type>::LRUCache(unsigned cacheSize)
    : _queue(cacheSize) 
    , _cacheSize(cacheSize)
    {
        _lookupTable.reserve(cacheSize);
        _objects.reserve(cacheSize);
    }

    template<typename Type>
        LRUCache<Type>::~LRUCache()
    {}

    template<typename Type>
        LRUCache<Type>::LRUCache(LRUCache<Type>&& moveFrom) never_throws
    : _objects(std::move(moveFrom._objects))
    , _lookupTable(std::move(moveFrom._lookupTable))
    , _queue(std::move(moveFrom._queue))
    , _cacheSize(moveFrom._cacheSize)
    {
        moveFrom._cacheSize = 0;
    }

    template<typename Type>
        LRUCache<Type>& LRUCache<Type>::operator=(LRUCache<Type>&& moveFrom) never_throws
    {
        _objects = std::move(moveFrom._objects);
        _lookupTable = std::move(moveFrom._lookupTable);
        _queue = std::move(moveFrom._queue);
        _cacheSize = moveFrom._cacheSize;
        moveFrom._cacheSize = 0;
        return *this;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Type> class FrameByFrameLRUHeap
    {
    public:
        struct QueryResult
        {
            Type& GetExisting();
            void Set(Type&& newValue);
            LRUCacheInsertType GetType() { return _type; }

            LRUCacheInsertType _type;
            typename std::vector<Type>::iterator _i;

            FrameByFrameLRUHeap<Type>* _heap = nullptr;
            typename std::vector<std::pair<uint64_t, unsigned>>::iterator _lookupTableIterator;
            uint64_t _hashName = 0;
        };
        QueryResult Query(uint64_t hashName);

        void OnFrameBarrier();

        struct Record { Type _value; unsigned _decayFrames = 0; };
        std::vector<Record> LogRecords() const;
        bool UnrecordedTest(uint64_t hashName);        // check if something is cached, without recording the cache (usually for debugging)
        IteratorRange<Type*> GetRawObjects() { return MakeIteratorRange(_objects); }
        IteratorRange<const Type*> GetRawObjects() const { return _objects; }

        FrameByFrameLRUHeap(unsigned cacheSize, unsigned decayGracePeriod = 32);
        ~FrameByFrameLRUHeap();
    private:
        std::vector<Type> _objects;
        std::vector<std::pair<uint64_t, unsigned>> _lookupTable;
        struct StateEntry { uint64_t _usedThisFrame = 0, _inDecayHeap = 0; };
        std::vector<StateEntry> _stateBits;
        std::vector<unsigned> _decayStartFrames;
        LRUQueue _lruQueue;
        unsigned _currentFrame = 0;
        unsigned _decayGracePeriod = 32;
        unsigned _cacheSize;
    };

    template<typename Type>
        auto FrameByFrameLRUHeap<Type>::Query(uint64_t hashName) -> QueryResult
    {
        auto i = std::lower_bound(_lookupTable.begin(), _lookupTable.end(), hashName, CompareFirst<uint64_t, unsigned>());
        if (i != _lookupTable.cend() && i->first == hashName) {
            unsigned idx = i->second;
            _stateBits[idx/64]._usedThisFrame |= 1ull<<uint64_t(idx);
            return QueryResult { LRUCacheInsertType::Update, _objects.begin() + idx };
        }

        if (_objects.size() < _cacheSize)
            return QueryResult { LRUCacheInsertType::Add, {}, this, i, hashName };

        // we need to evict an existing object
        unsigned eviction;
        for (;;) {
            eviction = _lruQueue.GetOldestValue();
            if (eviction == ~unsigned(0x0))
                return { LRUCacheInsertType::Fail };

            assert(_stateBits[eviction/64]._inDecayHeap & (1ull<<uint64_t(eviction%64)));
            // If this "oldest" entry was actually used this frame, we need to do some patch-up work
            if (_stateBits[eviction/64]._usedThisFrame & (1ull<<uint64_t(eviction%64))) {
                _stateBits[eviction/64]._inDecayHeap ^= 1ull<<uint64_t(eviction%64);
                _lruQueue.DisconnectOldest();
            } else
                break;
        }

        if ((_currentFrame - _decayStartFrames[eviction]) < _decayGracePeriod)
            return { LRUCacheInsertType::Fail };

        return QueryResult { LRUCacheInsertType::EvictAndReplace, _objects.begin() + eviction, this, i, hashName };
    }

    template<typename Type>
        void FrameByFrameLRUHeap<Type>::OnFrameBarrier()
    {
        // any objects not used this frame, but not in the decay heap get sent there now
        for (unsigned plane=0; plane<_stateBits.size(); ++plane) {
            auto notUsedNotInDecay = (~_stateBits[plane]._usedThisFrame) & (~_stateBits[plane]._inDecayHeap);
            auto usedAndInDecay = _stateBits[plane]._usedThisFrame & _stateBits[plane]._inDecayHeap;
            if (plane == _stateBits.size()-1) {
                unsigned bitsInLastOne = _cacheSize%64;
                notUsedNotInDecay &= (1ull<<uint64_t(bitsInLastOne))-1;
                usedAndInDecay &= (1ull<<uint64_t(bitsInLastOne))-1;
            }
            // unused entries start decaying...
            while (notUsedNotInDecay) {
                auto idx = xl_ctz8(notUsedNotInDecay);
                notUsedNotInDecay ^= 1ull<<uint64_t(idx);
                _stateBits[plane]._inDecayHeap |= 1ull<<uint64_t(idx);
                idx += plane * 64;
                _lruQueue.BringToFront(idx);
                _decayStartFrames[idx] = _currentFrame;
            }
            // if used, remove from decay
            while (usedAndInDecay) {
                auto idx = xl_ctz8(usedAndInDecay);
                usedAndInDecay ^= 1ull<<uint64_t(idx);
                assert(_stateBits[plane]._inDecayHeap & (1ull<<uint64_t(idx)));
                _stateBits[plane]._inDecayHeap ^= 1ull<<uint64_t(idx);
                idx += plane * 64;
                _lruQueue.SendToBack(idx);
                _lruQueue.DisconnectOldest();
            }
            _stateBits[plane]._usedThisFrame = 0;       // reset for next frame
        }
        ++_currentFrame;
    }

    template<typename Type>
        bool FrameByFrameLRUHeap<Type>::UnrecordedTest(uint64_t hashName)
    {
        auto i = std::lower_bound(_lookupTable.begin(), _lookupTable.end(), hashName, CompareFirst<uint64_t, unsigned>());
        return i != _lookupTable.cend() && i->first == hashName;
    }

    template<typename Type>
        Type& FrameByFrameLRUHeap<Type>::QueryResult::GetExisting()
    {
        assert(_type == LRUCacheInsertType::Update || _type == LRUCacheInsertType::EvictAndReplace);
        return *_i;
    }

    template<typename Type>
        void FrameByFrameLRUHeap<Type>::QueryResult::Set(Type&& newValue)
    {
        if (_type == LRUCacheInsertType::EvictAndReplace) {
            unsigned idx = (unsigned)std::distance(_heap->_objects.begin(), _i);

            _heap->_objects[idx] = std::move(newValue);
            _heap->_stateBits[idx/64]._usedThisFrame |= 1ull<<uint64_t(idx%64);
            assert(_heap->_stateBits[idx/64]._inDecayHeap & (1ull<<uint64_t(idx%64)));
            _heap->_stateBits[idx/64]._inDecayHeap &= ~(1ull<<uint64_t(idx%64));

            _heap->_lruQueue.SendToBack(idx);
            _heap->_lruQueue.DisconnectOldest();

            // Erase old entry from lookup table, and add new one. We could
            // do this a little more efficiently if we did in it one step and only
            // moved entries between the erase and insertion points
            auto oldLookup = std::find_if(_heap->_lookupTable.cbegin(), _heap->_lookupTable.cend(), [idx](const auto& p) { return p.second == idx; });
            _heap->_lookupTable.erase(oldLookup);
            auto newLookupTableIterator = std::lower_bound(_heap->_lookupTable.begin(), _heap->_lookupTable.end(), _hashName, CompareFirst<uint64_t, unsigned>());
            _heap->_lookupTable.insert(newLookupTableIterator, std::make_pair(_hashName, idx));
        } else if (_type == LRUCacheInsertType::Add) {
            assert((_heap->_objects.size()+1) <= _heap->_cacheSize);
            unsigned idx = (unsigned)_heap->_objects.size();
            _heap->_objects.push_back(std::move(newValue));
            _heap->_lookupTable.insert(_lookupTableIterator, std::make_pair(_hashName, idx));
            _heap->_stateBits[idx/64]._usedThisFrame |= 1ull<<uint64_t(idx%64);
        } else if (_type == LRUCacheInsertType::Update) {
            // "Update" means both:
            //      1. existing value is valid and usable
            //      2. replacements should go into the same slot
            unsigned idx = (unsigned)std::distance(_heap->_objects.begin(), _i);
            _heap->_objects[idx] = std::move(newValue);
        } else {
            UNREACHABLE();
        }
    }

    template<typename Type>
        auto FrameByFrameLRUHeap<Type>::LogRecords() const -> std::vector<Record>
    {
        std::vector<Record> result;
        result.reserve(_objects.size());
        for (unsigned c=0; c<_objects.size(); ++c) {
            unsigned decay = 0;
            if (_lruQueue.HasValue(c)) decay = _currentFrame - _decayStartFrames[c];
            result.push_back({_objects[c], decay});
        }
        return result;
    }

    template<typename Type>
        FrameByFrameLRUHeap<Type>::FrameByFrameLRUHeap(unsigned cacheSize, unsigned decayGracePeriod)
        : _lruQueue(cacheSize)
        , _decayGracePeriod(decayGracePeriod)
        , _cacheSize(cacheSize)
    {
        _objects.reserve(cacheSize);
        _lookupTable.reserve(cacheSize);
        _stateBits.resize((cacheSize+63)/64, {0,0});
        _decayStartFrames.resize(cacheSize, 0);
        _currentFrame = 0;
    }
    template<typename Type>
        FrameByFrameLRUHeap<Type>::~FrameByFrameLRUHeap()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class IndexingLRUCache
    {
    public:
        using Index = unsigned;
        std::pair<LRUCacheInsertType, Index> Insert(uint64_t hashName);
        Index Get(uint64_t hashName);

        unsigned GetCacheSize() const { return _cacheSize; }

        IndexingLRUCache(unsigned cacheSize);
        ~IndexingLRUCache();

        IndexingLRUCache(IndexingLRUCache&& moveFrom) never_throws;
        IndexingLRUCache& operator=(IndexingLRUCache&& moveFrom) never_throws;
    protected:
        std::vector<std::pair<uint64_t, Index>> _lookupTable;
        LRUQueue _queue;
        unsigned _usedSlots;
        unsigned _cacheSize;
    };

    inline auto IndexingLRUCache::Insert(uint64_t hashName) -> std::pair<LRUCacheInsertType, Index>
    {
        auto i = std::lower_bound(_lookupTable.cbegin(), _lookupTable.cend(), hashName, CompareFirst<uint64_t, unsigned>());
        if (i != _lookupTable.cend() && i->first == hashName) {
            _queue.BringToFront(i->second);
            return {LRUCacheInsertType::Update, i->second};
        }

        if (_usedSlots < _cacheSize) {
            ++_usedSlots;
            _lookupTable.insert(i, std::make_pair(hashName, unsigned(_usedSlots-1)));
            _queue.BringToFront(unsigned(_usedSlots-1));
            return {LRUCacheInsertType::Add, unsigned(_usedSlots-1)};
        }

            // we need to evict an existing object.
        unsigned eviction = _queue.GetOldestValue();
        if (eviction == ~0u) {
            return {LRUCacheInsertType::Fail, ~0u};
        }

        _lookupTable.insert(i, std::make_pair(hashName, eviction));

        auto oldLookup = std::find_if(_lookupTable.cbegin(), _lookupTable.cend(), 
            [=](const std::pair<uint64_t, unsigned>& p) { return p.second == eviction && p.first != hashName; });
        assert(oldLookup != _lookupTable.cend());
        _lookupTable.erase(oldLookup);

        _queue.BringToFront(eviction);
        return {LRUCacheInsertType::EvictAndReplace, eviction};
    }

    inline auto IndexingLRUCache::Get(uint64_t hashName) -> Index
    {
            // find the given object, and move it to the front of the queue
        auto i = std::lower_bound(_lookupTable.cbegin(), _lookupTable.cend(), hashName, CompareFirst<uint64_t, unsigned>());
        if (i != _lookupTable.cend() && i->first == hashName) {
            _queue.BringToFront(i->second);
            return i->second;
        }
        return ~0u;
    }

    inline IndexingLRUCache::IndexingLRUCache(unsigned cacheSize)
    : _queue(cacheSize) 
    , _cacheSize(cacheSize)
    {
        _lookupTable.reserve(cacheSize);
        _usedSlots = 0;
    }

    inline IndexingLRUCache::~IndexingLRUCache()
    {}

    inline IndexingLRUCache::IndexingLRUCache(IndexingLRUCache&& moveFrom) never_throws
    : _lookupTable(std::move(moveFrom._lookupTable))
    , _queue(std::move(moveFrom._queue))
    , _cacheSize(moveFrom._cacheSize)
    , _usedSlots(moveFrom._usedSlots)
    {
        moveFrom._cacheSize = 0;
        moveFrom._usedSlots = 0;
    }

    inline IndexingLRUCache& IndexingLRUCache::operator=(IndexingLRUCache&& moveFrom) never_throws
    {
        _lookupTable = std::move(moveFrom._lookupTable);
        _queue = std::move(moveFrom._queue);
        _cacheSize = moveFrom._cacheSize;
        _usedSlots = moveFrom._usedSlots;
        moveFrom._cacheSize = 0;
        moveFrom._usedSlots = 0;
        return *this;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename Marker>
        class MarkerHeap
    {
    public:
        static Marker      ToInternalSize(unsigned size);
        static unsigned    ToExternalSize(Marker size);
        static unsigned    AlignSize(unsigned size);
    };

    struct RepositionStep
    {
        unsigned _sourceStart, _sourceEnd;
        unsigned _destination;
    };

    template <typename Marker>
        class SpanningHeap : public MarkerHeap<Marker>
    {
    public:

            //
            //      It's a simple heap that deals only in spans. It doesn't record the 
            //      size of the blocks allocated from within it. It just knows what space
            //      is allocated, and what is unallocated. The client must deallocate the
            //      correct space from the buffer when it's done.
            //

        unsigned            Allocate(unsigned size);
        bool                Allocate(unsigned ptr, unsigned size);
        bool                Deallocate(unsigned ptr, unsigned size);
        
        unsigned            CalculateAvailableSpace() const;
        unsigned            CalculateLargestFreeBlock() const;
        unsigned            CalculateAllocatedSpace() const;
        unsigned            CalculateHeapSize() const;
        uint64_t            CalculateHash() const;
        bool                IsEmpty() const;

        unsigned            AppendNewBlock(unsigned size);

        std::vector<unsigned>           CalculateMetrics() const;
        std::vector<RepositionStep>     CalculateHeapCompression() const;
        void                            PerformReposition(IteratorRange<const RepositionStep*> repositions);

        struct IncrementalDefragCandidate { std::vector<RepositionStep> _steps; unsigned _newLargestFreeBlock; };
        IncrementalDefragCandidate CalculateIncrementalDefragCandidate() const;

        std::pair<std::unique_ptr<uint8_t[]>, size_t> Flatten() const;

        SpanningHeap();
        SpanningHeap(unsigned size);
        SpanningHeap(const uint8_t flattened[], size_t flattenedSize);
        ~SpanningHeap();

        SpanningHeap(SpanningHeap&& moveFrom) never_throws;
        const SpanningHeap& operator=(SpanningHeap&& moveFrom) never_throws;
        SpanningHeap(const SpanningHeap& cloneFrom);
        const SpanningHeap& operator=(const SpanningHeap& cloneFrom);
    protected:
        std::vector<Marker>         _markers;
        mutable bool                _largestFreeBlockValid;
        mutable Marker              _largestFreeBlock;

        #if defined(_DEBUG)
            mutable Threading::Mutex    _lock;
        #endif

        Marker      CalculateLargestFreeBlock_Internal() const;
        bool        BlockAdjust_Internal(unsigned ptr, unsigned size, bool allocateOperation);
        unsigned    CalculateAvailableSpace_AlreadyLocked() const;
        unsigned    CalculateLargestFreeBlock_AlreadyLocked() const;
    };

    using SimpleSpanningHeap = SpanningHeap<uint16_t>;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename Marker>
        inline auto MarkerHeap<Marker>::ToInternalSize(unsigned size) -> Marker
    {
        assert((size>>4) <= std::numeric_limits<Marker>::max());
        return Marker(size>>4); 
    }

    template <typename Marker>
        inline unsigned MarkerHeap<Marker>::ToExternalSize(Marker size)      
    {
        assert(size <= (std::numeric_limits<unsigned>::max()>>4));
        return unsigned(size)<<4; 
    }

    template <typename Marker>
        inline unsigned MarkerHeap<Marker>::AlignSize(unsigned size)
    {
        assert((size>>4) <= std::numeric_limits<Marker>::max());
        return (size&(~((1<<4)-1)))+((size&((1<<4)-1))?(1<<4):0); 
    }

	template <>
        inline unsigned MarkerHeap<unsigned>::ToInternalSize(unsigned size)
    {
        return size;
    }

    template <>
        inline unsigned MarkerHeap<unsigned>::ToExternalSize(unsigned size)      
    {
        return size;
    }

    template <>
        inline unsigned MarkerHeap<unsigned>::AlignSize(unsigned size)
    {
        return size;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		template<unsigned X, typename std::enable_if<(X & (X - 1)) == 0>::type* = nullptr>
			unsigned Modulo(unsigned value) { return value & (X-1); }
		template<unsigned X, typename std::enable_if<(X & (X - 1)) != 0>::type* = nullptr>
			unsigned Modulo(unsigned value) { return value % X; }
	}

	/// <summary>Fixed sized circular buffer of objects</summary>
	/// Constructors and destructors called correctly. But not thread safe.
	/// Also will not function correctly for types with alignment restrictions.
	template<typename Type, unsigned Count>
		class alignas(Type) CircularBuffer
	{
	public:
		Type& front();
		Type& back();
		void pop_front();
		void pop_back();
		template<typename... Params>
			bool try_emplace_back(Params&&... p);
		template<typename... Params>
			void emplace_front(Params&&... p);
		template<typename... Params>
			void emplace(size_t beforeIdx, Params&&... p);
		void erase(size_t idx);
		bool empty() const;
		bool full() const;
		size_t size() const;
		Type& at(size_t idx);
		const Type& at(size_t idx) const;

		void cycle_ordering();

		CircularBuffer();
		~CircularBuffer();
		CircularBuffer(CircularBuffer&& moveFrom) never_throws;
		CircularBuffer& operator=(CircularBuffer&& moveFrom) never_throws;
	private:
		uint8_t		_objects[sizeof(Type)*Count];
		unsigned	_start, _count;
	};

	template<typename Type, unsigned Count>
		bool CircularBuffer<Type, Count>::empty() const
	{
		return !_count;
	}

	template<typename Type, unsigned Count>
		bool CircularBuffer<Type, Count>::full() const
	{
		return _count == Count;
	}

	template<typename Type, unsigned Count>
		size_t CircularBuffer<Type, Count>::size() const
	{
		return _count;
	}

	template<typename Type, unsigned Count>
		Type& CircularBuffer<Type, Count>::front()
	{
		assert(!empty()); 
		return ((Type*)_objects)[_start];
	}

	template<typename Type, unsigned Count>
		Type& CircularBuffer<Type, Count>::back()
	{
		assert(!empty());
		auto b = Internal::Modulo<Count>(_start+_count-1);
		return ((Type*)_objects)[b];
	}

	template<typename Type, unsigned Count>
		Type& CircularBuffer<Type, Count>::at(size_t idx)
	{
		assert(idx < size());
		return ((Type*)_objects)[Internal::Modulo<Count>(_start+idx)];
	}

	template<typename Type, unsigned Count>
		const Type& CircularBuffer<Type, Count>::at(size_t idx) const
	{
		assert(idx < size());
		return ((Type*)_objects)[Internal::Modulo<Count>(_start+idx)];
	}

	template<typename Type, unsigned Count>
		void CircularBuffer<Type, Count>::pop_front()
	{
		assert(!empty());
		((Type*)_objects)[_start].~Type();
		_start = Internal::Modulo<Count>(_start+1);
		--_count;
	}

	template<typename Type, unsigned Count>
		void CircularBuffer<Type, Count>::pop_back()
	{
		assert(!empty());
		((Type*)_objects)[Internal::Modulo<Count>(_start+_count-1)].~Type();
		--_count;
	}

	template<typename Type, unsigned Count>
		template<typename... Params>
			bool CircularBuffer<Type, Count>::try_emplace_back(Params&&... p)
	{
		// When _start and _end are equal, the buffer is always full
		// note that when we're empty, _start=0 && _end == Count (to distinguish it
		// from the full case)
		if (full()) return false;
		
		#pragma push_macro("new")
		#undef new
			new(_objects + sizeof(Type)*Internal::Modulo<Count>(_start+_count)) Type(std::forward<Params>(p)...);
		#pragma pop_macro("new")
		++_count;
		return true;
	}

	template<typename Type, unsigned Count>
		template<typename... Params>
			void CircularBuffer<Type, Count>::emplace_front(Params&&... p)
	{
		assert(!full());
		auto newStart = Internal::Modulo<Count>(_start+Count-1);

		#pragma push_macro("new")
		#undef new
			new(_objects + sizeof(Type)*newStart) Type(std::forward<Params>(p)...);
		#pragma pop_macro("new")
		_start = newStart;
		++_count;
	}

	template<typename Type, unsigned Count>
		template<typename... Params>
			void CircularBuffer<Type, Count>::emplace(size_t beforeIdx, Params&&... p)
	{
		assert(!full());

		#pragma push_macro("new")
		#undef new

		// Shift all of the items around, either forward or backwards, to open up the space as beforeIdx
		// Note that we always do a destruct and reconstruct (rather than move) in the slot that we've
		// initializing -- to ensure that all of the emplace functions have consistency
		if (beforeIdx < size()/2) {
			// shift everything backwards
			auto newStart = Internal::Modulo<Count>(_start+Count-1);
			if (beforeIdx != 0) {
				new(_objects + sizeof(Type)*newStart) Type(std::move(((Type*)_objects)[Internal::Modulo<Count>(newStart+1)]));
				for (unsigned c=1; c<beforeIdx; ++c)
					((Type*)_objects)[Internal::Modulo<Count>(newStart+c)] = std::move(((Type*)_objects)[Internal::Modulo<Count>(newStart+c+1)]);
				((Type*)_objects)[Internal::Modulo<Count>(newStart+beforeIdx)].~Type();
			}
			_start = newStart;
		} else {
			// shift everything forwards
			auto newEnd = Internal::Modulo<Count>(_start+_count);
			auto shiftCount = _count - beforeIdx;
			if (shiftCount) {
				new(_objects + sizeof(Type)*newEnd) Type(std::move(((Type*)_objects)[Internal::Modulo<Count>(newEnd+Count-1)]));
				for (unsigned c=1; c<shiftCount; ++c)
					((Type*)_objects)[Internal::Modulo<Count>(newEnd+Count-c)] = std::move(((Type*)_objects)[Internal::Modulo<Count>(newEnd+Count-c-1)]);
				((Type*)_objects)[Internal::Modulo<Count>(_start+beforeIdx)].~Type();
			}
		}

		// space opened up, initialize the new object
		new(_objects + sizeof(Type)*Internal::Modulo<Count>(_start+beforeIdx)) Type(std::forward<Params>(p)...);
		++_count;

		#pragma pop_macro("new")
	}

	template<typename Type, unsigned Count>
		void CircularBuffer<Type, Count>::erase(size_t idx)
	{
		assert(idx < size());
		if (idx == 0) {
			pop_front();
			return;
		}

		// Call destructor for the one we're destroying
		auto moduloIdx = Internal::Modulo<Count>(_start+idx);
		auto* erasePtr = &((Type*)_objects)[moduloIdx];

		// Shift all of the future items down, and ultimately reduce _count
		auto endIdx = Internal::Modulo<Count>(_start+_count);
		if (endIdx < moduloIdx) {
			// there's a wrap around
			auto* e = &((Type*)_objects)[Count];
			while ((erasePtr+1) < e) {
				*erasePtr = std::move(*(erasePtr+1));
				++erasePtr;
			}
			*erasePtr = std::move(((Type*)_objects)[0]);
			erasePtr = (Type*)_objects;
			// continue --
		}

		auto* e = &((Type*)_objects)[endIdx];
		while ((erasePtr+1) < e) {
			*erasePtr = std::move(*(erasePtr+1));
			++erasePtr;
		}
		erasePtr->~Type(); // delete the trailing one
		_count--;
	}	

	template<typename Type, unsigned Count>
		void CircularBuffer<Type, Count>::cycle_ordering()
	{
		// special interface for CircularPagedHeap, which makes back the new front
		assert(full());
		_start = Internal::Modulo<Count>(_start+Count-1);
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type, Count>::CircularBuffer()
	{
		assert((size_t(_objects) % alignof(Type)) == 0);
		_start=_count=0;
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type,Count>::~CircularBuffer() 
	{
		for (unsigned c=0; c<_count; ++c)
			(((Type*)_objects)[Internal::Modulo<Count>(_start+c)]).~Type();
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type, Count>::CircularBuffer(CircularBuffer&& moveFrom) never_throws
	{
		_start = 0;
		_count = moveFrom._count;

		#pragma push_macro("new")
		#undef new
		for (unsigned c=0; c<_count; ++c) {
			auto& src = ((Type*)moveFrom._objects)[Internal::Modulo<Count>(moveFrom._start+c)];
			new(_objects + sizeof(Type)*c) Type(std::move(src));
			src.~Type();
		}
		#pragma pop_macro("new")

		moveFrom._start = 0;
		moveFrom._count = 0;
	}

	template<typename Type, unsigned Count>
		auto CircularBuffer<Type, Count>::operator=(CircularBuffer&& moveFrom) never_throws -> CircularBuffer&
	{
		for (unsigned c=0; c<_count; ++c)
			(((Type*)_objects)[Internal::Modulo<Count>(_start+c)]).~Type();

		_start = 0;
		_count = moveFrom._count;

		#pragma push_macro("new")
		#undef new
		for (unsigned c=0; c<_count; ++c) {
			auto& src = ((Type*)moveFrom._objects)[Internal::Modulo<Count>(moveFrom._start+c)];
			new(_objects + sizeof(Type)*c) Type(std::move(src));
			src.~Type();
		}
		#pragma pop_macro("new")

		moveFrom._start = 0;
		moveFrom._count = 0;
		return *this;
	}

    template<typename Type, unsigned Count>
		class ResizableCircularBuffer
	{
    public:
        Type& front();
		Type& back();
		void pop_front();
		template<typename... Params>
			void emplace_back(Params... p);
		bool empty() const;

        size_t page_count() const;

		ResizableCircularBuffer() = default;
		~ResizableCircularBuffer() = default;
		ResizableCircularBuffer(ResizableCircularBuffer&& moveFrom) never_throws = default;
		ResizableCircularBuffer& operator=(ResizableCircularBuffer&& moveFrom) never_throws = default;
    protected:
        std::vector<CircularBuffer<Type, Count>> _activePages;
        std::vector<CircularBuffer<Type, Count>> _inactivePages;
    };

    template<typename Type, unsigned Count>
        Type& ResizableCircularBuffer<Type,Count>::front()
    {
        assert(!empty());
        return _activePages.begin()->front();
    }

    template<typename Type, unsigned Count>
        Type& ResizableCircularBuffer<Type,Count>::back()
    {
        assert(!empty());
        return (_activePages.end()-1)->back();
    }

    template<typename Type, unsigned Count>
        void ResizableCircularBuffer<Type,Count>::pop_front()
    {
        assert(!empty());
        _activePages.begin()->pop_front();
        if (_activePages.begin()->empty()) {
            _inactivePages.emplace_back(std::move(*_activePages.begin()));
            _activePages.erase(_activePages.begin());
        }
    }

    template<typename Type, unsigned Count>
        template<typename... Params>
            void ResizableCircularBuffer<Type,Count>::emplace_back(Params... p)
    {
        if (!_activePages.empty() && (_activePages.end()-1)->try_emplace_back(std::forward<Params>(p)...))
            return;
        if (!_inactivePages.empty()) {
            _activePages.push_back(std::move(*(_inactivePages.end()-1)));
            _inactivePages.erase(_inactivePages.end()-1);
        } else {
            _activePages.push_back({});
        }

        bool res = (_activePages.end()-1)->try_emplace_back(std::forward<Params>(p)...);
        assert(res); (void)res;
    }

    template<typename Type, unsigned Count>
        bool ResizableCircularBuffer<Type,Count>::empty() const
    {
        return _activePages.empty();
    }

    template<typename Type, unsigned Count>
        size_t ResizableCircularBuffer<Type,Count>::page_count() const
    {
        return _activePages.size();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>A paged heap, similar to a deque, that uses circular buffers in each page</summary>
	/// This can significantly reduce the cost of inserting and erasing items in the middle of the heap, at 
	/// the cost of big penalties for random lookup and some overhead for iteration.
	template<typename Type, unsigned PageSize=64>
		class CircularPagedHeap
	{
	public:
		using Page = CircularBuffer<Type, PageSize>;
		std::vector<std::unique_ptr<Page>> _pages;
		std::vector<size_t> _indexLookups;

		struct Iterator
		{
			Page* _pageIterator = 0;
			unsigned _idxWithinPage = 0;
			unsigned _pageIdx = 0;
			unsigned _countInPriorPages = 0;
			CircularPagedHeap* _srcHeap = nullptr;

			Type& get() const { return _pageIterator->at(_idxWithinPage); }
			Type& operator*() const { return get(); }
			Type* operator->() const { return &get(); }
			explicit operator bool() const { return _pageIterator; }
			unsigned index() const { return _countInPriorPages + _idxWithinPage; }

			void operator++();
			void operator+=(ptrdiff_t offset);

			friend bool operator==(const Iterator& lhs, const Iterator& rhs)
			{
				assert(lhs._srcHeap == rhs._srcHeap);
				return lhs._pageIdx == rhs._pageIdx && lhs._idxWithinPage == rhs._idxWithinPage;
			}
			friend bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !operator==(lhs, rhs); }

			friend Iterator operator+(const Iterator& b, ptrdiff_t offset)
			{
				CircularPagedHeap<Type, PageSize>::Iterator result = b;
				result += offset;
				return result;
			}

			friend size_t operator-(const Iterator& lhs, const Iterator& rhs)
			{
				assert(lhs._srcHeap == rhs._srcHeap);
				return rhs.index() - lhs.index();
			}

		private:
			void CheckMovePage();
			friend class CircularPagedHeap;
		};

		Iterator begin();
		Iterator end();
		Iterator at(size_t);
		template<typename... Params>
			Iterator emplace_back(Params&&...);
		template<typename... Params>
			Iterator emplace_anywhere(Params&&...);
		template<typename... Params>
			Iterator emplace(const Iterator&, Params&&...);
		Iterator erase(const Iterator&);
		bool empty();
		size_t size();
		void clear();

		CircularPagedHeap();
		~CircularPagedHeap();
		CircularPagedHeap(CircularPagedHeap&& moveFrom) never_throws = default;
		CircularPagedHeap& operator=(CircularPagedHeap&& moveFrom) never_throws = default;
	};

	template<typename Type, unsigned PageSize>
		void CircularPagedHeap<Type, PageSize>::Iterator::CheckMovePage()
	{
		if (_idxWithinPage >= _srcHeap->_pages[_pageIdx]->size()) {
			_countInPriorPages += _srcHeap->_pages[_pageIdx]->size();
			_pageIdx++;
			_idxWithinPage = 0;
			if (_pageIdx < _srcHeap->_pages.size()) _pageIterator = _srcHeap->_pages[_pageIdx].get();
			else _pageIterator = nullptr;
		}
	}

	template<typename Type, unsigned PageSize>
		void CircularPagedHeap<Type, PageSize>::Iterator::operator++()
	{
		++_idxWithinPage;
		CheckMovePage();
	}

	template<typename Type, unsigned PageSize>
		void CircularPagedHeap<Type, PageSize>::Iterator::operator+=(ptrdiff_t offset)
	{
		if (!offset) return;
		if ((_idxWithinPage+offset) < _pageIterator->size() && (_idxWithinPage+offset) >= 0) {
			_idxWithinPage += offset;
		} else {
			// Use the lookup table "_srcHeap->_indexLookups" to accelerate the search for the page we need
			// We use this for all random lookups
			auto newIdx = _countInPriorPages + _idxWithinPage + offset;
			auto i = std::upper_bound(_srcHeap->_indexLookups.begin()+_pageIdx+1, _srcHeap->_indexLookups.end(), newIdx);
			assert(i != _srcHeap->_indexLookups.begin());
			--i;
			_pageIdx = i - _srcHeap->_indexLookups.begin();
			_countInPriorPages = *i;
			if (_pageIdx < _srcHeap->_pages.size()) {
				_pageIterator = _srcHeap->_pages[_pageIdx].get();
				_idxWithinPage = newIdx - _countInPriorPages;
			} else {
				_pageIterator = nullptr;
				_idxWithinPage = 0;
			}
			assert(_idxWithinPage < PageSize);
		}
	}

	template<typename Type, unsigned PageSize>
		auto CircularPagedHeap<Type, PageSize>::begin() -> Iterator
	{
		Iterator result;
		result._srcHeap = this;
		if (!_pages.empty())
			result._pageIterator = _pages.front().get();
		return result;
	}

	template<typename Type, unsigned PageSize>
		auto CircularPagedHeap<Type, PageSize>::end() -> Iterator
	{
		Iterator result;
		result._srcHeap = this;
		result._pageIdx = (unsigned)_pages.size();
		return result;
	}

	template<typename Type, unsigned PageSize>
		auto CircularPagedHeap<Type, PageSize>::at(size_t idx) -> Iterator
	{
		return begin() + idx;
	}

	template<typename Type, unsigned PageSize>
		auto CircularPagedHeap<Type, PageSize>::erase(const Iterator& i) -> Iterator
	{
		assert(i._srcHeap == this);
		_pages[i._pageIdx]->erase(i._idxWithinPage);

		// reduce the index lookups by one
		for (auto q=_indexLookups.begin()+i._pageIdx+1; q<_indexLookups.end(); ++q)
			--(*q);

		// if we've removed a page entirely, we can just go ahead and destroy it
		if (_pages[i._pageIdx]->empty()) {
			_pages.erase(_pages.begin()+i._pageIdx);
			_indexLookups.erase(_indexLookups.begin()+i._pageIdx);

			Iterator result = i;
			result._idxWithinPage = 0;
			if (result._pageIdx < _pages.size()) result._pageIterator = _pages[result._pageIdx].get();
			else result._pageIterator = nullptr;
			return result;
		} else {
			Iterator result = i;
			result.CheckMovePage();
			return result;
		}
	}

	template<typename Type, unsigned PageSize>
		template<typename... Params>
			auto CircularPagedHeap<Type, PageSize>::emplace_back(Params&&... p) -> Iterator
	{
		if (_pages.empty() || _pages.back()->full()) {
			_pages.emplace_back(std::make_unique<Page>());
			_indexLookups.emplace_back(_indexLookups.back());
		}

		_pages.back()->try_emplace_back(std::forward<Params>(p)...);
		++_indexLookups.back();

		Iterator result;
		result._srcHeap = this;
		result._pageIdx = _pages.size()-1;
		result._idxWithinPage = _pages.back()->size()-1;
		result._pageIterator = _pages.back().get();
		result._countInPriorPages = _indexLookups[result._pageIdx];
		return result;
	}

	template<typename Type, unsigned PageSize>
		template<typename... Params>
			auto CircularPagedHeap<Type, PageSize>::emplace_anywhere(Params&&... p) -> Iterator
	{
		for (unsigned c=0; c<_pages.size(); ++c) {
			if (!_pages[c]->full()) {
				_pages[c]->try_emplace_back(std::forward<Params>(p)...);

				Iterator result;
				result._srcHeap = this;
				result._pageIdx = c;
				result._idxWithinPage = _pages[c]->size()-1;
				result._pageIterator = _pages[c].get();
				result._countInPriorPages = _indexLookups[c];

				// update _indexLookups
				++c;
				for (; c<_indexLookups.size(); ++c) ++_indexLookups[c];
				return result;
			}
		}
		return emplace_back(std::forward<Params>(p)...);
	}

	template<typename Type, unsigned PageSize>
		template<typename... Params>
			 auto CircularPagedHeap<Type, PageSize>::emplace(const Iterator& before, Params&&...p) -> Iterator
	{
		// Insert before the given before the position given by the iterator, and return a new iterator
		// for this position
		// Here is when we can get some interesting things happening
		// 1. there is room in the page we want to insert into
		//		-> easy solution, quick add, but many require some shifting about within the page
		// 2. the page is full
		//		-> we must move the last element of the page out and try to fit it in a subsequent
		//			page. This can result in a recursive walk down the list of pages, doing one swap
		//			per page
		assert(before._pageIterator);
		if (!before._pageIterator->full()) {
			before._pageIterator->emplace(before._idxWithinPage, std::forward<Params>(p)...);
			unsigned c=before._pageIdx+1;
			for (; c<_indexLookups.size(); ++c) ++_indexLookups[c];
			return before;
		} else {
			auto swapper = std::move(before._pageIterator->back());
			before._pageIterator->pop_back();
			before._pageIterator->emplace(before._idxWithinPage, std::forward<Params>(p)...);

			// move the swapper down the list of pages until we find somewhere to place it
			unsigned c=before._pageIdx+1;
			for (; c<_pages.size(); ++c) {
				auto& page = *_pages[c];
				if (page.full()) {
					std::swap(swapper, page.back());
					page.cycle_ordering();	// back becomes the new front
				} else {
					page.emplace_front(std::move(swapper));
					break;
				}
			}

			if (c == _pages.size()) {
				// add a new page and make the swapper it's only element
				emplace_back(std::move(swapper));
			} else {
				// else, update the index lookups starting from the page that actually got a new item
				++c;
				for (; c<_indexLookups.size(); ++c) ++_indexLookups[c];
			}

			return before;
		}
	}

	template<typename Type, unsigned PageSize>
		size_t CircularPagedHeap<Type, PageSize>::size() { return _indexLookups.back(); }
	template<typename Type, unsigned PageSize>
		bool CircularPagedHeap<Type, PageSize>::empty() { return size() == 0; }

	template<typename Type, unsigned PageSize>
		void CircularPagedHeap<Type, PageSize>::clear()
	{
		_pages.clear();
		_indexLookups.clear();
		_indexLookups.emplace_back(0);
	}

	template<typename Type, unsigned PageSize>
		CircularPagedHeap<Type, PageSize>::CircularPagedHeap()
	{
		_indexLookups.emplace_back(0);
	}

	template<typename Type, unsigned PageSize>
		CircularPagedHeap<Type, PageSize>::~CircularPagedHeap()
	{}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		inline unsigned nthset(uint64_t x, unsigned n)
		{
			// https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
			// Note that _pdep_u64 uses BMI2 instruction set
			// Intel: introduced in Haswell
			// AMD: before Zen3, _pdep_u64 is microcode and so may not be optimal
			return _tzcnt_u64(_pdep_u64(1ull << n, x));
		}
	}

	/// <summary>Remaps a sparse ordered sequence of numbers onto a dense one with the same ordering</summary>
	template<typename T>
		class RemappingBitHeap
	{
	public:
		struct Entry
		{
			T _firstSparseValue;
			uint64_t _allocationFlags;
			unsigned _precedingDenseValues;
		};

		struct Iterator
		{
			const Entry* _entry;
			T _sparseValueOffset;

			T get() const { return _entry->_firstSparseValue + _sparseValueOffset; }
			T operator*() const { return get(); }
			T SparseSequenceValue() const { return get(); }
			unsigned DenseSequenceValue() const;
			explicit operator bool() const;

			void AdvanceDenseSequence(size_t);
			void AdvanceSparseSequence(T);
			void RegressSparseSequence(T);
			
			void operator++();
			void operator+=(ptrdiff_t offset) { assert(offset >= 0); return AdvanceDenseSequence(offset); }

			friend bool operator==(const Iterator& lhs, const Iterator& rhs) { return lhs._entry == rhs._entry && lhs._sparseValueOffset == rhs._sparseValueOffset; }
			friend bool operator!=(const Iterator& lhs, const Iterator& rhs) { return !operator==(lhs, rhs); }

			friend Iterator operator+(const Iterator& b, size_t offset)
			{
				Iterator result = b;
				result += offset;
				return result;
			}
		};

		Iterator Remap(T) const;
		Iterator Remap(T, Iterator hint) const;
		bool IsAllocated(T) const;
		Iterator Allocate(T);
		Iterator Allocate(Iterator i);
		Iterator Deallocate(Iterator);

		Iterator begin();
		Iterator end();
		Iterator at(size_t offset) { return begin() + offset; }
		Iterator erase(Iterator i) { return Deallocate(i); }
		bool empty() { return _allocationsTable.size() == 1; }
		size_t size() { return _allocationsTable.back()._precedingDenseValues; }
		void clear();

		RemappingBitHeap();
	private:
		std::vector<Entry> _allocationsTable;

		struct CompareUtil
		{
			inline bool operator()(const Entry& lhs, const Entry& rhs) const	{ return lhs._firstSparseValue < rhs._firstSparseValue; }
			inline bool operator()(const Entry& lhs, T rhs) const				{ return lhs._firstSparseValue < rhs; }
			inline bool operator()(T lhs, const Entry& rhs) const				{ return lhs < rhs._firstSparseValue; }
		};
	};

	template<typename T>
		unsigned RemappingBitHeap<T>::Iterator::DenseSequenceValue() const
	{
		assert(_sparseValueOffset < 64);
		uint64_t maskLower = (1ull << uint64_t(_sparseValueOffset)) - 1ull;
		return _entry->_precedingDenseValues + popcount(_entry->_allocationFlags & maskLower);
	}

	template<typename T>
		void RemappingBitHeap<T>::Iterator::operator++()
	{
		assert(_entry->_allocationFlags);
		// note weirdness from compiler related to 1ull << 64ull (can be non-zero)
		uint64_t maskBitOffsetAndLower = ((_sparseValueOffset < 63u) * (1ull << uint64_t(_sparseValueOffset+1))) - 1ull;
		uint64_t remainingBits = _entry->_allocationFlags & ~maskBitOffsetAndLower;
		if (remainingBits) {
			assert(xl_ctz8(remainingBits) != 0);
			_sparseValueOffset = xl_ctz8(remainingBits);
		} else {
			++_entry;
			_sparseValueOffset = xl_ctz8(_entry->_allocationFlags);
		}
		assert(_sparseValueOffset <= 64u);
	}

	template<typename T>
		void RemappingBitHeap<T>::Iterator::AdvanceDenseSequence(size_t offset)
	{
		assert(_entry->_allocationFlags);
		assert(_sparseValueOffset != 64ull);	// compiler weird about 1ull << 64ull
		// Advance forward the given number of entries in the dense sequence
		// Ie, we're advancing an arbitrary number of sparse sequence values
		uint64_t maskPriorBits = (1ull << uint64_t(_sparseValueOffset)) - 1ull;
		uint64_t remainingBits = _entry->_allocationFlags & ~maskPriorBits;
		// https://stackoverflow.com/questions/7669057/find-nth-set-bit-in-an-int
		if (auto q = _pdep_u64(1ull << uint64_t(offset), remainingBits)) {
			// we're advancing within the same range
			_sparseValueOffset = xl_ctz8(q);
		} else {
			offset -= popcount(remainingBits);
			_sparseValueOffset = 64;
			++_entry;
			while (_entry->_allocationFlags) {
				if (offset < 64) {
					auto q = _pdep_u64(1ull << uint64_t(offset), _entry->_allocationFlags);
					if (q) {
						_sparseValueOffset = xl_ctz8(q);
						assert(_sparseValueOffset < 64);
						return;
					} else {
						offset -= popcount(_entry->_allocationFlags);
						++_entry;
					}
				} else {
					offset -= popcount(_entry->_allocationFlags);
					++_entry;
				}
			}
		}
		assert(_sparseValueOffset <= 64u);
	}

	template<typename T>
		void RemappingBitHeap<T>::Iterator::AdvanceSparseSequence(T offset)
	{
		assert(offset >= 0 && offset < 0xffffff00);		// sanity check
		/*if ((_sparseValueOffset + offset) < 64) {
			_sparseValueOffset += offset;
		} else {*/
			// We have to find the new page. Note that since we don't know the end of the Entry
			// array, we can't do a binary search
			auto sparseSequenceValue = SparseSequenceValue() + offset;
			while (sparseSequenceValue >= (_entry+1)->_firstSparseValue) ++_entry;		// requires sentinel
			_sparseValueOffset = sparseSequenceValue - _entry->_firstSparseValue;
			_sparseValueOffset = std::min(_sparseValueOffset, 64u);						// not entirely essential, but means the iterator retains equality with end()
		// }
	}

	template<typename T>
		void RemappingBitHeap<T>::Iterator::RegressSparseSequence(T offset)
	{
		assert(offset >= 0);
		if ((_sparseValueOffset + offset) >= 0) {
			_sparseValueOffset += offset;
			assert(_sparseValueOffset < 64);
		} else {
			// We have to find the new page. Note that since we don't know the end of the Entry
			// array, we can't do a binary search
			auto sparseSequenceValue = SparseSequenceValue() + offset;
			assert(0);
			while (_entry->_firstSparseValue > sparseSequenceValue) --_entry;		// requires sentinel
			_sparseValueOffset = sparseSequenceValue - _entry->_firstSparseValue;
		}
	}

	template<typename T>
		RemappingBitHeap<T>::Iterator::operator bool() const
	{
		// shifting by a number equal or greater than 64ull is allowed to produce non-zero values
		return (_sparseValueOffset < 64u) * (_entry->_allocationFlags & (1ull << uint64_t(_sparseValueOffset)));
	}

	template<typename T>
		auto RemappingBitHeap<T>::Remap(T t) const -> Iterator
	{
		assert(t != std::numeric_limits<T>::max());	// using this value as a sentinel
		auto i = std::upper_bound(_allocationsTable.begin(), _allocationsTable.end(), t, CompareUtil{});	// requires sentinel
		--i;

		Iterator result;
		result._entry = AsPointer(i);
		result._sparseValueOffset = t - i->_firstSparseValue;
		return result;
	}

	template<typename T>
		auto RemappingBitHeap<T>::Remap(T, Iterator hint) const -> Iterator
	{
		assert(t != std::numeric_limits<T>::max());	// using this value as a sentinel
		auto i = std::upper_bound(hint._entry, AsPointer(_allocationsTable.end()), t, CompareUtil{});	// requires sentinel
		--i;

		Iterator result;
		result._entry = AsPointer(i);
		result._sparseValueOffset = t - i->_firstSparseValue;
		assert(result._sparseValueOffset < 64);
		return result;
	}

	template<typename T>
		bool RemappingBitHeap<T>::IsAllocated(T t) const
	{
		auto i = std::upper_bound(_allocationsTable.begin(), _allocationsTable.end(), t, CompareUtil{});	// requires sentinel
		--i;
		if ((t - i->_firstSparseValue) >= 64u) return false;
		return !!(i->_allocationFlags & (1ull << uint64_t(t - i->_firstSparseValue)));
	}

	template<typename T>
		auto RemappingBitHeap<T>::Allocate(T t) -> Iterator { return Allocate(Remap(t)); }

	template<typename T>
		auto RemappingBitHeap<T>::Allocate(Iterator insertion) -> Iterator
	{
		assert(!insertion);
		auto t = insertion.get();
		auto i = _allocationsTable.begin() + (insertion._entry - AsPointer(_allocationsTable.begin()));
		auto offset = t - i->_firstSparseValue;
		if (offset < 64) {
			assert(!(i->_allocationFlags & (1ull << uint64_t(offset))));	// ensure that it's not already allocated
			i->_allocationFlags |= 1ull << uint64_t(offset);				// mark allocated
		} else {
			// we have to insert a new entry into the _allocationsTable table
			auto alignedT = t & ~(64ull - 1ull);
			i = _allocationsTable.insert(i+1, Entry{(T)alignedT, 0, (i+1)->_precedingDenseValues});
			offset = t - alignedT;
			assert(offset < 64);
			i->_allocationFlags |= 1ull << uint64_t(offset);				// mark allocated
		}
		Iterator result;
		result._entry = AsPointer(i);
		result._sparseValueOffset = offset;

		++i; while (i!=_allocationsTable.end()) { ++i->_precedingDenseValues; ++i; }
		return result;
	}

	template<typename T>
		auto RemappingBitHeap<T>::Deallocate(Iterator i) -> Iterator
	{
		assert(i._sparseValueOffset != 64ull);	// compiler weird about 1ull << 64ull
		assert(i._entry->_allocationFlags & (1ull << uint64_t(i._sparseValueOffset)));
		auto newBits = i._entry->_allocationFlags & ~(1ull << uint64_t(i._sparseValueOffset));
		if (newBits && i._entry->_firstSparseValue != std::numeric_limits<T>::min()) {		// never remove the "min" sentinel
			auto q = const_cast<Entry*>(i._entry);
			++i;
			q->_allocationFlags = newBits;
			++q; while (q!=AsPointer(_allocationsTable.end())) { --q->_precedingDenseValues; ++q; }
			return i;
		} else {
			// we're removing an entire entry in the "_allocationsTable" range
			auto q = _allocationsTable.erase(_allocationsTable.begin() + (i._entry - AsPointer(_allocationsTable.begin())));
			i._entry = AsPointer(q);
			i._sparseValueOffset = xl_ctz8(q->_allocationFlags);
			++q; while (q!=_allocationsTable.end()) { --q->_precedingDenseValues; ++q; }
			return i;
		}
	}

	template<typename T>
		auto RemappingBitHeap<T>::begin() -> Iterator
	{
		Iterator result;
		result._entry = AsPointer(_allocationsTable.begin());
		if (!result._entry->_allocationFlags)
			// happens when empty, since we have a sentinel at the beginning as well as the end
			// (or when there's nothing in the first 64 sparse values)
			++result._entry;
		result._sparseValueOffset = xl_ctz8(result._entry->_allocationFlags);
		return result;
	}

	template<typename T>
		auto RemappingBitHeap<T>::end() -> Iterator
	{
		assert(!_allocationsTable.empty());
		Iterator result;
		result._entry = AsPointer(_allocationsTable.end()-1);
		result._sparseValueOffset = 64;
		return result;
	}

	template<typename T>
		void RemappingBitHeap<T>::clear()
	{
		_allocationsTable.clear();
		_allocationsTable.emplace_back(Entry{std::numeric_limits<T>::min(), 0, 0}); // sentinel
		_allocationsTable.emplace_back(Entry{std::numeric_limits<T>::max(), 0, 0}); // sentinel
	}

	template<typename T>
		RemappingBitHeap<T>::RemappingBitHeap()
	{
		_allocationsTable.emplace_back(Entry{std::numeric_limits<T>::min(), 0, 0}); // sentinel
		_allocationsTable.emplace_back(Entry{std::numeric_limits<T>::max(), 0, 0}); // sentinel
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class CircularHeap
	{
	public:
		unsigned	AllocateBack(unsigned size);
        unsigned	AllocateBack(unsigned size, unsigned alignment);
		void		ResetFront(unsigned newFront);
		void		UndoLastAllocation(unsigned size);
		unsigned	Back() const { return _end; }
		unsigned	Front() const { return _start; }
		unsigned	HeapSize() const { return _heapSize; }

        struct QuickMetrics { unsigned _bytesAllocated, _maxNextBlockBytes, _front, _back; };
        QuickMetrics GetQuickMetrics() const;

		CircularHeap(unsigned heapSize);
		CircularHeap();
		~CircularHeap();
	private:
		unsigned	_start, _end, _heapSize;
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    class ReferenceCountingLayer : public MarkerHeap<uint32_t>
    {
    public:
        std::pair<signed,signed> AddRef(unsigned start, unsigned size, StringSection<> = {});
        std::pair<signed,signed> Release(unsigned start, unsigned size);

        size_t      Validate();
        unsigned    CalculatedReferencedSpace() const;
        unsigned    GetEntryCount() const                               { return (unsigned)_entries.size(); }
        std::pair<unsigned,unsigned> GetEntry(unsigned index) const     { const Entry& entry = _entries[index]; return std::make_pair(ToExternalSize(entry._start), ToExternalSize(entry._end-entry._start)); }
        #if defined(_DEBUG)
            std::string      GetEntryName(unsigned index) const         { const Entry& entry = _entries[index]; return entry._name; }
        #endif
        bool        ValidateBlock(unsigned start, unsigned size) const;

        void        PerformDefrag(const std::vector<RepositionStep>& defrag);

        ReferenceCountingLayer(size_t size);
        ReferenceCountingLayer(const ReferenceCountingLayer& cloneFrom);
    protected:
        using Marker = uint32_t;
        class Entry
        {
        public:
            Marker _start, _end;    // _end is stl style -- one past the end of the allocation
            signed _refCount;
            #if defined(_DEBUG)
                std::string _name;
            #endif
        };
        std::vector<Entry> _entries;

        #if defined(_DEBUG)
            mutable Threading::Mutex    _lock;
        #endif

        struct CompareStart
        {
            bool operator()(const Entry&lhs, Marker value)      { return lhs._start < value; }
            bool operator()(Marker value, const Entry&rhs)      { return value < rhs._start; }
            bool operator()(const Entry&lhs, const Entry&rhs)   { return lhs._start < rhs._start; }
        };
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    inline unsigned	CircularHeap::AllocateBack(unsigned size)
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

    inline unsigned	CircularHeap::AllocateBack(unsigned size, unsigned alignment)
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

    inline void		CircularHeap::UndoLastAllocation(unsigned size)
    {
        // Roll back the last allocation we performed with AllocateBack
        // This can also be used to shrink the size of the last allocation
        // (for example, if it was an estimate) by giving the number of bytes
        // we want to give back to the heap
        assert(_end >= size);
        _end -= size;
    }

    inline auto CircularHeap::GetQuickMetrics() const -> QuickMetrics
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

	inline void		CircularHeap::ResetFront(unsigned newFront)
	{
		_start = newFront;
		if (_start == _end) {
			_start = _heapSize;
			_end = 0;
		}
	}

	inline CircularHeap::CircularHeap(unsigned heapSize) { _start = heapSize; _end = 0; _heapSize = heapSize; }
	inline CircularHeap::CircularHeap() { _start = _end = _heapSize = 0; }
	inline CircularHeap::~CircularHeap() {}

}

using namespace Utility;
