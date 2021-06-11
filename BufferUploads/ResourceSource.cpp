// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceSource.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/ResourceDesc.h"
#include "../OSServices/Log.h"
#include "../Utility/BitUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include <algorithm>

namespace BufferUploads
{
    namespace CPUAccess = RenderCore::CPUAccess;
	namespace GPUAccess = RenderCore::GPUAccess;
	namespace BindFlag = RenderCore::BindFlag;
	namespace AllocationRules = RenderCore::AllocationRules;

    // ~~~~~~~~~~~~ // ~~~~~~<   >~~~~~~ // ~~~~~~~~~~~~ //

    static unsigned RoundUpBufferSize(unsigned input)
    {
        unsigned log2 = IntegerLog2(input);
        if ((1<<log2)==input) {
            return input;
        }
        unsigned nextBit = 1<<(log2-1);
        unsigned nextBit2 = 1<<(log2-2);
        if (log2 >= 14 && !(input & nextBit2)) {
            return (input&((1<<log2)|nextBit))|nextBit2;
        }
        if (log2 >= 12 && !(input & nextBit)) {
            return (1<<log2)|nextBit;
        }
        return 1<<(log2+1);
    }

    // ~~~~~~~~~~~~ // ~~~~~~<   >~~~~~~ // ~~~~~~~~~~~~ //

        /////   R E S O U R C E S   P O O L   /////

    #define tdesc template<typename Desc>

    tdesc auto ResourcesPool<Desc>::PoolOfLikeResources::AllocateResource(size_t realSize, bool allowDeviceCreation) -> std::shared_ptr<IResource>
        {
            Entry* front = NULL;
            if (_allocableResources.try_front(front)) {
                ++_recentPoolCreateCount;
                std::shared_ptr<IResource> result = std::move(front->_underlying);
                _allocableResources.pop();
                return result;
            } else if (allowDeviceCreation) {
                auto result = _underlyingDevice->CreateResource(_desc);
                if (result) {
                    _totalRealSize += realSize;
                    _totalCreateSize += RenderCore::ByteCount(_desc);
                    ++_recentDeviceCreateCount;
                    ++_totalCreateCount;
                }
                return result;
            }
            return nullptr;
        }

    tdesc void ResourcesPool<Desc>::PoolOfLikeResources::Update(unsigned newFrameID)
        {
            _currentFrameID = newFrameID;
                // pop off any resources that have lived here for too long
            if (_retainFrames != ~unsigned(0x0)) {
                const unsigned minToKeep = 4;
                while (_allocableResources.size() > minToKeep) {
                    Entry* front = NULL;
                    if (!_allocableResources.try_front(front) || (newFrameID - front->_returnFrameID) < _retainFrames) {
                        break;
                    }
                    _allocableResources.pop();
                }
            }
        }

    tdesc void ResourcesPool<Desc>::PoolOfLikeResources::ReturnToPool(std::shared_ptr<IResource>&& resource)
        {
            Entry newEntry;
            newEntry._underlying = std::move(resource);
            newEntry._returnFrameID = _currentFrameID;
            _allocableResources.push(newEntry);
            ++_recentReleaseCount;
        }

    tdesc ResourcesPool<Desc>::PoolOfLikeResources::PoolOfLikeResources(
			RenderCore::IDevice& underlyingDevice, const Desc& desc, unsigned retainFrames) : _desc(desc)
        {
            _peakSize = 0;
            _recentDeviceCreateCount = _recentPoolCreateCount = _recentReleaseCount = 0;
            _totalCreateSize = _totalCreateCount = _totalRealSize = 0;
            _currentFrameID = 0;
            _retainFrames = retainFrames;
            _underlyingDevice = &underlyingDevice;
        }

    tdesc ResourcesPool<Desc>::PoolOfLikeResources::~PoolOfLikeResources()
        {
        }

    tdesc PoolMetrics    ResourcesPool<Desc>::PoolOfLikeResources::CalculateMetrics() const
    {
        PoolMetrics result;
        result._desc = _desc;
        {
            //ScopedLock(_lock);
            result._currentSize = (unsigned)_allocableResources.size();
        }
        result._peakSize = _peakSize = std::max(_peakSize, result._currentSize);
        result._topMostAge               = 0;
        result._recentDeviceCreateCount  = _recentDeviceCreateCount.exchange(0);
        result._recentPoolCreateCount    = _recentPoolCreateCount.exchange(0);
        result._recentReleaseCount       = _recentReleaseCount.exchange(0);
        result._totalRealSize            = _totalRealSize;
        result._totalCreateSize          = _totalCreateSize;
        result._totalCreateCount         = _totalCreateCount;
        return result;
    }

    tdesc ResourcesPool<Desc>::ResourcesPool(RenderCore::IDevice& device, unsigned retainFrames) 
	: _hashTableIndex(0), _retainFrames(retainFrames), _underlyingDevice(&device)
    {
        _readerCount[0] = _readerCount[1] = 0;
    }
    
    tdesc ResourcesPool<Desc>::~ResourcesPool() {}

    tdesc ResourceLocator   ResourcesPool<Desc>::CreateResource(
            const Desc& desc, unsigned realSize, bool allowDeviceCreation)
        {
            DescHash hashValue = desc.CalculateHash();
            {
                unsigned hashTableIndex = _hashTableIndex;
                ++_readerCount[hashTableIndex];
                HashTable& hashTable = _hashTables[hashTableIndex];
                auto entry = std::lower_bound(hashTable.begin(), hashTable.end(), hashValue, CompareFirst());
                if (entry != hashTable.end() && entry->first == hashValue) {
                    if (desc._type == ResourceDesc::Type::Texture) {
                        assert(desc._textureDesc._width == entry->second->GetDesc()._textureDesc._width);
                        assert(desc._textureDesc._height == entry->second->GetDesc()._textureDesc._height);
                        assert(desc._textureDesc._mipCount == entry->second->GetDesc()._textureDesc._mipCount);
                        assert(desc._textureDesc._format == entry->second->GetDesc()._textureDesc._format);
                    }
                    auto newResource = entry->second.get()->AllocateResource(realSize, allowDeviceCreation);
                    --_readerCount[hashTableIndex];
                    return MakeReturnToPoolPointer(std::move(newResource), hashValue);
                }
                --_readerCount[hashTableIndex];
            }

            if (!allowDeviceCreation)
                return {};

                //
                //              -=*=- Insert a new hash table entry for this type of resource -=*=-
                //
            {
                ScopedLock(_writerLock);

                    //
                    //      Doubled buffered writing scheme... We don't change the hash table very often, so let's optimise
                    //      for when the hash table isn't modified. 
                    //      Note there might be a problem here if we get a reader that uses a hash table while another thread
                    //      has enough table to modify the hash tables twice.
                    //
                unsigned oldHashTableIndex = _hashTableIndex;
                unsigned nextHashTableIndex = (oldHashTableIndex+1)%dimof(_hashTables);
                HashTable& newHashTable = _hashTables[nextHashTableIndex];
                newHashTable = _hashTables[oldHashTableIndex];
                auto entry = std::lower_bound(newHashTable.begin(), newHashTable.end(), hashValue, CompareFirst());

                HashTableEntry newEntry;
                newEntry.first = hashValue;
                newEntry.second = std::make_shared<PoolOfLikeResources>(
                    std::ref(*_underlyingDevice), desc, _retainFrames);
                auto newIterator = newHashTable.insert(entry, newEntry);
                _hashTableIndex = nextHashTableIndex;

                auto newResource = newIterator->second->AllocateResource(realSize, true);

                    //  We should wait until there are no more readers on the old hash table before we give up the "_writerLock" mutex. This is because
                    //  Another thread could create a new entry immediately after, and start writing over the old hash table while readers are still
                    //  using it (even though this is very rare, it still does happen). But if we wait until that hash table is no longer being used, we're safe 
                    //  to release the _writerLock. Unfortunately it requires an extra interlocked Increment/Decrement in every read operation...
                while (_readerCount[oldHashTableIndex]) {}

                return MakeReturnToPoolPointer(std::move(newResource), hashValue);
            }
        }

    tdesc void        ResourcesPool<Desc>::AddRef(
            uint64_t resourceMarker, IResource& resource, 
            size_t offset, size_t size)
        {
            // we don't have to do anything in this case
        }

    tdesc void        ResourcesPool<Desc>::Release(
            uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
            size_t offset, size_t size)
        {}

    tdesc std::shared_ptr<IResource> ResourcesPool<Desc>::MakeReturnToPoolPointer(std::shared_ptr<IResource>&& resource, uint64_t poolMarker)
    {
        // We're going to create a second std::shared_ptr<> that points to the same resource,
        // but it's destruction routine will return it to the pool.
        // The destruction routine also captures the original shared pointer!
        auto weakThisI = std::enable_shared_from_this<ResourcesPool<Desc>>::weak_from_this();
        auto* res = resource.get();
        return std::shared_ptr<IResource>(
            res,
            [originalPtr = std::move(resource), poolMarker, weakThis = std::move(weakThisI)](IResource*) mutable {
                auto strongThis = weakThis.lock();
                if (strongThis)
                    strongThis->ReturnToPool(std::move(originalPtr), poolMarker);
            });
    }
    
    tdesc void        ResourcesPool<Desc>::ReturnToPool(std::shared_ptr<IResource>&& resource, uint64_t resourceMarker)
        {
            unsigned hashTableIndex = _hashTableIndex;
            ++_readerCount[hashTableIndex];
            HashTable& hashTable = _hashTables[hashTableIndex];
            auto entry = std::lower_bound(hashTable.begin(), hashTable.end(), resourceMarker, CompareFirst());
            if (entry != hashTable.end() && entry->first == resourceMarker) {
                entry->second->ReturnToPool(std::move(resource));
                --_readerCount[hashTableIndex];
            } else {
                --_readerCount[hashTableIndex];
            }
        }

    tdesc void        ResourcesPool<Desc>::Update(unsigned newFrameID)
        {
            unsigned hashTableIndex = _hashTableIndex;
            ++_readerCount[hashTableIndex];
            HashTable& hashTable = _hashTables[hashTableIndex];
            for (auto i=hashTable.begin(); i!=hashTable.end(); ++i) {
                i->second->Update(newFrameID);
            }
            --_readerCount[hashTableIndex];
        }

    tdesc std::vector<PoolMetrics>        ResourcesPool<Desc>::CalculateMetrics() const
    {
        ScopedLock(_writerLock);
        const HashTable& hashTable = _hashTables[_hashTableIndex];
        std::vector<PoolMetrics> result;
        result.reserve(hashTable.size());
        for (auto i = hashTable.begin(); i!= hashTable.end(); ++i) {
            result.push_back(i->second->CalculateMetrics());
        }
        return result;
    }

    tdesc void            ResourcesPool<Desc>::OnLostDevice()
    {
        ScopedLock(_writerLock);
        for (unsigned c=0; c<dimof(_hashTables); ++c) {
            while (_readerCount[c]) {}
            _hashTables[c] = HashTable();
        }
        _hashTableIndex = 0;
    }

        /////   B A T C H E D   R E S O U R C E S   /////

    ResourceLocator    BatchedResources::Allocate(
        size_t size, const char name[])
    {
        if (size > RenderCore::ByteCount(_prototype)) {
            return {};
        }

        {
            std::unique_lock<decltype(_lock)> lk(_lock);
            {
                ScopedLock(_activeDefrag_Lock);  // prevent _activeDefragHeap from changing while doing this...
                                                //  (we can't allocate from a heap that is currently being defragged)
                // for (std::vector<HeapedResource*>::reverse_iterator i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
                //     if ((*i) != _activeDefragHeap) {
                //         assert(!_activeDefrag.get() || _activeDefrag->GetHeap()!=*i);
                //         unsigned allocation = (*i)->Allocate(size);
                //         if (allocation != ~unsigned(0x0)) {
                //             assert((allocation+size)<=PlatformInterface::ByteCount(_prototype));
                //             return ResourceLocator((*i)->_heapResource, allocation, size);
                //         }
                //     }
                // }

                HeapedResource* bestHeap = NULL;
                unsigned bestHeapLargestBlock = ~unsigned(0x0);
                for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
                    if (i->get() != _activeDefragHeap) {
                        assert(!_activeDefrag.get() || _activeDefrag->GetHeap()!=i->get());
                        unsigned largestBlock = (*i)->_heap.CalculateLargestFreeBlock();
                        if (largestBlock >= size && largestBlock < bestHeapLargestBlock) {
                            bestHeap = i->get();
                            bestHeapLargestBlock = largestBlock;
                        }
                    }
                }

                if (bestHeap) {
                    unsigned allocation = bestHeap->Allocate(size, name);
                    if (allocation != ~unsigned(0x0)) {
                        assert((allocation+size)<=RenderCore::ByteCount(_prototype));
                        // We take the reference count before the ResourceLocator is created in
                        // order to avoid looking up the HeapedResource a second time, and avoid
                        // issues with non-recursive mutex locks
                        bestHeap->AddRef(allocation, size, "<<unknown>>");
                        return ResourceLocator{
                            bestHeap->_heapResource, 
                            allocation, size, 
                            weak_from_this(), 0ull,
                            true};
                    }
                }
            }
        }

        auto heapResource = _device->CreateResource(_prototype);
        if (!heapResource) {
            return {};
        }

        ++_recentDeviceCreateCount;
        ++_totalCreateCount;

        auto newHeap = std::make_unique<HeapedResource>(_prototype, heapResource);
        unsigned allocation = newHeap->Allocate(size, name);
        assert(allocation != ~unsigned(0x0));
        newHeap->AddRef(allocation, size, "<<unknown>>");

        {
            ScopedModifyLock(_lock);
            _heaps.push_back(std::move(newHeap));
        }

        return ResourceLocator{std::move(heapResource), allocation, size, weak_from_this(), 0ull, true};
    }
    
    void BatchedResources::AddRef(
        uint64_t resourceMarker, IResource& resource, 
        size_t offset, size_t size)
    {
        ScopedReadLock(_lock);
        HeapedResource* heap = NULL;
        if (_activeDefrag.get() && _activeDefrag->GetHeap()->_heapResource.get() == &resource) {
            heap = _activeDefrag->GetHeap();
        } else {
            for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
                if ((*i)->_heapResource.get() == &resource) {
                    heap = i->get();
                    break;
                }
            }
        }

        if (heap) {
            heap->AddRef(offset, size, "<<unknown>>");
        } else {
            assert(0);
        }
    }

    void BatchedResources::Release(
        uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
        size_t offset, size_t size)
    {
        ScopedReadLock(_lock);
        HeapedResource* heap = NULL;
        if (_activeDefrag.get() && _activeDefrag->GetHeap()->_heapResource == resource) {
            heap = _activeDefrag->GetHeap();
        } else {
            for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
                if ((*i)->_heapResource == resource) {
                    heap = i->get();
                    break;
                }
            }
        }

        if (heap) {
            if (heap->Deref(offset, size)) {
                assert(!_activeDefrag.get() || heap != _activeDefrag->GetHeap());
                if (_activeDefragHeap == heap) {
                    _activeDefrag->QueueOperation(
                        ActiveDefrag::Operation::Deallocate, offset, offset+size);
                } else {
                    heap->Deallocate(offset, size);
                }
                #if defined(_DEBUG)
                    heap->ValidateRefsAndHeap();
                #endif
            }

                // (prevent caller from performing extra derefs)
            resource = nullptr;
        }
    }

    BatchedResources::ResultFlags::BitField BatchedResources::IsBatchedResource(
        IResource* resource) const
    {
        ScopedReadLock(_lock);
        if (_activeDefrag.get() && _activeDefrag->GetHeap()->_heapResource.get() == resource) {
            return ResultFlags::IsBatched;
        }
        for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
            if ((*i)->_heapResource.get() == resource) {
                return ResultFlags::IsBatched|(i->get()==_activeDefragHeap?ResultFlags::IsCurrentlyDefragging:0);
            }
        }
        return 0;
    }

    BatchedResources::ResultFlags::BitField BatchedResources::Validate(const ResourceLocator& locator) const
    {
        ScopedReadLock(_lock);

            //      check to make sure the same resource isn't showing up twice
        for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
            for (auto i2=i+1; i2!=_heaps.end(); ++i2) {
                assert((*i2)->_heapResource != (*i)->_heapResource);
            }
        }

        BatchedResources::ResultFlags::BitField result = 0;
        const HeapedResource* heapResource = NULL;
        if (_activeDefrag.get() && _activeDefrag->GetHeap()->_heapResource.get() == locator.GetContainingResource().get()) {
            heapResource = _activeDefrag->GetHeap();
        } else {
            for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
                if ((*i)->_heapResource.get() == locator.GetContainingResource().get()) {
                    heapResource = i->get();
                    break;
                }
            }
        }
        if (heapResource) {
            result |= BatchedResources::ResultFlags::IsBatched;
            auto range = locator.GetRangeInContainingResource();
            assert(heapResource->_refCounts.ValidateBlock(range.first, range.second-range.first));
        }
        return result;
    }

    BatchingSystemMetrics BatchedResources::CalculateMetrics() const
    {
        ScopedReadLock(_lock);
        BatchingSystemMetrics result;
        result._heaps.reserve(_heaps.size());
        for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
            result._heaps.push_back((*i)->CalculateMetrics());
        }
        result._recentDeviceCreateCount = _recentDeviceCreateCount.exchange(0);
        result._totalDeviceCreateCount = _totalCreateCount.load();
        return result;
    }

    void BatchedResources::TickDefrag(ThreadContext& context, IManager::EventListID processedEventList)
    {
        return;

        if (!_activeDefrag.get()) {

            assert(!_activeDefragHeap);

                    //                            -                                 //
                    //                         -------                              //
                    //                                                              //
                //////////////////////////////////////////////////////////////////////////
                    //      Start a new defrag step on the most fragmented heap     //
                    //                            -                                 //
                    //        Note that we defrag the allocated spans, rather       //
                    //        than the blocks. This means that adjacent blocks      //
                    //        always move with each other, regardless of            //
                    //        their size, and ideal finish position.                //
                    //                            -                                 //
                    //        On slower PCs we can end up consuming a lot of        //
                    //        time just doing the defrags. So we need to            //
                    //        throttle it a bit, and so that we only do the         //
                    //        defrag when we really need it.                        //
                //////////////////////////////////////////////////////////////////////////
                    //                                                              //
                    //                         -------                              //
                    //                            -                                 //

            const float minWeightToDoSomething = 20.f * 1024.f;   // only do something when there's a 20k difference between total available space and the largest block
            float bestWeight = minWeightToDoSomething;
            HeapedResource* bestHeap = NULL;
            {
                ScopedReadLock(_lock);
                for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
                    float weight = (*i)->CalculateFragmentationWeight();
                    if (weight > bestWeight && (*i)->_heapResource) {
                            //      if the heap hasn't changed since the last time this heap was used as a defrag source, then there's no use in picking it again
                        if ((*i)->_hashLastDefrag != (*i)->_heap.CalculateHash()) {
                            bestHeap = i->get();
                            bestWeight = weight;
                            break;
                        }
                    }
                }
            }

                                //      -=-=-=-=-=-=-=-=-=-=-       //

            if (bestHeap) {
                {
                    ScopedLock(_activeDefrag_Lock);  // must lock during the defrag commit & defrag create
                    _activeDefrag = std::make_unique<ActiveDefrag>();
                    _activeDefragHeap = bestHeap;
                }

                    // Now that we've set bestHeap->_activeDefrag, bestHeap->_heap is immutable...
                _activeDefrag->SetSteps(bestHeap->_heap, bestHeap->_heap.CalculateDefragSteps());
                bestHeap->_hashLastDefrag = bestHeap->_heap.CalculateHash();

                    // Copy the resource into our copy buffer, and set the count down
                if (PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground) {
                    context.GetResourceUploadHelper().ResourceCopy(
                        *_temporaryCopyBuffer, *bestHeap->_heapResource);
                    _temporaryCopyBufferCountDown = 10;
                }

                #if defined(_DEBUG)
                    unsigned blockCount = bestHeap->_refCounts.GetEntryCount();
                    for (unsigned b=0; b<blockCount; ++b) {
                        std::pair<unsigned,unsigned> block = bestHeap->_refCounts.GetEntry(b);
                        bool foundOne = false;
                        for (std::vector<DefragStep>::const_iterator i =_activeDefrag->GetSteps().begin(); i!=_activeDefrag->GetSteps().end(); ++i) {
                            if (block.first >= i->_sourceStart && block.second <= i->_sourceEnd) {
                                foundOne = true;
                                break;
                            }
                        }
                        assert(foundOne);
                    }
                #endif
            }

        } else {

                //
                //      Check on the status of the defrag step; and commit to the 
                //      active resource as necessary
                //
            ActiveDefrag* existingActiveDefrag = _activeDefrag.get();
            if (!existingActiveDefrag->GetHeap()->_heapResource) {

                    //////      Try to find a heap that is 100% free. We'll remove        //
                      //          this from the list, and use it as our new heap        //////

                {
                    ScopedModifyLock(_lock);
                    for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
                        if (i->get() != _activeDefragHeap && (*i)->_heap.IsEmpty()) {
                            existingActiveDefrag->GetHeap()->_heapResource = std::move((*i)->_heapResource);
                            _heaps.erase(i);
                            break;
                        }
                    }
                }

                if (!existingActiveDefrag->GetHeap()->_heapResource) {
                    existingActiveDefrag->GetHeap()->_heapResource = _device->CreateResource(_prototype);
                }

            } else {
                
                if (_temporaryCopyBufferCountDown > 0) {
                    --_temporaryCopyBufferCountDown;
                } else {
                    const bool useTemporaryCopyBuffer = PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground;
                    existingActiveDefrag->Tick(context, useTemporaryCopyBuffer?_temporaryCopyBuffer:_activeDefragHeap->_heapResource);
                    if (existingActiveDefrag->IsComplete(processedEventList, context)) {

                            //
                            //      Everything should be de-reffed from the original heap.
                            //          Sometimes there appears to be leaks here... We could just leave the old
                            //          heap as part of our list of heaps. It would then just be filled up
                            //          again with future allocations.
                            //
                        // assert(_activeDefragHeap->_refCounts.CalculatedReferencedSpace()==0);        it's ok now

                            //
                            //      Signal client which blocks that have moved; and change the 
                            //      _heapResource value of the real heap
                            //
                        _activeDefrag->ReleaseSteps();
                        _activeDefrag->ApplyPendingOperations(*_activeDefragHeap);
                        _activeDefrag->GetHeap()->_defragCount = _activeDefragHeap->_defragCount+1;
                        // assert(_activeDefragHeap->_heap.IsEmpty());      // it's ok now

                        ScopedModifyLock(_lock); // lock here to prevent any operations on _activeDefrag->GetHeap() while we do this...
                        _heaps.push_back(_activeDefrag->ReleaseHeap());

                        {
                            ScopedLock(_activeDefrag_Lock);
                            _activeDefrag.reset(NULL);
                            _activeDefragHeap = NULL;
                        }
                    }
                }
            }
        }
    }

    void               BatchedResources::OnLostDevice()
    {
        ScopedModifyLock(_lock);
        {
            ScopedLock(_activeDefrag_Lock);
            _activeDefrag.reset();
            _activeDefragHeap = nullptr;
        }

        _temporaryCopyBuffer = {};
        _temporaryCopyBufferCountDown = 0;

        _heaps.clear();
    }

    BatchedResources::BatchedResources(RenderCore::IDevice& device, const ResourceDesc& prototype)
    :       _prototype(prototype)
    ,       _device(&device)
    ,       _activeDefrag(nullptr)
    ,       _activeDefragHeap(nullptr)
    {
        ResourceDesc copyBufferDesc = prototype;
        copyBufferDesc._cpuAccess = CPUAccess::Read;
        copyBufferDesc._gpuAccess = 0;
        copyBufferDesc._allocationRules = AllocationRules::Staging;
        copyBufferDesc._bindFlags = 0;

        _temporaryCopyBuffer = {};
        _temporaryCopyBufferCountDown = 0;
        if (PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground) {
            _temporaryCopyBuffer = _device->CreateResource(copyBufferDesc);
        }

        _recentDeviceCreateCount = 0;
        _totalCreateCount = 0;
    }

    BatchedResources::~BatchedResources()
    {}

    unsigned    BatchedResources::HeapedResource::Allocate(unsigned size, const char name[])
    {
        // note -- we start out with no ref count registered in _refCounts for this range. The first ref count will come when we create a ResourceLocator
        return _heap.Allocate(size);
    }

    bool        BatchedResources::HeapedResource::AddRef(unsigned ptr, unsigned size, const char name[])
    {
        std::pair<signed,signed> newRefCounts = _refCounts.AddRef(ptr, size, name);
        assert(newRefCounts.first >= 0 && newRefCounts.second >= 0);
        assert(newRefCounts.first == newRefCounts.second);
        return newRefCounts.second==1;
    }
    
    bool        BatchedResources::HeapedResource::Deref(unsigned ptr, unsigned size)
    {
        std::pair<signed,signed> newRefCounts = _refCounts.Release(ptr, size);
        assert(newRefCounts.first >= 0 && newRefCounts.second >= 0);
        assert(newRefCounts.first == newRefCounts.second);
        return newRefCounts.second==0;
    }

    void    BatchedResources::HeapedResource::Allocate(unsigned ptr, unsigned size)
    {
        _heap.Allocate(ptr, size);
    }

    void        BatchedResources::HeapedResource::Deallocate(unsigned ptr, unsigned size)
    {
        _heap.Deallocate(ptr, size);
    }

    BatchedHeapMetrics BatchedResources::HeapedResource::CalculateMetrics() const
    {
        BatchedHeapMetrics result;
        result._markers          = _heap.CalculateMetrics();
        result._allocatedSpace   = result._unallocatedSpace = 0;
        result._heapSize         = _size;
        result._largestFreeBlock = result._spaceInReferencedCountedBlocks = result._referencedCountedBlockCount = 0;

        if (!result._markers.empty()) {
            unsigned previousStart = 0;
            for (auto i=result._markers.begin(); i<(result._markers.end()-1); i+=2) {
                unsigned start = *i, end = *(i+1);
                result._allocatedSpace   += start-previousStart;
                result._unallocatedSpace += end-start;
                result._largestFreeBlock  = std::max(result._largestFreeBlock, size_t(end-start));
                previousStart = end;
            }
        }

        result._spaceInReferencedCountedBlocks   = _refCounts.CalculatedReferencedSpace();
        result._referencedCountedBlockCount      = _refCounts.GetEntryCount();
        return result;
    }

    float BatchedResources::HeapedResource::CalculateFragmentationWeight() const
    {
        unsigned largestBlock    = _heap.CalculateLargestFreeBlock();
        unsigned availableSpace  = _heap.CalculateAvailableSpace();
        if (largestBlock > .5f * availableSpace) {
            return 0.f;
        }
        return float(availableSpace - largestBlock);
    }

    void BatchedResources::HeapedResource::ValidateRefsAndHeap()
    {
            //
            //      Check to make sure that the reference counting layer and the heap agree.
            //      There might be some discrepancies during defragging because of the delayed
            //      Deallocate. But otherwise they should match up.
            //
        #if defined(_DEBUG)
            unsigned referencedSpace = _refCounts.CalculatedReferencedSpace();
            unsigned heapAllocatedSpace = _heap.CalculateAllocatedSpace();
            assert(heapAllocatedSpace == referencedSpace);
        #endif
    }

    BatchedResources::HeapedResource::HeapedResource()
    : _size(0), _defragCount(0), _heap(0), _refCounts(0), _hashLastDefrag(0)
    {}

    BatchedResources::HeapedResource::HeapedResource(const ResourceDesc& desc, const std::shared_ptr<IResource>& heapResource)
    : _heapResource(heapResource)
    , _heap(RenderCore::ByteCount(desc))
    , _refCounts(RenderCore::ByteCount(desc))
    , _size(RenderCore::ByteCount(desc))
    , _defragCount(0)
    , _hashLastDefrag(0)
    {}

    BatchedResources::HeapedResource::~HeapedResource()
    {
        #if defined(_DEBUG)
            ValidateRefsAndHeap();
            if (_refCounts.GetEntryCount()) {
                assert(0);  // something leaked!
            }
        #endif
    }

    void BatchedResources::ActiveDefrag::QueueOperation(Operation::Enum operation, unsigned start, unsigned end)
    {
        assert(end>start);
        PendingOperation op;
        op._operation = operation;
        op._start = start;
        op._end = end;
        _pendingOperations.push_back(op);
    }

    void BatchedResources::ActiveDefrag::Tick(ThreadContext& context, const std::shared_ptr<IResource>& sourceResource)
    {
        if (!_initialCommandListID) {
            _initialCommandListID = context.CommandList_GetUnderConstruction();
        }
        if (!_steps.empty() && GetHeap()->_heapResource && !_doneResourceCopy) {
                // -----<   Copy from the old resource into the new resource   >----- //
            if (PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground) {
                context.GetCommitStepUnderConstruction().Add(
                    CommitStep::DeferredDefragCopy(GetHeap()->_heapResource, sourceResource, _steps));
            } else {
                context.GetResourceUploadHelper().ResourceCopy_DefragSteps(GetHeap()->_heapResource, sourceResource, _steps);
            }
            _doneResourceCopy = true;
        }

        if (_doneResourceCopy && !_eventId && context.CommandList_GetCommittedToImmediate() >= _initialCommandListID) {
            Event_ResourceReposition result;
            result._originalResource = sourceResource;
            result._newResource      = GetHeap()->_heapResource;
            result._defragSteps      = _steps;
            _eventId = context.EventList_Push(result);
        }
    }

    void BatchedResources::ActiveDefrag::SetSteps(const SimpleSpanningHeap& sourceHeap, const std::vector<DefragStep>& steps)
    {
        assert(_steps.empty());      // can't change the steps once they're specified!
        _steps = steps;
        _newHeap->_size = sourceHeap.CalculateHeapSize();
        _newHeap->_heap = SimpleSpanningHeap(_newHeap->_size);

        #if defined(_DEBUG)
            for (std::vector<DefragStep>::const_iterator i=_steps.begin(); i!=_steps.end(); ++i) {
                unsigned end = i->_destination + i->_sourceEnd - i->_sourceStart;
                assert(end<=_newHeap->_size);
            }
        #endif
    }

    void BatchedResources::ActiveDefrag::ReleaseSteps()
    {
        _steps.clear();
    }

    void BatchedResources::ActiveDefrag::ApplyPendingOperations(HeapedResource& destination)
    {
        if (_pendingOperations.empty()) {
            return;
        }

        for (std::vector<ActiveDefrag::PendingOperation>::iterator deallocateIterator = _pendingOperations.begin(); deallocateIterator != _pendingOperations.end(); ++deallocateIterator) {
            destination.Deallocate(deallocateIterator->_start, deallocateIterator->_end-deallocateIterator->_start);
        }

        assert(0);
        #if 0
            std::sort(_pendingOperations.begin(), _pendingOperations.end(), SortByPosition);
            std::vector<ActiveDefrag::PendingOperation>::iterator deallocateIterator = _pendingOperations.begin();
            for (std::vector<DefragStep>::const_iterator s=_steps.begin(); s!=_steps.end() && deallocateIterator!=_pendingOperations.end();) {
                if (s->_sourceEnd <= deallocateIterator->_start) {
                    ++s;
                    continue;
                }

                if (s->_sourceStart >= (deallocateIterator->_end)) {
                        //      This deallocate iterator doesn't have an adjustment
                    ++deallocateIterator;
                    continue;
                }

                    //
                    //      We shouldn't have any blocks that are stretched between multiple 
                    //      steps. If we've got a match it must match the entire deallocation block
                    //
                assert(deallocateIterator->_start >= s->_sourceStart && deallocateIterator->_start < s->_sourceEnd);
                assert((deallocateIterator->_end) > s->_sourceStart && (deallocateIterator->_end) <= s->_sourceEnd);

                signed offset = s->_destination - signed(s->_sourceStart);
                deallocateIterator->_start += offset;
                ++deallocateIterator;
            }

                //
                //      Now just deallocate those blocks... But note we've just done a defrag pass, so this
                //      will just create new gaps!
                //
            for (deallocateIterator = _pendingOperations.begin(); deallocateIterator != _pendingOperations.end(); ++deallocateIterator) {
                GetHeap()->Deallocate(deallocateIterator->_start, deallocateIterator->_end-deallocateIterator->_start);
            }
        #endif
    }

    bool BatchedResources::ActiveDefrag::IsComplete(IManager::EventListID processedEventList, ThreadContext& context)
    {
        return  GetHeap()->_heapResource && _doneResourceCopy && (processedEventList >= _eventId);
    }

    auto BatchedResources::ActiveDefrag::ReleaseHeap() -> std::unique_ptr<HeapedResource>&&
    {
        return std::move(_newHeap);
    }

    BatchedResources::ActiveDefrag::ActiveDefrag()
    : _doneResourceCopy(false), _eventId(0)
    , _newHeap(std::make_unique<HeapedResource>())
    , _initialCommandListID(0)
    {
    }

    BatchedResources::ActiveDefrag::~ActiveDefrag()
    {
        ReleaseSteps();
    }

    bool BatchedResources::ActiveDefrag::SortByPosition(const PendingOperation& lhs, const PendingOperation& rhs) { return lhs._start < rhs._start; }

    // static string Description(const ResourceDesc& desc)
    // {
    //     char buffer[2048];
    //     if (desc._type == ResourceDesc::Type::Texture) {
    //         _snprintf_s(buffer, _TRUNCATE, "Tex (%ix%i) [%s]", desc._textureDesc._width, desc._textureDesc._height, desc._name);
    //     } else if (desc._type == ResourceDesc::Type::LinearBuffer) {
    //         _snprintf_s(buffer, _TRUNCATE, "Buffer (%.3fKB)", desc._linearBufferDesc._sizeInBytes/1024.f);
    //     }
    //     return string(buffer);
    // }

        /////   R E S O U R C E   S O U R C E   /////

    static bool UsePooling(const ResourceDesc& input)     { return (input._type == ResourceDesc::Type::LinearBuffer) && (input._linearBufferDesc._sizeInBytes < (32*1024)) && (input._allocationRules & AllocationRules::Pooled); }
    static bool UseBatching(const ResourceDesc& input)    { return (input._type == ResourceDesc::Type::LinearBuffer) && !!(input._allocationRules & AllocationRules::Batched) && (input._bindFlags == BindFlag::IndexBuffer); }

    static ResourceDesc AdjustDescForReusableResource(const ResourceDesc& input)
    {
        ResourceDesc result = input;
        if (result._type == ResourceDesc::Type::LinearBuffer) {
            result._linearBufferDesc._sizeInBytes = RoundUpBufferSize(result._linearBufferDesc._sizeInBytes);
            result._cpuAccess = CPUAccess::Write;
        }
        return result;
    }

    ResourceSource::ResourceConstruction        ResourceSource::Create(const ResourceDesc& desc, IDataPacket* initialisationData, CreationOptions::BitField options)
    {
        bool allowDeviceCreation     = (options & CreationOptions::PreventDeviceCreation) == 0;
        const bool usePooling        = UsePooling(desc);
        const bool useBatching       = UseBatching(desc) && initialisationData;
        const bool useStaging        = !!(desc._allocationRules & AllocationRules::Staging);
        const unsigned objectSize    = RenderCore::ByteCount(desc);

        ResourceConstruction result;
        if (useStaging) {
            result._locator = _stagingBufferPool->CreateResource(AdjustDescForReusableResource(desc), objectSize, allowDeviceCreation);
        } else if (usePooling) {
            result._locator = _pooledGeometryBuffers->CreateResource(AdjustDescForReusableResource(desc), objectSize, allowDeviceCreation);
            result._flags |= allowDeviceCreation?ResourceConstruction::Flags::DeviceConstructionInvoked:0;
        } else if (allowDeviceCreation) {
            auto supportInit = 
                (desc._type == ResourceDesc::Type::Texture)
                ? PlatformInterface::SupportsResourceInitialisation_Texture
                : PlatformInterface::SupportsResourceInitialisation_Buffer;
            auto initPkt = supportInit ? initialisationData : nullptr;
            std::shared_ptr<RenderCore::IResource> renderCoreResource;
            if (initPkt) {
                renderCoreResource = _underlyingDevice->CreateResource(desc, PlatformInterface::AsResourceInitializer(*initPkt));
            } else {
                // If we want to initialize this object, but can't do so via the resource initialization method,
                // the caller will need to transfer that data via BlitEncoder. Let's jsut make sure the binding flag
                // is there to support that
                auto adjustedDesc = desc;
                if (initialisationData)
                    adjustedDesc._bindFlags |= BindFlag::TransferDst;
                renderCoreResource = _underlyingDevice->CreateResource(adjustedDesc);
            }
            result._locator = ResourceLocator{std::move(renderCoreResource)};
            result._flags |= initPkt ? ResourceConstruction::Flags::InitialisationSuccessful : 0;
            result._flags |= ResourceConstruction::Flags::DeviceConstructionInvoked;
        }
        return result;
    }

    BatchedResources::ResultFlags::BitField ResourceSource::IsBatchedResource(const ResourceLocator& locator, const ResourceDesc& desc)
    {
        const bool mightBeBatched = UsePooling(desc) && UseBatching(desc);
        if (mightBeBatched) {
            return _batchedIndexBuffers->IsBatchedResource(locator.GetContainingResource().get());
        }
        return 0;
    }

    bool ResourceSource::CanBeBatched(const ResourceDesc& desc)
    {
        return UseBatching(desc); 
    }

    void ResourceSource::Validate(const ResourceLocator& locator)
    {
        #if defined(_DEBUG)
            if (_batchedIndexBuffers->Validate(locator)==0) {
                ResourceDesc desc = locator.GetContainingResource()->GetDesc();
                assert(!(UsePooling(desc) && UseBatching(desc)));
            }
        #endif
    }

    void ResourceSource::Tick(ThreadContext& threadContext, IManager::EventListID processedEventList)
    {
        _stagingBufferPool->Update(threadContext.CommandList_GetUnderConstruction());

            // ------ Defrag ------ //

        if (_batchedIndexBuffers.get()) {
            _batchedIndexBuffers->TickDefrag(threadContext, processedEventList);
        }
    }

    PoolSystemMetrics   ResourceSource::CalculatePoolMetrics() const
    {
        PoolSystemMetrics result;
        result._resourcePools = _pooledGeometryBuffers->CalculateMetrics();
        result._stagingPools = _stagingBufferPool->CalculateMetrics();
        result._batchingSystemMetrics = _batchedIndexBuffers->CalculateMetrics();
        return result;
    }

    void ResourceSource::OnLostDevice()
    {
        _batchedIndexBuffers->OnLostDevice();       // (prefer calling OnLostDevice() on the batched index buffers first, because of links back to pooledGeometryBuffers)
        _pooledGeometryBuffers->OnLostDevice();
        _stagingBufferPool->OnLostDevice();
    }

    ResourceSource::ResourceSource(RenderCore::IDevice& device)
    :   _underlyingDevice(&device)
    {
        _frameID = 0;

        _stagingBufferPool = std::make_shared<ResourcesPool<ResourceDesc>>(device, 5*60);
        _pooledGeometryBuffers = std::make_shared<ResourcesPool<ResourceDesc>>(device);

        ResourceDesc batchableIndexBuffers;
        batchableIndexBuffers._type = ResourceDesc::Type::LinearBuffer;
        batchableIndexBuffers._cpuAccess = CPUAccess::Write;
        batchableIndexBuffers._gpuAccess = GPUAccess::Read;
        batchableIndexBuffers._bindFlags = BindFlag::IndexBuffer;
        if (PlatformInterface::UseMapBasedDefrag) {
            batchableIndexBuffers._cpuAccess = CPUAccess::Write|CPUAccess::Read;
        }

        batchableIndexBuffers._linearBufferDesc._sizeInBytes = 256 * 1024;
        XlCopyNString(batchableIndexBuffers._name, "BatchedBuffer", 13);
        batchableIndexBuffers._name[13] = '\0';

        _batchedIndexBuffers = std::make_shared<BatchedResources>(device, batchableIndexBuffers);
    }

    ResourceSource::~ResourceSource()
    {
    }


    std::shared_ptr<IResource> ResourceLocator::AsIndependentResource() const
    {
        return (!_managedByPool && IsWholeResource()) ? _resource : nullptr;
    }

    RenderCore::VertexBufferView ResourceLocator::CreateVertexBufferView() const
    {
        return RenderCore::VertexBufferView {
            _resource,
            (_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u
        };
    }

    RenderCore::IndexBufferView ResourceLocator::CreateIndexBufferView(RenderCore::Format indexFormat) const
    {
        return RenderCore::IndexBufferView { _resource, indexFormat, (_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u };
    }

    RenderCore::ConstantBufferView ResourceLocator::CreateConstantBufferView() const
    {
        if (_interiorOffset != ~size_t(0)) {
            return RenderCore::ConstantBufferView { _resource, unsigned(_interiorOffset), unsigned(_interiorOffset + _interiorSize) };
        } else {
            return RenderCore::ConstantBufferView { _resource };
        }
    }

    std::shared_ptr<RenderCore::IResourceView> ResourceLocator::CreateTextureView(BindFlag::Enum usage, const RenderCore::TextureViewDesc& window)
    {
        if (!IsWholeResource() || _managedByPool)
            Throw(std::runtime_error("Cannot create a texture view from a partial resource locator"));
        return _resource->CreateTextureView(usage, window);
    }

    std::shared_ptr<RenderCore::IResourceView> ResourceLocator::CreateBufferView(BindFlag::Enum usage, unsigned rangeOffset, unsigned rangeSize)
    {
        return _resource->CreateBufferView(usage, rangeOffset + ((_interiorOffset != ~size_t(0)) ? unsigned(_interiorOffset) : 0u), rangeSize);
    }

    bool ResourceLocator::IsWholeResource() const
    {
        return _interiorOffset == ~size_t(0) && _interiorSize == ~size_t(0);
    }

    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> independentResource)
    : _resource(std::move(independentResource))
    {
        _interiorOffset = _interiorSize = ~size_t(0);
        _poolMarker = ~0ull;
        _managedByPool = false;
        _completionCommandList = CommandListID_Invalid;
    }
    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> containingResource,
        size_t interiorOffset, size_t interiorSize,
        std::weak_ptr<IResourcePool> pool, uint64_t poolMarker,
        bool initialReferenceAlreadyTaken,
        CommandListID completionCommandList)
    : _resource(std::move(containingResource))
    , _pool(std::move(pool))
    {
        _interiorOffset = interiorOffset;
        _interiorSize = interiorSize;
        _poolMarker = poolMarker;
        _managedByPool = true;
        _completionCommandList = completionCommandList;

        if (!initialReferenceAlreadyTaken) {
            auto strongPool = _pool.lock();
            if (strongPool && _resource)
                strongPool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
    }

    ResourceLocator::ResourceLocator(
        std::shared_ptr<IResource> containingResource,
        size_t interiorOffset, size_t interiorSize,
        CommandListID completionCommandList)
    : _resource(std::move(containingResource))
    {
        _interiorOffset = interiorOffset;
        _interiorSize = interiorSize;
        _poolMarker = ~0ull;
        _managedByPool = false;
        _completionCommandList = completionCommandList;
    }

    ResourceLocator::ResourceLocator() {}
    ResourceLocator::~ResourceLocator() 
    {
        auto pool = _pool.lock();
        if (pool)
            pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
    }

    ResourceLocator::ResourceLocator(
        ResourceLocator&& moveFrom,
        CommandListID completionCommandList)
    : _resource(std::move(moveFrom._resource))
    , _interiorOffset(moveFrom._interiorOffset)
    , _interiorSize(moveFrom._interiorSize)
    , _pool(std::move(moveFrom._pool))
    , _poolMarker(moveFrom._poolMarker)
    , _managedByPool(moveFrom._managedByPool)
    , _completionCommandList(completionCommandList)
    {
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
    }

    ResourceLocator::ResourceLocator(ResourceLocator&& moveFrom) never_throws
    : _resource(std::move(moveFrom._resource))
    , _interiorOffset(moveFrom._interiorOffset)
    , _interiorSize(moveFrom._interiorSize)
    , _pool(std::move(moveFrom._pool))
    , _poolMarker(moveFrom._poolMarker)
    , _managedByPool(moveFrom._managedByPool)
    , _completionCommandList(moveFrom._completionCommandList)
    {
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
    }

    ResourceLocator& ResourceLocator::operator=(ResourceLocator&& moveFrom) never_throws
    {
        if (&moveFrom == this) return *this;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
        }

        _resource = std::move(moveFrom._resource);
        _interiorOffset = moveFrom._interiorOffset;
        _interiorSize = moveFrom._interiorSize;
        _pool = std::move(moveFrom._pool);
        _poolMarker = moveFrom._poolMarker;
        _managedByPool = moveFrom._managedByPool;
        _completionCommandList = moveFrom._completionCommandList;
        moveFrom._interiorOffset = moveFrom._interiorSize = ~size_t(0);
        moveFrom._poolMarker = ~0ull;
        moveFrom._managedByPool = false;
        moveFrom._completionCommandList = CommandListID_Invalid;
        return *this;
    }

    ResourceLocator::ResourceLocator(const ResourceLocator& copyFrom)
    : _resource(copyFrom._resource)
    , _interiorOffset(copyFrom._interiorOffset)
    , _interiorSize(copyFrom._interiorSize)
    , _pool(copyFrom._pool)
    , _poolMarker(copyFrom._poolMarker)
    , _managedByPool(copyFrom._managedByPool)
    , _completionCommandList(copyFrom._completionCommandList)
    {
        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
    }

    ResourceLocator& ResourceLocator::operator=(const ResourceLocator& copyFrom)
    {
        if (&copyFrom == this) return *this;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->Release(_poolMarker, std::move(_resource), _interiorOffset, _interiorSize);
        }

        _resource = copyFrom._resource;
        _interiorOffset = copyFrom._interiorOffset;
        _interiorSize = copyFrom._interiorSize;
        _pool = copyFrom._pool;
        _poolMarker = copyFrom._poolMarker;
        _managedByPool = copyFrom._managedByPool;
        _completionCommandList = copyFrom._completionCommandList;

        if (_managedByPool) {
            auto pool = _pool.lock();
            if (pool && _resource)
                pool->AddRef(_poolMarker, *_resource, _interiorOffset, _interiorSize);
        }
        return *this;
    }

    ResourceLocator ResourceLocator::MakeSubLocator(size_t offset, size_t size)
    {
        if (_managedByPool) {
            if (IsWholeResource()) {
                return ResourceLocator {
                    _resource,
                    offset, size,
                    _pool, _poolMarker,
                    false, _completionCommandList };
            } else {
                return ResourceLocator {
                    _resource,
                    _interiorOffset + offset, size,
                    _pool, _poolMarker,
                    false, _completionCommandList };
            }
        } else {
            if (IsWholeResource()) {
                return ResourceLocator {
                    _resource,
                    offset, size,
                    _completionCommandList };
            } else {
                return ResourceLocator {
                    _resource,
                    _interiorOffset + offset, size,
                    _completionCommandList };
            }
        }
    }

}
