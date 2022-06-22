// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"

#include "MemoryManagement.h"
#include "ResourceUploadHelper.h"
#include "ThreadContext.h"
#include "../RenderCore/IDevice_Forward.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Optional.h"
#include <atomic>

namespace BufferUploads
{
    class IResourcePool
    {
    public:
        virtual void AddRef(
            uint64_t resourceMarker, IResource& resource, 
            size_t offset, size_t size) = 0;
        virtual void Release(
            uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
            size_t offset, size_t size) = 0;
        virtual ~IResourcePool() {}
    };

        /////   R E S O U R C E S   P O O L   /////

    using DescHash = uint64_t;

    template <typename Desc> class ResourcesPool : public IResourcePool, public std::enable_shared_from_this<ResourcesPool<Desc>>
    {
    public:
        ResourceLocator     CreateResource(const Desc&, unsigned realSize, bool allowDeviceCreation);

        virtual void AddRef(
            uint64_t resourceMarker, IResource& resource, 
            size_t offset, size_t size) override;

        virtual void Release(
            uint64_t resourceMarker, std::shared_ptr<IResource>&& resource, 
            size_t offset, size_t size) override;

        std::vector<PoolMetrics>    CalculateMetrics() const;
        void                        OnLostDevice();
        void                        Update(unsigned newFrameID);

        ResourcesPool(RenderCore::IDevice& device, unsigned retainFrames = ~unsigned(0x0));
        ~ResourcesPool();
    protected:
        std::shared_ptr<IResource> MakeReturnToPoolPointer(std::shared_ptr<IResource>&& resource, uint64_t poolMarker);
        void ReturnToPool(std::shared_ptr<IResource>&& resource, uint64_t resourceMarker);

        class PoolOfLikeResources
        {
        public:
            auto        AllocateResource(size_t realSize, bool allowDeviceCreation) -> std::shared_ptr<IResource>;
            const Desc& GetDesc() const { return _desc; }
            PoolMetrics CalculateMetrics() const;
            void        Update(unsigned newFrameID);
            void        ReturnToPool(std::shared_ptr<IResource>&& resource);

            PoolOfLikeResources(RenderCore::IDevice& underlyingDevice, const Desc&, unsigned retainFrames = ~unsigned(0x0));
            ~PoolOfLikeResources();
        private:
            struct Entry
            {
                std::shared_ptr<IResource>  _underlying;
                unsigned            _returnFrameID;
            };
            LockFreeFixedSizeQueue<Entry, 512> _allocableResources;
            Desc                        _desc;
            mutable size_t              _peakSize;
            mutable std::atomic<unsigned>  _recentDeviceCreateCount, _recentPoolCreateCount, _recentReleaseCount;
            std::atomic<size_t>         _totalCreateSize, _totalCreateCount, _totalRealSize;
            unsigned                    _currentFrameID;
            unsigned                    _retainFrames;
			RenderCore::IDevice*        _underlyingDevice;
        };

            //
            //          >   Manual hash table; sorted vector with a     <
            //          >   pointer to the payload                      <
            //
        typedef std::pair<DescHash, std::shared_ptr<PoolOfLikeResources> > HashTableEntry;
        typedef std::vector<HashTableEntry> HashTable;
        HashTable                       _hashTables[2];
        volatile std::atomic<unsigned>  _readerCount[2];
        unsigned                        _hashTableIndex;
        mutable Threading::Mutex        _writerLock;
        unsigned                        _retainFrames;
		RenderCore::IDevice*            _underlyingDevice;

        struct CompareFirst
        {
            bool operator()(const HashTableEntry& lhs, const HashTableEntry& rhs) { return lhs.first < rhs.first; }
            bool operator()(const HashTableEntry& lhs, DescHash rhs) { return lhs.first < rhs; }
            bool operator()(DescHash lhs, const HashTableEntry& rhs) { return lhs < rhs.first; }
        };
    };

        /////   B A T C H E D   R E S O U R C E S   /////

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
        void                    OnLostDevice();

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

        /////   R E S O U R C E   S O U R C E   /////

    class ResourceSource
    {
    public:
        struct ResourceConstruction
        {
            struct Flags 
            { 
                enum Enum {
                    InitialisationSuccessful     = 1<<0,
                    DeviceConstructionInvoked    = 1<<1
                };
                using BitField = unsigned;
            };
            ResourceLocator _locator;
            Flags::BitField _flags = 0;
        };

        struct CreationOptions
        {
            enum Enum {
                PreventDeviceCreation = 1<<0,
                Staging = 1<<1
            };
            typedef unsigned BitField;
        };

        ResourceConstruction    Create(const ResourceDesc& desc, IDataPacket* initialisationData=nullptr, CreationOptions::BitField options=0);
        void                    Validate(const ResourceLocator& locator);

        BatchedResources&       GetBatchedResources()             { return *_batchedIndexBuffers; }
        PoolSystemMetrics       CalculatePoolMetrics() const;
        void                    Tick(ThreadContext& context, IManager::EventListID processedEventList);

        BatchedResources::ResultFlags::BitField     IsBatchedResource(const ResourceLocator& locator, const ResourceDesc& desc);
        bool                    CanBeBatched(const ResourceDesc& desc);

        void                    OnLostDevice();

        ResourceSource(RenderCore::IDevice& device);
        ~ResourceSource();

    protected:
        std::shared_ptr<ResourcesPool<ResourceDesc>>      _stagingBufferPool;
        std::shared_ptr<ResourcesPool<ResourceDesc>>      _pooledGeometryBuffers;
        std::shared_ptr<BatchedResources>   _batchedIndexBuffers;
        unsigned                            _frameID;
		RenderCore::IDevice*                _underlyingDevice;
    };

}
