// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"

namespace SceneEngine { class OceanLightingSettings; class DeepOceanSimSettings; }

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class OceanSettingsDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        OceanSettingsDisplay(SceneEngine::DeepOceanSimSettings& oceanSettings);
        ~OceanSettingsDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[14];
        SceneEngine::DeepOceanSimSettings* _oceanSettings;
    };

    class OceanLightingSettingsDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        OceanLightingSettingsDisplay(SceneEngine::OceanLightingSettings& oceanSettings);
        ~OceanLightingSettingsDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[10];
        SceneEngine::OceanLightingSettings* _oceanSettings;
    };

}

