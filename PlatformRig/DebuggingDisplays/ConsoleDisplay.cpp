// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ConsoleDisplay.h"
#include "../../RenderOverlays/Font.h"
#include "../../Assets/Assets.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/ResourceBox.h"
#include <assert.h>

namespace PlatformRig { namespace Overlays
{
    class ConsoleDisplayResources
    {
    public:
        std::shared_ptr<RenderOverlays::Font> _font;

        ConsoleDisplayResources(std::shared_ptr<RenderOverlays::Font> font)
        : _font(std::move(font))
        {}

        static void ConstructToPromise(std::promise<std::shared_ptr<ConsoleDisplayResources>>&& promise)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("OrbitronBlack", 20)).ThenConstructToPromise(std::move(promise));
        }
    };

            //////   C O N S O L E   D I S P L A Y   //////

    void    ConsoleDisplay::Render( IOverlayContext& context,       Layout& layout, 
                                    Interactables&interactables,    InterfaceState& interfaceState)
    {
        Rect consoleMaxSize                 = layout.GetMaximumSize();
        const unsigned height               = std::min(consoleMaxSize.Height() / 2, 512);
        consoleMaxSize._bottomRight[1]       = consoleMaxSize._topLeft[1] + height;

		auto* res = ConsoleRig::TryActualizeCachedBox<ConsoleDisplayResources>();
        if (!res) return;

        const float         textHeight      = res->_font->GetFontProperties()._lineHeight;
        const Coord         entryBoxHeight  = Coord(textHeight) + 2 * layout._paddingBetweenAllocations;

        const Rect          historyArea     = layout.AllocateFullWidth(consoleMaxSize.Height() - 2 * layout._paddingInternalBorder - layout._paddingBetweenAllocations - entryBoxHeight);
        const Rect          entryBoxArea    = layout.AllocateFullWidth(entryBoxHeight);

        Layout              historyAreaLayout(historyArea);
        historyAreaLayout._paddingInternalBorder = 0;
        unsigned            linesToRender   = (unsigned)XlFloor((historyArea.Height() - 2*historyAreaLayout._paddingInternalBorder) / (textHeight + historyAreaLayout._paddingBetweenAllocations));

        static ColorB       backColor       = ColorB(0x20, 0x20, 0x20, 0x90);
        static ColorB       borderColor     = ColorB(0xff, 0xff, 0xff, 0x7f);
        static ColorB       entryBoxColor   = ColorB(0x00, 0x00, 0x00, 0x4f);
        static ColorB       textColor       = ColorB(0xff, 0xff, 0xff);
        FillRectangle(context, consoleMaxSize, backColor);
        FillRectangle(context, 
            Rect(   Coord2(consoleMaxSize._topLeft[0],      consoleMaxSize._bottomRight[1]-3),
                    Coord2(consoleMaxSize._bottomRight[0],  consoleMaxSize._bottomRight[1]  )),
            borderColor);
        FillRectangle(context, 
            Rect(   Coord2(consoleMaxSize._topLeft[0],      entryBoxArea._topLeft[1]-3),
                    Coord2(consoleMaxSize._bottomRight[0],  consoleMaxSize._bottomRight[1]-3)),
            entryBoxColor);

        auto lines = _console->GetLines(linesToRender, _scrollBack);
        signed emptyLines = signed(linesToRender) - signed(lines.size());
        for (signed c=0; c<emptyLines; ++c) { historyAreaLayout.AllocateFullWidth(Coord(textHeight)); }
        for (auto i=lines.cbegin(); i!=lines.cend(); ++i) {
            char buffer[1024];
            ucs2_2_utf8(AsPointer(i->begin()), i->size(), (utf8*)buffer, dimof(buffer));
			DrawText()
                .Alignment(TextAlignment::Left)
                .Color(textColor)
                .Font(*res->_font)
                .Draw(context, historyAreaLayout.AllocateFullWidth(Coord(textHeight)), buffer);
        }

        RenderOverlays::CommonWidgets::Render(context, entryBoxArea, res->_font, _textEntry);
    }

    static const auto enter      = PlatformRig::KeyId_Make("enter");
    static const auto escape     = PlatformRig::KeyId_Make("escape");
    static const auto ctrl       = PlatformRig::KeyId_Make("control");
    static const auto pgdn       = PlatformRig::KeyId_Make("page down");
    static const auto pgup       = PlatformRig::KeyId_Make("page up");

    auto    ConsoleDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
    {
        auto beginI = input._activeButtons.cbegin();
        auto endI = input._activeButtons.cend();

        bool consume = _textEntry.ProcessInput(
            interfaceState, input,
            [](auto currentline) { return ConsoleRig::Console::GetInstance().AutoComplete(currentline); }) == ProcessInputResult::Consumed;

        if (input.IsPress(enter)) {
            if (!_textEntry._currentLine.empty())
                _console->Execute(_textEntry._currentLine);
            _textEntry._caret = 0;
            _textEntry._selectionStart = _textEntry._selectionEnd = _textEntry._caret;
            _textEntry._history.push_back(_textEntry._currentLine);
            _textEntry._historyCursor = 0;
            _scrollBack = 0;        // reset scroll?
            _scrollBackFractional = 0;
            _textEntry._currentLine = {};
            _textEntry._autoComplete.clear();
            consume = true;
        }

        if (input.IsPress(escape)) {
            _textEntry._caret = 0;
            _textEntry._selectionStart = _textEntry._selectionEnd = _textEntry._caret;
            _textEntry._currentLine = {};
            _textEntry._autoComplete.clear();
            consume = true;
        }

        auto lineCount = _console->GetLineCount();
        
        if (input.IsHeld(pgdn)) {
            if (lineCount > 0) {
                if (input.IsHeld(ctrl)) {
                    _scrollBack = 0;
                    _scrollBackFractional = 0;
                } else {
                    if ((_scrollBackFractional % 3) == 0) {
                        _scrollBack = unsigned(std::max(0, signed(_scrollBack)-1));
                    }
                    ++_scrollBackFractional;
                }
            } else { _scrollBack = _scrollBackFractional = 0; }
            consume = true;
        } else if (input.IsHeld(pgup)) {
            if (lineCount > 0) {
                if (input.IsHeld(ctrl)) {
                    _scrollBack = _console->GetLineCount()-1;
                    _scrollBackFractional = 0;
                } else {
                    if ((_scrollBackFractional % 3) == 0) {
                        _scrollBack = std::min(_console->GetLineCount()-1, _scrollBack+1u);
                    }
                    ++_scrollBackFractional;
                }
            } else { _scrollBack = _scrollBackFractional = 0; }
            consume = true;
        } else {
            _scrollBackFractional = 0;
        }

        return consume ? ProcessInputResult::Consumed : ProcessInputResult::Passthrough;
    }

    ConsoleDisplay::ConsoleDisplay(ConsoleRig::Console& console)
    : _console(&console)
    {
        _renderCounter = 0;
        _scrollBack = 0;
        _scrollBackFractional = 0;
    }

    ConsoleDisplay::~ConsoleDisplay()
    {
    }

}}
