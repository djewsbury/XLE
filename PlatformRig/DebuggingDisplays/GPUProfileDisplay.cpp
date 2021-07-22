// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GPUProfileDisplay.h"
#include "../../RenderCore/IAnnotator.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include <assert.h>
#include <functional>

namespace PlatformRig { namespace Overlays
{
    ////////////////////////////////////////////////////////////////////////////////////////////
    class GPUProfileDisplay::GPUFrameConstruction
    {
    public:
        struct Section
        {
            const char* _id;
            GPUTime _earliestTime;
            GPUTime _totalDuration;
            GPUTime _childDuration;
            unsigned _count;
            static bool SortBySelfTime(const Section&lhs, const Section&rhs);
        };
        std::vector<Section> _sections;
        FrameId _currentFrameId;
        GPUTime _currentFrequency;
        GPUTime _earliestTime, _latestTime;

        struct ActiveEvents
        {
            const char* _id;
            GPUTime _start;
            GPUTime _childDuration;
        };
        ActiveEvents _activeEventsStack[8];
        unsigned _activeEventsStackDepth;

        const void*     ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd);
        void            Reset();
        GPUFrameConstruction();
    };

    const void*    GPUProfileDisplay::GPUFrameConstruction::ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        //
        //      Decode the packet stream and figure out what happened when these events where
        //      recorded. Decoding gets a little complex...
        //
        const void * evnt = eventsBufferStart;
        while (evnt < eventsBufferEnd) {
            const void * packetStart = evnt;
            size_t eventType = *((const size_t*)evnt); evnt = PtrAdd(evnt, sizeof(size_t));
            if (eventType == ~size_t(0x0)) {
                FrameId frameId = (FrameId)*((const size_t*)evnt); evnt = PtrAdd(evnt, sizeof(size_t));
                GPUTime frequency = *((const uint64*)evnt); evnt = PtrAdd(evnt, sizeof(uint64));

                ///////////////////////////////////////////////////
                //      if this is the start of the next frame, we need to return,
                //      and pass back the start of the this new "begin frame" marker

                if (frameId != _currentFrameId && _currentFrameId != ~FrameId(0x0)) {
                    return packetStart;
                }
                _currentFrameId = frameId;
                _currentFrequency = frequency;
            } else {
                const char* eventName = *((const char**)evnt); evnt = PtrAdd(evnt, sizeof(const char*));
                //assert((size_t(evnt)%sizeof(uint64))==0);
                uint64 timeValue = *((const uint64*)evnt); evnt = PtrAdd(evnt, sizeof(uint64));
                _earliestTime = std::min(_earliestTime, timeValue);
                _latestTime = std::max(_latestTime, timeValue);

                if (eventType == 0) {

                        ///////////////////////////////////////////////////
                        //      This is a begin event; so push it onto the stack

                    assert(_activeEventsStackDepth+1 <= dimof(_activeEventsStack));
                    _activeEventsStack[_activeEventsStackDepth]._start = timeValue;
                    _activeEventsStack[_activeEventsStackDepth]._childDuration = 0;
                    _activeEventsStack[_activeEventsStackDepth]._id = eventName;
                    ++_activeEventsStackDepth;

                } else {

                        ///////////////////////////////////////////////////
                        //      This is an end event; so look for the being on the stack, and commit to results

                    if (_activeEventsStackDepth>=1) {   // sometimes we can start with an end event when the display first appears
                        assert(_activeEventsStack[_activeEventsStackDepth-1]._id==eventName);

                        GPUTime beginTime = _activeEventsStack[_activeEventsStackDepth-1]._start;
                        GPUTime childDuration = _activeEventsStack[_activeEventsStackDepth-1]._childDuration;
                        bool gotExisting = false;
                        for (std::vector<Section>::iterator i=_sections.begin(); i!=_sections.end(); ++i) {
                            if (i->_id == eventName) {
                                ++i->_count;
                                i->_totalDuration += timeValue - beginTime;
                                i->_childDuration += childDuration;
                                gotExisting = true;
                            }
                        }
                        if (!gotExisting) {
                            Section newSection;
                            newSection._id = eventName;
                            newSection._earliestTime = beginTime;
                            newSection._totalDuration = timeValue-beginTime;
                            newSection._childDuration = childDuration;
                            newSection._count = 0;
                            _sections.push_back(newSection);
                        }

                        --_activeEventsStackDepth;
                        if (_activeEventsStackDepth>0) {
                            //  Add our time to our parent's child time
                            _activeEventsStack[_activeEventsStackDepth-1]._childDuration += timeValue-beginTime;
                        }
                    }
                }
            }
        }
        return evnt;
    }

    void GPUProfileDisplay::GPUFrameConstruction::Reset()
    {
        _currentFrameId = ~FrameId(0x0);
        _activeEventsStackDepth = 0;
        _currentFrequency = 0;
        _earliestTime = ~GPUTime(0x0);
        _latestTime = 0;
        _sections.clear();
    }

    GPUProfileDisplay::GPUFrameConstruction::GPUFrameConstruction()
    {
        _currentFrameId = ~FrameId(0x0);
        _activeEventsStackDepth = 0;
        _currentFrequency = 0;
        _earliestTime = ~GPUTime(0x0);
        _latestTime = 0;
    }

    bool GPUProfileDisplay::GPUFrameConstruction::Section::SortBySelfTime(const Section&lhs, const Section&rhs)
    {
        return (lhs._totalDuration-lhs._childDuration)>(rhs._totalDuration-rhs._childDuration);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////
    static bool SortByFirst(const std::pair<float, unsigned>&lhs, const std::pair<float, unsigned>&rhs)
    {
        return lhs.first>rhs.first;
    }

    void    GPUProfileDisplay::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
    {
        std::pair<float, unsigned> smoothedSectionCosts[dimof(_sections)];
        float sectionVariances[dimof(_sections)];
        for (unsigned c=0; c<dimof(_sections); ++c) {
            if (_sections[c]._id) {
                float smoothedCost = 0.f;
                float minValue = MAX_FLOAT32, maxValue = -MAX_FLOAT32;
                for (unsigned f=0; f<_sections[c]._durationHistoryLength; ++f) {
                    smoothedCost += _sections[c]._durationHistory[f];
                    minValue = std::min(minValue, _sections[c]._durationHistory[f]);
                    maxValue = std::max(maxValue, _sections[c]._durationHistory[f]);
                }
                smoothedCost /= float(_sections[c]._durationHistoryLength);
                smoothedSectionCosts[c] = std::make_pair(smoothedCost,c);
                sectionVariances[c] = maxValue-minValue;
            } else {
                smoothedSectionCosts[c] = std::make_pair(0.f,c);
                sectionVariances[c] = 0.f;
            }
        }
        std::sort(smoothedSectionCosts, &smoothedSectionCosts[dimof(_sections)], SortByFirst);

        static const InteractableId sectionToolsId = InteractableId_Make("GPUProfilerSectionTools");

        unsigned sectionHeight = 96;
        for (unsigned c2=0; c2<dimof(_sections); ++c2) {
            Section& section = _sections[smoothedSectionCosts[c2].second];
            if (section._id && !(section._flags & Section::Flag_Hide)) {
                //  Main outline for the section...
                Rect sectionRect = layout.AllocateFullWidth( sectionHeight );
                if (!IsGood(sectionRect)) {
                    break;
                }

                // Disabled because the rounded rectangle shader isn't working for APIs other than DX11 
                // DrawRoundedRectangle(&context, sectionRect, ColorB(180,200,255,128), ColorB(255,255,255,128));

                Layout sectionLayout(sectionRect);
                Rect labelRect = sectionLayout.AllocateFullHeightFraction( .25f );
                Rect historyRect = sectionLayout.AllocateFullHeightFraction( .75f );

                //  Section name in the top third of the label rect
                Rect sectionNameRect( 
                    Coord2(labelRect._topLeft[0], labelRect._topLeft[1]),
                    Coord2(labelRect._bottomRight[0], LinearInterpolate(labelRect._topLeft[1], labelRect._bottomRight[1], 0.333f)) );
                DrawText(&context, sectionNameRect, 2.f, nullptr, ColorB(0xffffffffu), section._id);

				if (section._durationHistoryLength) {
                    Rect durationRect( 
                        Coord2(labelRect._topLeft[0], sectionNameRect._bottomRight[1]),
                        Coord2(labelRect._bottomRight[0], LinearInterpolate(labelRect._topLeft[1], labelRect._bottomRight[1], 0.667f)) );

                    float recentCost = section._durationHistory[section._durationHistoryLength-1];
                    float smoothedCost = smoothedSectionCosts[c2].first;
                    DrawFormatText(&context, durationRect, nullptr, ColorB(0xffffffffu), "%.2fms (%.2fms)", smoothedCost, recentCost);

                    Rect varianceRect( 
                        Coord2(labelRect._topLeft[0], durationRect._bottomRight[1]),
                        Coord2(labelRect._bottomRight[0], labelRect._bottomRight[1]) );
                    DrawFormatText(&context, varianceRect, nullptr, ColorB(0xffffffffu), "%.2fms variance", sectionVariances[smoothedSectionCosts[c2].second]);
                }

                //  Then draw the graph in the main part of the widget
                DrawHistoryGraph(&context, historyRect, section._durationHistory, section._durationHistoryLength, DurationHistoryLength, section._graphMin, section._graphMax);

                //  Interactables
                {
                    const unsigned sectionIndex = smoothedSectionCosts[c2].second;
                    Rect mouseOverRect(sectionRect._topLeft, Coord2(LinearInterpolate(labelRect._topLeft[0], labelRect._bottomRight[0], .12f), sectionRect._bottomRight[1]));
                    mouseOverRect._topLeft[0] += 4; mouseOverRect._topLeft[1] += 4;
                    mouseOverRect._bottomRight[0] -= 4; mouseOverRect._bottomRight[1] -= 4;
                    interactables.Register(Interactables::Widget(mouseOverRect, sectionToolsId+sectionIndex));

                    if (interfaceState.HasMouseOver(sectionToolsId+smoothedSectionCosts[c2].second)) {
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
                                DrawElipse(&context, buttonRect, ColorB(0xff000000u));
                                DrawText(&context, buttonRect, nullptr, ColorB(0xff000000u), buttonNames[c]);
                            } else {
                                DrawElipse(&context, buttonRect, ColorB(0xffffffffu));
                                DrawText(&context, buttonRect, nullptr, ColorB(0xffffffffu), buttonNames[c]);
                            }
                            interactables.Register(Interactables::Widget(buttonRect, id));
                        }
                    }
                }
            }
        }
    }

    bool    GPUProfileDisplay::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        if (interfaceState.TopMostId()) {
            if (input.IsRelease_LButton()) {
                const InteractableId topMostWidget = interfaceState.TopMostId();
                const InteractableId basePauseButton = InteractableId_Make("GPUProfiler_Pause");
                const InteractableId baseHideButton  = InteractableId_Make("GPUProfiler_Hide");
                const InteractableId baseResetButton  = InteractableId_Make("GPUProfiler_Reset");

                if (topMostWidget >= basePauseButton && topMostWidget < basePauseButton + dimof(_sections)) {
                    Section& section = _sections[topMostWidget-basePauseButton];
                    section._flags = section._flags ^ Section::Flag_Pause;
                    return true;
                } else if (topMostWidget >= baseHideButton && topMostWidget < baseHideButton + dimof(_sections)) {
                    Section& section = _sections[topMostWidget-baseHideButton];
                    section._flags = section._flags ^ Section::Flag_Hide;
                    return true;
                } else if (topMostWidget >= baseResetButton && topMostWidget < baseResetButton + dimof(_sections)) {
                    Section& section = _sections[topMostWidget-baseResetButton];
                    section._durationHistoryLength = 0;
                    return true;
                }
            }
        }
        return false;
    }

    float   GPUProfileDisplay::ToGPUDuration(GPUTime time, GPUTime frequency) 
    {
        return float(double(time)/double(frequency/GPUTime(1000)));
    }

    void    GPUProfileDisplay::PushSectionInfo(const char id[], GPUTime selfTime)
    {
        GPUDuration newDuration = ToGPUDuration(selfTime, _currentFrame->_currentFrequency);

        // add this section to our section history (if it fits)
        for (unsigned sectionIndex = 0;sectionIndex<dimof(_sections); ++sectionIndex) {
            if (_sections[sectionIndex]._id == id || !_sections[sectionIndex]._id) {
                if (_sections[sectionIndex]._flags & Section::Flag_Pause) {
                    break;  // stop adding sections it's marked "pause"
                }

                _sections[sectionIndex]._id = id;
                unsigned& historyLength = _sections[sectionIndex]._durationHistoryLength;
                if ((historyLength+1) > DurationHistoryLength) {
                    std::move(  _sections[sectionIndex]._durationHistory+1,
                        _sections[sectionIndex]._durationHistory+DurationHistoryLength,
                        _sections[sectionIndex]._durationHistory );
                    _sections[sectionIndex]._durationHistory[DurationHistoryLength-1] = newDuration;
                } else {
                    _sections[sectionIndex]._durationHistory[historyLength++] = newDuration;
                }
                break;
            }
        }
    }

    void    GPUProfileDisplay::ProcessGPUEvents(const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        for (;;) {
            eventsBufferStart = _currentFrame->ProcessGPUEvents(eventsBufferStart, eventsBufferEnd);
            if (eventsBufferStart>=eventsBufferEnd) {
                assert(eventsBufferStart==eventsBufferEnd);
                return;
            }

            PushSectionInfo("Total", _currentFrame->_latestTime-_currentFrame->_earliestTime);
            if (_endOfLastFrame!=0x0) {
                PushSectionInfo("Stall", _currentFrame->_earliestTime-_endOfLastFrame);
            }
            _endOfLastFrame = _currentFrame->_latestTime;

            // we finished a frame; let's colate the information and reset the builder (and go around again)
            std::sort(_currentFrame->_sections.begin(), _currentFrame->_sections.end(), GPUFrameConstruction::Section::SortBySelfTime);
            for (std::vector<GPUFrameConstruction::Section>::iterator i=_currentFrame->_sections.begin(); i!=_currentFrame->_sections.end(); ++i) {
                PushSectionInfo(i->_id, i->_totalDuration-i->_childDuration);
            }
            _currentFrame->Reset();
        }
    }

    static void GPUEventListener(GPUProfileDisplay& display, const void* eventsBufferStart, const void* eventsBufferEnd)
    {
        display.ProcessGPUEvents(eventsBufferStart, eventsBufferEnd);
    }

    GPUProfileDisplay::GPUProfileDisplay(RenderCore::IAnnotator& profiler)
    :   _currentFrame(std::make_unique<GPUFrameConstruction>())
    ,   _profiler(&profiler)
    {
        XlZeroMemory(_sections);
        _endOfLastFrame = 0;
		_listenerId = profiler.AddEventListener(std::bind(&GPUEventListener, std::ref(*this), std::placeholders::_1, std::placeholders::_2));
    }

    GPUProfileDisplay::~GPUProfileDisplay()
    {
        _profiler->RemoveEventListener(_listenerId);
    }
}}
