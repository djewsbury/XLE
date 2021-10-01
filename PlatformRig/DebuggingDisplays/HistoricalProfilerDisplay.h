#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"

namespace Utility { class IHierarchicalProfiler; }

namespace PlatformRig { namespace Overlays
{
    class HistoricalProfilerDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
    {
    public:
        typedef RenderOverlays::IOverlayContext IOverlayContext;
        typedef RenderOverlays::DebuggingDisplay::Layout Layout;
        typedef RenderOverlays::DebuggingDisplay::Interactables Interactables;
        typedef RenderOverlays::DebuggingDisplay::InterfaceState InterfaceState;
        typedef PlatformRig::InputSnapshot InputSnapshot;

        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        void    TrackLabel(const char label[]);
        void    UntrackLabel(const char label[]);

        HistoricalProfilerDisplay(IHierarchicalProfiler* profiler);
        ~HistoricalProfilerDisplay();
    private:
        IHierarchicalProfiler* _profiler;
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };
}}



