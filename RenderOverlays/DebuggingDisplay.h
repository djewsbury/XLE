// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IOverlayContext.h"
#include "OverlayPrimitives.h"
#include "../PlatformRig/InputListener.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"     // for StringSection
#include "../Core/Types.h"
#include <functional>
#include <vector>
#include <map>

namespace RenderOverlays { namespace DebuggingDisplay
{
    struct Layout /////////////////////////////////////////////////////////////////////
    {
        Rect    _maximumSize;
        Coord   _maxRowWidth;
        Coord   _caretX, _caretY;
        Coord   _currentRowMaxHeight;
        Coord   _paddingInternalBorder;
        Coord   _paddingBetweenAllocations;

        Layout(const Rect& maximumSize);
        Rect    AllocateFullWidth(Coord height);
        Rect    AllocateFullHeight(Coord width);
        Rect    AllocateFullHeightFraction(float proportionOfWidth);
        Rect    AllocateFullWidthFraction(float proportionOfHeight);
        Rect    Allocate(Coord2 dimensions);
        Rect    GetMaximumSize() const { return _maximumSize; }
        Coord   GetWidthRemaining();
    };

    typedef uint64_t InteractableId;
    InteractableId InteractableId_Make(StringSection<char> name);
    typedef uint32 KeyId;

    class InterfaceState;
    enum class ProcessInputResult { Passthrough, Consumed };

    ///////////////////////////////////////////////////////////////////////////////////
    class Interactables
    {
    public:
        struct Widget
        {
            Rect _rect;
            InteractableId _id = 0;
        };
        std::vector<Widget> _widgets;

        void                Register(const Widget& widget);
        std::vector<Widget> Intersect(const Coord2& position) const;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    class InterfaceState
    {
    public:
        bool                    HasMouseOver(InteractableId id);
        InteractableId          TopMostId() const;
        Interactables::Widget   TopMostWidget() const;
        bool                    IsMouseButtonHeld(unsigned buttonIndex = 0) const   { return !!(_mouseButtonsHeld&(1<<buttonIndex)); }
        Coord2                  MousePosition() const                               { return _mousePosition; }

        void BeginCapturing(const Interactables::Widget& widget);
        void EndCapturing();

        struct Capture
        {
            Coord2 _driftDuringCapture = Coord2{0,0};
            Interactables::Widget _widget;
        };
        const Capture& GetCapture() const                     { return _capture;  }

        const std::vector<Interactables::Widget>& GetMouseOverStack() const         { return _mouseOverStack; }
        const PlatformRig::InputContext& GetViewInputContext() const                { return _viewInputContext; }

        InterfaceState();
        InterfaceState( const PlatformRig::InputContext& viewInputContext,
                        const Coord2& mousePosition, unsigned mouseButtonsHeld, 
                        const std::vector<Interactables::Widget>& mouseStack,
                        const Capture& capture);
    protected:
        std::vector<Interactables::Widget> _mouseOverStack;
        Capture     _capture;
        Coord2      _mousePosition;
        unsigned    _mouseButtonsHeld;
        PlatformRig::InputContext _viewInputContext;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    class IWidget
    {
    public:
        using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = PlatformRig::InputSnapshot;
        using ProcessInputResult = RenderOverlays::DebuggingDisplay::ProcessInputResult;

        virtual void                    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        virtual ProcessInputResult      ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);
        virtual                         ~IWidget();
    };

    ///////////////////////////////////////////////////////////////////////////////////
    extern const ColorB   RandomPaletteColorTable[];
    extern const size_t   RandomPaletteColorTable_Size;

    void        OutlineEllipse(IOverlayContext& context, const Rect& rect, ColorB colour);
    void        FillEllipse(IOverlayContext& context, const Rect& rect, ColorB colour);

    namespace Corner
    {
        static const auto TopLeft = 1u;
        static const auto TopRight = 2u;
        static const auto BottomLeft = 4u;
        static const auto BottomRight = 8u;
        using BitField = unsigned;
    }

    void FillRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void FillAndOutlineRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB fillColor, ColorB outlineColour,
        float outlineWidth = 1.f, float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void OutlineRoundedRectangle(
        IOverlayContext& context, const Rect& rect, 
        ColorB colour, 
        float outlineWidth = 1.f, float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);

    void FillRaisedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);
    void FillDepressedRoundedRectangle(
        IOverlayContext& context, const Rect& rect,
        ColorB fillColor,
        float roundedProportion = 1.f / 8.f,
        Corner::BitField cornerFlags = 0xf);

    void        FillRectangle(IOverlayContext& context, const Rect& rect, ColorB colour);
    void        OutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB outlineColour, float outlineWidth = 1.f);
    void        FillAndOutlineRectangle(IOverlayContext& context, const Rect& rect, ColorB fillColour, ColorB outlineColour, float outlineWidth = 1.f);

    struct DrawText
    {
        mutable DrawTextFlags::BitField _flags = DrawTextFlags::Shadow;
        mutable Font* _font = nullptr;
        mutable ColorB _color = ColorB::White;
        mutable TextAlignment _alignment = TextAlignment::Left;

        Coord2 Draw(IOverlayContext& context, const Rect& rect, StringSection<>) const;
        Coord2 FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], ...) const;
        Coord2 FormatAndDraw(IOverlayContext& context, const Rect& rect, const char format[], va_list args) const;
        
        Coord2 operator()(IOverlayContext& context, const Rect& rect, StringSection<> text) { return Draw(context, rect, text); }

        const DrawText& Alignment(TextAlignment alignment) const { _alignment = alignment; return *this; }
        const DrawText& Flags(DrawTextFlags::BitField flags) const { _flags = flags; return *this; }
        const DrawText& Color(ColorB color) const { _color = color; return *this; }
        const DrawText& Font(Font& font) const { _font = &font; return *this; }
    };

    void        DrawHistoryGraph(IOverlayContext& context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory);
    void        DrawHistoryGraph_ExtraLine(IOverlayContext& context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float minValue, float maxValue);

    void        DrawBarGraph(IOverlayContext& context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory);

    void        DrawTriangles(IOverlayContext& context, const Coord2 triangleCoordinates[], const ColorB triangleColours[], unsigned triangleCount);
    void        DrawLines(IOverlayContext& context, const Coord2 lineCoordinates[], const ColorB lineColours[], unsigned lineCount);

    Float3      AsPixelCoords(Coord2 input);
    Float3      AsPixelCoords(Coord2 input, float depth);
    Float3      AsPixelCoords(Float2 input);
    Float3      AsPixelCoords(Float3 input);
    std::tuple<Float3, Float3> AsPixelCoords(const Rect& rect);

    ///////////////////////////////////////////////////////////////////////////////////

    template<typename Type>
        struct GraphSeries
    {
        Type _minValue, _maxValue;
        IteratorRange<const Type*> _values;

        GraphSeries(
            IteratorRange<const Type*> values,
            std::optional<Type>& historicalMin, std::optional<Type>& historicalMax)
        : _values(values)
        {
            _maxValue = -std::numeric_limits<float>::max();
            _minValue = std::numeric_limits<float>::max();
            unsigned peakIndex = 0;
            for (unsigned c=0; c<values.size(); ++c) {
                _maxValue = std::max(values[c], _maxValue);
                _minValue = std::min(values[c], _minValue);
                if (values[c] > values[peakIndex]) {peakIndex = c;}
            }

            _minValue = std::min(_minValue, _maxValue*.75f);

            if (historicalMin.has_value())
                _minValue = std::min(LinearInterpolate(historicalMin.value(), _minValue, 0.15f), _minValue);
            historicalMin = _minValue;

            if (historicalMax.has_value())
                _maxValue = std::max(LinearInterpolate(historicalMax.value(), _maxValue, 0.15f), _maxValue);
            historicalMax = _maxValue;
        }
    };

    struct BarChartColoring
    {
        ColorB _blocks[4];
    };

    template<typename Type>
        void DrawBarChartContents(IOverlayContext& context, const Rect& graphArea, GraphSeries<Type> series, unsigned horizontalAllocation);

    ///////////////////////////////////////////////////////////////////////////////////
    typedef std::tuple<Float3, Float3>      AABoundingBox;

    void        DrawBoundingBox(
        IOverlayContext& context, const AABoundingBox& box, 
        const Float3x4& localToWorld, 
        ColorB entryColour, unsigned partMask = 0x3);

    void        DrawFrustum(
        IOverlayContext& context, const Float4x4& worldToProjection, 
        ColorB entryColour, unsigned partMask = 0x3);

    ///////////////////////////////////////////////////////////////////////////////////
    //          S C R O L L   B A R S

    class ScrollBar
    {
    public:
        class Coordinates
        {
        public:
            struct Flags
            {
                enum Enum { NoUpDown = 1<<0, Horizontal = 1<<1 };
                typedef unsigned BitField;
            };

            Coordinates(const Rect& rect, float minValue, float maxValue, 
                        float visibleWindowSize, Flags::BitField flags = 0);

            bool    Collapse() const;
            Rect    InteractableRect() const        { return _interactableRect; }
            Rect    ScrollArea() const              { return _scrollAreaRect; }

            Rect    Thumb(float value) const;

            float   PixelsToValue(Coord pixels) const;
            float   MinValue() const                    { return _valueBase; }
            float   MaxValue() const                    { return _maxValue; }

        protected:
            float   _valueToPixels;
            float   _valueBase;
            float   _maxValue;

            Coord   _pixelsBase;
            Coord   _windowHeight;
            Coord   _thumbHeight;

            Rect    _interactableRect;
            Rect    _scrollAreaRect;
            Flags::BitField _flags;

            Coord   ValueToPixels(float value) const;
        };

        IWidget::ProcessInputResult                ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);
        float               CalculateCurrentOffset(const Coordinates& coordinates) const;
        float               CalculateCurrentOffset(const Coordinates& coordinates, float oldValue) const;
        InteractableId      GetID() const;
        bool                IsDragging() const { return _draggingScrollBar; }
        void                ProcessDelta(float delta) const;

        ScrollBar(InteractableId id=0, Coordinates::Flags::BitField flags=0);
    protected:
        InteractableId  _id;
        mutable Coord   _scrollOffsetPixels;
        mutable float   _resolvedScrollOffset;
        mutable float   _pendingDelta;
        bool            _draggingScrollBar;
        Coordinates::Flags::BitField _flags;
    };

    void DrawScrollBar(IOverlayContext& context, const ScrollBar::Coordinates& coordinates, float thumbPosition, ColorB fillColour = ColorB{0x57, 0x57, 0x57});

    void HTweakerBar_Draw(IOverlayContext& context, const ScrollBar::Coordinates& coordinates, float thumbPosition);
    void HTweakerBar_DrawLabel(IOverlayContext& context, const Rect& rect);
    void HTweakerBar_DrawGridBackground(IOverlayContext& context, const Rect& rect);

    ///////////////////////////////////////////////////////////////////////////////////
    //          T A B L E S
    struct TableElement
    {
        std::string _label; ColorB _bkColour;
        TableElement(const std::string& label, ColorB bkColour = ColorB(0xff000000)) : _label(label), _bkColour(bkColour) {}
        TableElement(const char label[],  ColorB bkColour = ColorB(0xff000000)) : _label(label), _bkColour(bkColour) {}
        TableElement() : _bkColour(0xff000000) {}
    };
    void DrawTableHeaders(IOverlayContext& context, const Rect& rect, const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, ColorB bkColor, Interactables* interactables=NULL);
    Coord DrawTableEntry(IOverlayContext& context, const Rect& rect, const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, const std::map<std::string, TableElement>& entry);

    ///////////////////////////////////////////////////////////////////////////////////
    class DebugScreensSystem : public PlatformRig::IInputListener
    {
    public:
        bool        OnInputEvent(const PlatformRig::InputContext& context, const PlatformRig::InputSnapshot& evnt);
        void        Render(IOverlayContext& overlayContext, const Rect& viewport);
        bool        IsAnythingVisible();
        bool        IsAnyPanelActive();
        
        enum Type { InPanel, SystemDisplay };
        void        Register(std::shared_ptr<IWidget> widget, StringSection<> name, Type type = InPanel);
        void        Unregister(StringSection<> name);
        void        Unregister(IWidget& widget);

        void        SwitchToScreen(unsigned panelIndex, StringSection<> name);
        bool        SwitchToScreen(unsigned panelIndex, uint64_t hashCode);
        void        SwitchToScreen(StringSection<> name);
        const char* CurrentScreen(unsigned panelIndex);
        
        struct WidgetAndName 
        {
            std::shared_ptr<IWidget>    _widget;
            std::string                 _name;
            uint64_t                      _hashCode;
        };
        const std::vector<WidgetAndName>& GetWidgets() const { return _widgets; }
        
        using WidgetChangeCallback = std::function<void()>;
        unsigned AddWidgetChangeCallback(WidgetChangeCallback&& callback);
        void RemoveWidgetChangeCallback(unsigned callbackid);

        DebugScreensSystem();
        ~DebugScreensSystem();

    private:
        Interactables   _currentInteractables;
        InterfaceState  _currentInterfaceState;
        
        std::vector<WidgetAndName> _widgets;
        std::vector<WidgetAndName> _systemWidgets;
        
        unsigned _nextWidgetChangeCallbackIndex;
        std::vector<std::pair<unsigned, WidgetChangeCallback>> _widgetChangeCallbacks;
        
        void TriggerWidgetChangeCallbacks();

        struct Panel
        {
            size_t      _widgetIndex;
            float       _size;
            bool        _horizontalDivider;
            std::string _backButton;
        };
        std::vector<Panel> _panels;

        Coord2      _currentMouse;
        unsigned    _currentMouseHeld;

        void    RenderPanelControls(        IOverlayContext&    context,
                                            unsigned            panelIndex, const std::string& name, Layout&layout, bool allowDestroy,
                                            Interactables&      interactables, InterfaceState& interfaceState);
        bool    ProcessInputPanelControls(  InterfaceState&     interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot&    evnt);
    };

    ///////////////////////////////////////////////////////////////////////////////////
    inline bool IsGood(const Rect& rect)
    {
        return  rect._topLeft[0] < rect._bottomRight[0]
            &&  rect._topLeft[1] < rect._bottomRight[1];
    }

    inline bool IsInside(const Rect& rect, const Coord2& pt)
    {
        return  (pt[0] >= rect._topLeft[0])
            &&  (pt[1] >= rect._topLeft[1])
            &&  (pt[0] < rect._bottomRight[0])
            &&  (pt[1] < rect._bottomRight[1]);
    }

}}

