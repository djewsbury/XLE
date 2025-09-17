#pragma once

#include "../Threading/ThreadingUtils.h"
#include "../IteratorUtils.h"
#include <vector>
#include <assert.h>

namespace Utility
{
////////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Profile features by disabling them in controlled circumstances</summary>
    /// Simple profiler that can record event types, and allows them to suppressed.
    /// This is useful for profiling when there are no other options -- by comparing results with
    /// given a feature in both an enabled and disabled state, we can get a sense of that feature's
    /// cost
    class SuppressionProfiler
    {
    public:
        using EventId = unsigned;
        using EventLiteral = const char*;

        EventId     BeginEvent(EventLiteral eventLiteral);
        void        EndEvent(EventId eventId);
        void        EndFrame();

        void        SuppressEvent(EventLiteral eventLiteral);
        void        DesuppressEvent(EventLiteral eventLiteral);

        bool        RecordEventTypes(bool enableFlag) { bool prev = _isRecordingKnownEvents; _isRecordingKnownEvents = enableFlag; return prev; }
        IteratorRange<const EventLiteral*> KnownEvents() const { return MakeIteratorRange(_knownEvents); }

        SuppressionProfiler();
        ~SuppressionProfiler();
    private:
        std::vector<EventLiteral> _suppressedEvents;
        uint32_t _workingId;

        std::vector<EventLiteral> _knownEvents;
        bool _isRecordingKnownEvents;

        #if !defined(NDEBUG)
            Threading::ThreadId _threadId;
            static const unsigned s_maxStackDepth = 16;
            uint32_t _aeStack[s_maxStackDepth];
            uint32_t _aeStackI;
        #endif
    };
    
    inline unsigned SuppressionProfiler::BeginEvent(const char eventLiteral[])
    {
        assert(Threading::CurrentThreadId() == _threadId);
        auto i = std::lower_bound(_suppressedEvents.begin(), _suppressedEvents.end(), eventLiteral);
        if (i != _suppressedEvents.end() && *i == eventLiteral)
            return ~0u;

        if (_isRecordingKnownEvents) {
            auto ki = std::lower_bound(_knownEvents.begin(), _knownEvents.end(), eventLiteral);
            if (ki == _knownEvents.end() || *ki != eventLiteral)
                _knownEvents.insert(ki, eventLiteral);
        }

        auto result = _workingId++;
        #if !defined(NDEBUG)
            assert(_aeStackI < dimof(_aeStack));
            _aeStack[_aeStackI++] = result;
        #endif
        return result;
    }

    inline void SuppressionProfiler::EndEvent(unsigned eventId)
    {
        assert(Threading::CurrentThreadId() == _threadId);
        #if !defined(NDEBUG)
            assert(_aeStackI > 0);
            assert(_aeStack[_aeStackI-1] == eventId);   // verify that this is the right event we're removing
            --_aeStackI;
        #endif
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    class SuppressionProfileEvent
    {
    public:
        SuppressionProfiler::EventId GetId() { return _id; }
        bool IsActive() { return !_profiler || (_id != ~0u); }

        SuppressionProfileEvent(const char label[], SuppressionProfiler* profiler)
        : _profiler(profiler)
        , _id(~0u)
        {
            if (_profiler)
                _id = _profiler->BeginEvent(label);
        }

        SuppressionProfileEvent()
        : _profiler(nullptr), _id(~0u)
        {
        }

        ~SuppressionProfileEvent()
        {
            if (_profiler && (_id != ~0u))
                _profiler->EndEvent(_id);
        }

        SuppressionProfileEvent(SuppressionProfileEvent&& moveFrom) never_throws
        {
            _profiler = moveFrom._profiler;
            _id = moveFrom._id;
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
        }

        SuppressionProfileEvent& operator=(SuppressionProfileEvent&& moveFrom) never_throws
        {
            if (_profiler && (_id != ~0u))
                _profiler->EndEvent(_id);

            _profiler = moveFrom._profiler;
            _id = moveFrom._id;
            moveFrom._profiler = nullptr;
            moveFrom._id = ~0u;
            return *this;
        }

    private:
        SuppressionProfiler* _profiler;
        SuppressionProfiler::EventId _id;
    };
}

using namespace Utility;
