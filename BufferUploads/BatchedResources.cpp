// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BatchedResources.h"
#include "Metrics.h"
#include "ResourceUploadHelper.h"
#include "ThreadContext.h"
#include "../Utility/HeapUtils.h"

namespace BufferUploads
{
	class BatchedResources : public IBatchedResources, public std::enable_shared_from_this<BatchedResources>
	{
	public:
		ResourceLocator Allocate(size_t size, const char name[]) override;

		virtual void AddRef(
			uint64_t resourceMarker, RenderCore::IResource& resource, 
			size_t offset, size_t size) override;

		virtual void Release(
			uint64_t resourceMarker, std::shared_ptr<RenderCore::IResource>&& resource, 
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
			RenderCore::IDevice&, std::shared_ptr<IManager>&, 
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

			//  Active defrag stuff...
		std::unique_ptr<ActiveReposition> _activeDefrag;

		mutable std::atomic<unsigned>   _recentDeviceCreateCount;
		std::atomic<size_t>             _totalCreateCount;

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
		unsigned            Allocate(unsigned size, const char name[]=nullptr);
		void                Allocate(unsigned ptr, unsigned size);
		void                Deallocate(unsigned ptr, unsigned size);

		bool                AddRef(unsigned ptr, unsigned size, const char name[]=nullptr);
		
		BatchedHeapMetrics  CalculateMetrics() const;
		void                ValidateRefsAndHeap();

		HeapedResource();
		HeapedResource(const ResourceDesc& desc, const std::shared_ptr<RenderCore::IResource>& heapResource);
		~HeapedResource();

		std::shared_ptr<RenderCore::IResource> _heapResource;
		SpanningHeap<uint32_t>  _heap;
		ReferenceCountingLayer _refCounts;
		unsigned _size;
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
						bestHeap->AddRef(allocation, size);
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
		newHeap->AddRef(allocation, size);

		{
			ScopedModifyLock(_lock);
			_heaps.push_back(std::move(newHeap));
		}

		return ResourceLocator{std::move(heapResource), allocation, size, weak_from_this(), 0ull, true};
	}
	
	void BatchedResources::AddRef(
		uint64_t resourceMarker, RenderCore::IResource& resource, 
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
		if (heap) heap->AddRef(offset, size);
	}

	void BatchedResources::Release(
		uint64_t resourceMarker, std::shared_ptr<RenderCore::IResource>&& resource, 
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
		if (heap) {
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
			#if defined(_DEBUG)
				heap->ValidateRefsAndHeap();
			#endif

			if (heap->_heap.IsEmpty()) {
				// If we get down to completely empty, just remove and the page entirely. This can happen frequently
				// after heap compression
				for (auto i=_heaps.begin(); i!=_heaps.end(); ++i)
					if (i->get() == heap) {
						_heaps.erase(i);
						break;
					}
			}
		}

			// (prevent caller from performing extra derefs)
		resource = nullptr;
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
				auto oldLocked = sourceHeap->_lockedForDefrag.exchange(false);
				assert(oldLocked);
				_activeDefrag = nullptr;
			}
			return;
		}


				//                            -                                 //
				//                         -------                              //
				//                                                              //
			//////////////////////////////////////////////////////////////////////////
				//      Start a new defrag step on the most fragmented heap     //
				//                            -                                 //
				//        First decide if we want to do a full heap             //
				//        compression on one of the heaps. We'll need some      //
				//        heuristic to figure out when it's the right time      //
				//        to do this.                                           //
			//////////////////////////////////////////////////////////////////////////
				//                                                              //
				//                         -------                              //
				//                            -                                 //

		
		const unsigned minWeightToDoSomething = _prototype._linearBufferDesc._sizeInBytes / 8; // only do something when there's X byte difference between total available space and the largest block
		const unsigned largestBlockThreshold = _prototype._linearBufferDesc._sizeInBytes / 8;
		unsigned bestWeight = minWeightToDoSomething;
		HeapedResource* bestHeapForCompression = nullptr;
		{
			ScopedReadLock(_lock);
			for (auto i=_heaps.begin(); i!=_heaps.end(); ++i) {
				auto largestBlock = (*i)->_heap.CalculateLargestFreeBlock();
				if (largestBlock > largestBlockThreshold) continue;		// only care about pages where the largest block has become small

				auto availableSpace = (*i)->_heap.CalculateAvailableSpace();
				if (largestBlock > availableSpace/2) continue;			// we want to at least double the largest block size in order to make this worthwhile

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
		}

							//      -=-=-=-=-=-=-=-=-=-=-       //

		if (bestHeapForCompression) {
			auto oldLocked = bestHeapForCompression->_lockedForDefrag.exchange(true);
			assert(!oldLocked);

			// Now that we've set bestHeap->_lockedForDefrag, bestHeap->_heap is immutable...
			auto compression = bestHeapForCompression->_heap.CalculateHeapCompression();
			bestHeapForCompression->_hashLastDefrag = bestHeapForCompression->_heap.CalculateHash();
			auto newDefrag = std::make_unique<ActiveReposition>(
				*this, *_bufferUploads.lock(), *bestHeapForCompression, std::move(compression));

			assert(!_activeDefrag);
			_activeDefrag = std::move(newDefrag);

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

	BatchedResources::BatchedResources(
		RenderCore::IDevice& device, std::shared_ptr<IManager>& bufferUploads,
		RenderCore::BindFlag::BitField bindFlags,
		unsigned pageSizeInBytes)
	: _device(&device)
	, _bufferUploads(bufferUploads)
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
		#endif
	}

	BatchedResources::HeapedResource::HeapedResource()
	: _size(0), _heap(0), _refCounts(0), _hashLastDefrag(0)
	{}

	BatchedResources::HeapedResource::HeapedResource(const ResourceDesc& desc, const std::shared_ptr<RenderCore::IResource>& heapResource)
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

	std::shared_ptr<IBatchedResources> CreateBatchedResources(
		RenderCore::IDevice& device, std::shared_ptr<IManager>& bufferUploads, 
		RenderCore::BindFlag::BitField bindFlags,
		unsigned pageSizeInBytes)
	{
		return std::make_shared<BatchedResources>(device, bufferUploads, bindFlags, pageSizeInBytes);
	}

}
