// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "ArithmeticUtils.h"
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
            Type GetExisting();
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
        Type FrameByFrameLRUHeap<Type>::QueryResult::GetExisting()
    {
        assert(_type == LRUCacheInsertType::Update);
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
            assert(0);
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
			for (unsigned c = _start;;) {
				auto& src = ((Type*)_objects)[c];
				(void)src;
				src.~Type();
                c=Internal::Modulo<Count>(c+1);
                if (c==_end) break;
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
			for (unsigned c=_start;;) {
				auto& src = ((Type*)moveFrom._objects)[c];
				new(_objects + sizeof(Type)*c) Type(std::move(src));
				src.~Type();
                c=Internal::Modulo<Count>(c+1);
                if (c==_end) break;
			}
		#pragma pop_macro("new")
	}

	template<typename Type, unsigned Count>
		auto CircularBuffer<Type, Count>::operator=(CircularBuffer&& moveFrom) never_throws -> CircularBuffer&
	{
		if (_start != 0 || _end != Count)
			for (unsigned c = _start;;) {
				auto& src = ((Type*)_objects)[c];
				(void)src;
				src.~Type();
                c=Internal::Modulo<Count>(c+1);
                if (c==_end) break;
			}

		_start = moveFrom._start;
		_end = moveFrom._end;
		moveFrom._start = 0;
		moveFrom._end = Count;

		#pragma push_macro("new")
		#undef new
		if (_start != 0 || _end != Count)
			for (unsigned c = _start;;) {
				auto& src = ((Type*)moveFrom._objects)[c];
				new(_objects + sizeof(Type)*c) Type(std::move(src));
				src.~Type();
                c=Internal::Modulo<Count>(c+1);
                if (c==_end) break;
			}
		#pragma pop_macro("new")
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

		CircularHeap(unsigned heapSize);
		CircularHeap();
		~CircularHeap();
	private:
		unsigned	_start, _end, _heapSize;
	};

}

using namespace Utility;
