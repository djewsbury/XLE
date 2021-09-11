// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VolFogSettings.h"
#include "../../SceneEngine/VolumetricFog.h"

namespace Overlays
{

    ////////////////////////////////////////////////////////////////////////////////////

    void    VolumetricFogSettings::Render(  IOverlayContext& context, Layout& layout, 
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

        ScrollBarObj objects[dimof(VolumetricFogSettings::_scrollers)] = 
        {
            { &_materialSettings->_opticalThickness,             "Density",              0.f,   1.f },
            { &_materialSettings->_noiseThicknessScale,   "NoiseDensityScale",    0.f,   1.f },
            { &_materialSettings->_noiseSpeed,          "NoiseSpeed",           0.001f,   10.f },
            // { &_materialSettings->_heightStart,         "HeightStart",          0.f,   300.f },
            // { &_materialSettings->_heightEnd,           "HeightEnd",            0.f,   300.f },

            { &_materialSettings->_sunInscatter[0],    "ForwardR",             0.f,   1.f },
            { &_materialSettings->_sunInscatter[1],    "ForwardG",             0.f,   1.f },
            { &_materialSettings->_sunInscatter[2],    "ForwardB",             0.f,   1.f },
            // { &_materialSettings->_forwardBrightness,   "ForwardBrightness",    0.f,   50.f },

            { &_materialSettings->_ambientInscatter[0],       "BackR",                0.f,   1.f },
            { &_materialSettings->_ambientInscatter[1],       "BackG",                0.f,   1.f },
            { &_materialSettings->_ambientInscatter[2],       "BackB",                0.f,   1.f },
            // { &_materialSettings->_backBrightness,      "BackBrightness",       0.f,   50.f },

            { &_materialSettings->_ESM_C,               "ESM_C",                0.f,   150.f },
            { &_materialSettings->_jitteringAmount,     "JitteringAmount",      0.f,   1.f }
        };

        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned q=0; q<dimof(objects); ++q) {
            static Coord2 scrollBarSize(600, 24);
            Rect windAngle0 = layout.Allocate(scrollBarSize);

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

            ScrollBar::Coordinates scrollCoordinates(
                scrollBar, objects[q]._min, objects[q]._max, (objects[q]._max - objects[q]._min)/40.f,
                ScrollBar::Coordinates::Flags::NoUpDown|ScrollBar::Coordinates::Flags::Horizontal);
            *objects[q]._member = _scrollers[q].CalculateCurrentOffset(scrollCoordinates, *objects[q]._member);
            HTweakerBar_Draw(&context, scrollCoordinates, *objects[q]._member);
            interactables.Register(
                Interactables::Widget(scrollCoordinates.InteractableRect(), scrollBarId+q));

            DrawFormatText(&context, scrollBar, 0.f, nullptr, ColorB(0xffffffff), TextAlignment::Right, "%.3f", *objects[q]._member);
        }
    }

    bool    VolumetricFogSettings::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input)
    {
            // allow the scroller we're currently dragging to go first...
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

    VolumetricFogSettings::VolumetricFogSettings(SceneEngine::VolumetricFogMaterial& materialSettings)
    : _materialSettings(&materialSettings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    VolumetricFogSettings::~VolumetricFogSettings() {}



}


