// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IBufferUploads.h"
#include "ResourceSource.h"
#include "../Utility/HeapUtils.h"

namespace BufferUploads
{
	class ThreadContext;
	using IResource = RenderCore::IResource;

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

		void                    TickDefrag(ThreadContext& deviceContext, IManager::EventListID processedEventList);

		BatchedResources(RenderCore::IDevice& device, const ResourceDesc& prototype);
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
			SimpleSpanningHeap  _heap;
			ReferenceCountingLayer _refCounts;
			unsigned _size;
			unsigned _defragCount;
			uint64_t _hashLastDefrag;
		};

		class ActiveDefrag
		{
		public:
			struct Operation  { enum Enum { Deallocate }; };
			void                QueueOperation(Operation::Enum operation, unsigned start, unsigned end);
			void                ApplyPendingOperations(HeapedResource& destination);

			void                Tick(ThreadContext& context, const std::shared_ptr<IResource>& sourceResource);
			bool                IsComplete(IManager::EventListID processedEventList, ThreadContext& context);

			void                SetSteps(const SimpleSpanningHeap& sourceHeap, const std::vector<DefragStep>& steps);
			void                ReleaseSteps();
			const std::vector<DefragStep>&  GetSteps() { return _steps; }

			HeapedResource*     GetHeap() { return _newHeap.get(); }
			std::unique_ptr<HeapedResource>&&    ReleaseHeap();

			ActiveDefrag();
			~ActiveDefrag();

		private:
			struct PendingOperation { unsigned _start, _end; Operation::Enum _operation; };
			std::vector<PendingOperation>   _pendingOperations;

			bool                            _doneResourceCopy;
			IManager::EventListID           _eventId;
			std::unique_ptr<HeapedResource>   _newHeap;
			std::vector<DefragStep>         _steps;

			CommandListID _initialCommandListID;

			static bool SortByPosition(const PendingOperation& lhs, const PendingOperation& rhs);
		};

		std::vector<std::unique_ptr<HeapedResource>> _heaps;
		ResourceDesc _prototype;
		RenderCore::IDevice* _device;
		mutable Threading::ReadWriteMutex _lock;

			//  Active defrag stuff...
		std::unique_ptr<ActiveDefrag> _activeDefrag;
		Threading::Mutex _activeDefrag_Lock;
		HeapedResource* _activeDefragHeap;

		std::shared_ptr<IResource> _temporaryCopyBuffer;
		unsigned _temporaryCopyBufferCountDown;

		mutable std::atomic<unsigned>   _recentDeviceCreateCount;
		std::atomic<size_t>             _totalCreateCount;

		BatchedResources(const BatchedResources&);
		BatchedResources& operator=(const BatchedResources&);
	};

}

