// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IOverlayContext.h"
#include "OverlayPrimitives.h"
#include "../PlatformRig/InputContext.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"     // for StringSection
#include <functional>
#include <vector>
#include <map>
#include <any>

namespace RenderOverlays { struct ImmediateLayout; }

namespace RenderOverlays { namespace DebuggingDisplay
{

    typedef uint64_t InteractableId;
    InteractableId InteractableId_Make(StringSection<char> name);
    typedef uint32_t KeyId;

    class InterfaceState;

    ///////////////////////////////////////////////////////////////////////////////////
    class Interactables
    {
    public:
        struct HotAreaLight { Rect _rect; InteractableId _id = 0; };
        struct HotArea { Rect _rect; InteractableId _id = 0; std::any _tag; };
        std::vector<HotArea> _hotAreas;
        std::vector<HotAreaLight> _hotAreaLights;

        void Register(Rect rect, InteractableId id);
        void Register(Rect rect, InteractableId id, std::any&& tag);
        auto Intersect(const Coord2& position) const -> std::vector<HotArea>;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    class InterfaceState
    {
    public:
        bool            HasMouseOver(InteractableId id);
        InteractableId  TopMostId() const;
        auto            TopMostHotArea() const -> const Interactables::HotArea&;
        bool            IsMouseButtonHeld(unsigned buttonIndex = 0) const   { return !!(_mouseButtonsHeld&(1<<buttonIndex)); }
        Coord2          MousePosition() const                               { return _mousePosition; }

        void BeginCapturing(const Interactables::HotArea& widget);
        void EndCapturing();

        struct Capture
        {
            Coord2 _driftDuringCapture = Coord2{0,0};
            Interactables::HotArea _hotArea;
        };
        const Capture& GetCapture() const           { return _capture;  }
        Capture& GetCapture()                       { return _capture;  }

        IteratorRange<const Interactables::HotArea*> GetMouseOverStack() const  { return _mouseOverStack; }
        const PlatformRig::WindowingSystemView& GetWindowingSystemView() const  { return _viewInputContext; }
        void SetWindowingSystemView(const PlatformRig::WindowingSystemView& view) { _viewInputContext = view; }

        InterfaceState();
        InterfaceState( const PlatformRig::InputContext& viewInputContext,
                        const Coord2& mousePosition, unsigned mouseButtonsHeld,
                        const std::vector<Interactables::HotArea>& mouseStack,
                        const Capture& capture);
    protected:
        std::vector<Interactables::HotArea> _mouseOverStack;
        Capture     _capture;
        Coord2      _mousePosition;
        unsigned    _mouseButtonsHeld;
        PlatformRig::WindowingSystemView _viewInputContext;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    class IWidget
    {
    public:
        using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::ImmediateLayout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = OSServices::InputSnapshot;
        using ProcessInputResult = PlatformRig::ProcessInputResult;

        virtual void                    Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState);
        virtual ProcessInputResult      ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input);
        virtual                         ~IWidget();
    };

    ///////////////////////////////////////////////////////////////////////////////////
    extern const ColorB   RandomPaletteColorTable[];
    extern const size_t   RandomPaletteColorTable_Size;


    void        DrawHistoryGraph(IOverlayContext& context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory);
    void        DrawHistoryGraph_ExtraLine(IOverlayContext& context, const Rect& rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float minValue, float maxValue);

    void        DrawBarGraph(IOverlayContext& context, const Rect & rect, float values[], unsigned valuesCount, unsigned maxValuesCount, float& minValueHistory, float& maxValueHistory);

    void        FillTriangles(IOverlayContext& context, const Float2 triangleCoordinates[], const ColorB triangleColours[], unsigned triangleCount);
    void        FillTriangles(IOverlayContext& context, const Float2 triangleCoordinates[], ColorB colour, unsigned triangleCount);
    void        DrawLines(IOverlayContext& context, const Coord2 lineCoordinates[], const ColorB lineColours[], unsigned lineCount);

    ///////////////////////////////////////////////////////////////////////////////////

    template<typename Type>
        struct GraphSeries
    {
        Type _minValue, _maxValue;
        IteratorRange<const Type*> _values;
        unsigned _peakIndex = ~0u;

        GraphSeries(
            IteratorRange<const Type*> values,
            std::optional<Type>& historicalMin, std::optional<Type>& historicalMax)
        : _values(values)
        {
            _maxValue = -std::numeric_limits<float>::max();
            _minValue = std::numeric_limits<float>::max();
            for (unsigned c=0; c<values.size(); ++c) {
                _minValue = std::min(values[c], _minValue);
                if (values[c] > _maxValue) { 
                    _peakIndex = c; 
                    _maxValue = values[c];
                }
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

        IWidget::ProcessInputResult                ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input);
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
    Coord DrawTableHeaders(IOverlayContext& context, const Rect& rect, IteratorRange<std::pair<std::string, unsigned>*> fieldHeaders);
    Coord DrawTableBase(IOverlayContext& context, const Rect& rect);
    Rect DrawEmbeddedInRightEdge(IOverlayContext& context, const Rect& rect);
    Coord DrawTableEntry(IOverlayContext& context, const Rect& rect, IteratorRange<const std::pair<std::string, unsigned>*> fieldHeaders, const std::map<std::string, TableElement>& entry, bool highlighted = false);

    void DrawTableHeaders(IOverlayContext& context, ImmediateLayout& layout, IteratorRange<std::pair<std::string, unsigned>*> fieldHeaders);
    void DrawTableBase(IOverlayContext& context, ImmediateLayout& layout);
    bool DrawTableEntry(IOverlayContext& context, ImmediateLayout& layout, IteratorRange<const std::pair<std::string, unsigned>*> fieldHeaders, const std::map<std::string, TableElement>& entry, bool highlighted = false);

    ///////////////////////////////////////////////////////////////////////////////////
    struct InterfaceStateHelper
    {
        Interactables   _currentInteractables;
        InterfaceState  _currentInterfaceState;

        Coord2      _currentMouse = {0,0};
        unsigned    _currentMouseHeld = 0;

        void PreRender();
        void PostRender();
        void OnInputEvent(const PlatformRig::InputContext& context, const OSServices::InputSnapshot& evnt);
    };

    class DebugScreensSystem : public PlatformRig::IInputListener
    {
    public:
        PlatformRig::ProcessInputResult        OnInputEvent(const PlatformRig::InputContext& context, const OSServices::InputSnapshot& evnt);
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
        InterfaceStateHelper _interfaceStateHelper;
        
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

        void    RenderPanelControls(        IOverlayContext&    context,
                                            unsigned            panelIndex, const std::string& name, ImmediateLayout&layout, bool allowDestroy,
                                            Interactables&      interactables, InterfaceState& interfaceState);
        bool    ProcessInputPanelControls(  InterfaceState&     interfaceState, const PlatformRig::InputContext& inputContext, const OSServices::InputSnapshot&    evnt);
    };

}}

