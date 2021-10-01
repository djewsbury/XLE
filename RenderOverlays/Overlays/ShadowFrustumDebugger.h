// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DebuggingDisplay.h"

namespace SceneEngine { class ILightingStateDelegate; }

namespace Overlays
{
    class ShadowFrustumDebugger : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        typedef RenderOverlays::DebuggingDisplay::Layout Layout;
        typedef RenderOverlays::DebuggingDisplay::Interactables Interactables;
        typedef RenderOverlays::DebuggingDisplay::InterfaceState InterfaceState;

        void    Render( RenderOverlays::IOverlayContext& context, Layout& layout, 
                        Interactables& interactables, InterfaceState& interfaceState);

        ShadowFrustumDebugger(std::shared_ptr<SceneEngine::ILightingStateDelegate> scene);
        ~ShadowFrustumDebugger();
    protected:
        std::shared_ptr<SceneEngine::ILightingStateDelegate> _scene;
    };
}

