// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ThreadContext.h"
#include "../RenderCore/IThreadContext.h"
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../OSServices/TimeUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/HeapUtils.h"

namespace BufferUploads
{
    void ThreadContext::BeginCommandList()
    {
        // note -- this will automatically begin a command list for us
        RenderCore::Metal::DeviceContext::Get(*_underlyingContext);
    }

    void ThreadContext::ResolveCommandList()
    {
        int64_t currentTime = OSServices::GetPerformanceCounter();
        CommandList newCommandList;
        newCommandList._metrics = _commandListUnderConstruction;
        newCommandList._metrics._resolveTime = currentTime;
        newCommandList._metrics._processingEnd = currentTime;
        newCommandList._id = _commandListIDUnderConstruction;

        if (!_isImmediateContext) {
            newCommandList._deviceCommandList = RenderCore::Metal::DeviceContext::Get(*_underlyingContext)->ResolveCommandList();
            newCommandList._commitStep.swap(_commitStepUnderConstruction);
            _queuedCommandLists.push_overflow(std::move(newCommandList));
        } else {
                    // immediate resolve -- skip the render thread resolve step...
            _commitStepUnderConstruction.CommitToImmediate_PreCommandList(*_underlyingContext);
            _commitStepUnderConstruction.CommitToImmediate_PostCommandList(*_underlyingContext);
            _commandListIDCommittedToImmediate = std::max(_commandListIDCommittedToImmediate, _commandListIDUnderConstruction);

            newCommandList._metrics._frameId = _underlyingContext->GetStateDesc()._frameId;
            newCommandList._metrics._commitTime = currentTime;
            #if defined(XL_BUFFER_UPLOAD_RECORD_THREAD_CONTEXT_METRICS)
                while (!_recentRetirements.push(newCommandList._metrics)) {
                    _recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
                }
            #endif
        }

        _commandListUnderConstruction = CommandListMetrics();
        _commandListUnderConstruction._processingStart = currentTime;
        CommitStep().swap(_commitStepUnderConstruction);
        ++_commandListIDUnderConstruction;
    }

    void ThreadContext::CommitToImmediate(
        RenderCore::IThreadContext& commitTo,
        LockFreeFixedSizeQueue<unsigned, 4>* framePriorityQueue)
    {
        if (_isImmediateContext) {
            ++_commitCountCurrent;
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

            CommandList* commandList = 0;
            while (_queuedCommandLists.try_front(commandList)) {
                TimeMarker stallEnd = OSServices::GetPerformanceCounter();
                if (!gotStart) {
                    commitTo.GetAnnotator().Event("BufferUploads", RenderCore::IAnnotator::EventTypes::MarkerBegin);
                    gotStart = true;
                }

                commandList->_commitStep.CommitToImmediate_PreCommandList(commitTo);
                if (commandList->_deviceCommandList)
                    immContext->ExecuteCommandList(std::move(*commandList->_deviceCommandList));
                commandList->_commitStep.CommitToImmediate_PostCommandList(commitTo);
                _commandListIDCommittedToImmediate = std::max(_commandListIDCommittedToImmediate, commandList->_id);
            
                commandList->_metrics._frameId                  = commitTo.GetStateDesc()._frameId;
                commandList->_metrics._commitTime               = OSServices::GetPerformanceCounter();
                commandList->_metrics._framePriorityStallTime   = stallEnd - stallStart;    // this should give us very small numbers, when we're not actually stalling for frame priority commits
                #if defined(XL_BUFFER_UPLOAD_RECORD_THREAD_CONTEXT_METRICS)
                    while (!_recentRetirements.push(commandList->_metrics))
                        _recentRetirements.pop();   // note -- this might violate the single-popping-thread rule!
                #endif
                _queuedCommandLists.pop();

                stallStart = OSServices::GetPerformanceCounter();                
            }
                
            if (!currentlyUncommitedFramePriorityCommandLists)
                break;

            Threading::YieldTimeSlice();
        }

        if (gotStart) {
            commitTo.GetAnnotator().Event("BufferUploads", RenderCore::IAnnotator::EventTypes::MarkerEnd);
        }
        
        ++_commitCountCurrent;
    }

    CommandListMetrics ThreadContext::PopMetrics()
    {
        #if defined(XL_BUFFER_UPLOAD_RECORD_THREAD_CONTEXT_METRICS)
            CommandListMetrics* ptr;
            if (_recentRetirements.try_front(ptr)) {
                CommandListMetrics result = *ptr;
                _recentRetirements.pop();
                return result;
            }
        #endif
        return CommandListMetrics();
    }

    IManager::EventListID   ThreadContext::EventList_GetWrittenID() const
    {
        return _currentEventListId;
    }

    IManager::EventListID   ThreadContext::EventList_GetPublishedID() const
    {
        return _currentEventListPublishedId;
    }

    IManager::EventListID   ThreadContext::EventList_GetProcessedID() const
    {
        return _currentEventListProcessedId;
    }

    void                    ThreadContext::EventList_Get(IManager::EventListID id, Event_ResourceReposition*& begin, Event_ResourceReposition*& end)
    {
        begin = end = nullptr;
        if (!id) return;
        for (unsigned c=0; c<dimof(_eventBuffers); ++c) {
            if (_eventBuffers[c]._id == id) {
                ++_eventBuffers[c]._clientReferences;
                    //  have to check again after the increment... because the client references value acts
                    //  as a lock.
                if (_eventBuffers[c]._id == id) {
                    begin = &_eventBuffers[c]._evnt;
                    end = begin+1;
                } else {
                    --_eventBuffers[c]._clientReferences;
                        // in this case, the event has just be freshly overwritten
                }
                return;
            }
        }
    }

    void                    ThreadContext::EventList_Release(IManager::EventListID id, bool silent)
    {
        if (!id) return;
        for (unsigned c=0; c<dimof(_eventBuffers); ++c) {
            if (_eventBuffers[c]._id == id) {
                auto newValue = --_eventBuffers[c]._clientReferences;
                assert(signed(newValue) >= 0);
                    
                if (!silent) {
                    for (;;) {      // lock-free max...
                        auto originalProcessedId = _currentEventListProcessedId.load();
                        auto newProcessedId = std::max(originalProcessedId, (IManager::EventListID)_eventBuffers[c]._id);
                        if (_currentEventListProcessedId.compare_exchange_strong(originalProcessedId, newProcessedId))
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
        if (!_eventBuffers[_eventListWritingIndex]._clientReferences.load()) {
            IManager::EventListID id = ++_currentEventListId;
            _eventBuffers[_eventListWritingIndex]._id = id;
            _eventBuffers[_eventListWritingIndex]._evnt = evnt;
            _eventListWritingIndex = (_eventListWritingIndex+1)%dimof(_eventBuffers);   // single writing thread, so it's ok
            return id;
        }
        assert(0);
        return ~IManager::EventListID(0x0);
    }

    void ThreadContext::EventList_Publish(IManager::EventListID toEvent)
    {
        _currentEventListPublishedId = toEvent;
    }
   
    void ThreadContext::OnLostDevice()
    {
        for (unsigned c=0; c<dimof(_eventBuffers); ++c) {
                //      Clear out these pointers because the things they point to have probably
                //      been destroyed.
            _eventBuffers[c]._evnt._newResource = 0;
            _eventBuffers[c]._evnt._originalResource = 0;
            _eventBuffers[c]._evnt._defragSteps.clear();
            _eventBuffers[c]._id = 0;
        }
    }

    ThreadContext::ThreadContext(std::shared_ptr<RenderCore::IThreadContext> underlyingContext) 
    : _resourceUploadHelper(*underlyingContext)
    , _currentEventListId(0), _eventListWritingIndex(0), _currentEventListProcessedId(0)
    , _currentEventListPublishedId(0)
    {
        _underlyingContext = std::move(underlyingContext);
        _lastResolve = 0;
        _commitCountCurrent = _commitCountLastResolve = 0;
        _isImmediateContext = _underlyingContext->IsImmediate();
        _commandListIDUnderConstruction = 1;
        _commandListIDCommittedToImmediate = 0;
    }

    ThreadContext::~ThreadContext()
    {
    }

        //////////////////////////////////////////////////////////////////////////////////////////////

    CommitStep::DeferredDefragCopy::DeferredDefragCopy(
		std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<DefragStep>& steps)
    : _destination(std::move(destination)), _source(std::move(source)), _steps(steps)
    {}

    CommitStep::DeferredDefragCopy::~DeferredDefragCopy()
    {}

    void CommitStep::Add(CommitStep::DeferredCopy&& copy)
    {
        _deferredCopies.push_back(std::forward<CommitStep::DeferredCopy>(copy));
    }

    void CommitStep::Add(CommitStep::DeferredDefragCopy&& copy)
    {
        _deferredDefragCopies.push_back(std::forward<CommitStep::DeferredDefragCopy>(copy));
    }

    void CommitStep::AddDelayedDelete(ResourceLocator&& locator)
    {
        _delayedDeletes.push_back(std::move(locator));
    }

    void CommitStep::CommitToImmediate_PreCommandList(RenderCore::IThreadContext& immContext)
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

    void CommitStep::CommitToImmediate_PostCommandList(RenderCore::IThreadContext& immContext)
    {
        if (!_deferredDefragCopies.empty()) {
            PlatformInterface::ResourceUploadHelper immediateContext(immContext);
            for (auto i=_deferredDefragCopies.begin(); i!=_deferredDefragCopies.end(); ++i)
                immediateContext.ResourceCopy_DefragSteps(i->_destination, i->_source, i->_steps);
            _deferredDefragCopies.clear();
        }
    }

    bool CommitStep::IsEmpty() const 
    {
        return _deferredCopies.empty() && _deferredDefragCopies.empty() && _delayedDeletes.empty();
    }

    void CommitStep::swap(CommitStep& other)
    {
        _deferredCopies.swap(other._deferredCopies);
        _deferredDefragCopies.swap(other._deferredDefragCopies);
        _delayedDeletes.swap(other._delayedDeletes);
    }

    CommitStep::CommitStep()
    {
    }

    CommitStep::~CommitStep()
    {
    }

}
