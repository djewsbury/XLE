// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TestMaterialSettings.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/SceneEngineUtils.h"

namespace Overlays
{
    ////////////////////////////////////////////////////////////////////////////////////

    void    TestMaterialSettings::Render(   IOverlayContext& context, Layout& layout, 
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

        ScrollBarObj objects[dimof(TestMaterialSettings::_scrollers)] = 
        {
            { &_materialSettings->_metallic,            "Metallic",         0.f,   1.f },
            { &_materialSettings->_roughness,           "Roughness",        0.f,   1.f },
            { &_materialSettings->_specular,            "Specular",         0.f,   1.f },
            { &_materialSettings->_specular2,           "Specular2",        0.f,   1.f },
            { &_materialSettings->_material,            "Material",         0.f,   1.f },
            { &_materialSettings->_material2,           "Material2",        0.f,   1.f },

            { &_materialSettings->_diffuseScale,        "DiffuseScale",     0.f,   4.f },
            { &_materialSettings->_reflectionsScale,    "ReflectionsScale", 0.f,   4.f },
            { &_materialSettings->_reflectionsBoost,    "ReflectionsBoost", 0.f,   1.f },
            { &_materialSettings->_specular0Scale,      "Specular0Scale",   0.f,   4.f },
            { &_materialSettings->_specular1Scale,      "Specular1Scale",   0.f,   4.f }
        };

        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned q=0; q<dimof(objects); ++q) {
            Rect windAngle0 = layout.AllocateFullWidth(32);

            Rect gridBackgroundRect = windAngle0;
            gridBackgroundRect._topLeft[0] += 128; gridBackgroundRect._bottomRight[0] -= 4;
            gridBackgroundRect._topLeft[1] += 4; gridBackgroundRect._bottomRight[1] -= 4;
            HTweakerBar_DrawGridBackground(context, gridBackgroundRect);
            
            Rect labelRect = windAngle0;
            labelRect._bottomRight[0] = labelRect._topLeft[0] + 256;
            HTweakerBar_DrawLabel(context, labelRect);
            
            Rect textRect = windAngle0;
            textRect._topLeft[0] += 32;
            /*Coord a = */DrawFormatText(context, textRect, nullptr, ColorB(0xffffffff), objects[q]._name);

            Rect scrollBar = windAngle0;
            scrollBar._topLeft[0] = labelRect._bottomRight[0];
            scrollBar._topLeft[0] += 16;
            scrollBar._bottomRight[0] -= 16;

            ScrollBar::Coordinates scrollCoordinates(scrollBar, objects[q]._min, objects[q]._max, (objects[q]._max - objects[q]._min)/40.f,
                ScrollBar::Coordinates::Flags::NoUpDown|ScrollBar::Coordinates::Flags::Horizontal);
            *objects[q]._member = _scrollers[q].CalculateCurrentOffset(scrollCoordinates, *objects[q]._member);
            HTweakerBar_Draw(context, scrollCoordinates, *objects[q]._member);
            interactables.Register(
                Interactables::Widget(scrollCoordinates.InteractableRect(), scrollBarId+q));

            DrawFormatText(context, scrollBar, 0.f, nullptr, ColorB(0xffffffff), TextAlignment::Right, "%.3f", *objects[q]._member);
        }
    }

    auto    TestMaterialSettings::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) -> ProcessInputResult
    {
            // allow the scroller we're currently dragging to go first...
        for (unsigned c=0; c<dimof(_scrollers); ++c)
            if (_scrollers[c].IsDragging()) {
                if (_scrollers[c].ProcessInput(interfaceState, input))
                    return ProcessInputResult::Consumed;
                break;
            }

        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            if (_scrollers[c].ProcessInput(interfaceState, input)) {
                return ProcessInputResult::Consumed;
            }
        }
        return ProcessInputResult::Passthrough;
    }

    TestMaterialSettings::TestMaterialSettings(SceneEngine::MaterialOverride& materialSettings)
    : _materialSettings(&materialSettings)
    {
        const auto scrollBarId = InteractableId_Make("ScrollBar");
        for (unsigned c=0; c<dimof(_scrollers); ++c) {
            _scrollers[c] = ScrollBar(scrollBarId+c, ScrollBar::Coordinates::Flags::Horizontal);
        }
    }

    TestMaterialSettings::~TestMaterialSettings() {}



}


