// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MemoryUtils.h"
#include "VariantUtils.h"
#include "PtrUtils.h"

namespace Utility
{
    void PODAlignedDeletor::operator()(void* ptr) 
    { 
        XlMemAlignFree(ptr); 
    }

    uint64_t ConstHash64FromString(const char* begin, const char* end)
    {
        // We want to match the results that we would
        // get from the compiler-time ConstHash64
        // Separate the string into 4 char types.
        // First char is in most significant bits,
        // but incomplete groups of 4 are aligned to
        // the least significant bits.

        uint64_t result = ConstHash64<0>::Seed;
        const char* i = begin;
        while (i != end) {
            const auto* s = i;
            unsigned newValue = 0;
            while (i != end && (i-s) < 4) {
                newValue = (newValue << 8) | (*i);
                ++i;
            }

            result = (newValue == 0) ? result : (((result << 21ull) | (result >> 43ull)) ^ uint64_t(newValue));
        }

        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void VariantArray::const_iterator::operator++()
    {
        auto size = _entryIterator->_size;
        ++_entryIterator;
        _dataStoreIterator += size;
    }

    bool operator==(const VariantArray::const_iterator& lhs, const VariantArray::const_iterator& rhs)
    {
        assert((lhs._entryIterator == rhs._entryIterator) == (lhs._dataStoreIterator == rhs._dataStoreIterator));
        return lhs._entryIterator == rhs._entryIterator;
    }

    bool operator!=(const VariantArray::const_iterator& lhs, const VariantArray::const_iterator& rhs)
    {
        assert((lhs._entryIterator == rhs._entryIterator) == (lhs._dataStoreIterator == rhs._dataStoreIterator));
        return lhs._entryIterator != rhs._entryIterator;
    }

    VariantArray::const_iterator::const_iterator(
        std::vector<Entry>::const_iterator entryIterator,
        const uint8_t* dataStoreIterator)
    : _entryIterator(entryIterator), _dataStoreIterator(dataStoreIterator)
    {}

    VariantArray::const_iterator::~const_iterator() {}

    auto VariantArray::begin() const -> const_iterator
    {
        return const_iterator{_entries.begin(), _dataStore.get()};
    }

    auto VariantArray::end() const -> const_iterator
    {
        return const_iterator{_entries.end(), &_dataStore[_dataStoreSize]};
    }

    void VariantArray::reserve(size_t byteCount)
    {
        if (_dataStoreAllocated >= byteCount)
            return;
        assert(_dataStoreSize <= byteCount);

        auto newDataStore = std::make_unique<uint8_t[]>(byteCount);
        auto* dstPtr = (void*)newDataStore.get();
        auto* srcPtr = (void*)_dataStore.get();
        for (auto i=_entries.begin(); i!=_entries.end(); ++i) {
            assert(srcPtr < PtrAdd(_dataStore.get(), _dataStoreSize));
            assert(dstPtr < PtrAdd(newDataStore.get(), byteCount));

            // Move objects into the new data store, and call destructor on old store
            (*i->_moveFn)(dstPtr, srcPtr);
            (*i->_destroyFn)(srcPtr);
            dstPtr = PtrAdd(dstPtr, i->_size);
            srcPtr = PtrAdd(srcPtr, i->_size);
        }
        _dataStore = std::move(newDataStore);
        _dataStoreAllocated = byteCount;
    }

    void VariantArray::clear()
    {
        auto* srcPtr = (void*)_dataStore.get();
        for (auto i=_entries.begin(); i!=_entries.end(); ++i) {
            (*i->_destroyFn)(srcPtr);
            srcPtr = PtrAdd(srcPtr, i->_size);
        }
        _dataStoreSize = 0;
        _entries.clear();
    }

    VariantArray::VariantArray(VariantArray&& moveFrom)
    : _dataStore(std::move(moveFrom._dataStore))
    , _dataStoreSize(moveFrom._dataStoreSize)
    , _dataStoreAllocated(moveFrom._dataStoreAllocated)
    , _entries(std::move(moveFrom._entries))
    {
        moveFrom._dataStoreSize = 0;
        moveFrom._dataStoreAllocated = 0;
    }

    VariantArray& VariantArray::operator=(VariantArray&& moveFrom)
    {
        _dataStore = std::move(moveFrom._dataStore);
        _dataStoreSize = moveFrom._dataStoreSize;
        _dataStoreAllocated = moveFrom._dataStoreAllocated;
        _entries = std::move(moveFrom._entries);
        moveFrom._dataStoreSize = 0;
        moveFrom._dataStoreAllocated = 0;
        return *this;
    }

    VariantArray::VariantArray()
    {
        _dataStoreSize = 0;
        _dataStoreAllocated = 0;
    }

    VariantArray::~VariantArray()
    {
        auto* srcPtr = (void*)_dataStore.get();
        for (auto i=_entries.begin(); i!=_entries.end(); ++i) {
            (*i->_destroyFn)(srcPtr);
            srcPtr = PtrAdd(srcPtr, i->_size);
        }
    }
}


