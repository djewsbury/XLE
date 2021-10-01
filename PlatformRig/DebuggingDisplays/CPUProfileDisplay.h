// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"

namespace Utility { class IHierarchicalProfiler; }

namespace PlatformRig { namespace Overlays
{
    class HierarchicalProfilerDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
    {
    public:
        typedef RenderOverlays::IOverlayContext IOverlayContext;
        typedef RenderOverlays::DebuggingDisplay::Layout Layout;
        typedef RenderOverlays::DebuggingDisplay::Interactables Interactables;
        typedef RenderOverlays::DebuggingDisplay::InterfaceState InterfaceState;
        
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        HierarchicalProfilerDisplay(IHierarchicalProfiler* profiler);
        ~HierarchicalProfilerDisplay();
    private:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}

