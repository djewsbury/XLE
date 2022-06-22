// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ThreadContext.h"
#include "Metrics.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../OSServices/TimeUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/HeapUtils.h"
#include "../Utility/Threading/LockFree.h"
#include <atomic>

namespace BufferUploads
{
    struct ThreadContext::Pimpl
    {
        CommandListMetrics _commandListUnderConstruction;
        DeferredOperations _deferredOperationsUnderConstruction;
        struct QueuedCommandList
        {
            std::shared_ptr<RenderCore::Metal::CommandList> _deviceCommandList;
            mutable CommandListMetrics _metrics;
            DeferredOperations _deferredOperations;
            CommandListID _id;
        };
        LockFreeFixedSizeQueue<QueuedCommandList, 32> _queuedCommandLists;
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            LockFreeFixedSizeQueue<CommandListMetrics, 32> _recentRetirements;
        #endif
        bool _isImmediateContext;

        TimeMarker  _lastResolve;
        unsigned    _commitCountCurrent, _commitCountLastResolve;

        CommandListID _commandListIDUnderConstruction, _commandListIDCommittedToImmediate;

        struct EventList
        {
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

        Pimpl() : _currentEventListId(0), _eventListWritingIndex(0), _currentEventListProcessedId(0), _currentEventListPublishedId(0) {}
    };

    void ThreadContext::ResolveCommandList()
    {
        int64_t currentTime = OSServices::GetPerformanceCounter();
        Pimpl::QueuedCommandList newCommandList;
        newCommandList._metrics = _pimpl->_commandListUnderConstruction;
        newCommandList._metrics._resolveTime = currentTime;
        newCommandList._metrics._processingEnd = currentTime;
        newCommandList._id = _pimpl->_commandListIDUnderConstruction;

        if (!_pimpl->_isImmediateContext) {
            newCommandList._deviceCommandList = RenderCore::Metal::DeviceContext::Get(*_underlyingContext)->ResolveCommandList();
            newCommandList._deferredOperations.swap(_pimpl->_deferredOperationsUnderConstruction);
            _pimpl->_queuedCommandLists.push_overflow(std::move(newCommandList));
        } else {
                    // immediate resolve -- skip the render thread resolve step...
            _pimpl->_deferredOperationsUnderConstruction.CommitToImmediate_PreCommandList(*_underlyingContext);
            _pimpl->_deferredOperationsUnderConstruction.CommitToImmediate_PostCommandList(*_underlyingContext);
            _pimpl->_commandListIDCommittedToImmediate = std::max(_pimpl->_commandListIDCommittedToImmediate, _pimpl->_commandListIDUnderConstruction);

            newCommandList._metrics._frameId = _underlyingContext->GetStateDesc()._frameId;
            newCommandList._metrics._commitTime = currentTime;
            #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
                while (!_pimpl->_recentRetirements.push(newCommandList._metrics)) {
                    _pimpl->_recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
                }
            #endif
        }

        _pimpl->_commandListUnderConstruction = CommandListMetrics();
        _pimpl->_commandListUnderConstruction._processingStart = currentTime;
        DeferredOperations().swap(_pimpl->_deferredOperationsUnderConstruction);
        ++_pimpl->_commandListIDUnderConstruction;
    }

    void ThreadContext::CommitToImmediate(
        RenderCore::IThreadContext& commitTo,
        LockFreeFixedSizeQueue<unsigned, 4>* framePriorityQueue)
    {
        if (_pimpl->_isImmediateContext) {
            assert(&commitTo == _underlyingContext.get());
            ++_pimpl->_commitCountCurrent;
            return;
        }

        auto immContext = RenderCore::Metal::DeviceContext::Get(commitTo);
        
        TimeMarker stallStart = OSServices::GetPerformanceCounter();
        bool gotStart = false;
        for (;;) {

                //
                //      While there are uncommitted frame-priority command lists, we need to 
                //      stall to wait until they are committed. Keep trying to drain the queue
                //      until there are no lists, and nothing pending.
                //

            const bool currentlyUncommitedFramePriorityCommandLists = framePriorityQueue && framePriorityQueue->size()!=0;

            Pimpl::QueuedCommandList* commandList = 0;
            while (_pimpl->_queuedCommandLists.try_front(commandList)) {
                TimeMarker stallEnd = OSServices::GetPerformanceCounter();
                if (!gotStart) {
                    commitTo.GetAnnotator().Event("BufferUploads", RenderCore::IAnnotator::EventTypes::MarkerBegin);
                    gotStart = true;
                }

                commandList->_deferredOperations.CommitToImmediate_PreCommandList(commitTo);
                if (commandList->_deviceCommandList) {
                    auto* deviceVulkan = (RenderCore::IThreadContextVulkan*)commitTo.QueryInterface(typeid(RenderCore::IThreadContextVulkan).hash_code());
                    if (deviceVulkan) {
                        deviceVulkan->CommitPrimaryCommandBufferToQueue(*commandList->_deviceCommandList);
                        commandList->_deviceCommandList = {};
                    } else {
                        immContext->ExecuteCommandList(std::move(*commandList->_deviceCommandList));
                    }
                }
                commandList->_deferredOperations.CommitToImmediate_PostCommandList(commitTo);
                _pimpl->_commandListIDCommittedToImmediate = std::max(_pimpl->_commandListIDCommittedToImmediate, commandList->_id);
            
                commandList->_metrics._frameId                  = commitTo.GetStateDesc()._frameId;
                commandList->_metrics._commitTime               = OSServices::GetPerformanceCounter();
                commandList->_metrics._framePriorityStallTime   = stallEnd - stallStart;    // this should give us very small numbers, when we're not actually stalling for frame priority commits
                #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
                    while (!_pimpl->_recentRetirements.push(commandList->_metrics))
                        _pimpl->_recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
                #endif
                _pimpl->_queuedCommandLists.pop();

                stallStart = OSServices::GetPerformanceCounter();
            }
                
            if (!currentlyUncommitedFramePriorityCommandLists)
                break;

            Threading::YieldTimeSlice();
        }

        if (gotStart) {
            commitTo.GetAnnotator().Event("BufferUploads", RenderCore::IAnnotator::EventTypes::MarkerEnd);
        }
        
        ++_pimpl->_commitCountCurrent;
    }

    CommandListMetrics ThreadContext::PopMetrics()
    {
        #if defined(RECORD_BU_THREAD_CONTEXT_METRICS)
            CommandListMetrics* ptr;
            if (_pimpl->_recentRetirements.try_front(ptr)) {
                CommandListMetrics result = *ptr;
                _pimpl->_recentRetirements.pop();
                return result;
            }
        #endif
        return CommandListMetrics();
    }

    IManager::EventListID   ThreadContext::EventList_GetWrittenID() const
    {
        return _pimpl->_currentEventListId;
    }

    IManager::EventListID   ThreadContext::EventList_GetPublishedID() const
    {
        return _pimpl->_currentEventListPublishedId;
    }

    IManager::EventListID   ThreadContext::EventList_GetProcessedID() const
    {
        return _pimpl->_currentEventListProcessedId;
    }

    void                    ThreadContext::EventList_Get(IManager::EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end)
    {
        begin = end = nullptr;
        if (!id) return;
        for (unsigned c=0; c<dimof(_pimpl->_eventBuffers); ++c) {
            if (_pimpl->_eventBuffers[c]._id == id) {
                ++_pimpl->_eventBuffers[c]._clientReferences;
                    //  have to check again after the increment... because the client references value acts
                    //  as a lock.
                if (_pimpl->_eventBuffers[c]._id == id) {
                    begin = &_pimpl->_eventBuffers[c]._evnt;
                    end = begin+1;
                } else {
                    --_pimpl->_eventBuffers[c]._clientReferences;
                        // in this case, the event has just be freshly overwritten
                }
                return;
            }
        }
    }

    void                    ThreadContext::EventList_Release(IManager::EventListID id, bool silent)
    {
        if (!id) return;
        for (unsigned c=0; c<dimof(_pimpl->_eventBuffers); ++c) {
            if (_pimpl->_eventBuffers[c]._id == id) {
                auto newValue = --_pimpl->_eventBuffers[c]._clientReferences;
                assert(signed(newValue) >= 0);
                    
                if (!silent) {
                    for (;;) {      // lock-free max...
                        auto originalProcessedId = _pimpl->_currentEventListProcessedId.load();
                        auto newProcessedId = std::max(originalProcessedId, (IManager::EventListID)_pimpl->_eventBuffers[c]._id);
                        if (_pimpl->_currentEventListProcessedId.compare_exchange_strong(originalProcessedId, newProcessedId))
                            break;
                    }
                }
                return;
            }
        }
    }

    IManager::EventListID   ThreadContext::EventList_Push(const Event_ResourceReposition& evnt)
    {
            //
            //      try to push this event into the small queue... but don't overwrite anything that
            //      currently has a client reference on it.
            //
        if (!_pimpl->_eventBuffers[_pimpl->_eventListWritingIndex]._clientReferences.load()) {
            IManager::EventListID id = ++_pimpl->_currentEventListId;
            _pimpl->_eventBuffers[_pimpl->_eventListWritingIndex]._id = id;
            _pimpl->_eventBuffers[_pimpl->_eventListWritingIndex]._evnt = evnt;
            _pimpl->_eventListWritingIndex = (_pimpl->_eventListWritingIndex+1)%dimof(_pimpl->_eventBuffers);   // single writing thread, so it's ok
            return id;
        }
        assert(0);
        return ~IManager::EventListID(0x0);
    }

    void ThreadContext::EventList_Publish(IManager::EventListID toEvent)
    {
        _pimpl->_currentEventListPublishedId = toEvent;
    }

    CommandListID           ThreadContext::CommandList_GetUnderConstruction() const        { return _pimpl->_commandListIDUnderConstruction; }
    CommandListID           ThreadContext::CommandList_GetCommittedToImmediate() const     { return _pimpl->_commandListIDCommittedToImmediate; }

    CommandListMetrics&     ThreadContext::GetMetricsUnderConstruction()                   { return _pimpl->_commandListUnderConstruction; }

    auto                    ThreadContext::GetDeferredOperationsUnderConstruction() -> DeferredOperations&        { return _pimpl->_deferredOperationsUnderConstruction; }

    unsigned                ThreadContext::CommitCount_Current()                           { return _pimpl->_commitCountCurrent; }
    unsigned&               ThreadContext::CommitCount_LastResolve()                       { return _pimpl->_commitCountLastResolve; }

    ThreadContext::ThreadContext(std::shared_ptr<RenderCore::IThreadContext> underlyingContext) 
    : _resourceUploadHelper(*underlyingContext)
    {
        _underlyingContext = std::move(underlyingContext);
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_lastResolve = 0;
        _pimpl->_commitCountCurrent = _pimpl->_commitCountLastResolve = 0;
        _pimpl->_isImmediateContext = _underlyingContext->IsImmediate();
        _pimpl->_commandListIDUnderConstruction = 1;
        _pimpl->_commandListIDCommittedToImmediate = 0;
    }

    ThreadContext::~ThreadContext()
    {
    }

        //////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContext::DeferredOperations::DeferredDefragCopy::DeferredDefragCopy(
		std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<DefragStep>& steps)
    : _destination(std::move(destination)), _source(std::move(source)), _steps(steps)
    {}

    ThreadContext::DeferredOperations::DeferredDefragCopy::~DeferredDefragCopy()
    {}

    void ThreadContext::DeferredOperations::Add(DeferredOperations::DeferredCopy&& copy)
    {
        _deferredCopies.push_back(std::forward<DeferredOperations::DeferredCopy>(copy));
    }

    void ThreadContext::DeferredOperations::Add(DeferredOperations::DeferredDefragCopy&& copy)
    {
        _deferredDefragCopies.push_back(std::forward<DeferredOperations::DeferredDefragCopy>(copy));
    }

    void ThreadContext::DeferredOperations::AddDelayedDelete(ResourceLocator&& locator)
    {
        _delayedDeletes.push_back(std::move(locator));
    }

    void ThreadContext::DeferredOperations::CommitToImmediate_PreCommandList(RenderCore::IThreadContext& immContext)
    {
        // D3D11 has some issues with mapping and writing to linear buffers from a background thread
        // we get around this by defering some write operations to the main thread, at the point
        // where we commit the command list to the device
        if (!_deferredCopies.empty()) {
            PlatformInterface::ResourceUploadHelper immediateContext(immContext);
            for (const auto&copy:_deferredCopies)
                immediateContext.WriteToBufferViaMap(copy._destination, copy._resourceDesc, 0, MakeIteratorRange(copy._temporaryBuffer));
            _deferredCopies.clear();
        }
    }

    void ThreadContext::DeferredOperations::CommitToImmediate_PostCommandList(RenderCore::IThreadContext& immContext)
    {
        if (!_deferredDefragCopies.empty()) {
            PlatformInterface::ResourceUploadHelper immediateContext(immContext);
            for (auto i=_deferredDefragCopies.begin(); i!=_deferredDefragCopies.end(); ++i)
                immediateContext.ResourceCopy_DefragSteps(i->_destination, i->_source, i->_steps);
            _deferredDefragCopies.clear();
        }
    }

    bool ThreadContext::DeferredOperations::IsEmpty() const 
    {
        return _deferredCopies.empty() && _deferredDefragCopies.empty() && _delayedDeletes.empty();
    }

    void ThreadContext::DeferredOperations::swap(DeferredOperations& other)
    {
        _deferredCopies.swap(other._deferredCopies);
        _deferredDefragCopies.swap(other._deferredDefragCopies);
        _delayedDeletes.swap(other._delayedDeletes);
    }

    ThreadContext::DeferredOperations::DeferredOperations()
    {
    }

    ThreadContext::DeferredOperations::~DeferredOperations()
    {
    }

}
