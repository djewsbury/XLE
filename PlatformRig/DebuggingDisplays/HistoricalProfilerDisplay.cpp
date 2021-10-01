#include "HistoricalProfilerDisplay.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include <stack>
#include <iomanip>
#include <functional>

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    static float AsMilliseconds(uint64 profilerTime)
    {
        static float freqMult = 1000.f / OSServices::GetPerformanceCounterFrequency();
        return float(profilerTime) * freqMult;
    }

    class HistoricalProfilerDisplay::Pimpl
    {
    public:
        using Duration = float;
        class TrackingLabel
        {
        public:
            static const unsigned DurationHistoryLength = 256u;
            Duration    _durationHistory[DurationHistoryLength];
            unsigned    _durationHistoryLength = 0;
            float       _graphMin = std::numeric_limits<float>::max();
            float       _graphMax = std::numeric_limits<float>::lowest();
            unsigned    _flags = 0;

            enum class Flags { Hide = 1<<0, Pause = 1<<1 };

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

        std::vector<std::pair<const char*, TrackingLabel>> _trackingLabels;
        IHierarchicalProfiler::ListenerId _listenerId;
        Threading::Mutex _trackingLabelsLock;

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

    ////////////////////////////////////////////////////////////////////////////////////////////

    void    HistoricalProfilerDisplay::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
    {
        static const InteractableId sectionToolsId = InteractableId_Make("GPUProfilerSectionTools");

        std::vector<std::pair<const char*, Pimpl::TrackingLabel>> trackingLabels;
        {
            ScopedLock(_pimpl->_trackingLabelsLock);
            trackingLabels = _pimpl->_trackingLabels;
        }

        unsigned sectionHeight = 96;
        for (unsigned sectionIndex=0; sectionIndex < trackingLabels.size(); ++sectionIndex) {
            const char* label = trackingLabels[sectionIndex].first;
            Pimpl::TrackingLabel& section = trackingLabels[sectionIndex].second;
            if (section._flags & unsigned(Pimpl::TrackingLabel::Flags::Hide))
                continue;

            //  Main outline for the section...
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
            DrawText().Draw(context, sectionNameRect, label);

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
            DrawHistoryGraph(context, historyRect, section._durationHistory, section._durationHistoryLength, Pimpl::TrackingLabel::DurationHistoryLength, section._graphMin, section._graphMax);

            //  Interactables
            {
                Rect mouseOverRect(sectionRect._topLeft, Coord2(LinearInterpolate(labelRect._topLeft[0], labelRect._bottomRight[0], .12f), sectionRect._bottomRight[1]));
                mouseOverRect._topLeft[0] += 4; mouseOverRect._topLeft[1] += 4;
                mouseOverRect._bottomRight[0] -= 4; mouseOverRect._bottomRight[1] -= 4;
                interactables.Register({mouseOverRect, sectionToolsId+sectionIndex});

                if (interfaceState.HasMouseOver(sectionToolsId+sectionIndex)) {
                    const char* buttonNames[] = {"P", "H", "R"};
                    static const InteractableId baseButtonIds[] = { InteractableId_Make("GPUProfiler_Pause"), InteractableId_Make("GPUProfiler_Hide"), InteractableId_Make("GPUProfiler_Reset") };
                    const unsigned buffer = 4, buttonSpacing = 2;
                    unsigned buttonSize0 =  std::min(mouseOverRect.Width(), mouseOverRect.Height()) - 2*buffer;
                    const auto buttonCount = dimof(buttonNames);
                    unsigned buttonSize1 = (std::max(mouseOverRect.Width(), mouseOverRect.Height()) - 2*buffer - ((buttonCount - 1)*buttonSpacing)) / buttonCount;
                    unsigned buttonSize  =  std::min(buttonSize0, buttonSize1);

                    Coord2 middle(  mouseOverRect._topLeft[0] + buffer + buttonSize/2,
                        LinearInterpolate(mouseOverRect._topLeft[1], mouseOverRect._bottomRight[1], 0.5f));
                    for (unsigned c=0; c<dimof(buttonNames); ++c) {
                        Coord2 buttonMiddle = middle;
                        buttonMiddle[1] += (c-1) * (buttonSize+buffer);
                        Rect buttonRect(
                            Coord2(buttonMiddle[0]-buttonSize/2, buttonMiddle[1]-buttonSize/2),
                            Coord2(buttonMiddle[0]+buttonSize/2, buttonMiddle[1]+buttonSize/2) );

                        InteractableId id = baseButtonIds[c]+sectionIndex;
                        if (interfaceState.HasMouseOver(id)) {
                            OutlineEllipse(context, buttonRect, ColorB(0xff000000u));
                            DrawText().Color(0xff000000u).Draw(context, buttonRect, buttonNames[c]);
                        } else {
                            OutlineEllipse(context, buttonRect, ColorB(0xffffffffu));
                            DrawText().Color(0xffffffffu).Draw(context, buttonRect, buttonNames[c]);
                        }
                        interactables.Register({buttonRect, id});
                    }
                }
            }
        }
    }

    auto    HistoricalProfilerDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        if (interfaceState.TopMostId()) {
            if (input.IsRelease_LButton()) {
                const InteractableId topMostWidget = interfaceState.TopMostId();
                const InteractableId basePauseButton = InteractableId_Make("GPUProfiler_Pause");
                const InteractableId baseHideButton  = InteractableId_Make("GPUProfiler_Hide");
                const InteractableId baseResetButton  = InteractableId_Make("GPUProfiler_Reset");

                ScopedLock(_pimpl->_trackingLabelsLock);
                if (topMostWidget >= basePauseButton && topMostWidget < basePauseButton + _pimpl->_trackingLabels.size()) {
                    auto& section = _pimpl->_trackingLabels[size_t(topMostWidget-basePauseButton)];
                    section.second._flags = section.second._flags ^ unsigned(Pimpl::TrackingLabel::Flags::Pause);
                    return ProcessInputResult::Consumed;
                } else if (topMostWidget >= baseHideButton && topMostWidget < baseHideButton + _pimpl->_trackingLabels.size()) {
                    auto& section = _pimpl->_trackingLabels[size_t(topMostWidget-baseHideButton)];
                    section.second._flags = section.second._flags ^ unsigned(Pimpl::TrackingLabel::Flags::Hide);
                    return ProcessInputResult::Consumed;
                } else if (topMostWidget >= baseResetButton && topMostWidget < baseResetButton + _pimpl->_trackingLabels.size()) {
                    auto& section = _pimpl->_trackingLabels[size_t(topMostWidget-baseResetButton)];
                    section.second._durationHistoryLength = 0;
                    return ProcessInputResult::Consumed;
                }
            }
        }
        return ProcessInputResult::Passthrough;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    void HistoricalProfilerDisplay::Pimpl::IngestFrameData(IteratorRange<const void*> rawData)
    {
        auto resolvedEvents = IHierarchicalProfiler::CalculateResolvedEvents(rawData);

        ScopedLock(_trackingLabelsLock);
        // find any occurance of the labels we're tracking; and add them in
        std::vector<bool> alreadyAddThisFrame(_trackingLabels.size(), false);
        for (const auto&evnt:resolvedEvents) {
            auto i = std::find_if(_trackingLabels.begin(), _trackingLabels.end(), [&evnt](const std::pair<const char*, Pimpl::TrackingLabel>&p) { return p.first == evnt._label; });
            if (i != _trackingLabels.end()) {
                auto idx = (unsigned)std::distance(_trackingLabels.begin(), i);
                if (!alreadyAddThisFrame[idx]) {
                    i->second.PushDuration(AsMilliseconds(evnt._inclusiveTime));
                    alreadyAddThisFrame[idx] = true;
                } else {
                    assert((i->second._durationHistoryLength-1) < dimof(i->second._durationHistory));
                    i->second._durationHistory[i->second._durationHistoryLength-1] += AsMilliseconds(evnt._inclusiveTime);
                }
            }
        }
    }

    void    HistoricalProfilerDisplay::TrackLabel(const char label[])
    {
        ScopedLock(_pimpl->_trackingLabelsLock);
        auto i = std::find_if(_pimpl->_trackingLabels.begin(), _pimpl->_trackingLabels.end(), [label](const std::pair<const char*, Pimpl::TrackingLabel>&p) { return p.first == label; });
        if (i == _pimpl->_trackingLabels.end())
            _pimpl->_trackingLabels.push_back({label, Pimpl::TrackingLabel{}});
    }

    void    HistoricalProfilerDisplay::UntrackLabel(const char label[])
    {
        ScopedLock(_pimpl->_trackingLabelsLock);
        auto i = std::find_if(_pimpl->_trackingLabels.begin(), _pimpl->_trackingLabels.end(), [label](const std::pair<const char*, Pimpl::TrackingLabel>&p) { return p.first == label; });
        if (i != _pimpl->_trackingLabels.end())
            _pimpl->_trackingLabels.erase(i);
    }

    HistoricalProfilerDisplay::HistoricalProfilerDisplay(IHierarchicalProfiler* profiler)
    : _profiler(profiler)
    {
        _pimpl = std::make_unique<Pimpl>();
        auto* pimpl = _pimpl.get();
        _pimpl->_listenerId = _profiler->AddEventListener(
            [pimpl](IHierarchicalProfiler::RawEventData data) { pimpl->IngestFrameData(data); });
    }

    HistoricalProfilerDisplay::~HistoricalProfilerDisplay()
    {
        if (_profiler)
            _profiler->RemoveEventListener(_pimpl->_listenerId);
    }

}}


