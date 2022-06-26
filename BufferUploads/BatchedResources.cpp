// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BatchedResources.h"
#include "Metrics.h"
#include "ResourceUploadHelper.h"
#include "ThreadContext.h"

namespace BufferUploads
{

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
				HeapedResource* bestHeap = NULL;
				unsigned bestHeapLargestBlock = ~unsigned(0x0);
				for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
					if ((*i)->_lockedForDefrag) continue;
					unsigned largestBlock = (*i)->_heap.CalculateLargestFreeBlock();
					if (largestBlock >= size && largestBlock < bestHeapLargestBlock) {
						bestHeap = i->get();
						bestHeapLargestBlock = largestBlock;
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
		for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
			if ((*i)->_heapResource.get() == &resource) {
				heap = i->get();
				break;
			}
		}

		assert(heap);
		if (heap) heap->AddRef(offset, size, "<<unknown>>");
	}

	void BatchedResources::Release(
		uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
		size_t offset, size_t size)
	{
		ScopedReadLock(_lock);
		HeapedResource* heap = NULL;
		for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i) {
			if ((*i)->_heapResource == resource) {
				heap = i->get();
				break;
			}
		}

		assert(heap);
		if (heap && heap->Deref(offset, size)) {
			heap->Deallocate(offset, size);
			#if defined(_DEBUG)
				heap->ValidateRefsAndHeap();
			#endif
		}

			// (prevent caller from performing extra derefs)
		resource = nullptr;
	}

	BatchedResources::ResultFlags::BitField BatchedResources::IsBatchedResource(
		IResource* resource) const
	{
		ScopedReadLock(_lock);
		for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i)
			if ((*i)->_heapResource.get() == resource)
				return ResultFlags::IsBatched|((*i)->_lockedForDefrag?ResultFlags::IsCurrentlyDefragging:0);
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
		for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i)
			if ((*i)->_heapResource.get() == locator.GetContainingResource().get()) {
				heapResource = i->get();
				break;
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

	void BatchedResources::Tick()
	{
		if (!_activeDefrag.get()) {

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
			HeapedResource* bestHeapForCompression = nullptr;
			{
				ScopedReadLock(_lock);
				for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
					float weight = (*i)->CalculateFragmentationWeight();
					if (weight > bestWeight && (*i)->_heapResource) {
							//      if the heap hasn't changed since the last time this heap was used as a defrag source, then there's no use in picking it again
						if ((*i)->_hashLastDefrag != (*i)->_heap.CalculateHash()) {
							bestHeapForCompression = i->get();
							bestWeight = weight;
							break;
						}
					}
				}
			}

								//      -=-=-=-=-=-=-=-=-=-=-       //

			if (bestHeapForCompression) {
				auto compression = bestHeapForCompression->_heap.CalculateHeapCompression();
				auto newDefrag = std::make_unique<ActiveReposition>(
					*this, *_bufferUploads.lock(), *bestHeapForCompression, std::move(compression));

				{
					ScopedLock(_lock);  // must lock during the defrag commit & defrag create
					assert(!_activeDefrag);
					_activeDefrag = std::move(newDefrag);
					bestHeapForCompression->_lockedForDefrag = true;
				}

				// Now that we've set bestHeap->_lockedForDefrag, bestHeap->_heap is immutable...
				bestHeapForCompression->_hashLastDefrag = bestHeapForCompression->_heap.CalculateHash();

				#if defined(_DEBUG)
					// Validate that everything recorded in the _refCounts is part of the repositioning
					unsigned blockCount = bestHeapForCompression->_refCounts.GetEntryCount();
					for (unsigned b=0; b<blockCount; ++b) {
						auto block = bestHeapForCompression->_refCounts.GetEntry(b);
						bool foundOne = false;
						for (auto i =_activeDefrag->GetSteps().begin(); i!=_activeDefrag->GetSteps().end(); ++i)
							if (block.first >= i->_sourceStart && block.second <= i->_sourceEnd) {
								foundOne = true;
								break;
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
			ActiveReposition* existingActiveDefrag = _activeDefrag.get();
			existingActiveDefrag->Tick(_eventListManager, *_bufferUploads.lock());
			if (existingActiveDefrag->IsComplete(_eventListManager._currentEventListProcessedId.load())) {
				assert(_activeDefrag->GetSourceHeap()->_lockedForDefrag);
				_activeDefrag->GetSourceHeap()->_lockedForDefrag = false;
				_activeDefrag->Clear();
				ScopedModifyLock(_lock); // lock here to prevent any operations on _activeDefrag->GetHeap() while we do this...
				_activeDefrag = nullptr;
			}
		}
	}

    EventListID   BatchedResources::EventList_GetWrittenID() const
    {
        return _eventListManager._currentEventListId;
    }

    EventListID   BatchedResources::EventList_GetPublishedID() const
    {
        return _eventListManager._currentEventListPublishedId;
    }

    EventListID   BatchedResources::EventList_GetProcessedID() const
    {
        return _eventListManager._currentEventListProcessedId;
    }

    void                    BatchedResources::EventList_Get(EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end)
    {
        begin = end = nullptr;
        if (!id) return;
        for (unsigned c=0; c<dimof(_eventListManager._eventBuffers); ++c) {
            if (_eventListManager._eventBuffers[c]._id == id) {
                ++_eventListManager._eventBuffers[c]._clientReferences;
                    //  have to check again after the increment... because the client references value acts
                    //  as a lock.
                if (_eventListManager._eventBuffers[c]._id == id) {
                    begin = &_eventListManager._eventBuffers[c]._evnt;
                    end = begin+1;
                } else {
                    --_eventListManager._eventBuffers[c]._clientReferences;
                        // in this case, the event has just be freshly overwritten
                }
                return;
            }
        }
    }

    void                    BatchedResources::EventList_Release(EventListID id)
    {
        if (!id) return;
        for (unsigned c=0; c<dimof(_eventListManager._eventBuffers); ++c) {
            if (_eventListManager._eventBuffers[c]._id == id) {
                auto newValue = --_eventListManager._eventBuffers[c]._clientReferences;
                assert(signed(newValue) >= 0);
                    
				for (;;) {      // lock-free max...
					auto originalProcessedId = _eventListManager._currentEventListProcessedId.load();
					auto newProcessedId = std::max(originalProcessedId, (EventListID)_eventListManager._eventBuffers[c]._id);
					if (_eventListManager._currentEventListProcessedId.compare_exchange_strong(originalProcessedId, newProcessedId))
						break;
				}
                return;
            }
        }
    }

    EventListID   BatchedResources::EventListManager::EventList_Push(const Event_ResourceReposition& evnt)
    {
            //
            //      try to push this event into the small queue... but don't overwrite anything that
            //      currently has a client reference on it.
            //
        if (!_eventBuffers[_eventListWritingIndex]._clientReferences.load()) {
            EventListID id = ++_currentEventListId;
            _eventBuffers[_eventListWritingIndex]._id = id;
            _eventBuffers[_eventListWritingIndex]._evnt = evnt;
            _eventListWritingIndex = (_eventListWritingIndex+1)%dimof(_eventBuffers);   // single writing thread, so it's ok
            return id;
        }
        assert(0);
        return ~EventListID(0x0);
    }

    void BatchedResources::EventListManager::EventList_Publish(EventListID toEvent)
    {
        _currentEventListPublishedId = toEvent;
    }

	BatchedResources::BatchedResources(RenderCore::IDevice& device, std::shared_ptr<IManager>& bufferUploads, const ResourceDesc& prototype)
	: _prototype(prototype), _device(&device)
	, _bufferUploads(bufferUploads)
	{
		assert(_prototype._bindFlags & BindFlag::TransferDst);
		assert(_prototype._bindFlags & BindFlag::TransferSrc);

		// ResourceDesc copyBufferDesc = prototype;
		// copyBufferDesc._allocationRules = RenderCore::AllocationRules::HostVisibleRandomAccess;
		// copyBufferDesc._bindFlags = BindFlag::TransferDst;
		// _temporaryCopyBuffer = {};
		// _temporaryCopyBufferCountDown = 0;
		// if (PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground) {
		// 	_temporaryCopyBuffer = _device->CreateResource(copyBufferDesc);
		// }

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
		if (largestBlock > .5f * availableSpace)
			return 0.f;
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
	: _size(0), _heap(0), _refCounts(0), _hashLastDefrag(0)
	{}

	BatchedResources::HeapedResource::HeapedResource(const ResourceDesc& desc, const std::shared_ptr<IResource>& heapResource)
	: _heapResource(heapResource)
	, _heap(RenderCore::ByteCount(desc))
	, _refCounts(RenderCore::ByteCount(desc))
	, _size(RenderCore::ByteCount(desc))
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

	void BatchedResources::ActiveReposition::Tick(EventListManager& evntListMan, IManager& bufferUploads)
	{
		if (!_repositionCmdList.has_value() && _futureRepositionCmdList.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			_repositionCmdList = _futureRepositionCmdList.get();
		
		/*if (!_steps.empty() && GetHeap()->_heapResource && !_doneResourceCopy) {
				// -----<   Copy from the old resource into the new resource   >----- //
			if (PlatformInterface::UseMapBasedDefrag && !PlatformInterface::CanDoNooverwriteMapInBackground) {
				context.GetDeferredOperationsUnderConstruction().Add(
					ThreadContext::DeferredOperations::DeferredDefragCopy(GetHeap()->_heapResource, sourceResource, _steps));
			} else {
				context.GetResourceUploadHelper().ResourceCopy_DefragSteps(GetHeap()->_heapResource, sourceResource, _steps);
			}
			_doneResourceCopy = true;
		}*/

		if (_repositionCmdList && !_eventId.has_value() && bufferUploads.IsComplete(_repositionCmdList.value())) {
			// publish the changes to encourage the client to move across to the 
			Event_ResourceReposition result;
			result._originalResource = _srcHeap->_heapResource;
			result._newResource = _dstUberBlock.GetContainingResource();
			result._defragSteps = _steps;
			_eventId = evntListMan.EventList_Push(result);
			evntListMan.EventList_Publish(_eventId.value());
		}
	}

	bool BatchedResources::ActiveReposition::IsComplete(EventListID processedEventList)
	{
		return _eventId.has_value() && (processedEventList >= _eventId.value());
	}

	void BatchedResources::ActiveReposition::Clear()
	{
		_dstUberBlock = {};
		_srcHeap = nullptr;
		_steps.clear();
	}

	BatchedResources::ActiveReposition::ActiveReposition(
		BatchedResources& resourceSystem,
		IManager& bufferUploads,
		HeapedResource& srcHeap,
		std::vector<RepositionStep>&& steps)
	: _srcHeap(&srcHeap), _steps(std::move(steps))
	{
		size_t dstSizeRequired = 0;
		for (const auto& s:_steps) {
			assert(s._sourceEnd > s._sourceStart);
			dstSizeRequired = std::max(dstSizeRequired, size_t(s._destination + s._sourceEnd - s._sourceStart));
		}
		assert(dstSizeRequired);
		_dstUberBlock = resourceSystem.Allocate(dstSizeRequired, "reposition-uber-block");
		assert(!_dstUberBlock.IsEmpty());
		if (!_dstUberBlock.IsWholeResource())
			for (auto& s:_steps)
				s._destination += _dstUberBlock.GetRangeInContainingResource().first;

		_futureRepositionCmdList = bufferUploads.Transaction_Begin(
			srcHeap._heapResource,
			_dstUberBlock.GetContainingResource(),
			_steps);
		if (!_futureRepositionCmdList.valid())
			Throw(std::runtime_error("Failed while queuing reposition transaction"));
	}

	BatchedResources::ActiveReposition::~ActiveReposition() {}

}
