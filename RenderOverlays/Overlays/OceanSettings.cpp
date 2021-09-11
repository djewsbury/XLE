// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OceanSettings.h"
#include "../../SceneEngine/DeepOceanSim.h"
#include "../../SceneEngine/Ocean.h"

namespace Overlays
{
    ////////////////////////////////////////////////////////////////////////////////////

    void    OceanSettingsDisplay::Render(   IOverlayContext& context, Layout& layout, 
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

        ScrollBarObj objects[dimof(OceanSettingsDisplay::_scrollers)] = 
        {
            { &_oceanSettings->_windAngle[0],           "WindAngle0",           0.f,   2.f*gPI },
            { &_oceanSettings->_windVelocity[0],        "WindVelocity0",        0.f, 200.f },
            { &_oceanSettings->_scaleAgainstWind[0],    "ScaleAgainstWind0",    0.f,   1.f },
            { &_oceanSettings->_suppressionFactor[0],   "SuppressionFactor0",   0.f,   3.f },

            { &_oceanSettings->_windAngle[1],           "WindAngle1",           0.f,   2.f*gPI },
            { &_oceanSettings->_windVelocity[1],        "WindVelocity1",        0.f, 200.f },
            { &_oceanSettings->_scaleAgainstWind[1],    "ScaleAgainstWind1",    0.f,   1.f },
            { &_oceanSettings->_suppressionFactor[1],   "SuppressionFactor1",   0.f,   3.f },

            { &_oceanSettings->_spectrumFade,           "SpectrumFade",         0.f,   1.f },

            { &_oceanSettings->_physicalDimensions,     "PhysicalDimensions",   100.f, 2000.f },
            { &_oceanSettings->_strengthConstantXY,     "StrengthConstantXY",     0.01f,  2.f },
            { &_oceanSettings->_strengthConstantZ,      "StrengthConstantZ",      0.01f,  2.f },
            { &_oceanSettings->_detailNormalsStrength,  "DetailNormalsStrength",  0.f,    1.f },

            { &_oceanSettings->_gridShiftSpeed,         "GridShiftSpeed",        -.25f,    .25f }
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

    bool    OceanSettingsDisplay::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
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

        if (interfaceState.TopMostId()) {
            if (input.IsRelease_LButton()) {
                /*InteractableId topMostWidget = interfaceState.TopMostId();
                for (unsigned c=0; c<dimof(GraphTabs::Names); ++c) {
                    if (topMostWidget == InteractableId_Make(GraphTabs::Names[c])) {
                        _graphsMode = c;
                        return true;
                    }
                }

                if (topMostWidget == InteractableId_Make("ShowUploadHistory")) {
                    _drawHistory = !_drawHistory;
                    return true;
                }

                const InteractableId framePicker = InteractableId_Make("FramePicker");
                if (topMostWidget >= framePicker && topMostWidget < (framePicker+s_MaxGraphSegments)) {
                    unsigned graphIndex = unsigned(topMostWidget - framePicker);
                    _lockedFrameId = _frames[std::max(0,signed(_frames.size())-signed(graphIndex)-1)]._frameId;
                    return true;
                }*/

                return false;
            }
        }
        return false;
    }

    OceanSettingsDisplay::OceanSettingsDisplay(SceneEngine::DeepOceanSimSettings& oceanSettings)
    : _oceanSettings(&oceanSettings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    OceanSettingsDisplay::~OceanSettingsDisplay() {}


    ////////////////////////////////////////////////////////////////////////////////////

    void    OceanLightingSettingsDisplay::Render(   IOverlayContext& context, Layout& layout, 
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

        ScrollBarObj objects[dimof(OceanLightingSettingsDisplay::_scrollers)] = 
        {
            { &_oceanSettings->_opticalThickness[0],                "OpticalThicknessR",        0.f,    .05f },
            { &_oceanSettings->_opticalThickness[1],                "OpticalThicknessG",        0.f,    .05f },
            { &_oceanSettings->_opticalThickness[2],                "OpticalThicknessB",        0.f,    .05f },
            { &_oceanSettings->_foamBrightness,                     "FoamBrightness",           0.f,   5.f },
            { &_oceanSettings->_skyReflectionBrightness,            "ReflectionBrightness",     0.f,   5.f },
            { &_oceanSettings->_upwellingScale,                     "UpwellingScale",           0.f,   1.f },
            { &_oceanSettings->_refractiveIndex,                    "RefractiveIndex",          1.f,   2.f },
            { &_oceanSettings->_reflectionBumpScale,                "ReflectionBumpScale",      0.f,   1.f },
            { &_oceanSettings->_detailNormalFrequency,              "DetailNormalFrequency",    0.f,  10.f },
            { &_oceanSettings->_specularityFrequency,               "SpecularityFrequency",     0.f,  10.f }
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

    bool    OceanLightingSettingsDisplay::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
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

    OceanLightingSettingsDisplay::OceanLightingSettingsDisplay(SceneEngine::OceanLightingSettings& oceanSettings)
    : _oceanSettings(&oceanSettings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    OceanLightingSettingsDisplay::~OceanLightingSettingsDisplay() {}

}


