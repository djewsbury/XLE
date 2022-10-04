// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Core/Exceptions.h"
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <algorithm>		// (for std::max)
#include <type_traits>
#include <assert.h>

template<typename Stream, typename Object, std::remove_reference_t<decltype(SerializationOperator(std::declval<Stream&>(), std::declval<const Object&>()))>* =nullptr>
	Stream& operator<<(Stream& stream, const Object& obj)
		{ SerializationOperator(stream, obj); return stream; }

template<typename Stream, typename Object, std::remove_reference_t<decltype(DeserializationOperator(std::declval<Stream&>(), std::declval<Object&>()))>* =nullptr>
	Stream& operator>>(Stream& stream, Object& obj)
		{ DeserializationOperator(stream, obj); return stream; }

template<typename Stream, typename Object, std::remove_reference_t<decltype(DeserializationOperator(std::declval<const Stream&>(), std::declval<Object&>()))>* =nullptr>
	const Stream& operator>>(const Stream& stream, Object& obj)
		{ DeserializationOperator(stream, obj); return stream; }

namespace Utility
{
        ////////////////////////////////////////////////////

    #pragma warning(push)
    #pragma warning(disable:4702)

    /// <summary>And STL allocator that will suppress deallocation when memory comes from a part of a larger heap block<summary>
    /// When we serialize an object in via the block serializer, we can loading it into a single heap block.
    /// However, objects loaded new can have STL containers and objects (like vectors and strings). In this case,
    /// the memory used by the container is a just a part of the larger heap block. When the container is
    /// destroyed, it will attempt to free it's memory. In the of a normal container, the memory is it's own
    /// unique heap block, and the normal deallocation functions can be used. But for our serialized containers,
    /// the memory is not a unique heap block. It is just a internal part of much larger block. In this case, we
    /// must suppress the deallocation.
    /// The BlockSerializerAllocator does exactly that. For vectors and containers that have been serialized in,
    /// we suppress the deallocation step.
    template<typename Type>
        class BlockSerializerAllocator : public std::allocator<Type>
    {
    public:
		using BaseAllocatorTraits = std::allocator_traits<std::allocator<Type>>;
        typename BaseAllocatorTraits::pointer allocate(typename BaseAllocatorTraits::size_type n, typename BaseAllocatorTraits::const_void_pointer ptr= 0)
        {
            if (_fromFixedStorage) {
                Throw(std::invalid_argument("Cannot allocate from a BlockSerializerAllocator than has been serialized in from a fixed block"));
                return nullptr;
            }
            return std::allocator<Type>::allocate(n, ptr);
        }

        void deallocate(typename BaseAllocatorTraits::pointer p, typename BaseAllocatorTraits::size_type n)
        {
            if (!_fromFixedStorage) {
                std::allocator<Type>::deallocate(p, n);
            }
        }

        template<class _Other>
		    struct rebind
		    {
		        typedef BlockSerializerAllocator<_Other> other;
		    };

        BlockSerializerAllocator()                                                  : _fromFixedStorage(0) {}
        explicit BlockSerializerAllocator(unsigned fromFixedStorage)                : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerAllocator(const std::allocator<Type>& copyFrom)              : _fromFixedStorage(0) {}
        BlockSerializerAllocator(std::allocator<Type>&& moveFrom)                   : _fromFixedStorage(0) {}
        BlockSerializerAllocator(const BlockSerializerAllocator<Type>& copyFrom)    : _fromFixedStorage(0) {}
        BlockSerializerAllocator(BlockSerializerAllocator<Type>&& moveFrom)         : _fromFixedStorage(moveFrom._fromFixedStorage) {}

		BlockSerializerAllocator& operator=(const BlockSerializerAllocator<Type>& copyFrom) { _fromFixedStorage = 0; return *this; }
		BlockSerializerAllocator& operator=(BlockSerializerAllocator<Type>&& moveFrom)		{ _fromFixedStorage = moveFrom._fromFixedStorage; return *this; }

    private:
        unsigned    _fromFixedStorage;
    };

    #pragma warning(pop)

        ////////////////////////////////////////////////////

    template<typename Type>
        class BlockSerializerDeleter : public std::default_delete<Type>
    {
    public:
        void operator()(Type *_Ptr) const
        {
            if (!_fromFixedStorage) {
                std::default_delete<Type>::operator()(_Ptr);
            }
        }

        BlockSerializerDeleter()                                                : _fromFixedStorage(0) {}
        explicit BlockSerializerDeleter(unsigned fromFixedStorage)              : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerDeleter(const std::default_delete<Type>& copyFrom)       : _fromFixedStorage(0) {}
        BlockSerializerDeleter(std::default_delete<Type>&& moveFrom)            : _fromFixedStorage(0) {}
        BlockSerializerDeleter(const BlockSerializerDeleter<Type>& copyFrom)    : _fromFixedStorage(copyFrom._fromFixedStorage) {}
        BlockSerializerDeleter(BlockSerializerDeleter<Type>&& moveFrom)         : _fromFixedStorage(moveFrom._fromFixedStorage) {}
    private:
        unsigned    _fromFixedStorage;
    };

        ////////////////////////////////////////////////////

    template<typename Type>
        class BlockSerializerDeleter<Type[]> : public std::default_delete<Type[]>
    {
    public:
        void operator()(Type *_Ptr) const
        {
            if (!_fromFixedStorage) {
                std::default_delete<Type[]>::operator()(_Ptr);
            }
        }

        BlockSerializerDeleter()                                                : _fromFixedStorage(0) {}
        explicit BlockSerializerDeleter(unsigned fromFixedStorage)              : _fromFixedStorage(fromFixedStorage) {}
        BlockSerializerDeleter(const std::default_delete<Type[]>& copyFrom)     : _fromFixedStorage(0) {}
        BlockSerializerDeleter(std::default_delete<Type[]>&& moveFrom)          : _fromFixedStorage(0) {}
        BlockSerializerDeleter(const BlockSerializerDeleter<Type[]>& copyFrom)  : _fromFixedStorage(copyFrom._fromFixedStorage) {}
        BlockSerializerDeleter(BlockSerializerDeleter<Type[]>&& moveFrom)       : _fromFixedStorage(moveFrom._fromFixedStorage) {}
		BlockSerializerDeleter& operator=(const BlockSerializerDeleter<Type[]>& copyFrom) { copyFrom._fromFixedStorage = 0; return *this; }
		BlockSerializerDeleter& operator=(BlockSerializerDeleter<Type[]>&& moveFrom) { _fromFixedStorage = moveFrom._fromFixedStorage; return *this; }
    private:
        unsigned    _fromFixedStorage;
    };

        ////////////////////////////////////////////////////

	#pragma push_macro("new")
	#undef new

    #if INTPTR_MAX == INT32_MAX
        #define DummyBuffer(c) unsigned _buffer_##c
    #else
        #define DummyBuffer(c)
    #endif

	template<typename Element>
		class SerializableVector
	{
	public:
		typedef Element value_type;
		typedef std::size_t size_type;
		typedef std::ptrdiff_t difference_type;
		typedef value_type& reference;
		typedef const value_type& const_reference;
		typedef value_type* pointer;
		typedef const value_type* const_pointer;
		typedef pointer iterator;
		typedef const_pointer const_iterator;

		size_type size() const { return _end - _begin; }
		bool empty() const { return _begin == _end; }
		size_type capacity() const { return _capacity - _begin; }
		reference operator[](size_type idx) { assert(idx < size());  return _begin[idx]; }
		const_reference operator[](size_type idx) const { assert(idx < size());  return _begin[idx]; }

		iterator begin() { return _begin; }
		const_iterator begin() const { return _begin; }
		const_iterator cbegin() const { return _begin; }
		iterator end() { return _end; }
		const_iterator end() const { return _end; }
		const_iterator cend() const { return _end; }
		Element* data() { return _begin; }
		const Element* data() const { return _begin; }

		void push_back(const Element& value) 
		{
			assert(OwnsHeapBlock());
			if ((_end+1) > _capacity) {
				Expand(size()+1);
			}
			new(_end) Element(value);
			++_end;
		}

		void push_back(Element&& value) 
		{
			assert(OwnsHeapBlock());
			if ((_end+1) > _capacity) {
				Expand(size()+1);
			}
			new(_end) Element(std::move(value));
			++_end;
		}

		template<typename... ConstructorArgs>
			void emplace_back(ConstructorArgs&&... args)
			{
				assert(OwnsHeapBlock());
				if ((_end+1) > _capacity) {
					Expand(size()+1);
				}
				new(_end) Element(std::forward<ConstructorArgs>(args)...);
				++_end;
			}

		iterator erase(const_iterator pos) 
		{
			assert(pos >= _begin && pos < _end);
			assert(OwnsHeapBlock());
			for (auto i= const_cast<iterator>(pos); (i+1)!=_end; ++i) *i = std::move(*(i+1));
			--_end;
			return const_cast<iterator>(pos);
		}

		iterator erase(const_iterator first, const_iterator last) 
		{
			assert(first >= _begin && first <= _end);
			assert(last >= _begin && last <= _end);
			if (first == last) return const_cast<iterator>(first);
			assert(OwnsHeapBlock());
			auto cnt = last-first;
			// note -- assuming move operators never throw! If we get an exception during
			// a move operator, we cannot reverse this operation.
			for (auto i=const_cast<iterator>(first); (i+cnt)!=_end; ++i) *i = std::move(*(i+cnt));
			_end = _end - cnt;
			return const_cast<iterator>(first);
		}

		iterator insert(const_iterator pos, const Element& ele)
		{
			assert(pos >= _begin && pos <= _end);
			auto idx = pos - _begin;
			if ((_end+1) > _capacity) {
				Expand(size()+1);
			}
			// note -- assuming move operators never throw! If we get an exception during
			// a move operator, we cannot reverse this operation.
			for (auto i=_end; i!=&_begin[idx]; --i) *(i) = std::move(*(i-1));
			_begin[idx] = ele;
			++_end;
			return &_begin[idx];
		}

		iterator insert(const_iterator pos, Element&& ele)
		{
			assert(pos >= _begin && pos <= _end);
			auto idx = pos - _begin;
			if ((_end+1) > _capacity) {
				Expand(size()+1);
			}
			// note -- assuming move operators never throw! If we get an exception during
			// a move operator, we cannot reverse this operation.
			for (auto i=_end; i!=&_begin[idx]; --i) *(i) = std::move(*(i-1));
			_begin[idx] = std::move(ele);
			++_end;
			return &_begin[idx];
		}

		template< class InputIt >
			iterator insert(const_iterator pos, InputIt first, InputIt last)
		{
			assert(pos >= _begin && pos <= _end);
			auto idx = pos - _begin;
			auto cnt = std::distance(first, last);
            if (!cnt) return &_begin[idx];

			if ((_end+cnt) > _capacity) {
				Expand(size()+cnt);
			}
			// note -- assuming move operators never throw! If we get an exception during
			// a move operator, we cannot reverse this operation.
			for (auto i=_end+cnt-1; i>&_begin[idx+cnt-1]; --i) *(i) = std::move(*(i-cnt));
			auto c = idx;
			for (auto i=first; i!=last; ++i, ++c)
				_begin[c] = *i;
			_end += cnt;
			return &_begin[idx];
		}

		void reserve(size_type amount)
		{
			if (amount > capacity())
				Expand(amount);		// (note; still runs the doubling metric, and so sometimes capacity will end up different from "amount")
		}

		void resize(size_type newSize)
		{
			if (newSize <= size()) {
				for (auto i=_begin+newSize; i!=_end; ++i) i->~Element();
				_end = _begin+newSize;
			} else {
				reserve(newSize);
				for (; _end!=_begin+newSize; ++_end) {
					new(_end) Element();
				}
			}
		}

		template<typename Initializer>
			void resize(size_type newSize, const Initializer& initializer)
		{
			if (newSize <= size()) {
				for (auto i = _begin + newSize; i != _end; ++i) i->~Element();
				_end = _begin + newSize;
			}
			else {
				reserve(newSize);
				for (; _end != _begin + newSize; ++_end) {
					new(_end) Element(initializer);		// (note, we can't do perfect forwarding here, because of course we're going to be using it mutliple times)
				}
			}
		}

		bool OwnsHeapBlock() const { return !(_capacity == nullptr && _begin != nullptr); }

		SerializableVector() : _begin(nullptr), _end(nullptr), _capacity(nullptr) {}
		
		template< class InputIt >
			SerializableVector(InputIt begin, InputIt end)
			: SerializableVector()
		{
			insert(this->end(), begin, end);
		}

		~SerializableVector()
		{
			for (auto i=_begin; i!=_end; ++i) i->~Element();
			if (OwnsHeapBlock()) delete[] (uint8_t*)_begin;
		}

		SerializableVector(SerializableVector&& moveFrom) never_throws : _begin(moveFrom._begin), _end(moveFrom._end), _capacity(moveFrom._capacity) 
		{
			moveFrom._begin = moveFrom._end = moveFrom._capacity = nullptr;
		}

		SerializableVector& operator=(SerializableVector&& moveFrom) never_throws
		{
			SerializableVector temp(std::move(*this));
			_begin = moveFrom._begin; _end = moveFrom._end; _capacity = moveFrom._capacity;
			moveFrom._begin = moveFrom._end = moveFrom._capacity = nullptr;
			return *this;
		}

		SerializableVector(const SerializableVector& copyFrom)
			: SerializableVector()
		{
			if (!copyFrom.empty())
				insert(this->end(), copyFrom.begin(), copyFrom.end());
		}

		SerializableVector& operator=(const SerializableVector& copyFrom)
		{
			SerializableVector newVec(copyFrom);
			*this = std::move(newVec);
			return *this;
		}

	private:
		Element* _begin;
        DummyBuffer(0);
		Element* _end;
        DummyBuffer(1);
		Element* _capacity;
        DummyBuffer(2);

		void Expand(size_type requiredSize)
		{
			auto originalSize = size();
			auto newCapacity = std::max(std::max(size()*2-size()/2, size_t(8)), requiredSize);
			auto newBlock = std::make_unique<uint8_t[]>(newCapacity*sizeof(Element));
			// note -- assuming move operators never throw! If we get an exception during
			// a move operator, we cannot reverse this operation.
			for (size_type c=0; c<originalSize; ++c)
				((Element*)newBlock.get())[c] = std::move(_begin[c]);
			SerializableVector temp(std::move(*this));
			_begin = (Element*)newBlock.release();
			_end = &_begin[originalSize];
			_capacity = &_begin[newCapacity];
		}
	};

	#pragma pop_macro("new")
}

using namespace Utility;

