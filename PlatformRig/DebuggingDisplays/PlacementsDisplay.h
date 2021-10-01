// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class PlacementsDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);

        PlacementsDisplay();
        ~PlacementsDisplay();
    };
}}
