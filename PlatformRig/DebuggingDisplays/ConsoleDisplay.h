// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Utility/UTFUtils.h"

namespace ConsoleRig { class Console; }

namespace PlatformRig { namespace Overlays
{
    using namespace RenderOverlays;
    using namespace RenderOverlays::DebuggingDisplay;

    template<typename CharType=char>
        class TextEntry
    {
    public:
        std::basic_string<CharType>     _currentLine;
        size_t                          _caret = 0;
        size_t                          _selectionStart = 0, _selectionEnd = 0;

        std::vector<std::basic_string<CharType>>    _history;
        unsigned								    _historyCursor = 0;

        std::vector<std::basic_string<CharType>>    _autoComplete;
        unsigned                                    _autoCompleteCursor = 0;

        ProcessInputResult    ProcessInput(
            InterfaceState& interfaceState, const InputSnapshot& input,
            const std::function<std::vector<std::basic_string<CharType>>(const std::basic_string<CharType>&)>& autocompleteFn = nullptr);

        void Reset(const std::basic_string<CharType>& currentLine);
    };

    void    Render(IOverlayContext& context, const Rect& entryBoxArea, const std::shared_ptr<Font>& font, const TextEntry<>& textEntry);

    class ConsoleDisplay : public IWidget ///////////////////////////////////////////////////////////
    {
    public:
        void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

        ConsoleDisplay(ConsoleRig::Console& console);
        ~ConsoleDisplay();
    private:
        TextEntry<>                     _textEntry;
        unsigned                        _renderCounter;
        ConsoleRig::Console*            _console;

        unsigned                        _scrollBack;
        unsigned                        _scrollBackFractional;
    };
}}

