// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"

namespace SceneEngine { class ToneMapSettings; class ColorGradingSettings; }

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class ToneMapSettingsDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        ToneMapSettingsDisplay(SceneEngine::ToneMapSettings& settings);
        ~ToneMapSettingsDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[11];
        SceneEngine::ToneMapSettings* _settings;
    };

    class ColorGradingSettingsDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        ColorGradingSettingsDisplay(SceneEngine::ColorGradingSettings& settings);
        ~ColorGradingSettingsDisplay();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[14];
        SceneEngine::ColorGradingSettings* _settings;
    };

}

