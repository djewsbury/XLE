// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PtrUtils.h"
#include <vector>
#include <memory>

namespace Utility
{
	/// <summary>A dynamically sized array containing variant types</summary>
	/// This array can contain arbitrary counts of different types of objects in contiguous memory.
	///
	/// Typically all of the types stored in any given array will have some common base class
	/// (such as an abstract interface base class). In this way, clients can iterate through
	/// the array and operate on each element polymorphically.
	///
	/// Elements are added to the array using the Allocate() method (which takes a template
	/// parameter for the type to allocate). The array will resize it's internal memory using
	/// an std::vector-like allocation scheme.
	///
	/// Use the begin() and end() functions to iterate through the array. The incrementing
	/// operator on the returned iterator will always advance one element forward, even if
	/// the elements are of varying sizes.
    class VariantArray
    {
    private:
        using MoveFn = void(*)(void*, void*);
        using DestroyFn = void(*)(void*);
        class Entry
        {
        public:
            MoveFn      _moveFn;
            DestroyFn   _destroyFn;
            size_t      _size;
        };

    public:
        template<typename Type>
            Type* Allocate(size_t count=1);

        class const_iterator
        {
        public:
            void operator++();
            friend bool operator==(const const_iterator& lhs, const const_iterator& rhs);
            friend bool operator!=(const const_iterator& lhs, const const_iterator& rhs);

            const void* operator->() const { return _dataStoreIterator; }
            const void* get() const { return _dataStoreIterator; }
            size_t GetItemSize() const { return _entryIterator->_size; }

            const_iterator(
                std::vector<Entry>::const_iterator entryIterator,
                const uint8_t* dataStoreIterator);
            ~const_iterator();

        private:
            std::vector<Entry>::const_iterator _entryIterator;
            const uint8_t* _dataStoreIterator;
        };

        const_iterator begin() const;
        const_iterator end() const;
        bool empty() const { return _entries.empty(); }
        void reserve(size_t byteCount);
        void reserve_entries(size_t count) { _entries.reserve(count); }
        size_t capacity() const { return _dataStoreAllocated; }
        size_t capacity_entries() const { return _entries.capacity(); }
        size_t size() const { return _dataStoreSize; }
        size_t size_entries() const { return _entries.size(); }
        void clear();

        VariantArray();
        ~VariantArray();
        VariantArray(VariantArray&&);
        VariantArray& operator=(VariantArray&&);

    private:
        std::unique_ptr<uint8_t[]> _dataStore;
        size_t _dataStoreSize;
        size_t _dataStoreAllocated;
        std::vector<Entry> _entries;

        template<typename Type> static Entry MakeEntry();
        template<typename Type> static void MoveFnWrapper(void*, void*);
        template<typename Type> static void DestroyFnWrapper(void*);
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma push_macro("new")
#undef new

    template<typename Type>
        Type* VariantArray::Allocate(size_t count)
    {
        // since we don't take into account alignment, we must ensure there's no padding required between array elements
        static_assert((sizeof(Type)%alignof(Type)) == 0);

        size_t startSize = _dataStoreSize;
        size_t allocationRequired = count*sizeof(Type);
        if ((startSize + allocationRequired) > _dataStoreAllocated) {
            size_t newAllocation = startSize + allocationRequired;
            newAllocation += newAllocation / 2;

            auto newDataStore = std::make_unique<uint8_t[]>(newAllocation);
            auto* dstPtr = (void*)newDataStore.get();
            auto* srcPtr = (void*)_dataStore.get();
            for (auto i=_entries.begin(); i!=_entries.end(); ++i) {
                assert(srcPtr < PtrAdd(_dataStore.get(), _dataStoreSize));
                assert(dstPtr < PtrAdd(newDataStore.get(), newAllocation));

                // Move objects into the new data store, and call destructor on old store
                (*i->_moveFn)(dstPtr, srcPtr);
                (*i->_destroyFn)(srcPtr);
                dstPtr = PtrAdd(dstPtr, i->_size);
                srcPtr = PtrAdd(srcPtr, i->_size);
            }
            _dataStore = std::move(newDataStore);
            _dataStoreAllocated = newAllocation;
        }

        // Initialize new elements, with default constructor
        auto it = startSize;
        for (unsigned c=0; c<count; ++c, it+=sizeof(Type)) {
            new(&_dataStore[it]) Type();
        }

        // Update entries array
        _dataStoreSize = startSize + allocationRequired;
        _entries.reserve(_entries.size()+count);
        for (unsigned c=0; c<count; ++c)
            _entries.push_back(MakeEntry<Type>());

        return (Type*)&_dataStore[startSize];
    }

#pragma pop_macro("new")

    template<typename Type>
        auto VariantArray::MakeEntry() -> Entry
    {
        return {
            &MoveFnWrapper<Type>,
            &DestroyFnWrapper<Type>,
            sizeof(Type),
        };
    }

    template<typename Type>
        void VariantArray::MoveFnWrapper(void* lhs, void* rhs)
    {
        *(Type*)lhs = std::move(*(Type*)rhs);
    }

    template<typename Type>
        void VariantArray::DestroyFnWrapper(void* obj)
    {
        ((Type*)obj)->~Type();
    }
}

