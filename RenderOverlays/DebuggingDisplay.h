// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IOverlayContext.h"
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

namespace RenderOverlays { class TextStyle; }

namespace RenderOverlays { namespace DebuggingDisplay
{
    typedef int         Coord;
    typedef Int2        Coord2;

    inline Coord2 AsCoord2(const Float2& input) { return Coord2(Coord(input[0]), Coord(input[1])); }
    inline Float2 AsFloat2(const Coord2& input) { return Float2(float(input[0]), float(input[1])); }

    struct Rect ///////////////////////////////////////////////////////////////////////
    {
        Coord2      _topLeft, _bottomRight;
        Rect(const Coord2& topLeft, const Coord2& bottomRight) : _topLeft(topLeft), _bottomRight(bottomRight) {}
        Rect() {}

        Coord       Width() const     { return _bottomRight[0] - _topLeft[0]; }
        Coord       Height() const    { return _bottomRight[1] - _topLeft[1]; }
    };

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
        Coord   GetWidthRemaining() const;
    };

    typedef uint64 InteractableId;
    InteractableId InteractableId_Make(StringSection<char> name);
    typedef uint32 KeyId;

    class InterfaceState;

    ///////////////////////////////////////////////////////////////////////////////////
    class Interactables
    {
    public:
        struct Widget
        {
            Rect _rect;
            InteractableId _id;
            Widget(const Rect& rect, InteractableId id) : _rect(rect), _id(id) {}
        };

        void                Register(const Widget& widget);
        std::vector<Widget> Intersect(const Coord2& position) const;
        InterfaceState      BuildInterfaceState(const Coord2& mousePosition, unsigned mouseButtonsHeld);
        Interactables();
    protected:
        std::vector<Widget> _widgets;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    class InterfaceState
    {
    public:
        InterfaceState();
        InterfaceState( const Coord2& mousePosition, unsigned mouseButtonsHeld, 
                        const std::vector<Interactables::Widget>& mouseStack);

        bool                    HasMouseOver(InteractableId id);
        InteractableId          TopMostId() const                                   { return (!_mouseOverStack.empty())?_mouseOverStack[_mouseOverStack.size()-1]._id:0; }
        Interactables::Widget   TopMostWidget() const                               { return (!_mouseOverStack.empty())?_mouseOverStack[_mouseOverStack.size()-1]:Interactables::Widget(Rect(), 0); }
        bool                    IsMouseButtonHeld(unsigned buttonIndex = 0) const   { return !!(_mouseButtonsHeld&(1<<buttonIndex)); }
        Coord2                  MousePosition() const                               { return _mousePosition; }

        const std::vector<Interactables::Widget>& GetMouseOverStack() const         { return _mouseOverStack; }

    protected:
        std::vector<Interactables::Widget> _mouseOverStack;
        Coord2      _mousePosition;
        unsigned    _mouseButtonsHeld;
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

        virtual void    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        virtual bool    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input);
        virtual         ~IWidget();
    };

    ///////////////////////////////////////////////////////////////////////////////////
    extern const ColorB   RandomPaletteColorTable[];
    extern const size_t   RandomPaletteColorTable_Size;

    void        DrawEllipse(IOverlayContext* context, const Rect& rect, ColorB colour);
    void        DrawFilledEllipse(IOverlayContext* context, const Rect& rect, ColorB colour);

    void DrawRoundedRectangle(
        IOverlayContext* context, const Rect& rect, 
        ColorB backgroundColour, ColorB outlineColour,
        float borderWidth = 1.f, float roundedProportion = 1.f / 8.f);
    void DrawRoundedRectangleOutline(
        IOverlayContext* context, const Rect& rect, 
        ColorB colour, 
        float width, float roundedProportion = 1.f / 8.f);

    void        DrawRectangle(IOverlayContext* context, const Rect& rect, ColorB colour);
    void        DrawRectangle(IOverlayContext* context, const Rect& rect, float depth, ColorB colour);
    void        DrawRectangleOutline(IOverlayContext* context, const Rect& rect, float depth = 0.f, ColorB colour = ColorB(0xff000000));

    Coord       DrawText(IOverlayContext* context, const Rect& rect, TextStyle* textStyle, ColorB colour, StringSection<> text);
    Coord       DrawText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, StringSection<> text);
    Coord       DrawText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, StringSection<> text);
    Coord       DrawFormatText(IOverlayContext* context, const Rect& rect, TextStyle* textStyle, ColorB colour, const char text[], ...);
    Coord       DrawFormatText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, const char text[], ...);
    Coord       DrawFormatText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, const char text[], ...);
    Coord       DrawFormatText(IOverlayContext* context, const Rect& rect, float depth, TextStyle* textStyle, ColorB colour, TextAlignment alignment, const char text[], va_list args);
    void        DrawHistoryGraph(IOverlayContext* context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory);
    void        DrawHistoryGraph_ExtraLine(IOverlayContext* context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float minValue, float maxValue);

    void        DrawTriangles(IOverlayContext* context, const Coord2 triangleCoordinates[], const ColorB triangleColours[], unsigned triangleCount);
    void        DrawLines(IOverlayContext* context, const Coord2 lineCoordinates[], const ColorB lineColours[], unsigned lineCount);

    Float3      AsPixelCoords(Coord2 input);
    Float3      AsPixelCoords(Coord2 input, float depth);
    Float3      AsPixelCoords(Float2 input);
    Float3      AsPixelCoords(Float3 input);
    std::tuple<Float3, Float3> AsPixelCoords(const Rect& rect);

    ///////////////////////////////////////////////////////////////////////////////////
    typedef std::tuple<Float3, Float3>      AABoundingBox;

    void        DrawBoundingBox(
        IOverlayContext* context, const AABoundingBox& box, 
        const Float3x4& localToWorld, 
        ColorB entryColour, unsigned partMask = 0x3);

    void        DrawFrustum(
        IOverlayContext* context, const Float4x4& worldToProjection, 
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
            Rect    UpArrow() const                 { return _upArrowRect; }
            Rect    DownArrow() const               { return _downArrowRect; }

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
            Rect    _upArrowRect;
            Rect    _downArrowRect;
            Flags::BitField _flags;

            Coord   ValueToPixels(float value) const;
        };

        bool                ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input);
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

    void DrawScrollBar(IOverlayContext* context, const ScrollBar::Coordinates& coordinates, float thumbPosition, ColorB fillColour = ColorB(0xffffffff), ColorB outlineColour = ColorB(0xff000000));

    void HTweakerBar_Draw(IOverlayContext* context, const ScrollBar::Coordinates& coordinates, float thumbPosition);
    void HTweakerBar_DrawLabel(IOverlayContext* context, const Rect& rect);
    void HTweakerBar_DrawGridBackground(IOverlayContext* context, const Rect& rect);

    ///////////////////////////////////////////////////////////////////////////////////
    //          T A B L E S
    struct TableElement
    {
        std::string _label; ColorB _bkColour;
        TableElement(const std::string& label, ColorB bkColour = ColorB(0xff000000)) : _label(label), _bkColour(bkColour) {}
        TableElement(const char label[],  ColorB bkColour = ColorB(0xff000000)) : _label(label), _bkColour(bkColour) {}
        TableElement() : _bkColour(0xff000000) {}
    };
    void DrawTableHeaders(IOverlayContext* context, const Rect& rect, const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, ColorB bkColor, Interactables* interactables=NULL);
    void DrawTableEntry(IOverlayContext* context, const Rect& rect, const IteratorRange<std::pair<std::string, unsigned>*>& fieldHeaders, const std::map<std::string, TableElement>& entry);

    ///////////////////////////////////////////////////////////////////////////////////
    class DebugScreensSystem : public PlatformRig::IInputListener
    {
    public:
        bool        OnInputEvent(const PlatformRig::InputContext& context, const PlatformRig::InputSnapshot& evnt);
        void        Render(IOverlayContext& overlayContext, const Rect& viewport);
        bool        IsAnythingVisible();
        
        enum Type { InPanel, SystemDisplay };
        void        Register(std::shared_ptr<IWidget> widget, const char name[], Type type = InPanel);
        void        Unregister(const char name[]);
        void        Unregister(IWidget& widget);

        void        SwitchToScreen(unsigned panelIndex, const char name[]);
        bool        SwitchToScreen(unsigned panelIndex, uint64 hashCode);
        void        SwitchToScreen(const char name[]);
        const char* CurrentScreen(unsigned panelIndex);
        
        struct WidgetAndName 
        {
            std::shared_ptr<IWidget>    _widget;
            std::string                 _name;
            uint64                      _hashCode;
        };
        const std::vector<WidgetAndName>& GetWidgets() const { return _widgets; }
        
        using WidgetChangeCallback = std::function<void()>;
        unsigned AddWidgetChangeCallback(WidgetChangeCallback&& callback);
        void RemoveWidgetChangeCallback(unsigned callbackid);

        bool    ConsumedInputEvent()       { return _consumedInputEvent; }
        void    ResetConsumedInputEvent()  { _consumedInputEvent = false; }

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
        bool        _consumedInputEvent;

        void    RenderPanelControls(        IOverlayContext*    context,
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

}}

