// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "IBufferUploads.h"
#include "ResourceSource.h"
#include "../Utility/HeapUtils.h"

namespace BufferUploads
{
	class ThreadContext;
	using IResource = RenderCore::IResource;
	struct BatchingSystemMetrics;

	class Event_ResourceReposition
	{
	public:
		std::shared_ptr<IResource> _originalResource;
		std::shared_ptr<IResource> _newResource;
		std::shared_ptr<IResourcePool> _pool;
		uint64_t _poolMarker;
		std::vector<Utility::RepositionStep> _defragSteps;
	};
	using EventListID = uint32_t;

	struct BatchedHeapMetrics
	{
		std::vector<unsigned> _markers;
		size_t _allocatedSpace, _unallocatedSpace;
		size_t _heapSize;
		size_t _largestFreeBlock;
		unsigned _spaceInReferencedCountedBlocks;
		unsigned _referencedCountedBlockCount;
	};

	struct BatchingSystemMetrics
	{
		std::vector<BatchedHeapMetrics> _heaps;
		unsigned _recentDeviceCreateCount;
		unsigned _totalDeviceCreateCount;
	};

	class BatchedResources : public IResourcePool, public std::enable_shared_from_this<BatchedResources>
	{
	public:
		ResourceLocator Allocate(size_t size, const char name[]);

		virtual void AddRef(
			uint64_t resourceMarker, IResource& resource, 
			size_t offset, size_t size) override;

		virtual void Release(
			uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
			size_t offset, size_t size) override;

			//
			//      Two step destruction process... Deref to remove the reference first. But if Deref returns
			//      "PerformDeallocate", caller must also call Deallocate. In some cases the caller may not 
			//      want to do the deallocate immediately -- (eg, waiting for GPU, or shifting it into another thread)
			//
		struct ResultFlags { enum Enum { IsBatched = 1<<0, PerformDeallocate = 1<<1, IsCurrentlyDefragging = 1<<2 }; typedef unsigned BitField; };
		ResultFlags::BitField   IsBatchedResource(IResource* resource) const;
		ResultFlags::BitField   Validate(const ResourceLocator& locator) const;
		BatchingSystemMetrics   CalculateMetrics() const;
		const ResourceDesc&     GetPrototype() const { return _prototype; }

		void                    Tick();

		//////////// event lists //////////////
		void          EventList_Get(EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end);
		void          EventList_Release(EventListID id);

		EventListID   EventList_GetWrittenID() const;
		EventListID   EventList_GetPublishedID() const;
		EventListID   EventList_GetProcessedID() const;
		///////////////////////////////////////

		BatchedResources(RenderCore::IDevice&, std::shared_ptr<IManager>&, const ResourceDesc& prototype);
		~BatchedResources();
	private:
		class HeapedResource
		{
		public:
			unsigned            Allocate(unsigned size, const char name[]);
			void                Allocate(unsigned ptr, unsigned size);
			void                Deallocate(unsigned ptr, unsigned size);

			bool                AddRef(unsigned ptr, unsigned size, const char name[]);
			bool                Deref(unsigned ptr, unsigned size);
			
			BatchedHeapMetrics  CalculateMetrics() const;
			float               CalculateFragmentationWeight() const;
			void                ValidateRefsAndHeap();

			HeapedResource();
			HeapedResource(const ResourceDesc& desc, const std::shared_ptr<IResource>& heapResource);
			~HeapedResource();

			std::shared_ptr<IResource> _heapResource;
			SpanningHeap<uint32_t>  _heap;
			ReferenceCountingLayer _refCounts;
			unsigned _size;
			uint64_t _hashLastDefrag;
			bool _lockedForDefrag = false;
		};

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

			EventListID EventList_Push(const Event_ResourceReposition& evnt);
			void EventList_Publish(EventListID toEvent);
		};

		class ActiveReposition
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

		std::vector<std::unique_ptr<HeapedResource>> _heaps;
		ResourceDesc _prototype;
		RenderCore::IDevice* _device;
		mutable Threading::ReadWriteMutex _lock;
		std::weak_ptr<IManager> _bufferUploads;

			//  Active defrag stuff...
		std::unique_ptr<ActiveReposition> _activeDefrag;

		// std::shared_ptr<IResource> _temporaryCopyBuffer;
		// unsigned _temporaryCopyBufferCountDown;

		mutable std::atomic<unsigned>   _recentDeviceCreateCount;
		std::atomic<size_t>             _totalCreateCount;

		EventListManager _eventListManager;

		BatchedResources(const BatchedResources&);
		BatchedResources& operator=(const BatchedResources&);
	};

}

