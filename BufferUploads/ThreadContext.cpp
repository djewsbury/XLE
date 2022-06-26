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
            LockFreeFixedSizeQueue<CommandListMetrics, 256> _recentRetirements;
        #endif
        bool _isImmediateContext;

        TimeMarker  _lastResolve;
        unsigned    _commitCountCurrent, _commitCountLastResolve;

        CommandListID _commandListIDUnderConstruction, _commandListIDCommittedToImmediate;

        std::shared_ptr<RenderCore::Metal_Vulkan::IAsyncTracker> _asyncTracker;
        std::unique_ptr<PlatformInterface::StagingPage> _stagingPage;

        unsigned _immediateContextLastFrameId = 0;
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

            newCommandList._metrics._frameId = _pimpl->_immediateContextLastFrameId+1;  // ie, assume it's just the next one after the last call to CommitToImmediate()
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
        unsigned frameId,
        LockFreeFixedSizeQueue<unsigned, 4>* framePriorityQueue)
    {
        if (_pimpl->_isImmediateContext) {
            assert(&commitTo == _underlyingContext.get());
            ++_pimpl->_commitCountCurrent;
            _pimpl->_immediateContextLastFrameId = frameId;
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
            
                commandList->_metrics._frameId                  = frameId;
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


    CommandListID           ThreadContext::CommandList_GetUnderConstruction() const        { return _pimpl->_commandListIDUnderConstruction; }
    CommandListID           ThreadContext::CommandList_GetCommittedToImmediate() const     { return _pimpl->_commandListIDCommittedToImmediate; }

    CommandListMetrics&     ThreadContext::GetMetricsUnderConstruction()                   { return _pimpl->_commandListUnderConstruction; }

    auto                    ThreadContext::GetDeferredOperationsUnderConstruction() -> DeferredOperations&        { return _pimpl->_deferredOperationsUnderConstruction; }

    unsigned                ThreadContext::CommitCount_Current()                           { return _pimpl->_commitCountCurrent; }
    unsigned&               ThreadContext::CommitCount_LastResolve()                       { return _pimpl->_commitCountLastResolve; }

    PlatformInterface::StagingPage&     ThreadContext::GetStagingPage()
    {
        assert(_pimpl->_stagingPage);
        return *_pimpl->_stagingPage;
    }

    PlatformInterface::QueueMarker      ThreadContext::GetProducerQueueMarker()
    {
        if (_pimpl->_asyncTracker) return _pimpl->_asyncTracker->GetProducerMarker();
        return 0;
    }

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

        if (!_pimpl->_isImmediateContext) {
            const unsigned stagingPageSize = 64*1024*1024;
            _pimpl->_stagingPage = std::make_unique<PlatformInterface::StagingPage>(*_underlyingContext->GetDevice(), stagingPageSize);
        }

        auto* deviceVulkan = (RenderCore::IDeviceVulkan*)_underlyingContext->GetDevice()->QueryInterface(typeid(RenderCore::IDeviceVulkan).hash_code());
        if (deviceVulkan)
            _pimpl->_asyncTracker = deviceVulkan->GetAsyncTracker();
    }

    ThreadContext::~ThreadContext()
    {
    }

        //////////////////////////////////////////////////////////////////////////////////////////////

    ThreadContext::DeferredOperations::DeferredDefragCopy::DeferredDefragCopy(
		std::shared_ptr<IResource> destination, std::shared_ptr<IResource> source, const std::vector<RepositionStep>& steps)
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
                immediateContext.WriteViaMap(copy._destination, MakeIteratorRange(copy._temporaryBuffer));
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
