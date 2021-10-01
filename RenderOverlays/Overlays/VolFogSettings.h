// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"

namespace SceneEngine { class VolumetricFogMaterial; }

namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class VolumetricFogSettings : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        VolumetricFogSettings(SceneEngine::VolumetricFogMaterial& materialSettings);
        ~VolumetricFogSettings();
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

    private:
        ScrollBar   _scrollers[15];
        SceneEngine::VolumetricFogMaterial* _materialSettings;
    };


}

