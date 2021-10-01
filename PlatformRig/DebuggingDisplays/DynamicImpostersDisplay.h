// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>

namespace SceneEngine { class DynamicImposters; }

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class DynamicImpostersDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);

        DynamicImpostersDisplay(std::weak_ptr<SceneEngine::DynamicImposters> manager);
        ~DynamicImpostersDisplay();
    protected:
        std::weak_ptr<SceneEngine::DynamicImposters> _manager;
    };
}}
