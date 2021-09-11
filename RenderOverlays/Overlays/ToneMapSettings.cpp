// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ToneMapSettings.h"
#include "../../SceneEngine/Tonemap.h"

namespace Overlays
{

    ////////////////////////////////////////////////////////////////////////////////////

    void    ToneMapSettingsDisplay::Render( IOverlayContext& context, Layout& layout, 
                                            Interactables& interactables, InterfaceState& interfaceState)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        layout.AllocateFullWidth(64);

        class ScrollBarObj
        {
        public:
            float *_member;
            const char* _name;
            float _min, _max;
        };

        ScrollBarObj objects[dimof(ToneMapSettingsDisplay::_scrollers)] = 
        {
            { &_settings->_bloomColor[0],           "BloomScaleR",              0.f,   1.f },
            { &_settings->_bloomColor[1],           "BloomScaleG",              0.f,   1.f },
            { &_settings->_bloomColor[2],           "BloomScaleB",              0.f,   1.f },
            { &_settings->_bloomBrightness,         "BloomBrightness",          0.f,  20.f },
            { &_settings->_bloomThreshold,          "BloomThreshold",           0.5f, 20.f },
            { &_settings->_bloomRampingFactor,      "BloomRampingFactor",       0.f,   1.f },
            { &_settings->_bloomDesaturationFactor, "BloomDesaturationFactor",  0.f,   1.f },
            { &_settings->_sceneKey,                "SceneKey",                 0.f,   1.f },
            { &_settings->_luminanceMin,            "LuminanceMin",             0.f,   1.f },
            { &_settings->_luminanceMax,            "LuminanceMax",             0.f,   1.f },
            { &_settings->_whitepoint,              "Whitepoint",               0.f,  20.f }
        };

        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned q=0; q<dimof(objects); ++q) {
            Rect windAngle0 = layout.AllocateFullWidth(32);

            Rect gridBackgroundRect = windAngle0;
            gridBackgroundRect._topLeft[0] += 128; gridBackgroundRect._bottomRight[0] -= 4;
            gridBackgroundRect._topLeft[1] += 4; gridBackgroundRect._bottomRight[1] -= 4;
            HTweakerBar_DrawGridBackground(&context, gridBackgroundRect);
            
            Rect labelRect = windAngle0;
            labelRect._bottomRight[0] = labelRect._topLeft[0] + 256;
            HTweakerBar_DrawLabel(&context, labelRect);
            
            Rect textRect = windAngle0;
            textRect._topLeft[0] += 32;
            /*Coord a = */DrawText(&context, textRect, 0.f, nullptr, ColorB(0xffffffff), TextAlignment::Left, objects[q]._name);

            Rect scrollBar = windAngle0;
            scrollBar._topLeft[0] = labelRect._bottomRight[0];
            scrollBar._topLeft[0] += 16;
            scrollBar._bottomRight[0] -= 16;

            ScrollBar::Coordinates scrollCoordinates(scrollBar, objects[q]._min, objects[q]._max, (objects[q]._max - objects[q]._min)/40.f,
                ScrollBar::Coordinates::Flags::NoUpDown|ScrollBar::Coordinates::Flags::Horizontal);
            *objects[q]._member = _scrollers[q].CalculateCurrentOffset(scrollCoordinates, *objects[q]._member);
            HTweakerBar_Draw(&context, scrollCoordinates, *objects[q]._member);
            interactables.Register(
                Interactables::Widget(scrollCoordinates.InteractableRect(), scrollBarId+q));

            DrawFormatText(&context, scrollBar, 0.f, nullptr, ColorB(0xffffffff), TextAlignment::Right, "%.3f", *objects[q]._member);
        }
    }

    bool    ToneMapSettingsDisplay::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
    {
        for (unsigned c=0; c<dimof(_scrollers); ++c)
            if (_scrollers[c].IsDragging()) {
                if (_scrollers[c].ProcessInput(interfaceState, inputContext, input))
                    return true;
                break;
            }

        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            if (_scrollers[c].ProcessInput(interfaceState, inputContext, input)) {
                return true;
            }
        }
        return false;
    }

    ToneMapSettingsDisplay::ToneMapSettingsDisplay(SceneEngine::ToneMapSettings& settings)
    : _settings(&settings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    ToneMapSettingsDisplay::~ToneMapSettingsDisplay() {}


    ////////////////////////////////////////////////////////////////////////////////////

    void    ColorGradingSettingsDisplay::Render(    IOverlayContext& context, Layout& layout, 
                                                    Interactables& interactables, InterfaceState& interfaceState)
    {
        using namespace RenderOverlays::DebuggingDisplay;
        layout.AllocateFullWidth(64);

        class ScrollBarObj
        {
        public:
            float *_member;
            const char* _name;
            float _min, _max;
        };

        ScrollBarObj objects[dimof(ColorGradingSettingsDisplay::_scrollers)] = 
        {
            { &_settings->_sharpenAmount,           "SharpenAmount",        0.f,   5.f },
            { &_settings->_minInput,                "LevelsMinInput",       0.f,   255.f },
            { &_settings->_gammaInput,              "LevelsGammaInput",     0.f,   3.f },
            { &_settings->_maxInput,                "LevelsMaxInput",       0.f,   255.f },
            { &_settings->_minOutput,               "LevelsMinOutput",      0.f,   255.f },
            { &_settings->_maxOutput,               "LevelsMaxOutput",      0.f,   255.f },
            { &_settings->_brightness,              "Brightness",           0.f,   2.f },
            { &_settings->_contrast,                "Contrast",             0.f,   2.f },
            { &_settings->_saturation,              "Saturation",           0.f,   2.f },
            { &_settings->_filterColor[0],          "FilterColorR",         0.f,   1.f },
            { &_settings->_filterColor[1],          "FilterColorG",         0.f,   1.f },
            { &_settings->_filterColor[2],          "FilterColorB",         0.f,   1.f },
            { &_settings->_filterColorDensity,      "FilterColorDensity",   0.f,   1.f },
            { &_settings->_grain,                   "Grain",                0.f,   1.f }
        };

        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned q=0; q<dimof(objects); ++q) {
            Rect windAngle0 = layout.AllocateFullWidth(32);

            Rect gridBackgroundRect = windAngle0;
            gridBackgroundRect._topLeft[0] += 128; gridBackgroundRect._bottomRight[0] -= 4;
            gridBackgroundRect._topLeft[1] += 4; gridBackgroundRect._bottomRight[1] -= 4;
            HTweakerBar_DrawGridBackground(&context, gridBackgroundRect);
            
            Rect labelRect = windAngle0;
            labelRect._bottomRight[0] = labelRect._topLeft[0] + 256;
            HTweakerBar_DrawLabel(&context, labelRect);
            
            Rect textRect = windAngle0;
            textRect._topLeft[0] += 32;
            /*Coord a = */DrawFormatText(&context, textRect, nullptr, ColorB(0xffffffff), objects[q]._name);

            Rect scrollBar = windAngle0;
            scrollBar._topLeft[0] = labelRect._bottomRight[0];
            scrollBar._topLeft[0] += 16;
            scrollBar._bottomRight[0] -= 16;

            ScrollBar::Coordinates scrollCoordinates(scrollBar, objects[q]._min, objects[q]._max, (objects[q]._max - objects[q]._min)/40.f,
                ScrollBar::Coordinates::Flags::NoUpDown|ScrollBar::Coordinates::Flags::Horizontal);
            *objects[q]._member = _scrollers[q].CalculateCurrentOffset(scrollCoordinates, *objects[q]._member);
            HTweakerBar_Draw(&context, scrollCoordinates, *objects[q]._member);
            interactables.Register(
                Interactables::Widget(scrollCoordinates.InteractableRect(), scrollBarId+q));

            DrawFormatText(&context, scrollBar, 0.f, nullptr, ColorB(0xffffffff), TextAlignment::Right, "%.3f", *objects[q]._member);
        }
    }

    bool    ColorGradingSettingsDisplay::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
    {
        for (unsigned c=0; c<dimof(_scrollers); ++c)
            if (_scrollers[c].IsDragging()) {
                if (_scrollers[c].ProcessInput(interfaceState, inputContext, input))
                    return true;
                break;
            }

        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            if (_scrollers[c].ProcessInput(interfaceState, inputContext, input)) {
                return true;
            }
        }

        return false;
    }

    ColorGradingSettingsDisplay::ColorGradingSettingsDisplay(SceneEngine::ColorGradingSettings& settings)
    : _settings(&settings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    ColorGradingSettingsDisplay::~ColorGradingSettingsDisplay() {}

}


