// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include "ResourceUploadHelper.h"

namespace Utility { template<typename Type, int Count> class LockFreeFixedSizeQueue; }

namespace BufferUploads
{
        //////   T H R E A D   C O N T E X T   //////

    class Event_ResourceReposition
    {
    public:
        std::shared_ptr<IResource> _originalResource;
        std::shared_ptr<IResource> _newResource;
        std::shared_ptr<IResourcePool> _pool;
        uint64_t _poolMarker;
        std::vector<Utility::DefragStep> _defragSteps;
    };

    #if !defined(NDEBUG)
        #define RECORD_BU_THREAD_CONTEXT_METRICS
    #endif

    class ThreadContext
    {
    public:
        void                    ResolveCommandList();
        void                    CommitToImmediate(
            RenderCore::IThreadContext& commitTo,
            unsigned frameId,
            LockFreeFixedSizeQueue<unsigned, 4>* framePriorityQueue = nullptr);

        CommandListMetrics      PopMetrics();

        void                    EventList_Get(IManager::EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end);
        void                    EventList_Release(IManager::EventListID id, bool silent = false);
        IManager::EventListID   EventList_Push(const Event_ResourceReposition& evnt);
        void                    EventList_Publish(IManager::EventListID toEvent);

        IManager::EventListID   EventList_GetWrittenID() const;
        IManager::EventListID   EventList_GetPublishedID() const;
        IManager::EventListID   EventList_GetProcessedID() const;

        CommandListID           CommandList_GetUnderConstruction() const;
        CommandListID           CommandList_GetCommittedToImmediate() const;

        CommandListMetrics&     GetMetricsUnderConstruction();

        class DeferredOperations;
        DeferredOperations&     GetDeferredOperationsUnderConstruction();

        unsigned                CommitCount_Current();
        unsigned&               CommitCount_LastResolve();

        PlatformInterface::StagingPage&     GetStagingPage();
        PlatformInterface::QueueMarker      GetProducerQueueMarker();

        PlatformInterface::ResourceUploadHelper&    GetResourceUploadHelper() { return _resourceUploadHelper; }
        const RenderCore::IThreadContext&           GetRenderCoreThreadContext() { return *_underlyingContext; }
        RenderCore::IDevice&                        GetRenderCoreDevice() { return *_underlyingContext->GetDevice(); }

        ThreadContext(std::shared_ptr<RenderCore::IThreadContext> underlyingContext);
        ~ThreadContext();
    private:
        std::shared_ptr<RenderCore::IThreadContext> _underlyingContext;
        PlatformInterface::ResourceUploadHelper _resourceUploadHelper;

        struct Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

    class ThreadContext::DeferredOperations
    {
    public:
        struct DeferredCopy
        {
            ResourceLocator _destination;
            ResourceDesc _resourceDesc;
            std::vector<uint8_t> _temporaryBuffer;
        };

        struct DeferredDefragCopy
        {
            std::shared_ptr<IResource> _destination;
            std::shared_ptr<IResource> _source;
            std::vector<DefragStep> _steps;
            DeferredDefragCopy(std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<DefragStep>& steps);
            ~DeferredDefragCopy();
        };

        void Add(DeferredCopy&& copy);
        void Add(DeferredDefragCopy&& copy);
        void AddDelayedDelete(ResourceLocator&& locator);
        void CommitToImmediate_PreCommandList(RenderCore::IThreadContext& immediateContext);
        void CommitToImmediate_PostCommandList(RenderCore::IThreadContext& immediateContext);
        bool IsEmpty() const;

        void swap(DeferredOperations& other);

        DeferredOperations();
        DeferredOperations(DeferredOperations&& moveFrom) = default;
        DeferredOperations& operator=(DeferredOperations&& moveFrom) = default;
        ~DeferredOperations();
    private:
        std::vector<DeferredCopy>       _deferredCopies;
        std::vector<DeferredDefragCopy> _deferredDefragCopies;
        std::vector<ResourceLocator>    _delayedDeletes;
    };
}
