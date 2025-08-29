// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CPUProfiler.h"
#include "../../OSServices/TimeUtils.h"
#include "../MemoryUtils.h"
#include "../PtrUtils.h"
#include <algorithm>
#include <queue>
#include <stack>

namespace Utility
{
    struct ParentAndChildLink
    {
        const uint64* _parent;
        const uint64* _child;
        const char* _label;
        unsigned _id;

        uint64 _resolvedInclusiveTime;
        uint64 _resolvedChildrenTime;
    };

    static bool CompareParentAndChildLink(const ParentAndChildLink& lhs, const ParentAndChildLink& rhs)
    {
            //  Sort by parent first, and then label, and finally child
            //  This will resolve in a breadth-first ordering, and all
            //  children with the same label will be sequential.
            //  Note that by sorting by label will knock the output out
            //  of chronological order.
        if (lhs._parent == rhs._parent) {
            if (lhs._label == rhs._label) {
                return lhs._child < rhs._child;
            }
            return lhs._label < rhs._label;
        }
        return lhs._parent < rhs._parent;
    }

    struct CompareParent
    {
        bool operator()(const ParentAndChildLink& lhs, const uint64* rhs) { return lhs._parent < rhs; }
        bool operator()(const uint64* lhs, const ParentAndChildLink& rhs) { return lhs < rhs._parent; }
        bool operator()(const ParentAndChildLink& lhs, const ParentAndChildLink& rhs) { return lhs._parent < rhs._parent; }
    };

    static IHierarchicalProfiler::ResolvedEvent BlankEvent(const char label[])
    {
        HierarchicalCPUProfiler::ResolvedEvent evnt;
        evnt._label = label;
        evnt._inclusiveTime = evnt._exclusiveTime = 0;
        evnt._eventCount = 0;
        evnt._firstChild = HierarchicalCPUProfiler::ResolvedEvent::s_id_Invalid;
        evnt._sibling = HierarchicalCPUProfiler::ResolvedEvent::s_id_Invalid;
        return evnt;
    }

    auto IHierarchicalProfiler::CalculateResolvedEvents(RawEventData rawEvents) -> std::vector<ResolvedEvent>
    {
            //  First, we need to rearrange the call stack in a
            //  breath-first hierarchy order (sortable by label)
            //  This requires iterating through the entire list of events.
            //  Once it's in breath-first order, it should become
            //  much easier to do the next few operations.
        std::vector<ParentAndChildLink> parentsAndChildren;
        IteratorRange<const uint64_t*> events((const uint64_t*)rawEvents.begin(), (const uint64_t*)rawEvents.end());
        parentsAndChildren.reserve(events.size()/2);    // Approximation of events count

        unsigned workingStack[32];
        unsigned _workingStackDepth = 0;
        auto i=events.cbegin();
        auto workingId = *i++;

        for (; i!=events.cend(); ++i) {
            uint64 time = *i;
            if (time & (1ull << 63ull)) {

                    //  This is an "end" event. We can resolve the last
                    //  event in the stack. If there's nothing on the stack,
                    //  then we've popped too many times!

                time &= ~(1ull << 63ull);
                if (!_workingStackDepth) {
                    UNREACHABLE();
                } else {
                    auto entryIndex = workingStack[_workingStackDepth-1];
                    assert(entryIndex < parentsAndChildren.size());
                    auto& entry = parentsAndChildren[entryIndex];

                    uint64 startTime = *entry._child;
                    assert(time >= startTime);
                    uint64 inclusiveTime = time - startTime;
                    entry._resolvedInclusiveTime = inclusiveTime;
                    --_workingStackDepth;

                    if (_workingStackDepth > 0) {
                        auto parentIndex = workingStack[_workingStackDepth-1];
                        assert(parentIndex < parentsAndChildren.size());
                        auto& parentEntry = parentsAndChildren[parentIndex];
                        parentEntry._resolvedChildrenTime += entry._resolvedInclusiveTime;
                    }
                }

            } else {

                assert((_workingStackDepth+1) <= dimof(workingStack));

                    // create a new parent and child link, and add to our list
                ParentAndChildLink link;
                link._parent = nullptr;
                if (_workingStackDepth > 0) {
                    link._parent = parentsAndChildren[workingStack[_workingStackDepth-1]]._child;
                }
                link._child = AsPointer(i);
                link._label = (const char*)*(i+1);
                link._id = (unsigned)workingId++;
                link._resolvedChildrenTime = link._resolvedInclusiveTime = 0;
                ++i;

                workingStack[_workingStackDepth++] = (unsigned)parentsAndChildren.size();
                parentsAndChildren.push_back(link);

            }
        }

            //  Sort into breadth first order. Note that it really is breadth first order.
            //  the first sorting term is a pointer into the event list, and this will
            //  always work out to put children immediately after their parent.
        std::sort(parentsAndChildren.begin(), parentsAndChildren.end(), CompareParentAndChildLink);

            //  Now we have to go through and generate breadth-first output
            //  While doing this, we'll also combine multiple calls to the same label
            //  into one. It's difficult to do this before the sorting step. In theory, it
            //  might be possible, but would probably require extra restrictions and bookkeeping.
        std::vector<ResolvedEvent> result;
        result.reserve(parentsAndChildren.size());

        class PreResolveEvent
        {
        public:
            ResolvedEvent::Id _parentOutput;
            const uint64* _parentLinkSearch;
        };

        std::queue<PreResolveEvent> finalResolveQueue;
        // finalResolveQueue.reserve(s_maxStackDepth);

        auto lastRootEventOutputId = ResolvedEvent::s_id_Invalid;

            //  For each root event, we must start the tree, and queue the children
            //  Note that we're not merging root events here! Merging occurs for every
            //  other events, just not the root events
        for (auto root=parentsAndChildren.cbegin(); root<parentsAndChildren.cend() && root->_parent==nullptr; ++root) {
            auto outputId = (ResolvedEvent::Id)result.size();
            if (lastRootEventOutputId != ResolvedEvent::s_id_Invalid) {
                    // attach the new root event as a sibling of the last one added
                result[lastRootEventOutputId]._sibling = outputId;
            }

            ResolvedEvent rootEvent;
            rootEvent._label = root->_label;
            rootEvent._inclusiveTime = root->_resolvedInclusiveTime;
            rootEvent._exclusiveTime = root->_resolvedInclusiveTime - root->_resolvedChildrenTime;
            rootEvent._eventCount = 1;
            rootEvent._firstChild = ResolvedEvent::s_id_Invalid;
            rootEvent._sibling = ResolvedEvent::s_id_Invalid;
            result.push_back(rootEvent);

            PreResolveEvent queuedEvent;
            queuedEvent._parentOutput = outputId;
            queuedEvent._parentLinkSearch = root->_child;

            finalResolveQueue.push(queuedEvent);
            lastRootEventOutputId = outputId;
        }

            //  While we have children in our "finalResolveQueue", we need to go
            //  through and turn them into ResolveEvents.
            //
            //  During this phase, we also need to do mergers. When we encounter
            //  multiple siblings with the same label, they must be merged into
            //  a single ResolvedEvent.
            //
            //  We must also merge children of events in the same way. The entire
            //  hierarchy of the merge labels will be collapsed together, and where
            //  identical labels occur at the same tree depth, they get merged to
            //  become a single event.
        while (!finalResolveQueue.empty()) {
            const auto w = finalResolveQueue.front();
            finalResolveQueue.pop();
            auto& parentOutput = result[w._parentOutput];

            auto childrenRange = std::equal_range(
                parentsAndChildren.cbegin(), parentsAndChildren.cend(), w._parentLinkSearch, CompareParent());

            auto childIterator = childrenRange.first;
            while (childIterator < childrenRange.second) {
                auto mergedChildStart = childIterator;
                auto mergedChildEnd = mergedChildStart+1;
                while (mergedChildEnd < childrenRange.second && mergedChildEnd->_label == mergedChildStart->_label) { ++mergedChildEnd; }

                    //  All of these children will be collapsed into a single
                    //  resolved event. But first we need to check if there
                    //  is already a ResolvedEvent attached to the same parent,
                    //  with the same label;
                auto existingChildIterator = parentOutput._firstChild;
                auto lastSibling = ResolvedEvent::s_id_Invalid;
                while (existingChildIterator != ResolvedEvent::s_id_Invalid) {
                    lastSibling = existingChildIterator;
                    if (result[existingChildIterator]._label == mergedChildStart->_label) {
                        break;
                    }
                    existingChildIterator = result[existingChildIterator]._sibling;
                }

                if (existingChildIterator == ResolvedEvent::s_id_Invalid) {
                        //  It doesn't exist. Create a new event, and attach it to the last sibling
                        //  of the parent
                    ResolvedEvent newEvent = BlankEvent(mergedChildStart->_label);
                    existingChildIterator = (ResolvedEvent::Id)result.size();
                    result.push_back(newEvent);
                    if (lastSibling != ResolvedEvent::s_id_Invalid) {
                        result[lastSibling]._sibling = existingChildIterator;
                    } else {
                        parentOutput._firstChild = existingChildIterator;
                    }
                }

                    //  Now either we've created a new event, or we're merging into an existing one.
                auto& dstEvent = result[existingChildIterator];
                dstEvent._eventCount += signed(mergedChildEnd - mergedChildStart);
                for (auto c = mergedChildStart; c<mergedChildEnd; ++c) {
                    dstEvent._inclusiveTime += c->_resolvedInclusiveTime;
                    dstEvent._exclusiveTime += c->_resolvedInclusiveTime - c->_resolvedChildrenTime;

                        //  This item becomes a new parent. We need to push in the
                        //  resolve operations for all of the children
                    PreResolveEvent queuedEvent;
                    queuedEvent._parentOutput = existingChildIterator;
                    queuedEvent._parentLinkSearch = c->_child;
                    finalResolveQueue.push(queuedEvent);
                }

                childIterator = mergedChildEnd;
            }
        }

        return result;
    }

    auto    IHierarchicalProfiler::AddEventListener(const EventListener& callback) -> ListenerId
    {
        ScopedLock(_listenersMutex);
        auto id = _nextListenerId++;
        _listeners.push_back(std::make_pair(id, callback));
        return id;
    }

    void    IHierarchicalProfiler::RemoveEventListener(ListenerId id)
    {
        ScopedLock(_listenersMutex);
        auto i = std::find_if(_listeners.begin(), _listeners.end(),
            [id](const std::pair<unsigned, EventListener>& p) { return p.first == id; });
        if (i != _listeners.end())
            _listeners.erase(i);
    }

    void    IHierarchicalProfiler::Publish(RawEventData rawData)
    {
        ScopedLock(_listenersMutex);
        for (const auto&listener:_listeners)
            listener.second(rawData);
    }

    IHierarchicalProfiler::IHierarchicalProfiler()
    {
        _nextListenerId = 1;
    }

    IHierarchicalProfiler::~IHierarchicalProfiler()
    {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    auto HierarchicalCPUProfiler::EndProfiling() -> RawEventData
    {
        // very similar to FrameBarrier(), we just return the final event data
        assert(Threading::CurrentThreadId() == _threadId);
        assert(_aeStackI==0);
        static_assert(s_bufferCount > 1, "Expecting at least 2 buffers");

        _frameMarkers[_frameMarkerNext] = OSServices::GetPerformanceCounter();
        _frameMarkerCount = std::min(_frameMarkerCount+1, (unsigned)dimof(_frameMarkers));
        _frameMarkerNext = (_frameMarkerNext+1) % (unsigned)dimof(_frameMarkers);

        std::swap(_events[0], _events[1]);  // (actually only the first 2 would be used)
        std::swap(_idAtEventsStart[0], _idAtEventsStart[1]);
        _idAtEventsStart[0] = _workingId;

            // erase without deleting memory
        _events[0].erase(_events[0].begin(), _events[0].end());
        _events[0].push_back(_idAtEventsStart[0]);
        assert(_aeStackI == 0);

        return MakeIteratorRange(_events[1]);
    }

    void HierarchicalCPUProfiler::FrameBarrier()
    {
        // publish the results to the listeners
        Publish(EndProfiling());
    }

    uint64_t HierarchicalCPUProfiler::GetAverageFrameInterval(unsigned windowFrameCount)
    {
        const auto limit = (unsigned)dimof(_frameMarkers);
        unsigned markerCount = (windowFrameCount == ~0u) ? _frameMarkerCount : std::min(windowFrameCount+1, _frameMarkerCount);
        if (markerCount < 2) return 0;
        
        unsigned iterator = (_frameMarkerNext + limit - markerCount) % limit;
        assert(iterator < dimof(_frameMarkers));
        auto prev = _frameMarkers[iterator];
        iterator = (iterator+1) % limit;
        
        uint64_t accumulator = 0;
        for (unsigned c=0; c<(markerCount-1); ++c) {
            assert(iterator < dimof(_frameMarkers));
            auto t = _frameMarkers[iterator];
            accumulator += t - prev;
            prev = t;
            iterator = (iterator+1) % limit;
        }
        
        assert(iterator == _frameMarkerNext);
        return accumulator / (markerCount-1);
    }

    HierarchicalCPUProfiler::HierarchicalCPUProfiler()
    {
        _workingId = 0;
        for (unsigned c=0; c<s_bufferCount; ++c) {
            _events[c].reserve(16 * 1024);
            _idAtEventsStart[c] = _workingId;
        }

        _events[0].push_back(_idAtEventsStart[0]);

        _frameMarkerCount = _frameMarkerNext = 0;

        #if !defined(NDEBUG)
            _threadId = Threading::CurrentThreadId();
            XlZeroMemory(_aeStack);
            _aeStackI = 0;
        #endif
    }

    HierarchicalCPUProfiler::~HierarchicalCPUProfiler()
    {
    }
}

