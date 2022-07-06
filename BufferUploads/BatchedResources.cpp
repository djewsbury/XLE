// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BatchedResources.h"
#include "Metrics.h"
#include "ResourceUploadHelper.h"
#include "../OSServices/Log.h"
#include "../Utility/HeapUtils.h"

namespace BufferUploads
{
	class BatchedResources : public IBatchedResources, public std::enable_shared_from_this<BatchedResources>
	{
	public:
		ResourceLocator Allocate(size_t size, StringSection<> name) override;
		RenderCore::ResourceDesc MakeFallbackDesc(size_t size, StringSection<> name) override;

		virtual bool AddRef(
			RenderCore::IResource& resource, 
			size_t offset, size_t size) override;

		virtual bool Release(
			RenderCore::IResource& resource, 
			size_t offset, size_t size) override;

		struct ResultFlags { enum Enum { IsBatched = 1<<0, ActiveReposition = 1<<1 }; using BitField=unsigned; };
		ResultFlags::BitField   IsBatchedResource(RenderCore::IResource* resource) const;
		ResultFlags::BitField   Validate(const ResourceLocator& locator) const;
		BatchingSystemMetrics   CalculateMetrics() const override;
		const ResourceDesc&     GetPrototype() const { return _prototype; }

		void                    TickDefrag() override;

		//////////// event lists //////////////
		IteratorRange<const Event_ResourceReposition*>	EventList_Get(EventListID id) override;
		void          EventList_Release(EventListID id) override;
		EventListID   EventList_GetPublishedID() const override;
		///////////////////////////////////////

		BatchedResources(
			RenderCore::IDevice&, const std::shared_ptr<IManager>&, 
			RenderCore::BindFlag::BitField bindFlags,
			unsigned pageSizeInBytes);
		~BatchedResources();
	private:
		class EventListManager;
		class HeapedResource;
		class ActiveReposition;

		std::vector<std::unique_ptr<HeapedResource>> _heaps;
		ResourceDesc _prototype;
		RenderCore::IDevice* _device;
		mutable Threading::ReadWriteMutex _lock;
		std::weak_ptr<IManager> _bufferUploads;
		RenderCore::BindFlag::BitField _fallbackBindFlags;

			//  Active defrag stuff...
		std::unique_ptr<ActiveReposition> _activeDefrag;

		mutable std::atomic<unsigned>   _recentDeviceCreateCount;
		std::atomic<size_t>             _totalCreateCount;

		mutable std::atomic<size_t>		_recentAllocateBytes;
		std::atomic<size_t>             _totalAllocateBytes;
		mutable std::atomic<size_t>		_recentRepositionBytes;
		std::atomic<size_t>             _totalRepositionBytes;

		struct EventListManager
		{
			struct EventList
			{
				volatile EventListID _id = ~EventListID(0x0);
				Event_ResourceReposition _evnt;
				std::atomic<unsigned> _clientReferences{0};
			};
			EventListID _currentEventListId = 0;
			EventListID _currentEventListPublishedId = 0;
			std::atomic<EventListID> _currentEventListProcessedId{0};
			EventList _eventBuffers[4];
			unsigned _eventListWritingIndex = 0;

			EventListID EventList_Publish(const Event_ResourceReposition& evnt);
		};
		EventListManager _eventListManager;

		#if defined(_DEBUG)
			std::optional<std::thread::id> _tickThread;
		#endif

		BatchedResources(const BatchedResources&);
		BatchedResources& operator=(const BatchedResources&);
	};

	class BatchedResources::HeapedResource
	{
	public:
		unsigned            Allocate(unsigned size, StringSection<> name = {});
		void                Allocate(unsigned ptr, unsigned size);
		void                Deallocate(unsigned ptr, unsigned size);

		bool                AddRef(unsigned ptr, unsigned size, StringSection<> name = {});
		
		BatchedHeapMetrics  CalculateMetrics() const;
		void                ValidateRefsAndHeap();

		HeapedResource();
		HeapedResource(const ResourceDesc& desc, const std::shared_ptr<RenderCore::IResource>& heapResource);
		~HeapedResource();

		std::shared_ptr<RenderCore::IResource> _heapResource;
		SpanningHeap<uint32_t>  _heap;
		ReferenceCountingLayer _refCounts;
		unsigned _size;
		unsigned _allocatedSpace;
		uint64_t _hashLastDefrag;
		std::atomic<bool> _lockedForDefrag = false;
	};

	class BatchedResources::ActiveReposition
	{
	public:
		void	Tick(EventListManager& evntListMan, IManager& bufferUploads);
		bool	IsComplete(EventListID processedEventList);
		void	Clear();

		HeapedResource*     GetSourceHeap() { return _srcHeap; }
		IteratorRange<const RepositionStep*> GetSteps() const { return _steps; }

		ActiveReposition(
			BatchedResources& resourceSystem,
			IManager& bufferUploads,
			HeapedResource& srcHeap,
			std::vector<RepositionStep>&& steps);
		~ActiveReposition();
	private:
		std::optional<EventListID>			_eventId;
		ResourceLocator						_dstUberBlock;
		HeapedResource*						_srcHeap;
		std::vector<RepositionStep>			_steps;
		std::future<CommandListID>			_futureRepositionCmdList;
		std::optional<CommandListID>		_repositionCmdList;
	};

	ResourceLocator    BatchedResources::Allocate(
		size_t size, StringSection<> name)
	{
		if (size >= _prototype._linearBufferDesc._sizeInBytes)		// support allocating a reposition uber-buffer
			return {};

		_recentAllocateBytes += size;
		_totalAllocateBytes += size;

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
						bestHeap->AddRef(allocation, size);
						return ResourceLocator{
							bestHeap->_heapResource, 
							allocation, size, 
							weak_from_this(),
							true, CommandListID_Invalid};
					}
				}
			}
		}

		auto heapResource = _device->CreateResource(_prototype);
		if (!heapResource)
			return {};

		++_recentDeviceCreateCount;
		++_totalCreateCount;

		auto newHeap = std::make_unique<HeapedResource>(_prototype, heapResource);
		unsigned allocation = newHeap->Allocate(size, name);
		assert(allocation != ~unsigned(0x0));
		newHeap->AddRef(allocation, size);

		{
			ScopedModifyLock(_lock);
			_heaps.push_back(std::move(newHeap));
		}

		return ResourceLocator{std::move(heapResource), allocation, size, weak_from_this(), true, CommandListID_Invalid};
	}
	
	bool BatchedResources::AddRef(
		RenderCore::IResource& resource, 
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

		if (!heap) return false;

		heap->AddRef(offset, size);
		return true;
	}

	bool BatchedResources::Release(
		RenderCore::IResource& resource, 
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

		if (!heap) return false;

		std::pair<signed,signed> newRefCounts = heap->_refCounts.Release(offset, size);
		assert(newRefCounts.first >= 0 && newRefCounts.second >= 0);
		if (newRefCounts.first == 0) {
			if (newRefCounts.second == 0) {
				// Simple case -- entire block dealloced
				heap->Deallocate(offset, size);
			} else {
				// Complex case -- some parts were left behind. We need to check what
				// parts of the block are still have references, and what were released
				// This should only happen when releasing the "uberblock" after a defrag
				// operation -- because that is an umbrella for many smaller blocks, and
				// some of those smaller blocks can be released before the defrag is fully
				// complete
				auto entryCount = heap->_refCounts.GetEntryCount();
				auto i = 0;
				while (i < entryCount) {
					auto e = heap->_refCounts.GetEntry(i);
					if ((e.first+e.second) > offset)
						break;
					++i;
				}
				unsigned start = offset, end = offset+size;
				while (i != entryCount && heap->_refCounts.GetEntry(i).first < end) {
					auto e = heap->_refCounts.GetEntry(i);
					unsigned allocatedPartsStart = e.first;
					if (allocatedPartsStart > start) heap->Deallocate(start, std::min(allocatedPartsStart, end)-start);
					start = e.first+e.second;		// This is the first point were we're possibly unallocated
					++i;
				}
				if (start < end) heap->Deallocate(start, end-start);	// last little bit
			}
		}
		// #if defined(_DEBUG)
		//	heap->ValidateRefsAndHeap();
		// #endif

		if (heap->_heap.IsEmpty() && !heap->_lockedForDefrag.load()) {
			// If we get down to completely empty, just remove and the page entirely. This can happen frequently
			// after heap compression
			for (auto i=_heaps.begin(); i!=_heaps.end(); ++i)
				if (i->get() == heap) {
					_heaps.erase(i);
					break;
				}
		}

		return true;
	}

	BatchedResources::ResultFlags::BitField BatchedResources::IsBatchedResource(
		RenderCore::IResource* resource) const
	{
		ScopedReadLock(_lock);
		for (auto i=_heaps.rbegin(); i!=_heaps.rend(); ++i)
			if ((*i)->_heapResource.get() == resource)
				return ResultFlags::IsBatched|((*i)->_lockedForDefrag?ResultFlags::ActiveReposition:0);
		return 0;
	}

	BatchedResources::ResultFlags::BitField BatchedResources::Validate(const ResourceLocator& locator) const
	{
		ScopedReadLock(_lock);

			//      check to make sure the same resource isn't showing up twice
		for (auto i=_heaps.begin(); i!=_heaps.end(); ++i)
			for (auto i2=i+1; i2!=_heaps.end(); ++i2)
				assert((*i2)->_heapResource != (*i)->_heapResource);

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
		for (auto i=_heaps.begin(); i!=_heaps.end(); ++i)
			result._heaps.push_back((*i)->CalculateMetrics());
		result._recentDeviceCreateCount = _recentDeviceCreateCount.exchange(0);
		result._totalDeviceCreateCount = _totalCreateCount.load();
		result._recentAllocateBytes = _recentAllocateBytes.exchange(0);
		result._totalAllocateBytes = _totalAllocateBytes.load();
		result._recentRepositionBytes = _recentRepositionBytes.exchange(0);
		result._totalRepositionBytes = _totalRepositionBytes.load();
		return result;
	}

	void BatchedResources::TickDefrag()
	{
		#if defined(_DEBUG)
			// BatchedResources::TickDefrag() is not reentrant, we're assuming it's always done on the same thread
			if (!_tickThread) _tickThread = std::this_thread::get_id();
			else assert(_tickThread == std::this_thread::get_id());
		#endif

		if (_activeDefrag.get()) {
				//
				//      Check on the status of the defrag step; and commit to the 
				//      active resource as necessary
				//
			ActiveReposition* existingActiveDefrag = _activeDefrag.get();
			existingActiveDefrag->Tick(_eventListManager, *_bufferUploads.lock());
			if (existingActiveDefrag->IsComplete(_eventListManager._currentEventListProcessedId.load())) {
				auto* sourceHeap = _activeDefrag->GetSourceHeap();
				_activeDefrag->Clear();

				ScopedReadLock(_lock);
				if (sourceHeap->_heap.IsEmpty()) {
					// we deferred destruction of this heap until now
					for (auto i=_heaps.begin(); i!=_heaps.end(); ++i)
						if (i->get() == sourceHeap) {
							_heaps.erase(i);
							break;
						}
				} else {
					#if defined(_DEBUG)
						auto newState = sourceHeap->_heap.CalculateHash();
						if (newState != sourceHeap->_hashLastDefrag)
							Log(Warning) << "In BatchedResources defrag, no blocks were released after processing a defrag operation. This is not immediately an issue, but it does mean fragmentation was not reduced" << std::endl;	// if you hit this, it means that nothing actually changed in the heap allocations. The blocks we were moving were not deallocated. 
					#endif
					// note -- don't update "sourceHeap->_hashLastDefrag" here. We update it at the point we decide to defrag the heap. So can be valid to choose for defrag immediately following the a previous one, so long as the heap is in a different state when we select it
					auto oldLocked = sourceHeap->_lockedForDefrag.exchange(false);
					assert(oldLocked);
				}
				_activeDefrag = nullptr;
			}
			return;
		}


		const unsigned minWeightToDoSomething = _prototype._linearBufferDesc._sizeInBytes / 4; // only do something when there's X byte difference between total available space and the largest block
		const unsigned largestBlockThreshold = _prototype._linearBufferDesc._sizeInBytes / 8;
		unsigned bestWeight = minWeightToDoSomething;
		unsigned largestBlockForHeapDrain = 0;
		HeapedResource* bestHeapForCompression = nullptr;
		std::vector<RepositionStep> compression;

		const unsigned heapDrainThreshold = _prototype._linearBufferDesc._sizeInBytes / 4;

		HeapedResource* bestIncrementalDefragHeap = nullptr;
		std::vector<RepositionStep> bestIncrementalDefragSteps;
		const unsigned minDefragQuant = 16*1024;
		int bestIncrementalDefragQuant = minDefragQuant;

		{
			ScopedReadLock(_lock);
			for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {

				// evaluate candidacy for a small incremental move
				if (((*i)->_size - (*i)->_allocatedSpace) > bestIncrementalDefragQuant) {
					auto steps = (*i)->_heap.CalculateIncrementalDefragCandidate();
					if (!steps._steps.empty()) {
						int increase = steps._newLargestFreeBlock - (int)(*i)->_heap.CalculateLargestFreeBlock();
						if (increase > bestIncrementalDefragQuant) {
							bestIncrementalDefragQuant = increase;
							bestIncrementalDefragSteps = std::move(steps._steps);
							bestIncrementalDefragHeap = i->get();
						}
					}
				}

				auto largestBlock = (*i)->_heap.CalculateLargestFreeBlock();
				if ((*i)->_allocatedSpace > heapDrainThreshold) largestBlockForHeapDrain = std::max(largestBlockForHeapDrain, largestBlock);
				if (largestBlock > largestBlockThreshold) continue;		// only care about pages where the largest block has become small

				auto availableSpace = (*i)->_size - (*i)->_allocatedSpace;
				if (largestBlock*2 > availableSpace) continue;			// we want to at least double the largest block size in order to make this worthwhile

				auto weight = availableSpace - largestBlock;
				if (weight > bestWeight) {
					assert((*i)->_heapResource);
						//      if the heap hasn't changed since the last time this heap was used as a defrag source, then there's no use in picking it again
					if ((*i)->_hashLastDefrag != (*i)->_heap.CalculateHash()) {
						bestHeapForCompression = i->get();
						bestWeight = weight;
					}
				}
			}

			if (bestIncrementalDefragHeap) {
				// set _lockedForDefrag before we exit _lock, because this prevents destroying this heap
				auto oldLocked = bestIncrementalDefragHeap->_lockedForDefrag.exchange(true);
				assert(!oldLocked); (void)oldLocked;
				// If you hit the following assert it means we're triggering the same defrag multiple times
				// This usually happens when non of the blocks from the defrag operation actually moved; meaning it most likely remains
				// the most optimal defrag operation
				auto newState = bestIncrementalDefragHeap->_heap.CalculateHash();
				assert(newState != bestIncrementalDefragHeap->_hashLastDefrag);
				bestIncrementalDefragHeap->_hashLastDefrag = newState;
			} else {
				if (!bestHeapForCompression && largestBlockForHeapDrain > heapDrainThreshold) {
					// Look for the first small heap that where we can move the entire contents to another heap
					for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
						if ((*i)->_allocatedSpace < heapDrainThreshold) {
							bestHeapForCompression = i->get();
							break;
						}
					}
				}

				// set _lockedForDefrag before we exit _lock, because this prevents destroying this heap
				if (bestHeapForCompression) {
					auto oldLocked = bestHeapForCompression->_lockedForDefrag.exchange(true);
					assert(!oldLocked); (void)oldLocked;

					compression = bestHeapForCompression->_heap.CalculateHeapCompression();

					auto newState = bestHeapForCompression->_heap.CalculateHash();
					assert(newState != bestHeapForCompression->_hashLastDefrag);
					bestHeapForCompression->_hashLastDefrag = newState;
				}
			}
		}

							//      -=-=-=-=-=-=-=-=-=-=-       //

		// prioritize the small incremental defrag op
		if (bestIncrementalDefragHeap) {
			size_t byteCount = 0;
			for (const auto& s:bestIncrementalDefragSteps) byteCount += s._sourceEnd-s._sourceStart;
			_recentRepositionBytes += byteCount;
			_totalRepositionBytes += byteCount;

			auto newDefrag = std::make_unique<ActiveReposition>(
				*this, *_bufferUploads.lock(), *bestIncrementalDefragHeap, std::move(bestIncrementalDefragSteps));

			assert(!_activeDefrag);
			_activeDefrag = std::move(newDefrag);
		} else if (bestHeapForCompression) {
			size_t byteCount = 0;
			for (const auto& s:compression) byteCount += s._sourceEnd-s._sourceStart;
			_recentRepositionBytes += byteCount;
			_totalRepositionBytes += byteCount;

			auto newDefrag = std::make_unique<ActiveReposition>(
				*this, *_bufferUploads.lock(), *bestHeapForCompression, std::move(compression));

			assert(!_activeDefrag);
			_activeDefrag = std::move(newDefrag);

			#if defined(_DEBUG)
				// Validate that everything recorded in the _refCounts is part of the repositioning
				{
					ScopedLock(_lock);
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
				}
			#endif
		}
	}

	EventListID   BatchedResources::EventList_GetPublishedID() const { return _eventListManager._currentEventListPublishedId; }

	IteratorRange<const Event_ResourceReposition*> BatchedResources::EventList_Get(EventListID id)
	{
		if (!id) return {};
		for (unsigned c=0; c<dimof(_eventListManager._eventBuffers); ++c) {
			if (_eventListManager._eventBuffers[c]._id == id) {
				++_eventListManager._eventBuffers[c]._clientReferences;
					//  have to check again after the increment... because the client references value acts
					//  as a lock.
				if (_eventListManager._eventBuffers[c]._id == id) {
					return {&_eventListManager._eventBuffers[c]._evnt, &_eventListManager._eventBuffers[c]._evnt+1};
				} else {
					--_eventListManager._eventBuffers[c]._clientReferences;
						// in this case, the event has just be freshly overwritten
				}
				return {};
			}
		}
		return {};
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

				#if defined(_DEBUG)
					{
						ScopedLock(_lock);
						for (auto& h:_heaps) h->ValidateRefsAndHeap();
					}
				#endif
				return;
			}
		}
	}

	EventListID   BatchedResources::EventListManager::EventList_Publish(const Event_ResourceReposition& evnt)
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
			_currentEventListPublishedId = id;
			return id;
		}
		assert(0);
		return ~EventListID(0x0);
	}

	RenderCore::ResourceDesc BatchedResources::MakeFallbackDesc(size_t size, StringSection<> name)
	{
		return CreateDesc(_fallbackBindFlags, LinearBufferDesc::Create(size), name);
	}

	BatchedResources::BatchedResources(
		RenderCore::IDevice& device, const std::shared_ptr<IManager>& bufferUploads,
		RenderCore::BindFlag::BitField bindFlags,
		unsigned pageSizeInBytes)
	: _device(&device)
	, _bufferUploads(bufferUploads)
	, _fallbackBindFlags(bindFlags)
	{
		_prototype = CreateDesc(
			bindFlags | BindFlag::TransferDst | BindFlag::TransferSrc, 0,
			LinearBufferDesc::Create(pageSizeInBytes),
			"batched-resources");
		_recentDeviceCreateCount = 0;
		_totalCreateCount = 0;
	}

	BatchedResources::~BatchedResources()
	{}

	unsigned    BatchedResources::HeapedResource::Allocate(unsigned size, StringSection<>)
	{
		// note -- we start out with no ref count registered in _refCounts for this range. The first ref count will come when we create a ResourceLocator
		auto result = _heap.Allocate(size);
		if (result != ~0u) _allocatedSpace += size;
		return result;
	}

	bool        BatchedResources::HeapedResource::AddRef(unsigned ptr, unsigned size, StringSection<> name)
	{
		std::pair<signed,signed> newRefCounts = _refCounts.AddRef(ptr, size, name);
		assert(newRefCounts.first >= 0 && newRefCounts.second >= 0);
		assert(newRefCounts.first == newRefCounts.second);
		return newRefCounts.second==1;
	}

	void    BatchedResources::HeapedResource::Allocate(unsigned ptr, unsigned size)
	{
		if (_heap.Allocate(ptr, size))
			_allocatedSpace += size;
	}

	void        BatchedResources::HeapedResource::Deallocate(unsigned ptr, unsigned size)
	{
		if (_heap.Deallocate(ptr, size))
			_allocatedSpace -= size;
		assert(_heap.CalculateAllocatedSpace() == _allocatedSpace);
	}

	BatchedHeapMetrics BatchedResources::HeapedResource::CalculateMetrics() const
	{
		BatchedHeapMetrics result;
		result._markers          = _heap.CalculateMetrics();
		result._allocatedSpace   = result._unallocatedSpace = 0;
		result._heapSize         = _size;
		result._largestFreeBlock = result._spaceInReferencedCountedBlocks = result._referencedCountedBlockCount = 0;
		result._guid = _heapResource->GetGUID();

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
			assert(_allocatedSpace == heapAllocatedSpace);
		#endif
	}

	BatchedResources::HeapedResource::HeapedResource()
	: _size(0), _heap(0), _refCounts(0), _hashLastDefrag(0), _allocatedSpace(0)
	{}

	BatchedResources::HeapedResource::HeapedResource(const ResourceDesc& desc, const std::shared_ptr<RenderCore::IResource>& heapResource)
	: _heapResource(heapResource)
	, _heap(RenderCore::ByteCount(desc))
	, _refCounts(RenderCore::ByteCount(desc))
	, _size(RenderCore::ByteCount(desc))
	, _hashLastDefrag(0)
	, _allocatedSpace(0)
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
		
		if (_repositionCmdList && !_eventId.has_value() && bufferUploads.IsComplete(_repositionCmdList.value())) {
			// publish the changes to encourage the client to move across to the 
			Event_ResourceReposition result;
			result._originalResource = _srcHeap->_heapResource.get();
			result._newResource = _dstUberBlock.GetContainingResource();
			result._defragSteps = _steps;
			_eventId = evntListMan.EventList_Publish(result);
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
		assert(dstSizeRequired < srcHeap._size);		// can't be 100% of a heap -- that would require no defrag, and would fail the upcoming Allocate()
		_dstUberBlock = resourceSystem.Allocate(dstSizeRequired, "reposition-uber-block");
		assert(!_dstUberBlock.IsEmpty());
		if (!_dstUberBlock.IsWholeResource())
			for (auto& s:_steps)
				s._destination += _dstUberBlock.GetRangeInContainingResource().first;

		_futureRepositionCmdList = bufferUploads.Begin(
			_dstUberBlock.GetContainingResource(),
			srcHeap._heapResource,
			_steps);
		if (!_futureRepositionCmdList.valid())
			Throw(std::runtime_error("Failed while queuing reposition transaction"));
	}

	BatchedResources::ActiveReposition::~ActiveReposition() {}

	std::shared_ptr<IBatchedResources> CreateBatchedResources(
		RenderCore::IDevice& device, const std::shared_ptr<IManager>& bufferUploads, 
		RenderCore::BindFlag::BitField bindFlags,
		unsigned pageSizeInBytes)
	{
		return std::make_shared<BatchedResources>(device, bufferUploads, bindFlags, pageSizeInBytes);
	}

}
