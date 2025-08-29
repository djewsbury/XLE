// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../OSServices/TimeUtils.h"
#include "../IteratorUtils.h"
#include "../Threading/Mutex.h"
#include "../Threading/ThreadingUtils.h"
#include "../../Core/Types.h"
#include <vector>
#include <assert.h>
#include <functional>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    typedef union _LARGE_INTEGER LARGE_INTEGER;
    extern "C" __declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER *);
#endif

namespace Utility
{
    class IHierarchicalProfiler
    {
    public:
        using RawEventData = IteratorRange<const void*>;
        using EventListener = std::function<void(RawEventData)>;
        using ListenerId = unsigned;
        ListenerId      AddEventListener(const EventListener& callback);
        void            RemoveEventListener(ListenerId id);

        using EventId = unsigned;

        class ResolvedEvent
        {
        public:
            const char* _label;
            uint64      _inclusiveTime;
            uint64      _exclusiveTime;
            unsigned    _eventCount;

            typedef unsigned Id;
            static const unsigned s_id_Invalid = ~Id(0x0);
            Id          _firstChild;
            Id          _sibling;
        };

        static std::vector<ResolvedEvent> CalculateResolvedEvents(RawEventData);

        IHierarchicalProfiler();
        ~IHierarchicalProfiler();
    protected:
        void Publish(RawEventData);

        Threading::Mutex _listenersMutex;
        std::vector<std::pair<ListenerId, EventListener>> _listeners;
        ListenerId _nextListenerId;
    };

    /// <summary>Hierarchical CPU call Profiler</summary>
    /// This is a light weight profiler that can give a reasonably
    /// accurate profile of CPU events.
    ///
    /// Profiling occurs by registering begin and end events associated
    /// with labels. When interpret these hierarchically, like a call
    /// stack. We expect socks & shoes type behaviour for these events
    /// (ie, first on, last off).
    ///
    /// We use a pointer to a string constant literal to look for equality
    /// between profiler labels. This turns out to be just incredibly convenient
    /// (but relies on the string pooling compiler setting).
    ///
    /// When calling BeginEvent or EndEvent, you should use a static literal
    /// string (or, at least, some pointer that will permanently point to a
    /// valid string). Normally this should look like:
    ///
    ///     <code>\code
    ///         HierarchicalCPUProfiler& profiler = ...;
    ///         auto id = profiler.BeginEvent("RenderFrame");
    ///         RenderFrame(); // (something that takes time)
    ///         profiler.EndEvent(id);
    ///     \endcode</code>
    ///
    /// Above, the string literal ("RenderFrame") will evaluate to the same pointer
    /// where ever it appears in the code. So we can compare those pointer when
    /// we want to check events for equivalence.
    ///
    /// Above, EndEvent() could be implemented to take a return code from BeginEvent,
    /// or it could use the same literal. Using the same literal might be slightly
    /// more efficient. However, I've decided to use an id -- this enables use to
    /// query the cost of a specific event. Querying by a string label would give
    /// use the cost of every event using the same label. But the id allows us to
    /// target a specific instance.
    ///
    /// The profiler will minimize it's cost during profiling, even if that means that
    /// interpreting the results afterwards is a little more expensive.
    ///
    /// This has 2 advantages:
    /// <list>
    ///   <item> We want to avoid distorting the profile while
    ///     we're profiling. If the profiler itself changes the
    ///     cost of functions, the results won't be perfectly
    ///     accurate. Similarly, if the performance of the CPU
    ///     relative to the GPU changes, it can change the profile.
    ///     So we want to avoid this at all costs!
    ///   <item> We can profile many items. Sometimes we want to
    ///     create a profile label in the middle of a loop, or some
    ///     frequently called function. This is only possible if the
    ///     profiler overhead is really low. So we need to limit the
    ///     overhead to the absolute minimum.
    /// </list>
    ///
    /// The overhead is small enough that enabling/disabling profiling
    /// with a condition is too expensive. So profiling can only be
    /// disabled at compile time.
    ///
    /// This is intended to be used on a single thread. When profiling
    /// multiple threads, use multiple instances.
    ///
    /// I've written variations of this class so many times! But this
    /// one is open-source. It's forever!
    class HierarchicalCPUProfiler : public IHierarchicalProfiler
    {
    public:
        EventId     BeginEvent(const char eventLiteral[]);
        void        EndEvent(EventId eventId);
        void            FrameBarrier();
        RawEventData    EndProfiling();

        uint64_t    GetAverageFrameInterval(unsigned windowFrameCount = ~0u);

        HierarchicalCPUProfiler();
        ~HierarchicalCPUProfiler();
    private:
        static const unsigned s_bufferCount = 2;
        std::vector<uint64> _events[s_bufferCount];

        uint32 _workingId;
        uint32 _idAtEventsStart[s_bufferCount];

        uint64_t _frameMarkers[64];
        unsigned _frameMarkerNext, _frameMarkerCount;

        #if !defined(NDEBUG)
            Threading::ThreadId _threadId;
            static const unsigned s_maxStackDepth = 16;
            uint32 _aeStack[s_maxStackDepth];
            uint32 _aeStackI;
        #endif
    };

    inline unsigned HierarchicalCPUProfiler::BeginEvent(const char eventLiteral[])
    {
        assert(Threading::CurrentThreadId() == _threadId);
        uint64 time;
        #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
                // special case inlined version for Windows API platforms
                // provides a little more performance, by avoiding one unnecessary
                // function call
            QueryPerformanceCounter((LARGE_INTEGER*)&time);
        #else
            time = OSServices::GetPerformanceCounter();
        #endif
            //  We use the very top bit to distinguish between a begin event, and an end event.
            //  This means the results will not be correct if the profile event straddles a time
            //  when the top bit changes. But that seems extremely unlikely.
        _events[0].push_back(~(1ull << 63ull) & time);
        _events[0].push_back(uint64(eventLiteral));     // should be ok for 32 or 64bit modes (but not 128bit+)!
        auto result = _workingId++;
        #if !defined(NDEBUG)
            assert(_aeStackI < dimof(_aeStack));
            _aeStack[_aeStackI++] = result;
        #endif
        return result;
    }

    inline void HierarchicalCPUProfiler::EndEvent(unsigned eventId)
    {
        assert(Threading::CurrentThreadId() == _threadId);
        uint64 time;
        #if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
            QueryPerformanceCounter((LARGE_INTEGER*)&time);
        #else
            time = OSServices::GetPerformanceCounter();
        #endif
        #if !defined(NDEBUG)
            assert(_aeStackI > 0);
            assert(_aeStack[_aeStackI-1] == eventId);   // verify that this is the right event we're removing
            --_aeStackI;
        #endif
        _events[0].push_back((1ull << 63ull) | time);
    }

    /// <summary>Begin and end a profiler event</summary>
    /// Begin a CPU profiler event, and then end it after this object
    /// leaves scope. Use this to manage CPU profiler events in a
    /// RAII-friendly way.
    class CPUProfileEvent
    {
    public:
        HierarchicalCPUProfiler::EventId GetId() { return _id; }

        CPUProfileEvent(const char label[], HierarchicalCPUProfiler& profiler)
        : _profiler(&profiler)
        {
            _id = _profiler->BeginEvent(label);
        }

        CPUProfileEvent() : _id(~0u), _profiler(nullptr) {}

        ~CPUProfileEvent()
        {
            if (_id != ~0u)
                _profiler->EndEvent(_id);
        }

        CPUProfileEvent(CPUProfileEvent&& moveFrom) never_throws
        : _profiler(moveFrom._profiler), _id(moveFrom._id)
        {
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
        }

        CPUProfileEvent& operator=(CPUProfileEvent&& moveFrom) never_throws
        {
            if (_id != ~0u)
                _profiler->EndEvent(_id);

            _profiler = moveFrom._profiler;
            _id = moveFrom._id;
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
            return *this;
        }

    private:
        HierarchicalCPUProfiler* _profiler;
        HierarchicalCPUProfiler::EventId _id;
    };

    class CPUProfileEvent_Conditional
    {
    public:
        HierarchicalCPUProfiler::EventId GetId() { return _id; }

        CPUProfileEvent_Conditional(const char label[], HierarchicalCPUProfiler* profiler)
        : _profiler(profiler)
        {
            if (_profiler) {
                _id = _profiler->BeginEvent(label);
            } else {
                _id = ~HierarchicalCPUProfiler::EventId(0x0ull);
            }
        }

        CPUProfileEvent_Conditional() : _id(~0u), _profiler(nullptr) {}

        ~CPUProfileEvent_Conditional()
        {
            if (_id != ~0u)
                _profiler->EndEvent(_id);
        }

        CPUProfileEvent_Conditional(CPUProfileEvent_Conditional&& moveFrom) never_throws
        : _profiler(moveFrom._profiler), _id(moveFrom._id)
        {
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
        }

        CPUProfileEvent_Conditional& operator=(CPUProfileEvent_Conditional&& moveFrom) never_throws
        {
            if (_id != ~0u)
                _profiler->EndEvent(_id);

            _profiler = moveFrom._profiler;
            _id = moveFrom._id;
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
            return *this;
        }

    private:
        HierarchicalCPUProfiler* _profiler;
        HierarchicalCPUProfiler::EventId _id;
    };
}

using namespace Utility;
