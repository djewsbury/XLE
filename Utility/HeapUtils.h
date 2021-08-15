// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
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

    class DefragStep
    {
    public:
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
        uint64_t              CalculateHash() const;
        bool                IsEmpty() const;

        unsigned            AppendNewBlock(unsigned size);

        std::vector<unsigned>       CalculateMetrics() const;
        std::vector<DefragStep>     CalculateDefragSteps() const;
        void                        PerformDefrag(const std::vector<DefragStep>& defrag);

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
		class CircularBuffer
	{
	public:
		Type& front();
		Type& back();
		void pop_front();
		template<typename... Params>
			bool try_emplace_back(Params... p);
		bool empty() const;

		CircularBuffer();
		~CircularBuffer();
		CircularBuffer(CircularBuffer&& moveFrom) never_throws;
		CircularBuffer& operator=(CircularBuffer&& moveFrom) never_throws;
	private:
		uint8_t		_objects[sizeof(Type)*Count];
		unsigned	_start, _end;
	};

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
		auto b = Internal::Modulo<Count>(_end+Count-1);
		return ((Type*)_objects)[b];
	}

	template<typename Type, unsigned Count>
		void CircularBuffer<Type, Count>::pop_front()
	{
		assert(!empty());
		((Type*)_objects)[_start].~Type();
		_start = Internal::Modulo<Count>(_start+1);
		if (_start == _end) {
			_start = 0;
			_end = Count;
		}
	}

	template<typename Type, unsigned Count>
		template<typename... Params>
			bool CircularBuffer<Type, Count>::try_emplace_back(Params... p)
	{
		// When _start and _end are equal, the buffer is always full
		// note that when we're empty, _start=0 && _end == Count (to distinguish it
		// from the full case)
		if (_start == _end) return false;
		
		#pragma push_macro("new")
		#undef new
			new(_objects + sizeof(Type)*Internal::Modulo<Count>(_end)) Type(std::forward<Params>(p)...);
		#pragma pop_macro("new")
		_end = Internal::Modulo<Count>(_end+1);
		return true;
	}

	template<typename Type, unsigned Count>
		bool CircularBuffer<Type, Count>::empty() const
	{
		return _start==0 && _end == Count;
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type, Count>::CircularBuffer()
	{
		// special case for empty buffers; _start=0 && _end=Count
		_start=0;
		_end=Count;
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type,Count>::~CircularBuffer() 
	{
		if (_start != 0 || _end != Count)
			for (unsigned c = _start; c != _end; c = Internal::Modulo<Count>(c + 1)) {
				auto& src = ((Type*)_objects)[c];
				(void)src;
				src.~Type();
			}
	}

	template<typename Type, unsigned Count>
		CircularBuffer<Type, Count>::CircularBuffer(CircularBuffer&& moveFrom) never_throws
	{
		_start = moveFrom._start;
		_end = moveFrom._end;
		moveFrom._start = 0;
		moveFrom._end = Count;

		#pragma push_macro("new")
		#undef new
		if (_start != 0 || _end != Count)
			for (unsigned c=_start; c!=_end; c=Internal::Modulo<Count>(c+1)) {
				auto& src = ((Type*)moveFrom._objects)[c];
				new(_objects + sizeof(Type)*c) Type(std::move(src));
				src.~Type();
			}
		#pragma pop_macro("new")
	}

	template<typename Type, unsigned Count>
		auto CircularBuffer<Type, Count>::operator=(CircularBuffer&& moveFrom) never_throws -> CircularBuffer&
	{
		if (_start != 0 || _end != Count)
			for (unsigned c = _start; c != _end; c = Internal::Modulo<Count>(c + 1)) {
				auto& src = ((Type*)_objects)[c];
				(void)src;
				src.~Type();
			}

		_start = moveFrom._start;
		_end = moveFrom._end;
		moveFrom._start = 0;
		moveFrom._end = Count;

		#pragma push_macro("new")
		#undef new
		if (_start != 0 || _end != Count)
			for (unsigned c = _start; c != _end; c = Internal::Modulo<Count>(c + 1)) {
				auto& src = ((Type*)moveFrom._objects)[c];
				new(_objects + sizeof(Type)*c) Type(std::move(src));
				src.~Type();
			}
		#pragma pop_macro("new")
		return *this;
	}
}

using namespace Utility;
