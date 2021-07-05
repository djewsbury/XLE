// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DebuggingDisplay.h"
#include "../RenderOverlays/Font.h"
#include "../RenderOverlays/FontRendering.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"
#include "../ConsoleRig/Console.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../ConsoleRig/ResourceBox.h"       // for FindCachedBox
#include "../Utility/PtrUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/IntrusivePtr.h"
#include "../xleres/FileList.h"
#include <stdarg.h>
#include <assert.h>
#include <string.h>

#if defined(HAS_XLE_CONSOLE_RIG)
    #include "../ConsoleRig/IncludeLUA.h"
#endif

#pragma warning(disable:4244)   // conversion from 'int' to 'float', possible loss of data

namespace RenderOverlays { namespace DebuggingDisplay
{
    const ColorB            RoundedRectOutlineColour(255,255,255,128);
    const ColorB            RoundedRectBackgroundColour(180,200,255,128);
    static const ColorB     HistoryGraphAxisColour(64,64,64,128);
    static const ColorB     HistoryGraphLineColor(255,255,255,255);
    static const ColorB     HistoryGraphExtraLineColor(255,128,128,255);
    static const ColorB     HistoryGraphTopOfGraphBackground(200,255,200,196);
    static const ColorB     HistoryGraphBottomOfGraphBackground(200,255,200,0);
    static const ColorB     HistoryGraphTopOfGraphBackground_Peak(128,200,255,196);
    static const ColorB     HistoryGraphBottomOfGraphBackground_Peak(128,200,255,64);
    static const ColorB     GraphLabel(255, 255, 255, 128);

    ///////////////////////////////////////////////////////////////////////////////////
    ScrollBar::Coordinates::Coordinates(const Rect& rect, float minValue, float maxValue, float visibleWindowSize, Flags::BitField flags)
    {
        const Coord buttonHeight = (flags&Flags::NoUpDown)?0:std::min(Coord(rect.Width()*.75f), rect.Height()/3);
        _interactableRect    = rect;
        if (!(flags&Flags::Horizontal)) {
            _upArrowRect         = Rect(rect._topLeft, Coord2(rect._bottomRight[0], rect._topLeft[1]+buttonHeight));
            _downArrowRect       = Rect(Coord2(rect._topLeft[0], rect._bottomRight[1]-buttonHeight), Coord2(rect._bottomRight[0], rect._bottomRight[1]));
            _scrollAreaRect      = Rect(Coord2(LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.2f), rect._topLeft[1]+buttonHeight), 
                                        Coord2(LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.8f), rect._bottomRight[1]-buttonHeight));
        } else {
            _upArrowRect         = Rect(rect._topLeft, Coord2(rect._topLeft[0]+buttonHeight, rect._bottomRight[1]));
            _downArrowRect       = Rect(Coord2(rect._bottomRight[0]-buttonHeight, rect._topLeft[1]), rect._bottomRight);
            _scrollAreaRect      = Rect(Coord2(rect._topLeft[0]+buttonHeight, rect._topLeft[1]), 
                                        Coord2(rect._bottomRight[0]-buttonHeight, rect._bottomRight[1]));
        }

        _flags = flags;

        if (maxValue > minValue) {
            if (!(flags&Flags::Horizontal)) {
                _thumbHeight = Coord(_scrollAreaRect.Height() * visibleWindowSize / (maxValue-minValue));
                _valueToPixels = float(_scrollAreaRect._bottomRight[1]-_scrollAreaRect._topLeft[1]-_thumbHeight) / (maxValue-minValue);
                _pixelsBase = _scrollAreaRect._topLeft[1] + _thumbHeight/2;
                _windowHeight = _scrollAreaRect.Height();
            } else {
                _thumbHeight = Coord(_scrollAreaRect.Width() * visibleWindowSize / (maxValue-minValue));
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
            const Coord thumbTop       = std::max(_scrollAreaRect._topLeft[1], thumbCentre-_thumbHeight/2);
            const Coord thumbBottom    = std::min(_scrollAreaRect._bottomRight[1], thumbCentre+_thumbHeight/2);
            return Rect( Coord2(thumbTop, _scrollAreaRect._topLeft[1]), Coord2(thumbBottom, _scrollAreaRect._bottomRight[1]) );
        }
    }

    bool            ScrollBar::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
    {
        const bool overScrollBar = (interfaceState.TopMostId() == _id);
        _draggingScrollBar = (_draggingScrollBar || overScrollBar) && (input._mouseButtonsDown&1);
        if (_draggingScrollBar) {
            _scrollOffsetPixels = (_flags&Coordinates::Flags::Horizontal)?interfaceState.MousePosition()[0]:interfaceState.MousePosition()[1];
            if (!(input._mouseButtonsDown&1)) {
                _draggingScrollBar = false;
            }
            return true;
        }
        return false;
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

    void DrawScrollBar(IOverlayContext* context, const ScrollBar::Coordinates& coordinates, float thumbPosition, ColorB fillColour, ColorB outlineColour)
    {

            //
            //        Divide the rectangle into 3 parts
            //      
            //            Up button
            //            Scroll area
            //            Down button
            //

        const Rect upButton = coordinates.UpArrow();
        const Rect downButton = coordinates.DownArrow();

        DrawFilledElipse(context, upButton, fillColour);
        DrawFilledElipse(context, downButton, fillColour);
        DrawElipse(context, upButton, outlineColour);
        DrawElipse(context, downButton, outlineColour);

        const float W = 7.f;
        const float Q = W - 1.f;
        const Coord2 upA    (upButton._topLeft[0]*(1.f/2.f)+upButton._bottomRight[0]*(1.f/2.f), upButton._topLeft[1]*1.f+upButton._bottomRight[1]*0.f-5);
        const Coord2 upB    (upButton._topLeft[0]*(Q/W)+upButton._bottomRight[0]*(1.f/W), upButton._topLeft[1]*0.f+upButton._bottomRight[1]*1.f);
        const Coord2 upC    (upButton._topLeft[0]*(1.f/W)+upButton._bottomRight[0]*(Q/W), upButton._topLeft[1]*0.f+upButton._bottomRight[1]*1.f);
        const Coord2 downA  (downButton._topLeft[0]*(1.f/2.f)+downButton._bottomRight[0]*(1.f/2.f), downButton._topLeft[1]*0.f+downButton._bottomRight[1]*1.f+5);
        const Coord2 downB  (downButton._topLeft[0]*(Q/W)+downButton._bottomRight[0]*(1.f/W), downButton._topLeft[1]*1.f+downButton._bottomRight[1]*0.f);
        const Coord2 downC  (downButton._topLeft[0]*(1.f/W)+downButton._bottomRight[0]*(Q/W), downButton._topLeft[1]*1.f+downButton._bottomRight[1]*0.f);

        const Coord2 leftA   (upButton._topLeft[0]*(3.f/5.f)+upButton._bottomRight[0]*(2.f/5.f), upButton._bottomRight[1]);
        const Coord2 leftB   (upButton._topLeft[0]*(3.f/5.f)+upButton._bottomRight[0]*(2.f/5.f), downButton._topLeft[1]);
        const Coord2 rightA  (upButton._topLeft[0]*(2.f/5.f)+upButton._bottomRight[0]*(3.f/5.f), upButton._bottomRight[1]);
        const Coord2 rightB  (upButton._topLeft[0]*(2.f/5.f)+upButton._bottomRight[0]*(3.f/5.f), downButton._topLeft[1]);
        const Coord2 midA    (upButton._topLeft[0]*(1.f/2.f)+upButton._bottomRight[0]*(1.f/2.f), upButton._bottomRight[1]);
        const Coord2 midB    (upButton._topLeft[0]*(1.f/2.f)+upButton._bottomRight[0]*(1.f/2.f), downButton._topLeft[1]);

        const Coord2 triangles[]         = {upA, upB, upC, downA, downB, downC};
        const ColorB triangleColours[]   = {fillColour, fillColour, fillColour, fillColour, fillColour, fillColour};
        DrawTriangles(context, triangles, triangleColours, dimof(triangles)/3);
        DrawRectangle(context, Rect(leftA, rightB), 0.f, fillColour);

        const Coord2 lines[] = { upA, upB, upA, upC, upB, upC, 
                                 downA, downB, downA, downC, downB, downC, 
                                 leftA, leftB, rightA, rightB };
        const ColorB lineColours[] = {  outlineColour, outlineColour, outlineColour, outlineColour, 
                                        outlineColour, outlineColour, outlineColour, outlineColour, 
                                        outlineColour, outlineColour, outlineColour, outlineColour,
                                        outlineColour, outlineColour, outlineColour, outlineColour };
        DrawLines(context, lines, lineColours, dimof(lines)/2);
        
        const Rect thumbRect = coordinates.Thumb(thumbPosition);
        DrawRoundedRectangle(context, thumbRect, fillColour, outlineColour, 1.f/2.7f);
    }

    ///////////////////////////////////////////////////////////////////////////////////

    Float3 AsPixelCoords(Coord2 input)              { return Float3(float(input[0]), float(input[1]), 0.f); }
    Float3 AsPixelCoords(Coord2 input, float depth) { return Float3(float(input[0]), float(input[1]), depth); }
    Float3 AsPixelCoords(Float2 input)              { return Expand(input, 0.f); }
    Float3 AsPixelCoords(Float3 input)              { return input; }
    std::tuple<Float3, Float3> AsPixelCoords(const Rect& rect)
    {
        return std::make_tuple(AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight));
    }

    ///////////////////////////////////////////////////////////////////////////////////

    void DrawElipse(IOverlayContext* context, const Rect& rect, ColorB colour)
    {
        Coord2 center( LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.5f), LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f) );

        //  we should also take into account the pixel aspect ratio, so that circles appear
        //  as a circles on screen
        Float3 pixelCenter = AsPixelCoords(center);
        Float2 pixelRadius(rect.Width()/2, rect.Height()/2);

        const unsigned segmentCount = 64;
        Float3 lines[segmentCount*2];
        float sinA = 0.f, cosA = 1.f;
        const float beta = (2.f*gPI)/float(segmentCount-1);
        float cosB, sinB;
        std::tie(sinB, cosB) = XlSinCos(beta);
        for (unsigned s=0; s<segmentCount; ++s) {
            //  using trigonometric addition formulae (avoid sin/cos in the loop)
            float nextSinA = sinA * cosB + sinB * cosA;
            float nextCosA = cosA * cosB - sinA * sinB;
            lines[s*2]   = Float3(pixelCenter[0]+pixelRadius[0]*cosA,       pixelCenter[1]+pixelRadius[1]*sinA,         pixelCenter[2]);
            lines[s*2+1] = Float3(pixelCenter[0]+pixelRadius[0]*nextCosA,   pixelCenter[1]+pixelRadius[1]*nextSinA,     pixelCenter[2]);
            sinA = nextSinA; cosA = nextCosA;
        }

        context->DrawLines(ProjectionMode::P2D, lines, dimof(lines), colour);
    }

    void DrawFilledElipse(IOverlayContext* context, const Rect& rect, ColorB colour)
    {
        Coord2 center( LinearInterpolate(rect._topLeft[0], rect._bottomRight[0], 0.5f), LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f) );
        Float3 pixelCenter = AsPixelCoords(center);
        Float2 pixelRadius(rect.Width()/2, rect.Height()/2);

        const unsigned segmentCount = 64;
        Float3 pts[segmentCount];
        float sinA = 0.f, cosA = 1.f;
        const float beta = (2.f*gPI)/float(segmentCount-1);
        float cosB, sinB;
        std::tie(sinB, cosB) = XlSinCos(beta);
        for (unsigned s=0; s<segmentCount; ++s) {
            pts[s]   = Float3(pixelCenter[0]+pixelRadius[0]*cosA, pixelCenter[1]+pixelRadius[1]*sinA, pixelCenter[2]);
            float nextSinA = sinA * cosB + sinB * cosA;
            float nextCosA = cosA * cosB - sinA * sinB;
            sinA = nextSinA; cosA = nextCosA;
        }

        Float3 triangles[(segmentCount-2)*3];
        Float3* t = triangles;
        *t++ = pts[0];
        *t++ = pts[1];
        *t++ = pts[segmentCount-1];
        unsigned lastA = segmentCount-1;
        unsigned lastB = 1;
        unsigned q = 0;
        while (t < &triangles[dimof(triangles)]) {
            unsigned C = (q&1)?(segmentCount-2-q/2):(q/2+2);
            ++q;
            *t++ = pts[lastA];
            *t++ = pts[lastB];
            *t++ = pts[C];
            lastA = lastB;
            lastB = C;
        }

        context->DrawTriangles(ProjectionMode::P2D, triangles, dimof(triangles), colour);
    }

    void DrawRoundedRectangleOutline(
        IOverlayContext* context, const Rect & rect, 
        ColorB colour, 
        float width, float roundedProportion)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            ColorB::Zero, colour,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(width, roundedProportion), Float2(width, roundedProportion),
            RENDEROVERLAYS_SHAPES_HLSL ":Paint,Shape=RoundedRectShape,Fill=None,Outline=SolidFill");
    }

    void DrawRoundedRectangle(
        IOverlayContext* context, 
        const Rect & rect,
        ColorB backgroundColour, ColorB outlineColour,
        float borderWidth, float roundedProportion)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1])
            return;

        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(rect._topLeft),
            AsPixelCoords(rect._bottomRight),
            backgroundColour, outlineColour,
            Float2(0.f, 0.f), Float2(1.f, 1.f), 
            Float2(borderWidth, roundedProportion), Float2(borderWidth, roundedProportion),
            RENDEROVERLAYS_SHAPES_HLSL ":Paint,Shape=RoundedRectShape,Fill=SolidFill,Outline=SolidFill");
    }

    void DrawRectangle(IOverlayContext* context, const Rect& rect, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])), colour,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1]-1)), colour,
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._topLeft[1])), colour);

        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._topLeft[1])), colour,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._bottomRight[1]-1)), colour,
            AsPixelCoords(Coord2(rect._bottomRight[0]-1, rect._bottomRight[1]-1)), colour);
    }

    void DrawRectangle(IOverlayContext* context, const Rect& rect, float depth, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Float3(rect._topLeft[0], rect._topLeft[1], depth)), colour,
            AsPixelCoords(Float3(rect._topLeft[0], rect._bottomRight[1]-1, depth)), colour,
            AsPixelCoords(Float3(rect._bottomRight[0]-1, rect._topLeft[1], depth)), colour);

        context->DrawTriangle(
            ProjectionMode::P2D, 
            AsPixelCoords(Float3(rect._bottomRight[0]-1, rect._topLeft[1], depth)), colour,
            AsPixelCoords(Float3(rect._topLeft[0], rect._bottomRight[1]-1, depth)), colour,
            AsPixelCoords(Float3(rect._bottomRight[0]-1, rect._bottomRight[1]-1, depth)), colour);
    }

    void DrawRectangleOutline(IOverlayContext* context, const Rect& rect, float depth, ColorB colour)
    {
        if (rect._bottomRight[0] <= rect._topLeft[0] || rect._bottomRight[1] <= rect._topLeft[1]) {
            return;
        }

        Float3 lines[8];
        lines[0] = AsPixelCoords(Float3(rect._topLeft[0],         rect._topLeft[1],        depth));
        lines[1] = AsPixelCoords(Float3(rect._bottomRight[0]-1,   rect._topLeft[1],        depth));
        lines[2] = AsPixelCoords(Float3(rect._bottomRight[0]-1,   rect._topLeft[1],        depth));
        lines[3] = AsPixelCoords(Float3(rect._bottomRight[0]-1,   rect._bottomRight[1]-1,  depth));
        lines[4] = AsPixelCoords(Float3(rect._bottomRight[0]-1,   rect._bottomRight[1]-1,  depth));
        lines[5] = AsPixelCoords(Float3(rect._topLeft[0],         rect._bottomRight[1]-1,  depth));
        lines[6] = AsPixelCoords(Float3(rect._topLeft[0],         rect._bottomRight[1]-1,  depth));
        lines[7] = AsPixelCoords(Float3(rect._topLeft[0],         rect._topLeft[1],        depth));
        context->DrawLines(ProjectionMode::P2D, lines, dimof(lines), colour);
    }

    Coord DrawText(IOverlayContext* context, const Rect& rect, TextStyle* textStyle, ColorB colour, StringSection<> text)
    {
		return (Coord)context->DrawText(AsPixelCoords(rect), GetDefaultFont(), textStyle ? *textStyle : TextStyle{}, colour, TextAlignment::TopLeft, text);
    }

    Coord DrawText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, StringSection<> text)
    {
        return (Coord)context->DrawText(AsPixelCoords(rect), GetDefaultFont(), textStyle ? *textStyle : TextStyle{}, colour, TextAlignment::TopLeft, text);
    }

    Coord DrawText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, StringSection<> text)
    {
        return (Coord)context->DrawText(AsPixelCoords(rect), GetDefaultFont(), textStyle ? *textStyle : TextStyle{}, colour, alignment, text);
    }

    Coord DrawFormatText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, const char text[], va_list args)
    {
        char buffer[4096];
        vsnprintf(buffer, dimof(buffer), text, args);
        return (Coord)context->DrawText(AsPixelCoords(rect), GetDefaultFont(), textStyle ? *textStyle : TextStyle{}, colour, alignment, buffer);
    }

    Coord DrawFormatText(IOverlayContext* context, const Rect & rect, TextStyle* textStyle, ColorB colour, const char text[], ...)
    {
        va_list args;
        va_start(args, text);
        auto result = DrawFormatText(context, rect, 0.f, textStyle, colour, TextAlignment::TopLeft, text, args);
        va_end(args);
        return result;
    }

    Coord DrawFormatText(IOverlayContext* context, const Rect & rect, float depth, TextStyle* textStyle, ColorB colour, const char text[], ...)
    {
        va_list args;
        va_start(args, text);
        auto result = DrawFormatText(context, rect, depth, textStyle, colour, TextAlignment::TopLeft, text, args);
        va_end(args);
        return result;
    }

    Coord DrawFormatText(IOverlayContext* context, const Rect & rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, const char text[], ...)
    {
        va_list args;
        va_start(args, text);
        auto result = DrawFormatText(context, rect, depth, textStyle, colour, alignment, text, args);
        va_end(args);
        return result;
    }

    void DrawHistoryGraph(IOverlayContext* context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory)
    {
        context->DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     rect._bottomRight[1])), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])), HistoryGraphAxisColour );
        context->DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],       rect._topLeft[1])), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._topLeft[0],       rect._bottomRight[1])), HistoryGraphAxisColour );

        Rect graphArea( Coord2( rect._topLeft[0]+1, rect._topLeft[1] ),
                        Coord2( rect._bottomRight[0], rect._bottomRight[1]-1 ) );

        context->DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.25f))), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.25f))), HistoryGraphAxisColour );
        context->DrawLine(
            ProjectionMode::P2D, 
            AsPixelCoords(Coord2(rect._topLeft[0],     LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f))), HistoryGraphAxisColour,
            AsPixelCoords(Coord2(rect._bottomRight[0], LinearInterpolate(rect._topLeft[1], rect._bottomRight[1], 0.5f))), HistoryGraphAxisColour );
        context->DrawLine(
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

            Float3 graphLinePoints[1024];
            assert(dimof(graphLinePoints)>=(valuesCount*2));

            //  figure out y axis coordination conversion...
            float yB = -(graphArea._bottomRight[1]-graphArea._topLeft[1]-20)/float(maxValue-minValue);
            float yA = float(graphArea._bottomRight[1]-10) - yB * minValue; 
            float xB = (graphArea._bottomRight[0]-graphArea._topLeft[0])/float(maxValuesCount-1);
            float yZ = float(graphArea._bottomRight[1]);

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
                context->DrawTriangle(  ProjectionMode::P2D, 
                                        AsPixelCoords(Coord2(x0+.5f, y0+.5f)), colorTop,
                                        AsPixelCoords(Coord2(x0+.5f, yZ+.5f)), colorBottom,
                                        AsPixelCoords(Coord2(x1+.5f, y1+.5f)), colorTop );
                context->DrawTriangle(  ProjectionMode::P2D, 
                                        AsPixelCoords(Coord2(x1+.5f, y1+.5f)), colorTop,
                                        AsPixelCoords(Coord2(x0+.5f, yZ+.5f)), colorBottom,
                                        AsPixelCoords(Coord2(x1+.5f, yZ+.5f)), colorBottom );
            }

            context->DrawLines(ProjectionMode::P2D, graphLinePoints, (valuesCount-1)*2, HistoryGraphLineColor);

            {
                    // label the peak & write min and max values
                Coord2 peakPos(graphArea._topLeft[0] + xB*peakIndex, yA + yB * values[peakIndex] - 14);
                Coord2 maxPos(graphArea._topLeft[0] + 14, graphArea._topLeft[1] + 8);
                Coord2 minPos(graphArea._topLeft[0] + 14, graphArea._bottomRight[1] - 18);

                DrawFormatText(context, Rect(peakPos, peakPos),  nullptr, GraphLabel, "%6.2f", values[peakIndex]);
                DrawFormatText(context, Rect(minPos, minPos),    nullptr, GraphLabel, "%6.2f", minValue);
                DrawFormatText(context, Rect(maxPos, maxPos),    nullptr, GraphLabel, "%6.2f", maxValue);
            }
        }
    }

    void DrawHistoryGraph_ExtraLine(IOverlayContext* context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float minValue, float maxValue)
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

            context->DrawLines(ProjectionMode::P2D, graphLinePoints, (valuesCount-1)*2, HistoryGraphExtraLineColor);
        }
    }

    void        DrawTriangles(IOverlayContext* context, const Coord2 triangleCoordinates[], const ColorB triangleColours[], unsigned triangleCount)
    {
        std::vector<Float3> pixelCoords;
        pixelCoords.resize(triangleCount*3);
        for (unsigned c=0; c<triangleCount*3; ++c) {
            pixelCoords[c] = AsPixelCoords(Coord2(triangleCoordinates[c][0], triangleCoordinates[c][1]));
        }

        context->DrawTriangles(ProjectionMode::P2D, AsPointer(pixelCoords.begin()), triangleCount*3, triangleColours);
    }

    void        DrawLines(IOverlayContext* context, const Coord2 lineCoordinates[], const ColorB lineColours[], unsigned lineCount)
    {
        std::vector<Float3> pixelCoords;
        pixelCoords.resize(lineCount*2);
        for (unsigned c=0; c<lineCount*2; ++c) {
            pixelCoords[c] = AsPixelCoords(Coord2(lineCoordinates[c][0], lineCoordinates[c][1]));
        }

        context->DrawLines(ProjectionMode::P2D, AsPointer(pixelCoords.begin()), lineCount*2, lineColours);
    }

    class TableFontBox
    {
    public:
        class Desc {};
        std::shared_ptr<RenderOverlays::Font> _headerFont;
		std::shared_ptr<RenderOverlays::Font> _valuesFont;
        TableFontBox(const Desc&) 
            : _headerFont(RenderOverlays::GetX2Font("DosisExtraBold", 20))
            , _valuesFont(RenderOverlays::GetX2Font("Petra", 20)) {}
    };

    ///////////////////////////////////////////////////////////////////////////////////
    void DrawTableHeaders(IOverlayContext* context, const Rect& rect, const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, ColorB bkColor, Interactables* interactables)
    {
        static const ColorB HeaderTextColor     ( 255, 255, 255, 255 );
        static const ColorB HeaderBkColor       (  96,  96,  96, 196 );
        static const ColorB HeaderBkOutColor    ( 255, 255, 255, 255 );
        static const ColorB SepColor            ( 255, 255, 255, 255 );

        context->DrawQuad(
            ProjectionMode::P2D, AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight),
            HeaderBkColor, HeaderBkOutColor, 
            Float2(0.f, 0.f), Float2(1.f, 1.f),
            Float2(0.f, 0.f), Float2(0.f, 0.f),
            RENDEROVERLAYS_SHAPES_HLSL ":Paint,Shape=RectShape,Fill=RaisedRefactiveFill,Outline=SolidFill");

        TextStyle style{DrawTextOptions(false, true)};

        Layout tempLayout(rect);
        tempLayout._paddingInternalBorder = 0;
        for (auto i=fieldHeaders.begin(); i!=fieldHeaders.end(); ++i) {
            Rect r = tempLayout.AllocateFullHeight(i->second);
            if (!i->first.empty() && i->second) {
                // DrawRectangle(context, r, bkColor);

                if (i != fieldHeaders.begin())
                    context->DrawLine(ProjectionMode::P2D,
                        AsPixelCoords(Coord2(r._topLeft[0], r._topLeft[1]+2)), SepColor,
                        AsPixelCoords(Coord2(r._topLeft[0], r._bottomRight[1]-2)), SepColor,
                        1.f);
                r._topLeft[0] += 8;

                const ColorB colour = HeaderTextColor;
                context->DrawText(AsPixelCoords(r), ConsoleRig::FindCachedBox2<TableFontBox>()._headerFont, style, colour, TextAlignment::TopLeft, MakeStringSection(i->first));

                if (interactables)
                    interactables->Register(Interactables::Widget(r, InteractableId_Make(MakeStringSection(i->first))));
            }
        }
    }

    void DrawTableEntry(        IOverlayContext* context,
                                const Rect& rect, 
                                const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, 
                                const std::map<std::string, TableElement>& entry)
    {
        static const ColorB TextColor   ( 255, 255, 255, 255 );
        static const ColorB BkColor     (   0,   0,   0,  20 );
        static const ColorB BkOutColor  ( 255, 255, 255, 255 );
        static const ColorB SepColor    ( 255, 255, 255, 255 );

        context->DrawQuad(
            ProjectionMode::P2D, AsPixelCoords(rect._topLeft), AsPixelCoords(rect._bottomRight),
            BkColor, BkOutColor, 
            Float2(0.f, 0.f), Float2(1.f, 1.f),
            Float2(0.f, 0.f), Float2(0.f, 0.f),
            RENDEROVERLAYS_SHAPES_HLSL ":Paint,Shape=RectShape,Fill=RaisedRefactiveFill,Outline=SolidFill");

        TextStyle style{DrawTextOptions(true, false)};

        Layout tempLayout(rect);
        tempLayout._paddingInternalBorder = 0;
        for (auto i=fieldHeaders.begin(); i!=fieldHeaders.end(); ++i) {
            if (i->second) {
                auto s = entry.find(i->first);
                Rect r = tempLayout.AllocateFullHeight(i->second);
                if (s != entry.end() && !s->second._label.empty()) {

                    if (i != fieldHeaders.begin())
                        context->DrawLine(ProjectionMode::P2D,
                            AsPixelCoords(Coord2(r._topLeft[0], r._topLeft[1]+2)), SepColor,
                            AsPixelCoords(Coord2(r._topLeft[0], r._bottomRight[1]-2)), SepColor,
                            1.f);
                    r._topLeft[0] += 8;


                    const ColorB colour = TextColor;
                    // DrawRectangle(context, r, s->second._bkColour);
                    context->DrawText(AsPixelCoords(r), ConsoleRig::FindCachedBox2<TableFontBox>()._valuesFont, style, colour, TextAlignment::TopLeft, MakeStringSection(s->second._label));
                }
            }
        }
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

    void DrawHexahedronCorners(IOverlayContext* context, const HexahedronCorners&corners, ColorB entryColour, unsigned partMask)
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

            context->DrawLines( 
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

            context->DrawTriangles(
                ProjectionMode::P3D, 
                triangles, dimof(triangles),
                ColorB(entryColour.r, entryColour.g, entryColour.b, BoundingBoxTriangleAlpha));
        }
    }

    void DrawBoundingBox(
        IOverlayContext* context, const AABoundingBox& box, 
        const Float3x4& localToWorld, 
        ColorB entryColour, unsigned partMask)
    {
        auto corners = HexahedronCorners::FromAABB(box, localToWorld);
        DrawHexahedronCorners(context, corners, entryColour, partMask);
    }
    
    void DrawFrustum(
        IOverlayContext* context, const Float4x4& worldToProjection, 
        ColorB entryColour, unsigned partMask)
    {
        auto corners = HexahedronCorners::FromFrustumCorners(worldToProjection);
        DrawHexahedronCorners(context, corners, entryColour, partMask);
    }

    ///////////////////////////////////////////////////////////////////////////////////
    static float Saturate(float value) { return std::max(std::min(value, 1.f), 0.f); }

    void HScrollBar_Draw(IOverlayContext* context, const ScrollBar::Coordinates& coordinates, float thumbPosition)
    {
        const auto r = coordinates.InteractableRect();
        float t = Saturate((thumbPosition - coordinates.MinValue()) / float(coordinates.MaxValue() - coordinates.MinValue()));
        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(Coord2(r._topLeft[0], r._topLeft[1])),
            AsPixelCoords(Coord2(r._bottomRight[0], r._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(t, 0.f), Float2(t, 0.f),
            "Utility\\DebuggingShapes.pixel.hlsl:ScrollBarShader");
    }

    void HScrollBar_DrawLabel(IOverlayContext* context, const Rect& rect)
    {
        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(0.f, 0.f), Float2(0.f, 0.f),
            "Utility\\DebuggingShapes.pixel.hlsl:TagShader");
    }

    void HScrollBar_DrawGridBackground(IOverlayContext* context, const Rect& rect)
    {
        context->DrawQuad(
            ProjectionMode::P2D,
            AsPixelCoords(Coord2(rect._topLeft[0], rect._topLeft[1])),
            AsPixelCoords(Coord2(rect._bottomRight[0], rect._bottomRight[1])),
            ColorB(0xffffffff), ColorB(0xffffffff),
            Float2(0.f, 0.f), Float2(1.f, 1.f), Float2(0.f, 0.f), Float2(0.f, 0.f),
            "Utility\\DebuggingShapes.pixel.hlsl:GridBackgroundShader");
    }

    ///////////////////////////////////////////////////////////////////////////////////
    IWidget::~IWidget() {}

    InteractableId  InteractableId_Make(StringSection<char> name)   { return Hash64(name.begin(), name.end()); }

    bool DebugScreensSystem::OnInputEvent(const PlatformRig::InputContext& context, const PlatformRig::InputSnapshot& evnt)
    {
        bool consumedEvent      = false;
        _currentMouseHeld       = evnt._mouseButtonsDown;
        if (_currentMouse[0] != evnt._mousePosition[0] || _currentMouse[1] != evnt._mousePosition[1]) {
            _currentMouse = evnt._mousePosition;
            _currentInterfaceState  = _currentInteractables.BuildInterfaceState(_currentMouse, _currentMouseHeld);
        }

        for (auto i=_systemWidgets.begin(); i!=_systemWidgets.end() && !consumedEvent; ++i) {
            consumedEvent |= i->_widget->ProcessInput(_currentInterfaceState, context, evnt);
        }

        for (auto i=_panels.begin(); i!=_panels.end() && !consumedEvent; ++i) {
            if (i->_widgetIndex < _widgets.size()) {

                bool alreadySeen = false;
                for (auto i2=_panels.begin(); i2!=i; ++i2) {
                    alreadySeen |= i2->_widgetIndex == i->_widgetIndex;
                }
                
                if (!alreadySeen) {
                    consumedEvent |= _widgets[i->_widgetIndex]._widget->ProcessInput(_currentInterfaceState, context, evnt);
                }
            }
        }

        // if (!(evnt._mouseButtonsDown & (1<<0)) && (evnt._mouseButtonsTransition & (1<<0)) && !consumedEvent) {
        if (!consumedEvent) {
            consumedEvent |= ProcessInputPanelControls(_currentInterfaceState, context, evnt);
        }

        _consumedInputEvent |= consumedEvent;
        return consumedEvent;
    }

    static const char* s_PanelControlsButtons[] = {"<", ">", "H", "V", "X"};
    
    void DebugScreensSystem::RenderPanelControls(   IOverlayContext* context,
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
        interactables.Register(Interactables::Widget(buttonsRect, panelControlsId));

            //      panel controls are only visible when we've got a mouse over...
        if (interfaceState.HasMouseOver(panelControlsId) || interfaceState.HasMouseOver(nameDropDownId)) {
            DrawRoundedRectangle(context, buttonsRect, RoundedRectBackgroundColour, RoundedRectOutlineColour);

            Layout buttonsLayout(buttonsRect);
            buttonsLayout._paddingBetweenAllocations = buttonsLayout._paddingInternalBorder = buttonPadding;
            for (unsigned c=0; c<buttonCount; ++c) {
                Rect buttonRect = buttonsLayout.Allocate(Coord2(buttonSize, buttonSize));
                InteractableId id = InteractableId_Make(s_PanelControlsButtons[c])+panelIndex;
                if (interfaceState.HasMouseOver(id)) {
                    DrawElipse(context, buttonRect, ColorB(0xff000000u));
                    DrawText(context, buttonRect, nullptr, ColorB(0xff000000u), s_PanelControlsButtons[c]);
                } else {
                    DrawElipse(context, buttonRect, ColorB(0xffffffffu));
                    DrawText(context, buttonRect, nullptr, ColorB(0xffffffffu), s_PanelControlsButtons[c]);
                }
                interactables.Register(Interactables::Widget(buttonRect, id));
            }

            Rect nameRect = buttonsLayout.Allocate(Coord2(nameSize, buttonSize));
            DrawText(context, nameRect, nullptr, ColorB(0xffffffffu), MakeStringSection(name));

                //
                //      If the mouse is over the name rect, we get a drop list list
                //      of the screens available...
                //
            interactables.Register(Interactables::Widget(nameRect, nameRectId));
            if (interfaceState.HasMouseOver(nameRectId) || interfaceState.HasMouseOver(nameDropDownId)) {
                    /////////////////////////////
                const Coord dropDownSize = Coord(_widgets.size() * buttonSize + (_widgets.size()+1) * buttonPadding);
                const Rect dropDownRect(    Coord2(nameRect._topLeft[0], nameRect._bottomRight[1]-3),
                                            Coord2(nameRect._topLeft[0]+nameSize, nameRect._bottomRight[1]-3+dropDownSize));
                DrawRectangle(context, dropDownRect, RoundedRectBackgroundColour);
                const Rect dropDownInteractableRect(Coord2(dropDownRect._topLeft[0], dropDownRect._topLeft[1]-8), Coord2(dropDownRect._bottomRight[0], dropDownRect._bottomRight[1]));
                interactables.Register(Interactables::Widget(dropDownInteractableRect, nameDropDownId));

                    /////////////////////////////
                unsigned y = dropDownRect._topLeft[1] + buttonPadding;
                for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                    Rect partRect(Coord2(dropDownRect._topLeft[0], y), Coord2(dropDownRect._topLeft[0]+nameSize, y+buttonSize));
                    const InteractableId thisId = nameDropDownWidgetId 
                                                + std::distance(_widgets.cbegin(), i) 
                                                + panelIndex * _widgets.size();
                    const bool mouseOver = interfaceState.HasMouseOver(thisId);
                    if (mouseOver) {
                        DrawRectangle(context, partRect, ColorB(180, 200, 255, 64));
                    }
                    DrawText(context, partRect, nullptr, ColorB(0xffffffffu), MakeStringSection(i->_name));
                    y += buttonSize + buttonPadding;
                    interactables.Register(Interactables::Widget(partRect, thisId));
                }
                    /////////////////////////////
            }
        }

            //  If we've got a back button render it in the top left
        if (panelIndex < _panels.size() && !_panels[panelIndex]._backButton.empty()) {
            Rect backButtonRect(    Coord2(layout._maximumSize._topLeft[0] + 8, layout._maximumSize._topLeft[1] + 4),
                                    Coord2(layout._maximumSize._topLeft[0] + 8 + 100, layout._maximumSize._topLeft[1] + 4 + buttonSize));
            interactables.Register(Interactables::Widget(backButtonRect, backButtonId));
            const bool mouseOver = interfaceState.HasMouseOver(backButtonId);
            if (mouseOver) {
                DrawRoundedRectangle(context, backButtonRect, RoundedRectBackgroundColour, RoundedRectOutlineColour);
                ColorB colour = ColorB(0x7fffffffu);
                if (interfaceState.IsMouseButtonHeld()) {
                    colour = ColorB(0xffffffffu);
                }
                DrawFormatText(context, backButtonRect, nullptr, colour, "%s", "Back");
            }
        }
    }

    bool    DebugScreensSystem::ProcessInputPanelControls(  InterfaceState& interfaceState, 
                                                            const PlatformRig::InputContext& inputContext,
															const PlatformRig::InputSnapshot& evnt)
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

        const KeyId ctrl    = PlatformRig::KeyId_Make("control");
        const KeyId left    = PlatformRig::KeyId_Make("left");
        const KeyId right   = PlatformRig::KeyId_Make("right");
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
                                &overlayContext, (unsigned)std::distance(_panels.begin(), i),
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
        _currentInterfaceState = _currentInteractables.BuildInterfaceState(_currentMouse, _currentMouseHeld);
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

    void DebugScreensSystem::Register(std::shared_ptr<IWidget> widget, const char name[], Type type)
    {
        WidgetAndName wAndN;
        wAndN._widget = std::move(widget);
        wAndN._name = name;
        wAndN._hashCode = Hash64(wAndN._name);
        
        if (type == InPanel) {
            _widgets.push_back(wAndN);
            TriggerWidgetChangeCallbacks();
        } else if (type == SystemDisplay) {
            _systemWidgets.push_back(wAndN);
        }
    }
    
    void DebugScreensSystem::Unregister(const char name[])
    {
        auto it = _widgets.begin();
        for (; it != _widgets.end(); ++it) {
            if (strcmp(it->_name.c_str(), name) == 0) {
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

    void DebugScreensSystem::SwitchToScreen(unsigned panelIndex, const char name[])
    {
        if (panelIndex < _panels.size()) {
            if (!name) {
                _panels[panelIndex]._widgetIndex = size_t(-1);
                _panels[panelIndex]._backButton = std::string();
                return;
            }

                // look for exact match first...
            for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                if (!XlCompareStringI(i->_name.c_str(), name)) {
                    _panels[panelIndex]._widgetIndex = std::distance(_widgets.cbegin(), i);
                    _panels[panelIndex]._backButton = std::string();  // clear out the back button
                    return;
                }
            }

                // If we don't have an exact match, just find a substring...
            for (std::vector<WidgetAndName>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
                if (XlFindStringI(i->_name.c_str(), name)) {
                    _panels[panelIndex]._widgetIndex = std::distance(_widgets.cbegin(), i);
                    _panels[panelIndex]._backButton = std::string();  // clear out the back button
                    return;
                }
            }
        }
    }

    void DebugScreensSystem::SwitchToScreen(const char name[])
    {
        SwitchToScreen(0, name);
    }

    bool DebugScreensSystem::SwitchToScreen(unsigned panelIndex, uint64 hashCode)
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
        _currentMouse = Coord2(0,0);
        _currentMouseHeld = 0;
        _nextWidgetChangeCallbackIndex = 0;

        Panel p;
        p._widgetIndex = size_t(-1);
        p._size = .5f;
        p._horizontalDivider = false;
        _panels.push_back(p);

#if defined(HAS_XLE_CONSOLE_RIG)
       {
           using namespace luabridge;
           auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
           void (DebugScreensSystem::*switchToScreen)(const char[]) = &DebugScreensSystem::SwitchToScreen;
           getGlobalNamespace(luaState)
               .beginClass<DebugScreensSystem>("DebugScreensSystem")
                   .addFunction("SwitchToScreen", switchToScreen)
               .endClass();

           setGlobal(luaState, this, "MainDebug");
       }
#endif
    }

    DebugScreensSystem::~DebugScreensSystem()
    {
#if defined(HAS_XLE_CONSOLE_RIG)
       auto* luaState = ConsoleRig::Console::GetInstance().GetLuaState();
       lua_pushnil(luaState);
       lua_setglobal(luaState, "MainDebug");
#endif
    }

    ///////////////////////////////////////////////////////////////////////////////////
    InterfaceState::InterfaceState()
    {
        _mousePosition = Coord2(std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
        _mouseButtonsHeld = 0;
    }

    InterfaceState::InterfaceState(const Coord2& mousePosition, unsigned mouseButtonsHeld, const std::vector<Interactables::Widget>& mouseStack)
    :   _mousePosition(mousePosition)
    ,   _mouseButtonsHeld(mouseButtonsHeld)
    ,   _mouseOverStack(mouseStack)
    {}

    bool InterfaceState::HasMouseOver(InteractableId id) 
    { 
        // return std::find(_mouseOverStack.begin(), _mouseOverStack.end(), id) != _mouseOverStack.end(); 
        for(std::vector<Interactables::Widget>::iterator i=_mouseOverStack.begin(); i!=_mouseOverStack.end(); ++i) {
            if (i->_id == id) {
                return true;
            }
        }
        return false;
    }

    ///////////////////////////////////////////////////////////////////////////////////
    Interactables::Interactables()
    {
    }

    void Interactables::Register(const Widget& widget)
    {
        _widgets.push_back( widget );
    }

    static bool Intersection(const Rect& rect, const Coord2& position)
    {
        return rect._topLeft[0] <= position[0] && position[0] < rect._bottomRight[0]
            && rect._topLeft[1] <= position[1] && position[1] < rect._bottomRight[1]
            ;
    }

    std::vector<Interactables::Widget> Interactables::Intersect(const Coord2& position) const
    {
        std::vector<Widget> result;
        for (std::vector<Widget>::const_iterator i=_widgets.begin(); i!=_widgets.end(); ++i) {
            if (Intersection(i->_rect, position)) {
                result.push_back(*i);
            }
        }
        return result;
    }

    InterfaceState Interactables::BuildInterfaceState(const Coord2& mousePosition, unsigned mouseButtonsHeld)
    {
        return InterfaceState(mousePosition, mouseButtonsHeld, Intersect(mousePosition));
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

    Coord Layout::GetWidthRemaining() const
    {
        auto maxSizeWidth = _maximumSize._bottomRight[0] - _maximumSize._topLeft[0];

            // get the remaining space on the current line
        if (!_caretX) {
            return maxSizeWidth - 2 * _paddingInternalBorder;
        }

        return maxSizeWidth - _caretX - _paddingInternalBorder - _paddingBetweenAllocations;
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

        Rect result;
        result._topLeft[0]        = _maximumSize._topLeft[0] + _paddingInternalBorder;
        result._bottomRight[0]    = _maximumSize._bottomRight[0] - _paddingInternalBorder;
        result._topLeft[1]        = _maximumSize._topLeft[1] + _caretY;
        result._bottomRight[1]    = std::min(result._topLeft[1]+height, _maximumSize._bottomRight[1]-_paddingInternalBorder);
        _caretY = result._bottomRight[1] - _maximumSize._topLeft[1] + _paddingBetweenAllocations;
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
        ColorB( 205,74,74   ),
        ColorB( 204,102,102 ),
        ColorB( 188,93,88   ),
        ColorB( 255,83,73   ),
        ColorB( 253,94,83   ),
        ColorB( 253,124,110 ),
        ColorB( 253,188,180 ),
        ColorB( 255,110,74  ),
        ColorB( 255,160,137 ),
        ColorB( 234,126,93  ),
        ColorB( 180,103,77  ),
        ColorB( 165,105,79  ),
        ColorB( 255,117,56  ),
        ColorB( 255,127,73  ),
        ColorB( 221,148,117 ),
        ColorB( 255,130,67  ),
        ColorB( 255,164,116 ),
        ColorB( 159,129,112 ),
        ColorB( 205,149,117 ),
        ColorB( 239,205,184 ),
        ColorB( 214,138,89  ),
        ColorB( 222,170,136 ),
        ColorB( 250,167,108 ),
        ColorB( 255,207,171 ),
        ColorB( 255,189,136 ),
        ColorB( 253,217,181 ),
        ColorB( 255,163,67  ),
        ColorB( 239,219,197 ),
        ColorB( 255,182,83  ),
        ColorB( 231,198,151 ),
        ColorB( 138,121,93  ),
        ColorB( 250,231,181 ),
        ColorB( 255,207,72  ),
        ColorB( 252,217,117 ),
        ColorB( 253,219,109 ),
        ColorB( 252,232,131 ),
        ColorB( 240,232,145 ),
        ColorB( 236,234,190 ),
        ColorB( 186,184,108 ),
        ColorB( 253,252,116 ),
        ColorB( 253,252,116 ),
        ColorB( 255,255,153 ),
        ColorB( 197,227,132 ),
        ColorB( 178,236,93  ),
        ColorB( 135,169,107 ),
        ColorB( 168,228,160 ),
        ColorB( 29,249,20   ),
        ColorB( 118,255,122 ),
        ColorB( 113,188,120 ),
        ColorB( 109,174,129 ),
        ColorB( 159,226,191 ),
        ColorB( 28,172,120  ),
        ColorB( 48,186,143  ),
        ColorB( 69,206,162  ),
        ColorB( 59,176,143  ),
        ColorB( 28,211,162  ),
        ColorB( 23,128,109  ),
        ColorB( 21,128,120  ),
        ColorB( 31,206,203  ),
        ColorB( 120,219,226 ),
        ColorB( 119,221,231 ),
        ColorB( 128,218,235 ),
        ColorB( 65,74,76    ),
        ColorB( 25,158,189  ),
        ColorB( 28,169,201  ),
        ColorB( 29,172,214  ),
        ColorB( 154,206,235 ),
        ColorB( 26,72,118   ),
        ColorB( 25,116,210  ),
        ColorB( 43,108,196  ),
        ColorB( 31,117,254  ),
        ColorB( 197,208,230 ),
        ColorB( 176,183,198 ),
        ColorB( 93,118,203  ),
        ColorB( 162,173,208 ),
        ColorB( 151,154,170 ),
        ColorB( 173,173,214 ),
        ColorB( 115,102,189 ),
        ColorB( 116,66,200  ),
        ColorB( 120,81,169  ),
        ColorB( 157,129,186 ),
        ColorB( 146,110,174 ),
        ColorB( 205,164,222 ),
        ColorB( 143,80,157  ),
        ColorB( 195,100,197 ),
        ColorB( 251,126,253 ),
        ColorB( 252,116,253 ),
        ColorB( 142,69,133  ),
        ColorB( 255,29,206  ),
        ColorB( 255,29,206  ),
        ColorB( 255,72,208  ),
        ColorB( 230,168,215 ),
        ColorB( 192,68,143  ),
        ColorB( 110,81,96   ),
        ColorB( 221,68,146  ),
        ColorB( 255,67,164  ),
        ColorB( 246,100,175 ),
        ColorB( 252,180,213 ),
        ColorB( 255,188,217 ),
        ColorB( 247,83,148  ),
        ColorB( 255,170,204 ),
        ColorB( 227,37,107  ),
        ColorB( 253,215,228 ),
        ColorB( 202,55,103  ),
        ColorB( 222,93,131  ),
        ColorB( 252,137,172 ),
        ColorB( 247,128,161 ),
        ColorB( 200,56,90   ),
        ColorB( 238,32,77   ),
        ColorB( 255,73,108  ),
        ColorB( 239,152,170 ),
        ColorB( 252,108,133 ),
        ColorB( 252,40,71   ),
        ColorB( 255,155,170 ),
        ColorB( 203,65,84   ),
        ColorB( 237,237,237 ),
        ColorB( 219,215,210 ),
        ColorB( 205,197,194 ),
        ColorB( 149,145,140 ),
        ColorB( 35,35,35    )
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

