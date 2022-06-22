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

	BatchedResources::BatchedResources(RenderCore::IDevice& device, const ResourceDesc& prototype)
	:       _prototype(prototype)
	,       _device(&device)
	,       _activeDefrag(nullptr)
	,       _activeDefragHeap(nullptr)
	{
		ResourceDesc copyBufferDesc = prototype;
		copyBufferDesc._allocationRules = RenderCore::AllocationRules::HostVisibleRandomAccess;
		copyBufferDesc._bindFlags = BindFlag::TransferDst;

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

}
