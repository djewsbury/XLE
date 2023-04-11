// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebuggingDisplay.h"
#include "Font.h"
#include "FontRendering.h"
#include "ShapesRendering.h"
#include "DrawText.h"
#include "ShapesInternal.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../RenderCore/UniformsStream.h"
#include "../ConsoleRig/Console.h"
#include "../Tools/EntityInterface/MountedData.h"
#include "../Formatters/IDynamicFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../ConsoleRig/ResourceBox.h"       // for FindCachedBox
#include "../Assets/Marker.h"
#include "../Assets/Assets.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/IntrusivePtr.h"
#include "../xleres/FileList.h"
#include <stdarg.h>
#include <assert.h>
#include <string.h>

using namespace Utility::Literals;
using namespace PlatformRig::Literals;
using namespace Assets::Literals;

#pragma warning(disable:4244)   // conversion from 'int' to 'float', possible loss of data

namespace RenderOverlays { namespace DebuggingDisplay
{
    const ColorB            RoundedRectOutlineColour(255,255,255,128);
    const ColorB            RoundedRectBackgroundColour(180,200,255,128);
    static const ColorB     HistoryGraphAxisColour(64,64,64,128);
    static const ColorB     HistoryGraphLineColor(255,255,255,255);
    static const ColorB     HistoryGraphExtraLineColor(255,128,128,255);
    static const ColorB     HistoryGraphTopOfGraphBackground(200,255,200,64);
    static const ColorB     HistoryGraphBottomOfGraphBackground(200,255,200,0);
    static const ColorB     HistoryGraphTopOfGraphBackground_Peak(128,200,255,196);
    static const ColorB     HistoryGraphBottomOfGraphBackground_Peak(128,200,255,64);
    static const ColorB     GraphLabel(255, 255, 255, 128);

    ///////////////////////////////////////////////////////////////////////////////////
    ScrollBar::Coordinates::Coordinates(const Rect& rect, float minValue, float maxValue, float visibleWindowSize, Flags::BitField flags)
    {
        const Coord buttonHeight = (flags&Flags::NoUpDown)?0:std::min(Coord(rect.Width()*.75f), rect.Height()/3);
        _interactableRect = rect;
        _scrollAreaRect = rect;
        _flags = flags;

        if (maxValue > minValue) {
            if (!(flags&Flags::Horizontal)) {
                _thumbHeight = Coord(_scrollAreaRect.Height() * std::min(1.f, visibleWindowSize / (maxValue-minValue)));
                _valueToPixels = float(_scrollAreaRect._bottomRight[1]-_scrollAreaRect._topLeft[1]-_thumbHeight) / (maxValue-minValue);
                _pixelsBase = _scrollAreaRect._topLeft[1] + _thumbHeight/2;
                _windowHeight = _scrollAreaRect.Height();
            } else {
                _thumbHeight = Coord(_scrollAreaRect.Width() * std::min(1.f, visibleWindowSize / (maxValue-minValue)));
                _valueToPixels = float(_scrollAreaRect._bottomRight[0]-_scrollAreaRect._topLeft[0]-_thumbHeight) / (maxValue-minValue);
                _pixelsBase = _scrollAreaRect._topLeft[0] + _thumbHeight/2;
                _windowHeight = _scrollAreaRect.Width();
            }

            _valueBase = minValue;
            _maxValue = maxValue;
        } else {
            _valueToPixels = 0;
            _valueBase = minValue;
            _maxValue = minValue;
            _pixelsBase = _scrollAreaRect._topLeft[1] + _scrollAreaRect.Height()/2;
            _thumbHeight = _windowHeight = _scrollAreaRect.Height();
        }
    }

    Coord   ScrollBar::Coordinates::ValueToPixels(float value) const      { return Coord(_pixelsBase + ((value-_valueBase)*_valueToPixels)); }
    float   ScrollBar::Coordinates::PixelsToValue(Coord pixels) const     { return ((pixels-_pixelsBase) / _valueToPixels) + _valueBase; }
    bool    ScrollBar::Coordinates::Collapse() const                      { return _thumbHeight>=_windowHeight; }
    Rect    ScrollBar::Coordinates::Thumb(float value) const
    {
        const Coord thumbCentre    = ValueToPixels(value);
        if (!(_flags&Flags::Horizontal)) {
            const Coord thumbTop       = std::max(_scrollAreaRect._topLeft[1], thumbCentre-_thumbHeight/2);
            const Coord thumbBottom    = std::min(_scrollAreaRect._bottomRight[1], thumbCentre+_thumbHeight/2);
            return Rect( Coord2(_scrollAreaRect._topLeft[0], thumbTop), Coord2(_scrollAreaRect._bottomRight[0], thumbBottom) );
        } else {
            const Coord thumbTop       = std::max(_scrollAreaRect._topLeft[0], thumbCentre-_thumbHeight/2);
            const Coord thumbBottom    = std::min(_scrollAreaRect._bottomRight[0], thumbCentre+_thumbHeight/2);
            return Rect( Coord2(thumbTop, _scrollAreaRect._topLeft[1]), Coord2(thumbBottom, _scrollAreaRect._bottomRight[1]) );
        }
    }

    auto            ScrollBar::ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) -> IWidget::ProcessInputResult
    {
        const bool overScrollBar = (interfaceState.TopMostId() == _id);
        _draggingScrollBar = (_draggingScrollBar || overScrollBar) && (input._mouseButtonsDown&1);
        if (_draggingScrollBar) {
            _scrollOffsetPixels = (_flags&Coordinates::Flags::Horizontal)?interfaceState.MousePosition()[0]:interfaceState.MousePosition()[1];
            if (!(input._mouseButtonsDown&1)) {
                _draggingScrollBar = false;
            }
            return IWidget::ProcessInputResult::Consumed;
        }
        return IWidget::ProcessInputResult::Passthrough;
    }

    float           ScrollBar::CalculateCurrentOffset(const Coordinates& coordinates) const
    {
        if (coordinates.Collapse()) {
            _scrollOffsetPixels = ~Coord(0x0);
            _resolvedScrollOffset = 0.f;
            _pendingDelta = 0.f;
        }
        if (_scrollOffsetPixels != ~Coord(0x0)) {
            _resolvedScrollOffset = coordinates.PixelsToValue(_scrollOffsetPixels);
            _scrollOffsetPixels = ~Coord(0x0);
        }
        _resolvedScrollOffset = std::max(coordinates.MinValue(), std::min(coordinates.MaxValue(), _resolvedScrollOffset+_pendingDelta));
        _pendingDelta = 0.f;
        return _resolvedScrollOffset; 
    }

    float           ScrollBar::CalculateCurrentOffset(const Coordinates& coordinates, float oldValue) const
    {
        if (coordinates.Collapse()) {
            _scrollOffsetPixels = ~Coord(0x0);
            _resolvedScrollOffset = 0.f;
            _pendingDelta = 0.f;
        }
        if (_scrollOffsetPixels != ~Coord(0x0)) {
            _resolvedScrollOffset = coordinates.PixelsToValue(_scrollOffsetPixels);
            _scrollOffsetPixels = ~Coord(0x0);
        } else {
            _resolvedScrollOffset = oldValue;
        }
        _resolvedScrollOffset = std::max(coordinates.MinValue(), std::min(coordinates.MaxValue(), _resolvedScrollOffset+_pendingDelta));
        _pendingDelta = 0.f;
        return _resolvedScrollOffset; 
    }

    InteractableId  ScrollBar::GetID() const { return _id; }

    void ScrollBar::ProcessDelta(float delta) const { _pendingDelta+=delta; }

    ScrollBar::ScrollBar(InteractableId id, Coordinates::Flags::BitField flags)
    :       _id(id)
    ,       _flags(flags)
    {
        _scrollOffsetPixels = ~Coord(0x0);
        _resolvedScrollOffset = 0;
        _draggingScrollBar = false;
        _pendingDelta = 0.f;
    }

    static ColorB DeserializeColor(Formatters::IDynamicInputFormatter& fmttr)
	{
		IteratorRange<const void*> value;
		ImpliedTyping::TypeDesc typeDesc;
		if (!fmttr.TryRawValue(value, typeDesc))
			Throw(Formatters::FormatException("Expecting color value", fmttr.GetLocation()));

		if (auto intForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<unsigned>()) {
			return *intForm;
		} else if (auto tripletForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt3>()) {
			return ColorB{uint8_t((*tripletForm)[0]), uint8_t((*tripletForm)[1]), uint8_t((*tripletForm)[2])};
		} else if (auto quadForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt4>()) {
			return ColorB{uint8_t((*quadForm)[0]), uint8_t((*quadForm)[1]), uint8_t((*quadForm)[2]), uint8_t((*quadForm)[3])};
		} else {
			Throw(Formatters::FormatException("Could not interpret value as color", fmttr.GetLocation()));
		}
	}

    struct ScrollBarStaticData
	{
        unsigned _sectionHeight = 8;
        unsigned _sectionMargin = 2;
        unsigned _inactiveHalfWidth = 2;
        ColorB _colorA = 0xffa3be8c;
        ColorB _colorB = 0xffebcb8b;
        ColorB _colorC = 0xffbf616a;

        ScrollBarStaticData() = default;
        template<typename Formatter>
            ScrollBarStaticData(Formatter& fmttr)
        {
            uint64_t keyname;
            while (fmttr.TryKeyedItem(keyname)) {
                switch (keyname) {
                case "SectionHeight"_h: _sectionHeight = Formatters::RequireCastValue<decltype(_sectionHeight)>(fmttr); break;
                case "SectionMargin"_h: _sectionMargin = Formatters::RequireCastValue<decltype(_sectionMargin)>(fmttr); break;
                case "InactiveHalfWidth"_h: _inactiveHalfWidth = Formatters::RequireCastValue<decltype(_inactiveHalfWidth)>(fmttr); break;
                case "ColorA"_h: _colorA = DeserializeColor(fmttr); break;
                case "ColorB"_h: _colorB = DeserializeColor(fmttr); break;
                case "ColorC"_h: _colorC = DeserializeColor(fmttr); break;
                default: SkipValueOrElement(fmttr); break;
                }
            }
        }
    };

    void DrawScrollBar(IOverlayContext& context, const ScrollBar::Coordinates& coordinates, float thumbPosition, ColorB fillColour)
    {
        const Rect thumbRect = coordinates.Thumb(thumbPosition);

        auto& staticData = EntityInterface::MountedData<ScrollBarStaticData>::LoadOrDefault("cfg/displays/scrollbar"_initializer);
        unsigned sectionCount = (coordinates.ScrollArea().Height() - staticData._sectionMargin) / (staticData._sectionHeight + staticData._sectionMargin);
        if (sectionCount == 0) return;

        auto middle = (coordinates.ScrollArea()._topLeft[0] + coordinates.ScrollArea()._bottomRight[0]) / 2;

        ColorB colors[] { staticData._colorA, staticData._colorB, staticData._colorC };
        for (unsigned c=0; c<sectionCount; ++c) {
            int top = coordinates.ScrollArea()._topLeft[1] + staticData._sectionMargin + c * (staticData._sectionHeight + staticData._sectionMargin);
            int bottom = top + staticData._sectionHeight;
            bool active = top <= thumbRect._bottomRight[1] && bottom >= thumbRect._topLeft[1];
            if (active) {
                FillRectangle(
                    context,
                    {
                        Coord2 { coordinates.ScrollArea()._topLeft[0], top },
                        Coord2 { coordinates.ScrollArea()._bottomRight[0], bottom }
                    },
                    colors[c * dimof(colors) / sectionCount]);
            } else {
                FillRectangle(
                    context,
                    {
                        Coord2 { middle - staticData._inactiveHalfWidth, top },
                        Coord2 { middle + staticData._inactiveHalfWidth, bottom }
                    },
                    colors[c * dimof(colors) / sectionCount]);
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////////////

    static BarChartColoring MakeDefaultBarChartColoring()
    {
        BarChartColoring result;
        result._blocks[0] = ColorB{ 86, 232, 110};
        result._blocks[1] = ColorB{192, 220,  78};
        result._blocks[2] = ColorB{216, 125,  73};
        result._blocks[3] = ColorB{189,  57,  35};
        return result;
    }

    static BarChartColoring s_defaultBarChartColoring = MakeDefaultBarChartColoring();

    template<typename Type>
        void DrawBarChartContents(IOverlayContext& context, const Rect& graphArea, GraphSeries<Type> series, unsigned horizontalAllocation)
    {
        if (series._values.empty() || !horizontalAllocation) return;
        if (series._minValue >= series._maxValue) return;

        const auto& coloring = s_defaultBarChartColoring;

        const unsigned gapBetweenBlocks = 2;
        if (unsigned(graphArea.Width() / horizontalAllocation) >= (gapBetweenBlocks+1)) {
            // magnification horizontally
            unsigned valueLeft = (unsigned)std::max(int(series._values.size() - horizontalAllocation), 0);
            unsigned valueRight = (unsigned)series._values.size();
            unsigned allocationRight = valueRight-valueLeft;

            int blockSize = graphArea.Width() / horizontalAllocation;      // round down
            int maxVerticalBlocks = std::abs(graphArea._bottomRight[1] - graphArea._topLeft[1]) / blockSize;

            Float3 pts[6*4096];
            ColorB colors[6*4096];
            unsigned q = 0;
            bool normalWinding = graphArea._bottomRight[1] > graphArea._topLeft[1];

            for (unsigned a=0; a<allocationRight; ++a) {
                float x = LinearInterpolate((float)graphArea._topLeft[0], (float)graphArea._bottomRight[0], a/float(horizontalAllocation));
                float x2 = LinearInterpolate((float)graphArea._topLeft[0], (float)graphArea._bottomRight[0], (a+1)/float(horizontalAllocation));
                float A = (series._values[valueLeft+a] - series._minValue) / float(series._maxValue - series._minValue);
                A = std::clamp(A, 0.f, 1.f);
                float yBottom = graphArea._bottomRight[1];
                float yTop = LinearInterpolate(graphArea._bottomRight[1], graphArea._topLeft[1], A);
                unsigned verticalBlocks = std::abs(yBottom - yTop) / blockSize;
                if (!verticalBlocks) continue;

                for (int b=0; b<(int)verticalBlocks; ++b) {
                    if (q >= dimof(pts)) {
                        context.DrawTriangles(ProjectionMode::P2D, pts, q, colors);
                        q = 0;
                    }
                    if (normalWinding) {
                        int top = int(yBottom) - ((b+1)*blockSize - gapBetweenBlocks);
                        int bottom = int(yBottom) - b*blockSize;
                        pts[q+0] = AsPixelCoords(Coord2{x, top});
                        pts[q+1] = AsPixelCoords(Coord2{x, bottom});
                        pts[q+2] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, top});
                        pts[q+3] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, top});
                        pts[q+4] = AsPixelCoords(Coord2{x, bottom});
                        pts[q+5] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, bottom});
                    } else {
                        // opposite winding
                        int top = int(yBottom) + ((b+1)*blockSize - gapBetweenBlocks);
                        int bottom = int(yBottom) + b*blockSize;
                        pts[q+0] = AsPixelCoords(Coord2{x, top});
                        pts[q+1] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, top});
                        pts[q+2] = AsPixelCoords(Coord2{x, bottom});
                        pts[q+3] = AsPixelCoords(Coord2{x, bottom});
                        pts[q+4] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, top});
                        pts[q+5] = AsPixelCoords(Coord2{x2-gapBetweenBlocks, bottom});
                    }
                    auto color = coloring._blocks[std::min(3u, b * 4u / maxVerticalBlocks)];
                    colors[q+0] = color;
                    colors[q+1] = color;
                    colors[q+2] = color;
                    colors[q+3] = color;
                    colors[q+4] = color;
                    colors[q+5] = color;
                    q+=6;
                }
            }

            if (q)
                context.DrawTriangles(ProjectionMode::P2D, pts, q, colors);
        } else {
            // minification horizontally (ie, some entries will be skipped)
            assert(0);
        }
    }

    template void DrawBarChartContents(IOverlayContext& context, const Rect& graphArea, GraphSeries<unsigned> series, unsigned horizontalAllocation);
    template void DrawBarChartContents(IOverlayContext& context, const Rect& graphArea, GraphSeries<int> series, unsigned horizontalAllocation);
    template void DrawBarChartContents(IOverlayContext& context, const Rect& graphArea, GraphSeries<float> series, unsigned horizontalAllocation);

    void DrawBarGraph(IOverlayContext& context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory)
    {
        std::optional<float> t0 = minValueHistory, t1 = maxValueHistory;
        DrawBarChartContents(context, rect, GraphSeries<float>{MakeIteratorRange(values, &values[valuesCount]), t0, t1}, maxValuesCount);
        minValueHistory = t0.value(); maxValueHistory = t1.value();
    }

    void DrawHistoryGraph(IOverlayContext& context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory)
    {
        context.DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     rect._bottomRight[1])), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])), HistoryGraphAxisColour );
        context.DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],       rect._topLeft[1])), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._topLeft[0],       rect._bottomRight[1])), HistoryGraphAxisColour );

        Rect graphArea( Coord2( rect._topLeft[0]+1, rect._topLeft[1] ),
                        Coord2( rect._bottomRight[0], rect._bottomRight[1]-1 ) );

        context.DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.25f))), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.25f))), HistoryGraphAxisColour );
        context.DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f))), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f))), HistoryGraphAxisColour );
        context.DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.75f))), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.75f))), HistoryGraphAxisColour );

        if (valuesCount) {
                // find the max and min values in our data set...
            float maxValue = -std::numeric_limits<float>::max(), minValue = std::numeric_limits<float>::max();
            unsigned peakIndex = 0;
            for (unsigned c=0; c<valuesCount; ++c) {
                maxValue = std::max(values[c], maxValue);
                minValue = std::min(values[c], minValue);
                if (values[c] > values[peakIndex]) {peakIndex = c;}
            }

            minValue = std::min(minValue, maxValue*.75f);

            minValue = minValueHistory = std::min(LinearInterpolate(minValueHistory, minValue, 0.15f), minValue);
            maxValue = maxValueHistory = std::max(LinearInterpolate(maxValueHistory, maxValue, 0.15f), maxValue);

            VLA_UNSAFE_FORCE(Float3, graphLinePoints, valuesCount*2);
            VLA_UNSAFE_FORCE(Float3, graphTrianglePoints, (valuesCount-1)*3*2);
            VLA_UNSAFE_FORCE(ColorB, graphTriangleColors, (valuesCount-1)*3*2);

            //  figure out y axis coordination conversion...
            float yB = -(graphArea._bottomRight[1]-graphArea._topLeft[1]-20)/float(maxValue-minValue);
            float yA = float(graphArea._bottomRight[1]-10) - yB * minValue; 
            float xB = (graphArea._bottomRight[0]-graphArea._topLeft[0])/float(maxValuesCount-1);
            float yZ = float(graphArea._bottomRight[1]);

            Float3* ptIterator = graphTrianglePoints;
            ColorB* colorIterator = graphTriangleColors;
            for (unsigned c=0; c<(valuesCount-1); ++c) {
                float x0 = graphArea._topLeft[0] + xB*c;
                float x1 = graphArea._topLeft[0] + xB*(c+1);
                float y0 = yA + yB * values[c];
                float y1 = yA + yB * values[c+1];

                graphLinePoints[c*2]    = AsPixelCoords(Coord2(x0+.5f, y0+.5f));
                graphLinePoints[c*2+1]  = AsPixelCoords(Coord2(x1+.5f, y1+.5f));
                    
                bool peak = (c == peakIndex || (c+1) == peakIndex);
                ColorB colorTop      = peak?HistoryGraphTopOfGraphBackground_Peak:HistoryGraphTopOfGraphBackground;
                ColorB colorBottom   = peak?HistoryGraphBottomOfGraphBackground_Peak:HistoryGraphBottomOfGraphBackground;
                *ptIterator++ = AsPixelCoords(Coord2(x0+.5f, y0+.5f));
                *ptIterator++ = AsPixelCoords(Coord2(x0+.5f, yZ+.5f));
                *ptIterator++ = AsPixelCoords(Coord2(x1+.5f, y1+.5f));
                *colorIterator++ = colorTop;
                *colorIterator++ = colorBottom;
                *colorIterator++ = colorTop;

                *ptIterator++ = AsPixelCoords(Coord2(x1+.5f, y1+.5f));
                *ptIterator++ = AsPixelCoords(Coord2(x0+.5f, yZ+.5f));
                *ptIterator++ = AsPixelCoords(Coord2(x1+.5f, yZ+.5f));
                *colorIterator++ = colorTop;
                *colorIterator++ = colorBottom;
                *colorIterator++ = colorBottom;
            }

            assert((ptIterator-graphTrianglePoints) == (valuesCount-1)*3*2);
            assert((colorIterator-graphTriangleColors) == (valuesCount-1)*3*2);
            context.DrawTriangles(ProjectionMode::P2D, graphTrianglePoints, ptIterator-graphTrianglePoints, graphTriangleColors);
            context.DrawLines(ProjectionMode::P2D, graphLinePoints, (valuesCount-1)*2, HistoryGraphLineColor);

            {
                    // label the peak & write min and max values
                Coord2 peakPos(graphArea._topLeft[0] + xB*peakIndex, yA + yB * values[peakIndex] - 14);
                Coord2 maxPos(graphArea._topLeft[0] + 14, graphArea._topLeft[1] + 8);
                Coord2 minPos(graphArea._topLeft[0] + 14, graphArea._bottomRight[1] - 18);

                DrawText().Color(GraphLabel).FormatAndDraw(context, Rect(peakPos, peakPos),  "%6.2f", values[peakIndex]);
                DrawText().Color(GraphLabel).FormatAndDraw(context, Rect(minPos, minPos),    "%6.2f", minValue);
                DrawText().Color(GraphLabel).FormatAndDraw(context, Rect(maxPos, maxPos),    "%6.2f", maxValue);
            }
        }
    }

    void DrawHistoryGraph_ExtraLine(IOverlayContext& context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float minValue, float maxValue)
    {
        Rect graphArea( Coord2( rect._topLeft[0]+1, rect._topLeft[1] ),
                        Coord2( rect._bottomRight[0], rect._bottomRight[1]-1 ));

        if (valuesCount) {
            Float3 graphLinePoints[1024];
            assert(dimof(graphLinePoints)>=(valuesCount*2));

                //  figure out y axis coordination conversion...
            float yB = -(graphArea._bottomRight[1]-graphArea._topLeft[1]-20)/float(maxValue-minValue);
            float yA = float(graphArea._bottomRight[1]-10) - yB * minValue; 
            float xB = (graphArea._bottomRight[0]-graphArea._topLeft[0])/float(maxValuesCount-1);

            for (unsigned c=0; c<(valuesCount-1); ++c) {
                float x0 = graphArea._topLeft[0] + xB*c;
                float x1 = graphArea._topLeft[0] + xB*(c+1);
                float y0 = yA + yB * values[c];
                float y1 = yA + yB * values[c+1];

                graphLinePoints[c*2]    = AsPixelCoords(Float2(x0+.5f, y0+.5f));
                graphLinePoints[c*2+1]  = AsPixelCoords(Float2(x1+.5f, y1+.5f));
            }

            context.DrawLines(ProjectionMode::P2D, graphLinePoints, (valuesCount-1)*2, HistoryGraphExtraLineColor);
        }
    }

    void        FillTriangles(IOverlayContext& context, const Float2 triangleCoordinates[], const ColorB triangleColours[], unsigned triangleCount)
    {
        VLA_UNSAFE_FORCE(Float3, pixelCoords, triangleCount*3);
        for (unsigned c=0; c<triangleCount*3; ++c)
            pixelCoords[c] = AsPixelCoords(triangleCoordinates[c]);
        context.DrawTriangles(ProjectionMode::P2D, pixelCoords, triangleCount*3, triangleColours);
    }

    void        FillTriangles(IOverlayContext& context, const Float2 triangleCoordinates[], ColorB colour, unsigned triangleCount)
    {
        VLA_UNSAFE_FORCE(Float3, pixelCoords, triangleCount*3);
        for (unsigned c=0; c<triangleCount*3; ++c)
            pixelCoords[c] = AsPixelCoords(triangleCoordinates[c]);
        context.DrawTriangles(ProjectionMode::P2D, pixelCoords, triangleCount*3, colour);
    }

    void        DrawLines(IOverlayContext& context, const Coord2 lineCoordinates[], const ColorB lineColours[], unsigned lineCount)
    {
        VLA_UNSAFE_FORCE(Float3, pixelCoords, lineCount*2);
        for (unsigned c=0; c<lineCount*2; ++c)
            pixelCoords[c] = AsPixelCoords(Coord2(lineCoordinates[c][0], lineCoordinates[c][1]));
        context.DrawLines(ProjectionMode::P2D, pixelCoords, lineCount*2, lineColours);
    }

    ///////////////////////////////////////////////////////////////////////////////////

    struct TableStaticData
	{
        ColorB _frameColor = 0xffefe9d9;
        ColorB _headerLabelColor = 0xffffffff;
        int _headerLabelHorzPadding = 16;
        int _headerLabelLeftMargin = 16;
        int _frameLineWeight = 4;
        int _downStrokeLength = 24;
        int _downStrokeWeight = 2;
        int _leftAndRightBorderArea = 16;
        int _valueHorizPadding = 6;
        int _headerAreaHeight = 60;

        TableStaticData() = default;
        template<typename Formatter>
            TableStaticData(Formatter& fmttr)
        {
            uint64_t keyname;
            while (fmttr.TryKeyedItem(keyname)) {
                switch (keyname) {
                case "FrameColor"_h: _frameColor = DeserializeColor(fmttr); break;
                case "HeaderLabelColor"_h: _headerLabelColor = DeserializeColor(fmttr); break;
                case "HeaderLabelHorzPadding"_h: _headerLabelHorzPadding = Formatters::RequireCastValue<decltype(_headerLabelHorzPadding)>(fmttr); break;
                case "HeaderLabelLeftMargin"_h: _headerLabelLeftMargin = Formatters::RequireCastValue<decltype(_headerLabelLeftMargin)>(fmttr); break;
                case "FrameLineWeight"_h: _frameLineWeight = Formatters::RequireCastValue<decltype(_frameLineWeight)>(fmttr); break;
                case "DownStrokeLength"_h: _downStrokeLength = Formatters::RequireCastValue<decltype(_downStrokeLength)>(fmttr); break;
                case "DownStrokeWeight"_h: _downStrokeWeight = Formatters::RequireCastValue<decltype(_downStrokeWeight)>(fmttr); break;
                case "LeftAndRightBorderArea"_h: _leftAndRightBorderArea = Formatters::RequireCastValue<decltype(_leftAndRightBorderArea)>(fmttr); break;
                case "ValueHorizPadding"_h: _valueHorizPadding = Formatters::RequireCastValue<decltype(_valueHorizPadding)>(fmttr); break;
                case "HeaderAreaHeight"_h: _headerAreaHeight = Formatters::RequireCastValue<decltype(_headerAreaHeight)>(fmttr); break;
                default: SkipValueOrElement(fmttr); break;
                }
            }
        }
    };

    ///////////////////////////////////////////////////////////////////////////////////
    Coord DrawTableHeaders(IOverlayContext& context, const Rect& initialRect, IteratorRange<std::pair<std::string, unsigned>*> fieldHeaders)
    {
        auto& staticData = EntityInterface::MountedData<TableStaticData>::LoadOrDefault("cfg/displays/table"_initializer);
        auto& fnt = *ConsoleRig::FindCachedBox<Internal::DefaultFontsBox>()._tableHeaderFont;
        auto fntLineHeight = fnt.GetFontProperties()._lineHeight;

        Layout tempLayout(
            Rect { initialRect._topLeft, { initialRect._bottomRight[0], std::min(initialRect._topLeft[1] + int(fntLineHeight)*2, initialRect._bottomRight[1])} });
        tempLayout._paddingInternalBorder = 0;
        tempLayout._paddingBetweenAllocations = 0;
        float middle = float(tempLayout.GetMaximumSize()._topLeft[1] + tempLayout.GetMaximumSize()._bottomRight[1]) / 2.f;
        float downStrokeEnd = middle + staticData._downStrokeLength;
        float longDownStrokeEnd = middle + 2*staticData._downStrokeLength;

        unsigned lastLabelRight = tempLayout.GetMaximumSize()._topLeft[0];
        {
            auto leftArea = tempLayout.AllocateFullHeight(staticData._leftAndRightBorderArea);
            lastLabelRight = (leftArea._topLeft[0] + leftArea._bottomRight[0])/2;
        }
        unsigned rightMostDownStroke = tempLayout.GetMaximumSize()._bottomRight[0] - staticData._leftAndRightBorderArea / 2;
        tempLayout._maximumSize._bottomRight[0] -= staticData._leftAndRightBorderArea;

        VLA_UNSAFE_FORCE(Rect, labelRects, fieldHeaders.size());
        unsigned labelRectCount = 0;
        for (auto i=fieldHeaders.begin(); i!=fieldHeaders.end(); ++i) {
            assert(i->second);

            // calculate rectangles for the label area
            auto labelWidth = StringWidth(fnt, MakeStringSection(i->first));
            unsigned additionalPadding = 0;
            if (i!=fieldHeaders.begin()) additionalPadding += staticData._valueHorizPadding/2;
            if ((i+1)!=fieldHeaders.end()) additionalPadding += staticData._valueHorizPadding/2;
            Rect r;
            if ((i+1) != fieldHeaders.end())
                r = tempLayout.AllocateFullHeight(i->second+additionalPadding);
            else
                r = tempLayout.AllocateFullHeight(tempLayout.GetWidthRemaining());     // allocate remaining space
            i->second = std::max(0u, r.Width() - additionalPadding);      // update width with final calculated value

            Rect labelFrame { 
                { r._topLeft[0] + staticData._headerLabelLeftMargin, r._topLeft[1] },
                { r._topLeft[0] + staticData._headerLabelLeftMargin + 2*staticData._headerLabelHorzPadding + 2*staticData._frameLineWeight + int(labelWidth), r._bottomRight[1] }
            };
            labelFrame._bottomRight[1] = std::min(labelFrame._bottomRight[1], r._bottomRight[1]);
            Rect labelContent {
                { labelFrame._topLeft[0] + staticData._headerLabelHorzPadding + staticData._frameLineWeight, r._topLeft[1] },
                { labelFrame._topLeft[0] + staticData._headerLabelHorzPadding + staticData._frameLineWeight + int(labelWidth), r._bottomRight[1] }
            };
            labelFrame._bottomRight[1] = std::min(labelContent._bottomRight[1], labelFrame._bottomRight[1]);

            // down strokes separating columns
            if (i != fieldHeaders.begin()) {
                Float2 downwardStroke[] {
                    { r._topLeft[0], middle },
                    { r._topLeft[0], downStrokeEnd }
                };
                SolidLine(context, downwardStroke, staticData._frameColor, staticData._downStrokeWeight);
            }

            // strokes along the top
            {
                if (i == fieldHeaders.begin()) {
                    Float2 horizStroke[] {
                        { lastLabelRight, longDownStrokeEnd },
                        { lastLabelRight, middle },
                        { labelFrame._topLeft[0], middle }
                    };
                    SolidLine(context, horizStroke, staticData._frameColor, staticData._frameLineWeight);
                } else {
                    Float2 horizStroke[] {
                        { lastLabelRight, middle },
                        { labelFrame._topLeft[0], middle }
                    };
                    SolidLine(context, horizStroke, staticData._frameColor, staticData._frameLineWeight);
                }

                Float2 A[] {
                    { labelFrame._topLeft[0], middle - fntLineHeight/2 },
                    { labelFrame._topLeft[0], middle + fntLineHeight/2 }
                };
                SolidLine(context, A, staticData._frameColor, staticData._frameLineWeight);
                Float2 B[] {
                    { labelFrame._bottomRight[0], middle - fntLineHeight/2 },
                    { labelFrame._bottomRight[0], middle + fntLineHeight/2 }
                };
                SolidLine(context, B, staticData._frameColor, staticData._frameLineWeight);
            }
            lastLabelRight = labelFrame._bottomRight[0];

            labelRects[labelRectCount++] = labelContent;
        }

        // last stroke along the top
        {
            Float2 horizStroke[] {
                { lastLabelRight, middle },
                { rightMostDownStroke, middle },
                { rightMostDownStroke, longDownStrokeEnd }
            };
            SolidLine(context, horizStroke, staticData._frameColor, staticData._frameLineWeight);
        }

        for (unsigned c=0; c<labelRectCount; ++c) {
            auto labelContent = labelRects[c];
            if (!IsGood(labelContent)) continue;
            DrawText()
                .Font(fnt)
                .Alignment(TextAlignment::Left)
                .Color(RandomPaletteColorTable[c * RandomPaletteColorTable_Size / labelRectCount])
                .Flags(DrawTextFlags::Shadow)
                .Draw(context, labelContent, MakeStringSection(fieldHeaders[c].first));
        }

        return staticData._headerAreaHeight;
    }

    Rect DrawEmbeddedInRightEdge(IOverlayContext& context, const Rect& rect)
    {
        // Sometimes we embed widgets in the frame of a table (such as a scrollbar)
        // Here we draw something into the frame to highlight it, and calculate the correct position for the embedded widget
        auto& staticData = EntityInterface::MountedData<TableStaticData>::LoadOrDefault("cfg/displays/table"_initializer);

        auto& fnt = *ConsoleRig::FindCachedBox<Internal::DefaultFontsBox>()._tableHeaderFont;

        Rect frame {
            { rect._bottomRight[0] - staticData._leftAndRightBorderArea, rect._topLeft[1] + fnt.GetFontProperties()._lineHeight + 2 * staticData._downStrokeLength },
            { rect._bottomRight[0], rect._bottomRight[1] - staticData._headerAreaHeight / 2 - 2 * staticData._downStrokeLength }
        };

        Float2 A[] {
            frame._topLeft,
            {frame._bottomRight[0], frame._topLeft[1]}
        };
        Float2 B[] {
            {frame._topLeft[0], frame._bottomRight[1]},
            frame._bottomRight
        };
        SolidLine(context, A, staticData._frameColor, staticData._frameLineWeight);
        SolidLine(context, B, staticData._frameColor, staticData._frameLineWeight);

        return {
            frame._topLeft + Coord2{ staticData._frameLineWeight, staticData._frameLineWeight },
            frame._bottomRight - Coord2{ staticData._frameLineWeight, staticData._frameLineWeight }
        };
    }

    Coord DrawTableBase(IOverlayContext& context, const Rect& rect)
    {
        auto& staticData = EntityInterface::MountedData<TableStaticData>::LoadOrDefault("cfg/displays/table"_initializer);
        auto& fnt = *ConsoleRig::FindCachedBox<Internal::DefaultFontsBox>()._tableHeaderFont;
        auto fntLineHeight = fnt.GetFontProperties()._lineHeight;

        auto bottom = std::min(rect._topLeft[1] + int(fntLineHeight)*2, rect._bottomRight[1]);
        auto middle = (rect._topLeft[1] + bottom) / 2;
        Float2 stroke[] {
            { rect._topLeft[0] + staticData._leftAndRightBorderArea/2, middle - 2*staticData._downStrokeLength },
            { rect._topLeft[0] + staticData._leftAndRightBorderArea/2, middle },
            { rect._bottomRight[0] - staticData._leftAndRightBorderArea/2, middle },
            { rect._bottomRight[0] - staticData._leftAndRightBorderArea/2, middle - 2*staticData._downStrokeLength }
        };
        SolidLine(context, stroke, staticData._frameColor, staticData._frameLineWeight);
        return fntLineHeight*2;
    }

    static Rect AllocateTableEntry(Layout& layout, const TableStaticData& staticData, unsigned width, bool first, bool last)
    {
        Rect r;
        unsigned additionalPadding = 0;
        if (!first) additionalPadding += staticData._valueHorizPadding/2;
        if (!last) additionalPadding += staticData._valueHorizPadding/2;
        if (!last)
            r = layout.AllocateFullHeight(width + additionalPadding);
        else
            r = layout.AllocateFullHeight(std::max(0, layout.GetWidthRemaining()));     // allocate remaining space        
        if (!first) r._topLeft[0] += staticData._valueHorizPadding/2;
        if (!last) r._bottomRight[0] -= staticData._valueHorizPadding/2;
        return r;
    }

    Coord DrawTableEntry(       IOverlayContext& context,
                                const Rect& rect,
                                IteratorRange<const std::pair<std::string, unsigned>*> fieldHeaders, 
                                const std::map<std::string, TableElement>& entry,
                                bool highlighted)
    {
        static const ColorB TextColor ( 255, 255, 255, 255 );

        auto* fonts = ConsoleRig::TryActualizeCachedBox<Internal::DefaultFontsBox>();
        if (!fonts) return 0;
        
        auto& staticData = EntityInterface::MountedData<TableStaticData>::LoadOrDefault("cfg/displays/table"_initializer);
        Layout tempLayout(rect);
        tempLayout._paddingInternalBorder = 0;
        tempLayout._paddingBetweenAllocations = 0;

        auto leftArea = tempLayout.AllocateFullHeight(staticData._leftAndRightBorderArea);
        tempLayout._maximumSize._bottomRight[0] -= staticData._leftAndRightBorderArea;

        if (highlighted) {
            // when highlighted, we need to calculate the height we're going to need before we draw the values
            auto layoutCopy = tempLayout;
            Coord heightUsed = 0;
            for (auto i=fieldHeaders.begin(); i!=fieldHeaders.end(); ++i) {
                assert(i->second);
                auto s = entry.find(i->first);
                auto r = AllocateTableEntry(layoutCopy, staticData, i->second, i==fieldHeaders.begin(), (i+1)==fieldHeaders.end());

                if (s != entry.end() && !s->second._label.empty() && IsGood(r)) {
                    auto lineCount = StringSplitByWidth(*fonts->_tableValuesFont, MakeStringSection(s->second._label), FLT_MAX, {}, {}, 0.f, false)._sections.size();
                    heightUsed = std::max(heightUsed, Coord(lineCount * fonts->_tableValuesFont->GetFontProperties()._lineHeight));
                }
            }
            FillRectangle(context, {rect._topLeft, {rect._bottomRight[0], std::min(rect._topLeft[1] + heightUsed, rect._bottomRight[1])}}, ColorB::White);
        }

        Coord heightUsed = 0;
        for (auto i=fieldHeaders.begin(); i!=fieldHeaders.end(); ++i) {
            assert(i->second);

            auto s = entry.find(i->first);
            auto r = AllocateTableEntry(tempLayout, staticData, i->second, i==fieldHeaders.begin(), (i+1)==fieldHeaders.end());

            if (s != entry.end() && !s->second._label.empty() && IsGood(r)) {
                auto textSpace = DrawText()
                    .Alignment(TextAlignment::TopLeft)
                    .Color(highlighted ? ColorB::Black : TextColor)
                    .Font(*fonts->_tableValuesFont)
                    .Flags((highlighted ? 0 : DrawTextFlags::Shadow) | DrawTextFlags::Clip)
                    .Draw(context, r, MakeStringSection(s->second._label));
                heightUsed = std::max(textSpace[1] - r._topLeft[1], heightUsed);
            }
        }

        return heightUsed;
    }

    void DrawTableHeaders(IOverlayContext& context, Layout& layout, IteratorRange<std::pair<std::string, unsigned>*> fieldHeaders)
    {
        auto rect = Layout{layout}.AllocateFullWidthFraction(1.f);
        if (!IsGood(rect)) return;
        auto height = DrawTableHeaders(context, rect, fieldHeaders);
        layout.AllocateFullWidth(height);
    }

    void DrawTableBase(IOverlayContext& context, Layout& layout)
    {
        auto rect = Layout{layout}.AllocateFullWidthFraction(1.f);
        if (!IsGood(rect)) return;
        auto height = DrawTableBase(context, rect);
        layout.AllocateFullWidth(height);
    }

    bool DrawTableEntry(IOverlayContext& context, Layout& layout, IteratorRange<const std::pair<std::string, unsigned>*> fieldHeaders, const std::map<std::string, TableElement>& entry, bool highlighted)
    {
        auto rect = Layout{layout}.AllocateFullWidthFraction(1.f);
        if (!IsGood(rect)) return false;
        auto height = DrawTableEntry(context, rect, fieldHeaders, entry, highlighted);
        layout.AllocateFullWidth(height);
        return true;
    }

    ///////////////////////////////////////////////////////////////////////////////////
    static void SetQuadPts(Float3 destination[], const Float3& A, const Float3& B, const Float3& C, const Float3& D)
    {
        // z pattern ordering
        destination[0] = A; destination[1] = B; destination[2] = C;
        destination[3] = C; destination[4] = B; destination[5] = D;
    }

    class HexahedronCorners
    {
    public:
        Float3  _worldSpacePts[8];

        static HexahedronCorners FromAABB(const AABoundingBox& box, const Float3x4& localToWorld);
        static HexahedronCorners FromFrustumCorners(const Float4x4& worldToProjection);
    };

#pragma warning(push)
#pragma warning(disable:4701)
    HexahedronCorners HexahedronCorners::FromAABB(const AABoundingBox& box, const Float3x4& localToWorld)
    {
        HexahedronCorners result;
        const Float3 bbpts[] = 
        {
            // z pattern ordering to match FromFrustumCorners
            Float3(0.f, 0.f, 0.f), Float3(0.f, 1.f, 0.f),
            Float3(1.f, 0.f, 0.f), Float3(1.f, 1.f, 0.f),
            Float3(0.f, 0.f, 1.f), Float3(0.f, 1.f, 1.f),
            Float3(1.f, 0.f, 1.f), Float3(1.f, 1.f, 1.f)
        };

        for (unsigned c=0; c<dimof(bbpts); ++c) {
            result._worldSpacePts[c] = Float3(     
                LinearInterpolate(std::get<0>(box)[0], std::get<1>(box)[0], bbpts[c][0]),
                LinearInterpolate(std::get<0>(box)[1], std::get<1>(box)[1], bbpts[c][1]),
                LinearInterpolate(std::get<0>(box)[2], std::get<1>(box)[2], bbpts[c][2]));
            result._worldSpacePts[c] = TransformPoint(localToWorld, result._worldSpacePts[c]);
        }
        return result;
    }
#pragma warning(pop)

    HexahedronCorners HexahedronCorners::FromFrustumCorners(const Float4x4& worldToProjection)
    {
        HexahedronCorners result;
        CalculateAbsFrustumCorners(result._worldSpacePts, worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
        return result;
    }

    static const float          BoundingBoxLineThickness = 3.f;
    static const unsigned char  BoundingBoxTriangleAlpha = 0x1f;
    static const unsigned char  BoundingBoxLineAlpha     = 0xff;

    void DrawHexahedronCorners(IOverlayContext& context, const HexahedronCorners&corners, ColorB entryColour, unsigned partMask)
    {
        if (partMask & 0x2) {
            Float3 lines[12*2];
            lines[ 0*2+0] = corners._worldSpacePts[0]; lines[ 0*2+1] = corners._worldSpacePts[1];
            lines[ 1*2+0] = corners._worldSpacePts[1]; lines[ 1*2+1] = corners._worldSpacePts[3];
            lines[ 2*2+0] = corners._worldSpacePts[3]; lines[ 2*2+1] = corners._worldSpacePts[2];
            lines[ 3*2+0] = corners._worldSpacePts[2]; lines[ 3*2+1] = corners._worldSpacePts[0];

            lines[ 4*2+0] = corners._worldSpacePts[4]; lines[ 4*2+1] = corners._worldSpacePts[5];
            lines[ 5*2+0] = corners._worldSpacePts[5]; lines[ 5*2+1] = corners._worldSpacePts[7];
            lines[ 6*2+0] = corners._worldSpacePts[7]; lines[ 6*2+1] = corners._worldSpacePts[6];
            lines[ 7*2+0] = corners._worldSpacePts[6]; lines[ 7*2+1] = corners._worldSpacePts[4];

            lines[ 8*2+0] = corners._worldSpacePts[0]; lines[ 8*2+1] = corners._worldSpacePts[4];
            lines[ 9*2+0] = corners._worldSpacePts[1]; lines[ 9*2+1] = corners._worldSpacePts[5];
            lines[10*2+0] = corners._worldSpacePts[2]; lines[10*2+1] = corners._worldSpacePts[6];
            lines[11*2+0] = corners._worldSpacePts[3]; lines[11*2+1] = corners._worldSpacePts[7];

            context.DrawLines( 
                ProjectionMode::P3D, 
                lines, dimof(lines), 
                ColorB(entryColour.r, entryColour.g, entryColour.b, BoundingBoxLineAlpha), 
                BoundingBoxLineThickness);
        }

        if (partMask & 0x1) {
            Float3 triangles[6*2*3];
            SetQuadPts(&triangles[0*2*3], corners._worldSpacePts[0], corners._worldSpacePts[1], corners._worldSpacePts[2], corners._worldSpacePts[3]);
            SetQuadPts(&triangles[1*2*3], corners._worldSpacePts[4], corners._worldSpacePts[5], corners._worldSpacePts[0], corners._worldSpacePts[1]);
            SetQuadPts(&triangles[2*2*3], corners._worldSpacePts[2], corners._worldSpacePts[3], corners._worldSpacePts[6], corners._worldSpacePts[7]);
            SetQuadPts(&triangles[3*2*3], corners._worldSpacePts[6], corners._worldSpacePts[7], corners._worldSpacePts[4], corners._worldSpacePts[5]);

            SetQuadPts(&triangles[4*2*3], corners._worldSpacePts[4], corners._worldSpacePts[0], corners._worldSpacePts[6], corners._worldSpacePts[2]);
            SetQuadPts(&triangles[5*2*3], corners._worldSpacePts[1], corners._worldSpacePts[5], corners._worldSpacePts[3], corners._worldSpacePts[7]);

            context.DrawTriangles(
                ProjectionMode::P3D, 
                triangles, dimof(triangles),
                ColorB(entryColour.r, entryColour.g, entryColour.b, BoundingBoxTriangleAlpha));
        }
    }

    void DrawBoundingBox(
        IOverlayContext& context, const AABoundingBox& box, 
        const Float3x4& localToWorld, 
        ColorB entryColour, unsigned partMask)
    {
        auto corners = HexahedronCorners::FromAABB(box, localToWorld);
        DrawHexahedronCorners(context, corners, entryColour, partMask);
    }
    
    void DrawFrustum(
        IOverlayContext& context, const Float4x4& worldToProjection, 
        ColorB entryColour, unsigned partMask)
    {
        auto corners = HexahedronCorners::FromFrustumCorners(worldToProjection);
        DrawHexahedronCorners(context, corners, entryColour, partMask);
    }

    ///////////////////////////////////////////////////////////////////////////////////
    static float Saturate(float value) { return std::max(std::min(value, 1.f), 0.f); }

    class DebugDisplayResources
    {
    public:
        RenderCore::Techniques::ImmediateDrawableMaterial _horizTweakerBarMaterial;
        RenderCore::Techniques::ImmediateDrawableMaterial _tagShaderMaterial;
        RenderCore::Techniques::ImmediateDrawableMaterial _gridBackgroundMaterial;

        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; };
        ::Assets::DependencyValidation _depVal;

        DebugDisplayResources(
            const RenderCore::Assets::ResolvedMaterial& horizTweakerBarMaterial,
            const RenderCore::Assets::ResolvedMaterial& tagShaderMaterial,
            const RenderCore::Assets::ResolvedMaterial& gridBackgroundMaterial)
        {
            _horizTweakerBarMaterial = BuildImmediateDrawableMaterial(horizTweakerBarMaterial);
            _tagShaderMaterial = BuildImmediateDrawableMaterial(tagShaderMaterial);
            _gridBackgroundMaterial = BuildImmediateDrawableMaterial(gridBackgroundMaterial);

            _depVal = ::Assets::GetDepValSys().Make();
            _depVal.RegisterDependency(horizTweakerBarMaterial.GetDependencyValidation());
            _depVal.RegisterDependency(tagShaderMaterial.GetDependencyValidation());
            _depVal.RegisterDependency(gridBackgroundMaterial.GetDependencyValidation());
        }

        static void ConstructToPromise(std::promise<std::shared_ptr<DebugDisplayResources>>&& promise)
        {
            auto horizTweakerBarMaterial = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":HorizTweakerBar");
            auto tagShaderMaterial = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":TagShader");
            auto gridBackgroundMaterial = ::Assets::MakeAsset<RenderCore::Assets::ResolvedMaterial>(RENDEROVERLAYS_SHAPES_MATERIAL ":GridBackgroundShader");
            ::Assets::WhenAll(horizTweakerBarMaterial, tagShaderMaterial, gridBackgroundMaterial).ThenConstructToPromise(std::move(promise));
        }

        std::vector<std::unique_ptr<ParameterBox>> _retainedParameterBoxes;
        RenderCore::Techniques::ImmediateDrawableMaterial BuildImmediateDrawableMaterial(
            const RenderCore::Assets::ResolvedMaterial& rawMat)
        {
            RenderCore::Techniques::ImmediateDrawableMaterial result;
            // somewhat awkwardly, we need to protect the lifetime of the shader selector box so it lives as long as the result
            if (rawMat._selectors.GetCount() != 0) {
                auto newBox = std::make_unique<ParameterBox>(rawMat._selectors);
                result._shaderSelectors = newBox.get();
                _retainedParameterBoxes.emplace_back(std::move(newBox));
            }
            result._stateSet = rawMat._stateSet;
            result._patchCollection = std::make_shared<RenderCore::Assets::ShaderPatchCollection>(rawMat._patchCollection);
            return result;
        }
    };

    void HTweakerBar_Draw(IOverlayContext& context, const ScrollBar::Coordinates& coordinates, float thumbPosition)
    {
        const auto r = coordinates.InteractableRect();
        float t = Saturate((thumbPosition - coordinates.MinValue()) / float(coordinates.MaxValue() - coordinates.MinValue()));
        auto* res = ConsoleRig::TryActualizeCachedBox<DebugDisplayResources>();
        if (!res) return;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(Coord2(r._topLeft[0], r._topLeft[1])),
            AsPixelCoords(Coord2(r._bottomRight[0], r._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(t, 0.f), Float2(t, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_horizTweakerBarMaterial});
    }

    void HTweakerBar_DrawLabel(IOverlayContext& context, const Rect& rect)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<DebugDisplayResources>();
        if (!res) return;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(0.f, 0.f), Float2(0.f, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_tagShaderMaterial});
    }

    void HTweakerBar_DrawGridBackground(IOverlayContext& context, const Rect& rect)
    {
        auto* res = ConsoleRig::TryActualizeCachedBox<DebugDisplayResources>();
        if (!res) return;
        Internal::DrawPCCTTQuad(
            context,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(0.f, 0.f), Float2(0.f, 0.f),
            RenderCore::Techniques::ImmediateDrawableMaterial{res->_gridBackgroundMaterial});
    }

    ///////////////////////////////////////////////////////////////////////////////////
    void    IWidget::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) {}
    auto    IWidget::ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) -> ProcessInputResult { return ProcessInputResult::Passthrough; }
    IWidget::~IWidget() {}

    InteractableId  InteractableId_Make(StringSection<char> name)   { return Hash64(name.begin(), name.end()); }

    static InterfaceState BuildInterfaceState(Interactables& interactables, const PlatformRig::InputContext& viewInputContext, const Coord2& mousePosition, unsigned mouseButtonsHeld, InterfaceState::Capture capture)
    {
        auto i = std::find_if(interactables._hotAreas.begin(), interactables._hotAreas.end(), [capturingId=capture._hotArea._id](const auto& c) { return c._id == capturingId; });
        if (i != interactables._hotAreas.end()) {
            capture._hotArea = *i;
        } else
            capture = {};

        return InterfaceState(viewInputContext, mousePosition, mouseButtonsHeld, interactables.Intersect(mousePosition), capture);
    }

    PlatformRig::ProcessInputResult DebugScreensSystem::OnInputEvent(const PlatformRig::InputContext& context, const OSServices::InputSnapshot& evnt)
    {
        bool consumedEvent      = false;
        _currentMouseHeld       = evnt._mouseButtonsDown;
        if (_currentMouse[0] != evnt._mousePosition[0] || _currentMouse[1] != evnt._mousePosition[1]) {
            auto drift = Coord2{evnt._mousePosition._x, evnt._mousePosition._y} - _currentMouse;
            _currentMouse = {evnt._mousePosition._x, evnt._mousePosition._y};
            auto capture = _currentInterfaceState.GetCapture();
            capture._driftDuringCapture += Coord2{std::abs(drift[0]), std::abs(drift[1])};
            _currentInterfaceState  = BuildInterfaceState(_currentInteractables, context, _currentMouse, _currentMouseHeld, capture);
        }

        for (auto i=_systemWidgets.begin(); i!=_systemWidgets.end() && !consumedEvent; ++i) {
            consumedEvent |= (i->_widget->ProcessInput(_currentInterfaceState, evnt) == IWidget::ProcessInputResult::Consumed);
        }

        for (auto i=_panels.begin(); i!=_panels.end() && !consumedEvent; ++i) {
            if (i->_widgetIndex < _widgets.size()) {

                bool alreadySeen = false;
                for (auto i2=_panels.begin(); i2!=i; ++i2) {
                    alreadySeen |= i2->_widgetIndex == i->_widgetIndex;
                }
                
                if (!alreadySeen) {
                    consumedEvent |= (_widgets[i->_widgetIndex]._widget->ProcessInput(_currentInterfaceState, evnt) == IWidget::ProcessInputResult::Consumed);
                }
            }
        }

        // if (!(evnt._mouseButtonsDown & (1<<0)) && (evnt._mouseButtonsTransition & (1<<0)) && !consumedEvent) {
        if (!consumedEvent) {
            consumedEvent |= ProcessInputPanelControls(_currentInterfaceState, context, evnt);
        }

        return consumedEvent ? PlatformRig::ProcessInputResult::Consumed : PlatformRig::ProcessInputResult::Passthrough;
    }

    static const char* s_PanelControlsButtons[] = {"<", ">", "H", "V", "X"};
    
    void DebugScreensSystem::RenderPanelControls(   IOverlayContext& context,
                                                    unsigned panelIndex, const std::string& name, Layout&layout, bool allowDestroy,
                                                    Interactables& interactables, InterfaceState& interfaceState)
    {
        const unsigned buttonCount   = dimof(s_PanelControlsButtons) - 1 + unsigned(allowDestroy);
        const unsigned buttonSize    = 20;
        const unsigned buttonPadding = 4;
        const unsigned nameSize      = 250;
        const unsigned buttonsRectWidth = buttonCount * buttonSize + nameSize + (buttonCount+2) * buttonPadding;
        Rect buttonsRect(
            Coord2(LinearInterpolate(layout._maximumSize._topLeft[0], layout._maximumSize._bottomRight[0], .5f) - buttonsRectWidth/2,    layout._maximumSize._topLeft[1] + layout._paddingInternalBorder ),
            Coord2(LinearInterpolate(layout._maximumSize._topLeft[0], layout._maximumSize._bottomRight[0], .5f) + buttonsRectWidth/2,    layout._maximumSize._topLeft[1] + layout._paddingInternalBorder + buttonSize + 2*buttonPadding));

        const InteractableId panelControlsId          = InteractableId_Make("PanelControls")                + panelIndex;
        const InteractableId nameRectId               = InteractableId_Make("PanelControls_NameRect")       + panelIndex;
        const InteractableId nameDropDownId           = InteractableId_Make("PanelControls_NameDropDown")   + panelIndex;
        const InteractableId nameDropDownWidgetId     = InteractableId_Make("PanelControls_NameDropDownWidget");
        const InteractableId backButtonId             = InteractableId_Make("PanelControls_BackButton")     + panelIndex;
        interactables.Register({buttonsRect, panelControlsId});

            //      panel controls are only visible when we've got a mouse over...
        if (interfaceState.HasMouseOver(panelControlsId) || interfaceState.HasMouseOver(nameDropDownId)) {
            FillAndOutlineRoundedRectangle(context, buttonsRect, RoundedRectBackgroundColour, RoundedRectOutlineColour);

            Layout buttonsLayout(buttonsRect);
            buttonsLayout._paddingBetweenAllocations = buttonsLayout._paddingInternalBorder = buttonPadding;
            for (unsigned c=0; c<buttonCount; ++c) {
                Rect buttonRect = buttonsLayout.Allocate(Coord2(buttonSize, buttonSize));
                InteractableId id = InteractableId_Make(s_PanelControlsButtons[c])+panelIndex;
                if (interfaceState.HasMouseOver(id)) {
                    OutlineEllipse(context, buttonRect, ColorB(0xff000000u));
                    DrawText().Color(0xff000000u).Draw(context, buttonRect, s_PanelControlsButtons[c]);
                } else {
                    OutlineEllipse(context, buttonRect, ColorB(0xffffffffu));
                    DrawText().Color(0xffffffffu).Draw(context, buttonRect, s_PanelControlsButtons[c]);
                }
                interactables.Register({buttonRect, id});
            }

            Rect nameRect = buttonsLayout.Allocate(Coord2(nameSize, buttonSize));
            DrawText().Draw(context, nameRect, MakeStringSection(name));

                //
                //      If the mouse is over the name rect, we get a drop list list
                //      of the screens available...
                //
            interactables.Register({nameRect, nameRectId});
            if (interfaceState.HasMouseOver(nameRectId) || interfaceState.HasMouseOver(nameDropDownId)) {
                    /////////////////////////////
                const Coord dropDownSize = Coord(_widgets.size() * buttonSize + (_widgets.size()+1) * buttonPadding);
                const Rect dropDownRect(    Coord2(nameRect._topLeft[0], nameRect._bottomRight[1]-3),
                                            Coord2(nameRect._topLeft[0]+nameSize, nameRect._bottomRight[1]-3+dropDownSize));
                FillRectangle(context, dropDownRect, RoundedRectBackgroundColour);
                const Rect dropDownInteractableRect(Coord2(dropDownRect._topLeft[0], dropDownRect._topLeft[1]-8), Coord2(dropDownRect._bottomRight[0], dropDownRect._bottomRight[1]));
                interactables.Register({dropDownInteractableRect, nameDropDownId});

                    /////////////////////////////
                unsigned y = dropDownRect._topLeft[1] + buttonPadding;
                for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                    Rect partRect(Coord2(dropDownRect._topLeft[0], y), Coord2(dropDownRect._topLeft[0]+nameSize, y+buttonSize));
                    const InteractableId thisId = nameDropDownWidgetId 
                                                + std::distance(_widgets.cbegin(), i) 
                                                + panelIndex * _widgets.size();
                    const bool mouseOver = interfaceState.HasMouseOver(thisId);
                    if (mouseOver) {
                        FillRectangle(context, partRect, ColorB(180, 200, 255, 64));
                    }
                    DrawText().Draw(context, partRect, MakeStringSection(i->_name));
                    y += buttonSize + buttonPadding;
                    interactables.Register({partRect, thisId});
                }
                    /////////////////////////////
            }
        }

            //  If we've got a back button render it in the top left
        if (panelIndex < _panels.size() && !_panels[panelIndex]._backButton.empty()) {
            Rect backButtonRect(    Coord2(layout._maximumSize._topLeft[0] + 8, layout._maximumSize._topLeft[1] + 4),
                                    Coord2(layout._maximumSize._topLeft[0] + 8 + 100, layout._maximumSize._topLeft[1] + 4 + buttonSize));
            interactables.Register({backButtonRect, backButtonId});
            const bool mouseOver = interfaceState.HasMouseOver(backButtonId);
            if (mouseOver) {
                FillAndOutlineRoundedRectangle(context, backButtonRect, RoundedRectBackgroundColour, RoundedRectOutlineColour);
                ColorB colour = ColorB(0x7fffffffu);
                if (interfaceState.IsMouseButtonHeld()) {
                    colour = ColorB(0xffffffffu);
                }
                DrawText().Color(colour).Draw(context, backButtonRect, "Back");
            }
        }
    }

    bool    DebugScreensSystem::ProcessInputPanelControls(  InterfaceState& interfaceState, 
                                                            const PlatformRig::InputContext& inputContext,
															const OSServices::InputSnapshot& evnt)
    {
        if (interfaceState.TopMostId() && evnt.IsRelease_LButton()) {
            InteractableId topMostWidget = interfaceState.TopMostId();
            for (unsigned buttonIndex=0; buttonIndex<dimof(s_PanelControlsButtons); ++buttonIndex) {

                    //      Handle the behaviour for the various buttons in the panel control...
                InteractableId id = InteractableId_Make(s_PanelControlsButtons[buttonIndex]);
                if (topMostWidget >= id && topMostWidget < id+_panels.size()) {
                    const unsigned panelIndex = unsigned(topMostWidget - id);
                    if (buttonIndex == 0) { // left
                        _panels[panelIndex]._widgetIndex = (_panels[panelIndex]._widgetIndex + _widgets.size() - 1)%_widgets.size();
                        return true;
                    } else if (buttonIndex == 1) { // right
                        _panels[panelIndex]._widgetIndex = (_panels[panelIndex]._widgetIndex + 1)%_widgets.size();
                        return true;
                    } else if (buttonIndex == 2||buttonIndex == 3) { // horizontal or vertical division
                        Panel newPanel = _panels[panelIndex];
                        newPanel._horizontalDivider = buttonIndex == 2;
                        _panels.insert(_panels.begin()+panelIndex+1, newPanel);
                        return true;
                    } else if (buttonIndex == 4) { // destroy (make sure to never destroy the last panel)
                        if (_panels.size() > 1) {
                            _panels.erase(_panels.begin()+panelIndex);
                        }
                        return true;
                    }
                }

            }

            const InteractableId backButtonId = InteractableId_Make("PanelControls_BackButton");
            if (topMostWidget >= backButtonId && topMostWidget < backButtonId + _panels.size()) {
                unsigned panelIndex = (unsigned)(topMostWidget-backButtonId);
                if (!_panels[panelIndex]._backButton.empty()) {
                    SwitchToScreen(panelIndex, _panels[panelIndex]._backButton.c_str());
                    _panels[panelIndex]._backButton = std::string();
                    return true;
                }
            }

            const InteractableId nameDropDownWidgetId = InteractableId_Make("PanelControls_NameDropDownWidget");
            if (topMostWidget >= nameDropDownWidgetId && topMostWidget < (nameDropDownWidgetId + _panels.size() * _widgets.size())) {
                unsigned panelId = unsigned((topMostWidget-nameDropDownWidgetId)/_widgets.size());
                unsigned widgetId = unsigned((topMostWidget-nameDropDownWidgetId)%_widgets.size());
                assert(panelId < _panels.size() && widgetId < _widgets.size());
                _panels[panelId]._widgetIndex = widgetId;
                _panels[panelId]._backButton = std::string();
                return true;
            }
        }

        constexpr auto ctrl    = "control"_key;
        constexpr auto left    = "left"_key;
        constexpr auto right   = "right"_key;
        if (evnt.IsHeld(ctrl) && !_widgets.empty()) {
            if (evnt.IsPress(left)) {
                const unsigned panelIndex = 0;
                _panels[panelIndex]._widgetIndex = (_panels[panelIndex]._widgetIndex + _widgets.size() - 1)%_widgets.size();
                return true;
            } else if (evnt.IsPress(right)) {
                const unsigned panelIndex = 0;
                _panels[panelIndex]._widgetIndex = (_panels[panelIndex]._widgetIndex + 1)%_widgets.size();
                return true;
            }
        }

        return false;
    }

    void DebugScreensSystem::Render(IOverlayContext& overlayContext, const Rect& viewport)
    {
        _currentInteractables = Interactables();
        
        Layout completeLayout(viewport);
        
        overlayContext.CaptureState();

        TRY {

                //
                //      Either we're rendering a single child widget over the complete screen, or we've
                //      separated the screen into multiple panels. When we only have a single panel, don't
                //      bother allocating panel space from the completeLayout, because that will just add
                //      extra borders
                //
            for (std::vector<Panel>::iterator i=_panels.begin(); i!=_panels.end(); ++i) {
                if (i->_widgetIndex < _widgets.size()) {
                    Rect widgetRect, nextWidgetRect;
                    if (i+1 >= _panels.end()) {
                        widgetRect = completeLayout._maximumSize;
                    } else {
                        if (i->_horizontalDivider) {
                            widgetRect = completeLayout.AllocateFullWidthFraction(i->_size);
                            nextWidgetRect = completeLayout.AllocateFullWidthFraction(1.f-i->_size);
                        } else {
                            widgetRect = completeLayout.AllocateFullHeightFraction(i->_size);
                            nextWidgetRect = completeLayout.AllocateFullHeightFraction(1.f-i->_size);
                        }
                    }
                    if (IsGood(widgetRect)) {
                        Layout widgetLayout(widgetRect);
                        _widgets[i->_widgetIndex]._widget->Render(overlayContext, widgetLayout, _currentInteractables, _currentInterfaceState);

                            //  if we don't have any system widgets registered, we 
                            //  get some basic default gui elements...
                        if (_systemWidgets.empty()) {
                            RenderPanelControls(
                                overlayContext, (unsigned)std::distance(_panels.begin(), i),
                                _widgets[i->_widgetIndex]._name, widgetLayout, _panels.size()!=1, _currentInteractables, _currentInterfaceState);
                        }
                    }
                    completeLayout = Layout(nextWidgetRect);
                    completeLayout._paddingInternalBorder = 0;
                }
            }

                // render the system widgets last (they will render over the top of anything else that is visible)
            for (auto i=_systemWidgets.cbegin(); i!=_systemWidgets.cend(); ++i) {
                Layout systemLayout(viewport);
                i->_widget->Render(overlayContext, systemLayout, _currentInteractables, _currentInterfaceState);
            }

        } CATCH(const std::exception&) {
            // suppress exception
        } CATCH_END

        overlayContext.ReleaseState();

        //      Redo the current interface state, in case any of the interactables have moved during the render...
        _currentInterfaceState = BuildInterfaceState(_currentInteractables, {}, _currentMouse, _currentMouseHeld, _currentInterfaceState.GetCapture());
    }
    
    bool DebugScreensSystem::IsAnythingVisible()
    {
        if (!_systemWidgets.empty())
            return true;
        for (const auto& i:_panels)
            if (i._widgetIndex < _widgets.size())
                return true;
        return false;
    }

    bool DebugScreensSystem::IsAnyPanelActive()
    {
        for (const auto& i:_panels)
            if (i._widgetIndex < _widgets.size())
                return true;
        return false;
    }

    void DebugScreensSystem::Register(std::shared_ptr<IWidget> widget, StringSection<> name, Type type)
    {
        WidgetAndName wAndN;
        wAndN._widget = std::move(widget);
        wAndN._name = name.AsString();
        wAndN._hashCode = Hash64(wAndN._name);
        
        if (type == InPanel) {
            _widgets.push_back(wAndN);
            TriggerWidgetChangeCallbacks();
        } else if (type == SystemDisplay) {
            _systemWidgets.push_back(wAndN);
        }
    }
    
    void DebugScreensSystem::Unregister(StringSection<> name)
    {
        auto it = _widgets.begin();
        for (; it != _widgets.end(); ++it) {
            if (XlEqString(name, it->_name)) {
                break;
            }
        }
        if (it != _widgets.end()) {
            _widgets.erase(it);
            TriggerWidgetChangeCallbacks();
        }
    }
    
    void DebugScreensSystem::Unregister(IWidget& widget)
    {
        auto it = _widgets.begin();
        for (; it != _widgets.end(); ++it) {
            if (it->_widget.get() == &widget) {
                break;
            }
        }
        if (it != _widgets.end()) {
            _widgets.erase(it);
            TriggerWidgetChangeCallbacks();
        }
    }

    void DebugScreensSystem::SwitchToScreen(unsigned panelIndex, StringSection<> name)
    {
        if (panelIndex < _panels.size()) {
            if (name.IsEmpty()) {
                _panels[panelIndex]._widgetIndex = size_t(-1);
                _panels[panelIndex]._backButton = std::string();
                return;
            }

                // look for exact match first...
            for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                if (XlEqStringI(name, i->_name)) {
                    _panels[panelIndex]._widgetIndex = std::distance(_widgets.cbegin(), i);
                    _panels[panelIndex]._backButton = std::string();  // clear out the back button
                    return;
                }
            }

                // If we don't have an exact match, just find a substring...
            for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                if (XlFindStringI(i->_name, name)) {
                    _panels[panelIndex]._widgetIndex = std::distance(_widgets.cbegin(), i);
                    _panels[panelIndex]._backButton = std::string();  // clear out the back button
                    return;
                }
            }
        }
    }

    void DebugScreensSystem::SwitchToScreen(StringSection<> name)
    {
        SwitchToScreen(0, name);
    }

    bool DebugScreensSystem::SwitchToScreen(unsigned panelIndex, uint64_t hashCode)
    {
        if (panelIndex < _panels.size()) {
            for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                if (i->_hashCode == hashCode) {
                    _panels[panelIndex]._widgetIndex = std::distance(_widgets.cbegin(), i);
                    _panels[panelIndex]._backButton = std::string();  // clear out the back button
                    return true;
                }
            }
        }
        return false;
    }

    const char*     DebugScreensSystem::CurrentScreen(unsigned panelIndex)
    {
        if (panelIndex < _panels.size()) {
            if (_panels[panelIndex]._widgetIndex < _widgets.size()) {
                return _widgets[_panels[panelIndex]._widgetIndex]._name.c_str();        // a bit dangerous...
            }
        }
        return nullptr;
    }
    
    unsigned DebugScreensSystem::AddWidgetChangeCallback(WidgetChangeCallback&& callback)
    {
        auto id = _nextWidgetChangeCallbackIndex++;
        _widgetChangeCallbacks.push_back(std::make_pair(id, std::move(callback)));
        return id;
    }
    
    void DebugScreensSystem::RemoveWidgetChangeCallback(unsigned callbackid)
    {
        _widgetChangeCallbacks.erase(
            std::remove_if(_widgetChangeCallbacks.begin(), _widgetChangeCallbacks.end(),
                           [callbackid](const std::pair<unsigned, WidgetChangeCallback>& p) { return p.first == callbackid; }));
    }
    
    void DebugScreensSystem::TriggerWidgetChangeCallbacks()
    {
        for (const auto&c:_widgetChangeCallbacks)
            c.second();
    }

    // template<typename Type> void Delete(Type* type) { delete type; }

    #pragma warning(disable:4355)      // warning C4355: 'this' : used in base member initializer list

    DebugScreensSystem::DebugScreensSystem() 
    {
        _currentMouse = {0,0};
        _currentMouseHeld = 0;
        _nextWidgetChangeCallbackIndex = 0;

        Panel p;
        p._widgetIndex = size_t(-1);
        p._size = .5f;
        p._horizontalDivider = false;
        _panels.push_back(p);
    }

    DebugScreensSystem::~DebugScreensSystem()
    {
    }

    ///////////////////////////////////////////////////////////////////////////////////
    InterfaceState::InterfaceState()
    {
        _mousePosition = Coord2(std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
        _mouseButtonsHeld = 0;
    }

    InterfaceState::InterfaceState(
        const PlatformRig::InputContext& viewInputContext,
        const Coord2& mousePosition, unsigned mouseButtonsHeld,
        const std::vector<Interactables::HotArea>& mouseStack,
        const Capture& capture)
    :   _mousePosition(mousePosition)
    ,   _mouseButtonsHeld(mouseButtonsHeld)
    ,   _mouseOverStack(mouseStack)
    ,   _capture(capture)
    {
        if (auto* v=viewInputContext.GetService<PlatformRig::WindowingSystemView>())
            _viewInputContext = *v;
    }

    bool InterfaceState::HasMouseOver(InteractableId id) 
    { 
        for(auto i=_mouseOverStack.begin(); i!=_mouseOverStack.end(); ++i)
            if (i->_id == id)
                return true;
        return false;
    }

    InteractableId          InterfaceState::TopMostId() const
    { 
        // when a capture is set, it hides other widgets from being returned by this method
        if (_capture._hotArea._id) return _capture._hotArea._id;
        return (!_mouseOverStack.empty())?_mouseOverStack[_mouseOverStack.size()-1]._id:0;
    }

    Interactables::HotArea   InterfaceState::TopMostHotArea() const
    {
        if (_capture._hotArea._id) return _capture._hotArea;  // unfortunately we only have the widget information for widgets under the cursor
        if (!_mouseOverStack.empty())
            return _mouseOverStack[_mouseOverStack.size()-1];
        return {};
    }

    void InterfaceState::BeginCapturing(const Interactables::HotArea& widget)
    {
        _capture._hotArea = widget;
        _capture._driftDuringCapture = {0,0};
    }
    
    void InterfaceState::EndCapturing()
    {
        _capture = {};
    }

    ///////////////////////////////////////////////////////////////////////////////////
    void Interactables::Register(const HotArea& widget)
    {
        _hotAreas.push_back( widget );
    }

    static bool Intersection(const Rect& rect, const Coord2& position)
    {
        return rect._topLeft[0] <= position[0] && position[0] < rect._bottomRight[0]
            && rect._topLeft[1] <= position[1] && position[1] < rect._bottomRight[1]
            ;
    }

    std::vector<Interactables::HotArea> Interactables::Intersect(const Coord2& position) const
    {
        std::vector<HotArea> result;
        for (std::vector<HotArea>::const_iterator i=_hotAreas.begin(); i!=_hotAreas.end(); ++i) {
            if (Intersection(i->_rect, position)) {
                result.push_back(*i);
            }
        }
        return result;
    }

    ///////////////////////////////////////////////////////////////////////////////////
    Layout::Layout(const Rect& maximumSize)
    {
        _maximumSize = maximumSize;
        _maxRowWidth = 0;
        _caretX = _caretY = 0;
        _currentRowMaxHeight = 0;
        _paddingInternalBorder = 8;
        _paddingBetweenAllocations = 4;
    }

    Rect Layout::Allocate(Coord2 dimensions)
    {
        Rect rect;
        unsigned paddedCaretX = _caretX;
        if (!paddedCaretX) { paddedCaretX += _paddingInternalBorder; } else { paddedCaretX += _paddingBetweenAllocations; }
        rect._topLeft[0] = _maximumSize._topLeft[0] + paddedCaretX;
        rect._bottomRight[0] = rect._topLeft[0] + dimensions[0];
        if (_caretX && rect._bottomRight[0] > (_maximumSize._bottomRight[0] - _paddingInternalBorder)) {
                // restart row
             _caretY += _currentRowMaxHeight+_paddingBetweenAllocations;
            _maxRowWidth = std::max(_maxRowWidth, _currentRowMaxHeight);
            _currentRowMaxHeight = 0;

            paddedCaretX = _paddingInternalBorder;
            rect._topLeft[0] = _maximumSize._topLeft[0] + paddedCaretX;
            rect._bottomRight[0] = rect._topLeft[0] + dimensions[0];
        }

        _currentRowMaxHeight = std::max(_currentRowMaxHeight, dimensions[1]);
        if (!_caretY) { _caretY += _paddingInternalBorder; }
        rect._topLeft[1] = _maximumSize._topLeft[1] + _caretY;
        rect._bottomRight[1] = rect._topLeft[1] + dimensions[1];
        _caretX = paddedCaretX + dimensions[0];
        return rect;
    }

    Coord Layout::GetWidthRemaining()
    {
        auto maxSizeWidth = _maximumSize._bottomRight[0] - _maximumSize._topLeft[0];

            // get the remaining space on the current line
        if (!_caretX) {
            return maxSizeWidth - 2 * _paddingInternalBorder;
        }

        auto attempt = maxSizeWidth - _caretX - _paddingInternalBorder - _paddingBetweenAllocations;
        if (attempt > 0)
            return attempt;

        // no space left on the current row. We must implicitly move onto the next row
        if (!_currentRowMaxHeight)
            return attempt;

        _caretY += _currentRowMaxHeight+_paddingBetweenAllocations;
        _maxRowWidth = std::max( _maxRowWidth, _currentRowMaxHeight );
        _currentRowMaxHeight = 0;
        _caretX = 0;
        return maxSizeWidth - 2 * _paddingInternalBorder;
    }

    Rect Layout::AllocateFullWidth(Coord height)
    {
            // restart row
        if (_currentRowMaxHeight) {
            _caretY += _currentRowMaxHeight+_paddingBetweenAllocations;
            _maxRowWidth = std::max( _maxRowWidth, _currentRowMaxHeight );
            _currentRowMaxHeight = 0;
            _caretX = 0;
        }

        if (!_caretY) { _caretY += _paddingInternalBorder; }

        auto maxY = _maximumSize._bottomRight[1]-_paddingInternalBorder;

        Rect result;
        result._topLeft[0]        = _maximumSize._topLeft[0] + _paddingInternalBorder;
        result._bottomRight[0]    = _maximumSize._bottomRight[0] - _paddingInternalBorder;
        result._topLeft[1]        = std::min(maxY, _maximumSize._topLeft[1] + _caretY);
        result._bottomRight[1]    = std::min(maxY, result._topLeft[1] + height);
        _caretY += height + _paddingBetweenAllocations;
    
        return result;
    }

    Rect Layout::AllocateFullHeight(Coord width)
    {
        // restart row, unless we're already in the middle of an allocateFullHeight
        bool currentlyAllocatingFullHeight = (_caretY + _currentRowMaxHeight) >= (_maximumSize._bottomRight[1]-_maximumSize._topLeft[1]-2*_paddingInternalBorder);
        if (!currentlyAllocatingFullHeight && _currentRowMaxHeight) {
            _caretY += _currentRowMaxHeight+_paddingBetweenAllocations;
            _maxRowWidth = std::max( _maxRowWidth, _currentRowMaxHeight );
            _currentRowMaxHeight = 0;
            _caretX = 0;
        }

        if (!_caretY) { _caretY += _paddingInternalBorder; }
        if (!_caretX) { _caretX += _paddingInternalBorder; } else { _caretX += _paddingBetweenAllocations; }

        Rect result;
        result._topLeft[1]        = _maximumSize._topLeft[1] + _caretY;
        result._bottomRight[1]    = _maximumSize._bottomRight[1] - _paddingInternalBorder;

        result._topLeft[0]        = _maximumSize._topLeft[0] + _caretX;
        result._bottomRight[0]    = std::min( result._topLeft[0]+width, _maximumSize._bottomRight[0] - _paddingInternalBorder );

        _currentRowMaxHeight = std::max(_currentRowMaxHeight, result._bottomRight[1]-result._topLeft[1]);
        _caretX = result._bottomRight[0] - _maximumSize._topLeft[0];
        return result;
    }
    
    Rect Layout::AllocateFullHeightFraction(float proportionOfWidth)
    {
        signed widthAvailable = _maximumSize._bottomRight[0] - _maximumSize._topLeft[0] - 2*_paddingInternalBorder;
        signed width = unsigned(widthAvailable*proportionOfWidth);
        return AllocateFullHeight(width);
    }

    Rect Layout::AllocateFullWidthFraction(float proportionOfHeight)
    {
            // restart row
        if (_currentRowMaxHeight) {
            _caretY += _currentRowMaxHeight+_paddingBetweenAllocations;
            _maxRowWidth = std::max( _maxRowWidth, _currentRowMaxHeight );
            _currentRowMaxHeight = 0;
            _caretX = 0;
        }

        signed heightAvailable = _maximumSize._bottomRight[1] - _maximumSize._topLeft[1] - _caretY - _paddingInternalBorder;
        signed maxHeight = (_maximumSize._bottomRight[1] - _maximumSize._topLeft[1] - _paddingInternalBorder*2);
        return AllocateFullWidth(std::min(heightAvailable, Coord(maxHeight * proportionOfHeight)));
    }

    const ColorB RandomPaletteColorTable[] = 
    {
        ColorB( 255,207,171 ),
        ColorB( 165,105,79  ),
        ColorB( 251,126,253 ),
        ColorB( 252,232,131 ),
        ColorB( 250,231,181 ),
        ColorB( 197,227,132 ),
        ColorB( 255,207,72  ),
        ColorB( 255,188,217 ),
        ColorB( 252,40,71   ),
        ColorB( 151,154,170 ),
        ColorB( 252,108,133 ),
        ColorB( 252,116,253 ),
        ColorB( 65,74,76    ),
        ColorB( 239,205,184 ),
        ColorB( 253,215,228 ),
        ColorB( 255,170,204 ),
        ColorB( 119,221,231 ),
        ColorB( 118,255,122 ),
        ColorB( 204,102,102 ),
        ColorB( 25,116,210  ),
        ColorB( 247,128,161 ),
        ColorB( 48,186,143  ),
        ColorB( 253,217,181 ),
        ColorB( 239,152,170 ),
        ColorB( 173,173,214 ),
        ColorB( 231,198,151 ),
        ColorB( 227,37,107  ),
        ColorB( 109,174,129 ),
        ColorB( 219,215,210 ),
        ColorB( 138,121,93  ),
        ColorB( 113,188,120 ),
        ColorB( 222,93,131  ),
        ColorB( 28,169,201  ),
        ColorB( 29,249,20   ),
        ColorB( 31,206,203  ),
        ColorB( 180,103,77  ),
        ColorB( 221,148,117 ),
        ColorB( 253,188,180 ),
        ColorB( 35,35,35    ),
        ColorB( 26,72,118   ),
        ColorB( 205,164,222 ),
        ColorB( 253,252,116 ),
        ColorB( 29,172,214  ),
        ColorB( 255,83,73   ),
        ColorB( 93,118,203  ),
        ColorB( 25,158,189  ),
        ColorB( 240,232,145 ),
        ColorB( 255,182,83  ),
        ColorB( 253,124,110 ),
        ColorB( 255,29,206  ),
        ColorB( 115,102,189 ),
        ColorB( 237,237,237 ),
        ColorB( 162,173,208 ),
        ColorB( 221,68,146  ),
        ColorB( 120,219,226 ),
        ColorB( 159,129,112 ),
        ColorB( 222,170,136 ),
        ColorB( 69,206,162  ),
        ColorB( 205,74,74   ),
        ColorB( 252,137,172 ),
        ColorB( 255,117,56  ),
        ColorB( 178,236,93  ),
        ColorB( 255,127,73  ),
        ColorB( 143,80,157  ),
        ColorB( 110,81,96   ),
        ColorB( 234,126,93  ),
        ColorB( 186,184,108 ),
        ColorB( 188,93,88   ),
        ColorB( 120,81,169  ),
        ColorB( 31,117,254  ),
        ColorB( 159,226,191 ),
        ColorB( 28,211,162  ),
        ColorB( 176,183,198 ),
        ColorB( 252,180,213 ),
        ColorB( 253,94,83   ),
        ColorB( 135,169,107 ),
        ColorB( 202,55,103  ),
        ColorB( 255,67,164  ),
        ColorB( 205,149,117 ),
        ColorB( 239,219,197 ),
        ColorB( 195,100,197 ),
        ColorB( 23,128,109  ),
        ColorB( 255,110,74  ),
        ColorB( 246,100,175 ),
        ColorB( 255,164,116 ),
        ColorB( 28,172,120  ),
        ColorB( 200,56,90   ),
        ColorB( 255,155,170 ),
        ColorB( 214,138,89  ),
        ColorB( 142,69,133  ),
        ColorB( 247,83,148  ),
        ColorB( 197,208,230 ),
        ColorB( 59,176,143  ),
        ColorB( 203,65,84   ),
        ColorB( 116,66,200  ),
        ColorB( 255,73,108  ),
        ColorB( 255,29,206  ),
        ColorB( 128,218,235 ),
        ColorB( 21,128,120  ),
        ColorB( 43,108,196  ),
        ColorB( 255,72,208  ),
        ColorB( 255,160,137 ),
        ColorB( 253,252,116 ),
        ColorB( 250,167,108 ),
        ColorB( 157,129,186 ),
        ColorB( 253,219,109 ),
        ColorB( 238,32,77   ),
        ColorB( 252,217,117 ),
        ColorB( 255,255,153 ),
        ColorB( 255,163,67  ),
        ColorB( 149,145,140 ),
        ColorB( 230,168,215 ),
        ColorB( 146,110,174 ),
        ColorB( 236,234,190 ),
        ColorB( 192,68,143  ),
        ColorB( 205,197,194 ),
        ColorB( 168,228,160 ),
        ColorB( 154,206,235 ),
        ColorB( 255,189,136 ),
        ColorB( 255,130,67  )
    };

    const size_t RandomPaletteColorTable_Size = dimof(RandomPaletteColorTable);
}}

std::string ShortBytesString(unsigned byteCount)
{
    if (byteCount < 1024*1024) {
        return XlDynFormatString("$3%.1f$oKB", byteCount/1024.f);
    } else {
        return XlDynFormatString("$6%.1f$oMB", byteCount/(1024.f*1024.f));
    }
}

std::string ShortNumberString(unsigned number)
{
    if (number < 1024) {
        return XlDynFormatString("%i", number);
    } else if (number < 1024*1024) {
        return XlDynFormatString("$3%i$oK", number/1024);
    } else {
        return XlDynFormatString("$6%i$oM", number/(1024*1024));
    }
}

