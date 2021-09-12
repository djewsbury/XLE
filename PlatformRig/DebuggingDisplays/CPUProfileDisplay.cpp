// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CPUProfileDisplay.h"
#include "../../RenderOverlays/Font.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../OSServices/TimeUtils.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include "../../xleres/FileList.h"
#include <stack>
#include <iomanip>
#include <functional>

namespace PlatformRig { namespace Overlays
{
    float g_fpsDisplay = 0.f;
	float g_loadDisplay = 0.f;

    static float AsMilliseconds(uint64 profilerTime)
    {
        static float freqMult = 1000.f / OSServices::GetPerformanceCounterFrequency();
        return float(profilerTime) * freqMult;
    }

    static uint64 MillisecondsAsTimerValue(float milliseconds)
    {
        static float converter = OSServices::GetPerformanceCounterFrequency() / 1000.f;
        return uint64(milliseconds * converter);
    }

    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class ProfilerTableSettings
    {
    public:
        unsigned _lineHeight;

        ColorB _barColor0, _barColor1;
        ColorB _highlightBarColor0, _highlightBarColor1;

        ColorB _bkColor;
        ColorB _leftColor;
        ColorB _middleColor;
        ColorB _rightColor;
        ColorB _dividingLineColor;
        ColorB _barBackgroundColor;

        unsigned _leftPartWidth;
        unsigned _middlePartWidth;
        unsigned _precision;

        float _barBorderSize;
        float _barRoundedProportion;

        ProfilerTableSettings()
        {
            _lineHeight = 30;
            _barColor0 = ColorB( 32, 196, 196);
            _barColor1 = ColorB( 96,  96,  96);
            _highlightBarColor0 = ColorB(192, 140, 140);
            _highlightBarColor1 = ColorB( 96,  64,  64);
            _barBackgroundColor = ColorB(0, 0, 0, 96);

            _bkColor = ColorB(128, 128, 128, 96);
            _leftColor = ColorB(255, 255, 255);
            _middleColor = ColorB(255, 255, 255);
            _rightColor = ColorB(255, 255, 255);
            _dividingLineColor = ColorB(0, 0, 0);

            _leftPartWidth = 700;
            _middlePartWidth = 120;
            _precision = 1;

            _barBorderSize = 2.f;
            _barRoundedProportion = 1.f / 2.f;
        }
    };

    class DrawProfilerResources
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _leftFont;
		std::shared_ptr<RenderOverlays::Font> _middleFont;
		std::shared_ptr<RenderOverlays::Font> _rightFont;

        DrawProfilerResources()
        {
            _leftFont = RenderOverlays::GetX2Font("DosisBook", 20);
            _middleFont = RenderOverlays::GetX2Font("Shojumaru", 32);
            _rightFont = RenderOverlays::GetX2Font("PoiretOne", 24);
        }
    };

    static void DrawProfilerBar(
        const ProfilerTableSettings& settings,
        IOverlayContext* context,
        const Rect& rect, Coord middleX, bool highlighted, float barSize)
    {
            // draw a hatched bar behind in the given rect, centred on the given
            // position
        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1] + 4)),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1] - 4)),
            settings._barBackgroundColor);

        const bool extendFromMiddle = false;
        if (extendFromMiddle) {
            Coord barMaxHalfWidth = std::min(middleX - rect._topLeft[0], rect._bottomRight[0] - middleX);
            Coord barHalfWidth = Coord(std::min(barSize, 1.f) * float(barMaxHalfWidth));
            FillAndOutlineRoundedRectangle(
                context,
                Rect{Coord2(middleX - barHalfWidth, rect._topLeft[1]), Coord2(middleX + barHalfWidth, rect._bottomRight[1])},
                highlighted ? settings._highlightBarColor0 : settings._barColor0, ColorB::White,
                settings._barBorderSize, settings._barRoundedProportion);
        } else {
            Coord barMaxWidth = rect._bottomRight[0] - rect._topLeft[0];
            Coord barWidth = Coord(std::min(barSize, 1.f) * float(barMaxWidth));
            context->DrawQuad(
                ProjectionMode::P2D,
                AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
                AsPixelCoords(Coord2(rect._topLeft[0] + barWidth, rect._bottomRight[1])),
                highlighted ? settings._highlightBarColor0 : settings._barColor0);
        }
    }

    static const char g_InteractableIdTopPartStr[] = "Hierchical Profiler";
    static const uint32 g_InteractableIdTopPart = Hash32(
        g_InteractableIdTopPartStr, &g_InteractableIdTopPartStr[dimof(g_InteractableIdTopPartStr)]);

    static void DrawProfilerTable(
        const std::vector<IHierarchicalProfiler::ResolvedEvent>& resolvedEvents,
        const std::vector<uint64>& toggledItems,
        const ProfilerTableSettings& settings,
        IOverlayContext* context, Layout& layout,
        Interactables&interactables, InterfaceState& interfaceState)
    {

            //  The resolved events are arranged as a tree. We just want
            //  to traverse in depth-first order, and display as a tree

        // FillRectangle(context, layout.GetMaximumSize(), settings._bkColor);

        if (resolvedEvents.empty()) return;

        static std::stack<std::pair<unsigned, unsigned>> items;
        uint64 rootItemsTotalTime = 0;
        auto rootItem = 0;
        while (rootItem != HierarchicalCPUProfiler::ResolvedEvent::s_id_Invalid) {
            items.push(std::make_pair(rootItem,0));
            rootItemsTotalTime += resolvedEvents[rootItem]._inclusiveTime;
            rootItem = resolvedEvents[rootItem]._sibling;
        }

        Float3 dividingLines[256];
        Float3* divingLinesIterator = dividingLines;

        auto& res = ConsoleRig::FindCachedBox<DrawProfilerResources>();
        TextStyle leftStyle; leftStyle._options.shadow = 0;
        TextStyle middleStyle; middleStyle._options.outline = 1; middleStyle._options.shadow = 0;
        TextStyle rightStyle;

        while (!items.empty()) {
            unsigned treeDepth = items.top().second;
            const auto& evnt = resolvedEvents[items.top().first];
            items.pop();

            const auto idLowerPart = evnt._label ? Hash32(evnt._label, XlStringEnd(evnt._label)) : 0;
            const auto elementId = uint64(g_InteractableIdTopPart) << 32ull | uint64(idLowerPart);

            auto leftPart = layout.Allocate(Coord2(settings._leftPartWidth, settings._lineHeight));
            auto middlePart = layout.Allocate(Coord2(settings._middlePartWidth, settings._lineHeight));
            auto rightPart = layout.Allocate(Coord2(layout.GetWidthRemaining(), settings._lineHeight));

            if (leftPart._topLeft[1] >= rightPart._bottomRight[1]) {
                break;  // out of space. Can't fit any more in.
            }

                // We consider the item "open" by default. But if it exists within the "_toggledItems" list,
                // then we should not render the children
            auto t = std::find(toggledItems.cbegin(), toggledItems.cend(), elementId);
            const bool closed = (t!=toggledItems.cend() && *t == elementId)
                && (evnt._firstChild != HierarchicalCPUProfiler::ResolvedEvent::s_id_Invalid);

            Rect totalElement(leftPart._topLeft, rightPart._bottomRight);
            interactables.Register(Interactables::Widget(totalElement, elementId));
            bool highlighted = interfaceState.HasMouseOver(elementId);

                //  Behind the text readout, we want to draw a bar that represents the "inclusive" time
                //  for the profile marker.
                //  The size of the marker should be calibrated from the root items. So, we want to calculate
                //  a percentage of the total time.
                //  The bar should be centered on the in the middle of the "middle part" and shouldn't go
                //  beyond the outer area;
            const float barSize = float(evnt._inclusiveTime) / float(rootItemsTotalTime);
            DrawProfilerBar(
                settings, context, totalElement,
                LinearInterpolate(middlePart._topLeft[0], middlePart._bottomRight[0], 0.5f),
                highlighted, barSize);

            context->DrawText(
                std::make_tuple(AsPixelCoords(leftPart._topLeft), AsPixelCoords(Coord2(leftPart._bottomRight - Coord2(treeDepth * 16, 0)))),
                res._leftFont, leftStyle, settings._leftColor, TextAlignment::Right, evnt._label);

            context->DrawText(
                std::make_tuple(AsPixelCoords(middlePart._topLeft), AsPixelCoords(middlePart._bottomRight)),
                res._middleFont, middleStyle, settings._middleColor, TextAlignment::Center,
                StringMeld<32>() << std::setprecision(settings._precision) << std::fixed << AsMilliseconds(evnt._inclusiveTime));

            StringMeld<64> workingBuffer;
            static const auto exclusiveThreshold = MillisecondsAsTimerValue(0.05f);
            if (evnt._exclusiveTime > exclusiveThreshold && evnt._exclusiveTime < evnt._inclusiveTime) {
                float childFract = 1.0f - float(evnt._exclusiveTime) / float(evnt._inclusiveTime);
                workingBuffer << std::setprecision(settings._precision) << std::fixed << childFract * 100.f << "% in children";
            }
            if (evnt._eventCount > 1) {
                workingBuffer << " (" << evnt._eventCount << ")";
            }
            if (closed) {
                workingBuffer << " <<closed>>";
            }

            context->DrawText(
                std::make_tuple(AsPixelCoords(rightPart._topLeft), AsPixelCoords(rightPart._bottomRight)),
                res._rightFont, rightStyle, settings._rightColor, TextAlignment::Left,
                workingBuffer);

            if ((divingLinesIterator+2) <= &dividingLines[dimof(dividingLines)]) {
                *divingLinesIterator++ = AsPixelCoords(Coord2(totalElement._topLeft[0], totalElement._bottomRight[1] + layout._paddingBetweenAllocations/2));
                *divingLinesIterator++ = AsPixelCoords(Coord2(totalElement._bottomRight[0], totalElement._bottomRight[1] + layout._paddingBetweenAllocations/2));
            }

            if (closed) { continue; }

                // push all children onto the stack
            auto child = evnt._firstChild;
            while (child != HierarchicalCPUProfiler::ResolvedEvent::s_id_Invalid) {
                items.push(std::make_pair(child, treeDepth+1));
                child = resolvedEvents[child]._sibling;
            }
        }

        context->DrawLines(
            ProjectionMode::P2D, dividingLines, unsigned(divingLinesIterator - dividingLines), settings._dividingLineColor);
    }

    class HierarchicalProfilerDisplay::Pimpl
    {
    public:
        IHierarchicalProfiler* _profiler;
        IHierarchicalProfiler::ListenerId _listenerId;
        std::vector<uint64> _toggledItems;
        std::vector<IHierarchicalProfiler::ResolvedEvent> _resolvedEvents;
        Threading::Mutex _resolvedEventsLock;
        int _rowOffset = 0;

        void IngestFrameData(IteratorRange<const void*> rawData);
    };

    void HierarchicalProfilerDisplay::Pimpl::IngestFrameData(IteratorRange<const void*> rawData)
    {
        auto resolvedEvents = IHierarchicalProfiler::CalculateResolvedEvents(rawData);
        ScopedLock(_resolvedEventsLock);
        _resolvedEvents = resolvedEvents;
    }

    void HierarchicalProfilerDisplay::Render(
        IOverlayContext& context, Layout& layout,
        Interactables&interactables, InterfaceState& interfaceState)
    {
        if (true) {
            std::vector<IHierarchicalProfiler::ResolvedEvent> resolvedEvents;
            {
                ScopedLock(_pimpl->_resolvedEventsLock);
                resolvedEvents = _pimpl->_resolvedEvents;
            }
            Layout tableView(layout.GetMaximumSize());
            tableView._caretY -= _pimpl->_rowOffset * 200;
            static ProfilerTableSettings settings;
            DrawProfilerTable(resolvedEvents, _pimpl->_toggledItems, settings, &context, tableView,
                              interactables, interfaceState);
        }

	    {
		    context.DrawText(
			    std::make_tuple(AsPixelCoords(Coord2(layout.GetMaximumSize()._bottomRight[0] - 100,
			                                         layout.GetMaximumSize()._topLeft[1])),
			                    AsPixelCoords(layout.GetMaximumSize()._bottomRight)),
				GetDefaultFont(64), TextStyle{}, ColorB{0xff, 0xff, 0xff}, TextAlignment::Left,
			    StringMeld<64>() << std::setprecision(3) << g_fpsDisplay);
	    }

	    if (g_loadDisplay != 0.f) {
		    context.DrawText(
			    std::make_tuple(AsPixelCoords(Coord2(layout.GetMaximumSize()._bottomRight[0] - 100,
			                                         layout.GetMaximumSize()._topLeft[1] + 64)),
			                    AsPixelCoords(layout.GetMaximumSize()._bottomRight)),
				GetDefaultFont(32), TextStyle{}, ColorB{0xff, 0xff, 0xff}, TextAlignment::Left,
			    StringMeld<64>() << std::setprecision(3) << g_loadDisplay * 100.f << "%");
	    }
    }

    bool HierarchicalProfilerDisplay::ProcessInput(
        InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        if (input.IsPress_LButton() || input.IsRelease_LButton()) {
            auto topId = interfaceState.TopMostWidget()._id;
            if ((topId >> 32ull) == g_InteractableIdTopPart) {
                if (input.IsRelease_LButton()) {
                    auto i = std::lower_bound(_pimpl->_toggledItems.cbegin(), _pimpl->_toggledItems.cend(), topId);
                    if (i!=_pimpl->_toggledItems.cend() && *i == topId) {
                        _pimpl->_toggledItems.erase(i);
                    } else {
                        _pimpl->_toggledItems.insert(i, topId);
                    }
                }

                return true;
            }
        }
        for (const auto& b:input._activeButtons) {
            if (b._name == KeyId_Make("up") && b._transition && b._state) {
                _pimpl->_rowOffset = std::max(0, _pimpl->_rowOffset-1);
            } else if (b._name == KeyId_Make("down") && b._transition && b._state)
                ++_pimpl->_rowOffset;
        }
        return false;
    }

    HierarchicalProfilerDisplay::HierarchicalProfilerDisplay(IHierarchicalProfiler* profiler)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_profiler = profiler;
        auto* pimpl = _pimpl.get();
        _pimpl->_listenerId = _pimpl->_profiler->AddEventListener(
            [pimpl](IHierarchicalProfiler::RawEventData data) { pimpl->IngestFrameData(data); });
    }

    HierarchicalProfilerDisplay::~HierarchicalProfilerDisplay()
    {
        _pimpl->_profiler->RemoveEventListener(_pimpl->_listenerId);
    }
}}
