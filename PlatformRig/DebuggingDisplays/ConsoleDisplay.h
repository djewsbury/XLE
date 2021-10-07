// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../Utility/UTFUtils.h"

namespace ConsoleRig { class Console; }

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    class ConsoleDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        ConsoleDisplay(ConsoleRig::Console& console);
        ~ConsoleDisplay();
    private:
        RenderOverlays::CommonWidgets::TextEntry<> _textEntry;
        unsigned                        _renderCounter;
        ConsoleRig::Console*            _console;

        unsigned                        _scrollBack;
        unsigned                        _scrollBackFractional;
    };
}}

