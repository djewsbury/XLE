// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"

#include "ResourceUploadHelper.h"
#include "Metrics.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/Metal/DeviceContext.h"		// for command list ptr
#include "../Utility/Threading/LockFree.h"
#include <atomic>

namespace BufferUploads
{
	using IResource = RenderCore::IResource;

        //////   C O M M I T   S T E P   //////

    class CommitStep
    {
    public:
        class DeferredCopy
        {
        public:
            ResourceLocator _destination;
            ResourceDesc _resourceDesc;
            std::vector<uint8_t> _temporaryBuffer;
        };

        class DeferredDefragCopy
        {
        public:
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

        void swap(CommitStep& other);

        CommitStep();
        CommitStep(CommitStep&& moveFrom) = default;
        CommitStep& operator=(CommitStep&& moveFrom) = default;
        ~CommitStep();
    private:
        std::vector<DeferredCopy>       _deferredCopies;
        std::vector<DeferredDefragCopy> _deferredDefragCopies;
        std::vector<ResourceLocator>    _delayedDeletes;
    };

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
        #define XL_BUFFER_UPLOAD_RECORD_THREAD_CONTEXT_METRICS
    #endif

    class ThreadContext
    {
    public:
        void                    ResolveCommandList();
        void                    CommitToImmediate(RenderCore::IThreadContext& commitTo, LockFreeFixedSizeQueue<unsigned, 4>* framePriorityQueue = nullptr);

        CommandListMetrics      PopMetrics();

        void                    EventList_Get(IManager::EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end);
        void                    EventList_Release(IManager::EventListID id, bool silent = false);
        IManager::EventListID   EventList_Push(const Event_ResourceReposition& evnt);
        void                    EventList_Publish(IManager::EventListID toEvent);

        IManager::EventListID   EventList_GetWrittenID() const;
        IManager::EventListID   EventList_GetPublishedID() const;
        IManager::EventListID   EventList_GetProcessedID() const;

        CommandListID           CommandList_GetUnderConstruction() const        { return _commandListIDUnderConstruction; }
        CommandListID           CommandList_GetCommittedToImmediate() const     { return _commandListIDCommittedToImmediate; }

        CommandListMetrics&     GetMetricsUnderConstruction()                   { return _commandListUnderConstruction; }
        CommitStep&             GetCommitStepUnderConstruction()                { return _commitStepUnderConstruction; }

        unsigned                CommitCount_Current()                           { return _commitCountCurrent; }
        unsigned&               CommitCount_LastResolve()                       { return _commitCountLastResolve; }

        PlatformInterface::ResourceUploadHelper& GetResourceUploadHelper() { return _resourceUploadHelper; }
        const std::shared_ptr<RenderCore::IThreadContext>& GetRenderCoreThreadContext() { return _underlyingContext; }

        void                    OnLostDevice();

        ThreadContext(std::shared_ptr<RenderCore::IThreadContext> underlyingContext);
        ~ThreadContext();
    private:
        std::shared_ptr<RenderCore::IThreadContext> _underlyingContext;
        PlatformInterface::ResourceUploadHelper _resourceUploadHelper;
        CommandListMetrics _commandListUnderConstruction;
        CommitStep _commitStepUnderConstruction;
        class CommandList
        {
        public:
            std::shared_ptr<RenderCore::Metal::CommandList> _deviceCommandList;
            mutable CommandListMetrics _metrics;
            CommitStep _commitStep;
            CommandListID _id;
        };
        LockFreeFixedSizeQueue<CommandList, 32> _queuedCommandLists;
        #if defined(XL_BUFFER_UPLOAD_RECORD_THREAD_CONTEXT_METRICS)
            LockFreeFixedSizeQueue<CommandListMetrics, 32> _recentRetirements;
        #endif
        bool _isImmediateContext;

        TimeMarker  _lastResolve;
        unsigned    _commitCountCurrent, _commitCountLastResolve;

        CommandListID _commandListIDUnderConstruction, _commandListIDCommittedToImmediate;

        class EventList
        {
        public:
            volatile IManager::EventListID _id;
            Event_ResourceReposition _evnt;
            std::atomic<unsigned> _clientReferences;
            EventList() : _id(~IManager::EventListID(0x0)), _clientReferences(0) {}
        };
        IManager::EventListID   _currentEventListId;
        IManager::EventListID   _currentEventListPublishedId;
        std::atomic<IManager::EventListID>   _currentEventListProcessedId;
        EventList               _eventBuffers[4];
        unsigned                _eventListWritingIndex;
    };
}
