// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HierarchicalSpikesDisplay.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include <stack>
#include <iomanip>
#include <functional>
#include <unordered_map>

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    static float AsMilliseconds(uint64_t profilerTime)
    {
        static float freqMult = 1000.f / OSServices::GetPerformanceCounterFrequency();
        return float(profilerTime) * freqMult;
    }

    class CumulativeStats
    {
    public:
        float _m = 0.f, _s = 0.f;
        unsigned _n = 0;

        void Append(float value)
        {
            auto prevMean = _m;
            ++_n;
            _m = _m + (value - _m) / float(_n);
            _s = _s + (value - _m)*(value - prevMean);
        }

        float Mean() const { return _m; }
        float StandardDeviation() const
        {
            // Bessel's correction (https://en.wikipedia.org/wiki/Bessel%27s_correction)
            // not applied here
            return std::sqrt(_s/float(_n));
        }
        unsigned SampleCount() const { return _n; }
    };

    class Entry
    {
    public:
        CumulativeStats _cumulativeStats;
        float _lastSample = 0.f;
    };

    class HierarchicalSpikesDisplay::Pimpl
    {
    public:
        IHierarchicalProfiler* _profiler;
        IHierarchicalProfiler::ListenerId _listenerId;

        Threading::Mutex _entriesLock;
        std::unordered_map<size_t, Entry> _entries;

        using Duration = float;
        struct TrackingLabel
        {
            size_t _key;
            unsigned _lastSampleFrame;

            static const unsigned DurationHistoryLength = 256u;
            Duration    _durationHistory[DurationHistoryLength];
            unsigned    _durationHistoryLength = 0;
            float       _graphMin = std::numeric_limits<float>::max();
            float       _graphMax = std::numeric_limits<float>::lowest();

            void PushDuration(Duration newDuration)
            {
                if ((_durationHistoryLength+1) > DurationHistoryLength) {
                    std::move(_durationHistory+1, _durationHistory+DurationHistoryLength, _durationHistory);
                    _durationHistory[DurationHistoryLength-1] = newDuration;
                    _durationHistoryLength = DurationHistoryLength;
                } else {
                    _durationHistory[_durationHistoryLength++] = newDuration;
                }
            }
        };
        std::vector<TrackingLabel> _trackingEntries;

        unsigned _currentFrameIndex = 0;

        void IngestFrameData(IteratorRange<const void*> rawData);

        struct DurationStats
        {
            float _mean;
            float _variance;
        };

        static DurationStats CalculateDurationStats(IteratorRange<const Duration*> durations)
        {
            float smoothedCost = 0.f;
            float minValue = MAX_FLOAT32, maxValue = -MAX_FLOAT32;
            for (unsigned f=0; f<durations.size(); ++f) {
                auto millis = durations[f];
                smoothedCost += millis;
                minValue = std::min(minValue, millis);
                maxValue = std::max(maxValue, millis);
            }
            smoothedCost /= float(durations.size());

            return {smoothedCost, maxValue - minValue};
        }
    };

    void HierarchicalSpikesDisplay::Pimpl::IngestFrameData(IteratorRange<const void*> rawData)
    {
        auto resolvedEvents = IHierarchicalProfiler::CalculateResolvedEvents(rawData);
        ScopedLock(_entriesLock);
        for (auto& entry:_entries)
            entry.second._lastSample = 0.f;

        for (const auto& event:resolvedEvents) {
            auto& entry = _entries[(size_t)event._label];
            entry._lastSample += AsMilliseconds(event._exclusiveTime);
        }

        ++_currentFrameIndex;
        for (auto& entry:_entries) {
            entry.second._cumulativeStats.Append(entry.second._lastSample);

            if (entry.second._cumulativeStats.SampleCount() < 64) continue;
            auto sd = entry.second._cumulativeStats.StandardDeviation();
            auto sds = (entry.second._lastSample - entry.second._cumulativeStats.Mean()) / sd;
            auto i = std::find_if(_trackingEntries.begin(), _trackingEntries.end(), [&entry](const Pimpl::TrackingLabel& te) { return te._key == entry.first; });
            if (sds >= 5.0f && sd > 5.0f) {
                if (i == _trackingEntries.end()) {
                    _trackingEntries.push_back(Pimpl::TrackingLabel{entry.first, _currentFrameIndex});
                    i = _trackingEntries.end()-1;
                } else {
                    i->_lastSampleFrame = _currentFrameIndex;
                }
            }
            if (i != _trackingEntries.end()) {
                i->PushDuration(entry.second._lastSample);
            }
        }

        // filter entries that haven't been heard of for awhile
        const unsigned idealEntryCount = 6;
        const unsigned retainEntryFrame = 128;
        if (_trackingEntries.size() > idealEntryCount) {
            auto temp = _trackingEntries;
            std::sort(temp.begin(), temp.end(), [](const Pimpl::TrackingLabel& lhs, const Pimpl::TrackingLabel& rhs) { return rhs._lastSampleFrame < lhs._lastSampleFrame; });
            for (auto i = temp.begin() + idealEntryCount; i != temp.end(); ++i) {
                if ((_currentFrameIndex - i->_lastSampleFrame) < retainEntryFrame) continue;
                _trackingEntries.erase(std::find_if(_trackingEntries.begin(), _trackingEntries.end(), [i](const Pimpl::TrackingLabel& te) { return te._key == i->_key; }));
            }
        }
    }

    class SpikesProfilerResources
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _largeFont;

        SpikesProfilerResources(
            std::shared_ptr<RenderOverlays::Font> largeFont)
        : _largeFont(std::move(largeFont))
        {}

        static void ConstructToPromise(std::promise<std::shared_ptr<SpikesProfilerResources>>&& promise)
        {
            ::Assets::WhenAll(RenderOverlays::MakeFont("Petra", 64)).ThenConstructToPromise(std::move(promise));
        }
    };

    void HierarchicalSpikesDisplay::Render(
        IOverlayContext& context, Layout& layout,
        Interactables&interactables, InterfaceState& interfaceState)
    {
        const unsigned sectionHeight = 96;

        {
            ScopedLock(_pimpl->_entriesLock);
            for (auto&section:_pimpl->_trackingEntries) {
                auto i = _pimpl->_entries.find(section._key);
                assert(i!=_pimpl->_entries.end());

                Rect sectionRect = layout.AllocateFullWidth( sectionHeight );
                if (!IsGood(sectionRect)) {
                    break;
                }

                FillAndOutlineRoundedRectangle(context, sectionRect, ColorB(180,200,255,128), ColorB(255,255,255,128));

                Layout sectionLayout(sectionRect);
                Rect labelRect = sectionLayout.AllocateFullHeightFraction( .25f );
                Rect historyRect = sectionLayout.AllocateFullHeightFraction( .75f );

                //  Section name in the top third of the label rect
                Rect sectionNameRect(
                    Coord2(labelRect._topLeft[0], labelRect._topLeft[1]),
                    Coord2(labelRect._bottomRight[0], LinearInterpolate(labelRect._topLeft[1], labelRect._bottomRight[1], 0.333f)) );
                DrawText().Draw(context, sectionNameRect, (const char*)i->first);

                if (section._durationHistoryLength) {
                    auto stats = Pimpl::CalculateDurationStats({section._durationHistory, &section._durationHistory[section._durationHistoryLength]});

                    Rect durationRect(
                        Coord2(labelRect._topLeft[0], sectionNameRect._bottomRight[1]),
                        Coord2(labelRect._bottomRight[0], LinearInterpolate(labelRect._topLeft[1], labelRect._bottomRight[1], 0.667f)) );

                    float recentCost = section._durationHistory[section._durationHistoryLength-1];
                    DrawText().FormatAndDraw(context, durationRect, "%.2fms (%.2fms)", stats._mean, recentCost);

                    Rect varianceRect(
                        Coord2(labelRect._topLeft[0], durationRect._bottomRight[1]),
                        Coord2(labelRect._bottomRight[0], labelRect._bottomRight[1]) );
                    DrawText().FormatAndDraw(context, varianceRect, "%.2fms variance", stats._variance);
                }

                //  Then draw the graph in the main part of the widget
                DrawHistoryGraph(
                    context, historyRect, section._durationHistory,
                    section._durationHistoryLength, Pimpl::TrackingLabel::DurationHistoryLength,
                    section._graphMin, section._graphMax);
            }
        }
    }

    HierarchicalSpikesDisplay::HierarchicalSpikesDisplay(IHierarchicalProfiler* profiler)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_profiler = profiler;
        auto* pimpl = _pimpl.get();
        _pimpl->_listenerId = _pimpl->_profiler->AddEventListener(
            [pimpl](IHierarchicalProfiler::RawEventData data) { pimpl->IngestFrameData(data); });
    }

    HierarchicalSpikesDisplay::~HierarchicalSpikesDisplay()
    {
        _pimpl->_profiler->RemoveEventListener(_pimpl->_listenerId);
    }
}}
