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
#include <assert.h>

namespace PlatformRig { namespace Overlays
{

            //////   C O N S O L E   D I S P L A Y   //////

    void    ConsoleDisplay::Render( IOverlayContext& context,       Layout& layout, 
                                    Interactables&interactables,    InterfaceState& interfaceState)
    {
        Rect consoleMaxSize                 = layout.GetMaximumSize();
        const unsigned height               = std::min(consoleMaxSize.Height() / 2, 512);
        consoleMaxSize._bottomRight[1]       = consoleMaxSize._topLeft[1] + height;

		auto font = GetX2Font("OrbitronBlack", 20);

        const float         textHeight      = font->GetFontProperties()._lineHeight;
        const Coord         entryBoxHeight  = Coord(textHeight) + 2 * layout._paddingBetweenAllocations;

        const Rect          historyArea     = layout.AllocateFullWidth(consoleMaxSize.Height() - 2 * layout._paddingInternalBorder - layout._paddingBetweenAllocations - entryBoxHeight);
        const Rect          entryBoxArea    = layout.AllocateFullWidth(entryBoxHeight);

        Layout              historyAreaLayout(historyArea);
        historyAreaLayout._paddingInternalBorder = 0;
        unsigned            linesToRender   = (unsigned)XlFloor((historyArea.Height() - 2*historyAreaLayout._paddingInternalBorder) / (textHeight + historyAreaLayout._paddingBetweenAllocations));

        static ColorB       backColor       = ColorB(0x20, 0x20, 0x20, 0x90);
        static ColorB       textColor       = ColorB(0xff, 0xff, 0xff);
        static ColorB       caretColor      = ColorB(0xaf, 0xaf, 0xaf);
        static ColorB       selectionColor  = ColorB(0x7f, 0x7f, 0x7f, 0x7f);
        static ColorB       borderColor     = ColorB(0xff, 0xff, 0xff, 0x7f);
        static ColorB       entryBoxColor   = ColorB(0x00, 0x00, 0x00, 0x4f);
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
			context.DrawText(AsPixelCoords(historyAreaLayout.AllocateFullWidth(Coord(textHeight))), font, TextStyle{}, textColor, TextAlignment::Left, buffer);
        }

        Coord caretOffset = 0;
        Coord selStart = 0, selEnd = 0;
        if (!_currentLine.empty()) {

            size_t firstPart = std::min(_caret, _currentLine.size());
            if (firstPart)
                caretOffset = (Coord)StringWidth(*font, MakeStringSection(_currentLine.begin(), _currentLine.begin() + firstPart));

            firstPart = std::min(_selectionStart, _currentLine.size());
            if (firstPart) {
                selStart = (Coord)StringWidth(*font, MakeStringSection(_currentLine.begin(), _currentLine.begin() + firstPart));
            }

            firstPart = std::min(_selectionEnd, _currentLine.size());
            if (firstPart)
                selEnd = (Coord)StringWidth(*font, MakeStringSection(_currentLine.begin(), _currentLine.begin() + firstPart));

            if (selStart != selEnd) {
                Rect rect(  Coord2(entryBoxArea._topLeft[0] + std::min(selStart, selEnd), entryBoxArea._topLeft[1]),
                            Coord2(entryBoxArea._topLeft[0] + std::max(selStart, selEnd), entryBoxArea._bottomRight[1]));
                FillRectangle(context, rect, selectionColor);
            }

            char buffer[1024];
			ucs2_2_utf8(AsPointer(_currentLine.begin()), _currentLine.size(), (utf8*)buffer, dimof(buffer));
            buffer[dimof(buffer)-1] = '\0';
			context.DrawText(AsPixelCoords(entryBoxArea), font, TextStyle{}, textColor, TextAlignment::Left, buffer);

        }

        if ((_renderCounter / 20) & 0x1) {
            Rect rect(  Coord2(entryBoxArea._topLeft[0] + caretOffset - 1, entryBoxArea._topLeft[1]),
                        Coord2(entryBoxArea._topLeft[0] + caretOffset + 2, entryBoxArea._bottomRight[1]));
            FillRectangle(context, rect, caretColor);
        }

        ++_renderCounter;
    }

    static std::string      AsUTF8(const std::basic_string<ucs2>& input)
    {
        char buffer[1024];
        ucs2_2_utf8(AsPointer(input.begin()), input.size(), (utf8*)buffer, dimof(buffer));
        return std::string(buffer);
    }

    static std::basic_string<ucs2>      AsUTF16(const std::string& input)
    {
        ucs2 buffer[1024];
        utf8_2_ucs2((utf8*)AsPointer(input.begin()), input.size(), buffer, dimof(buffer));
        return std::basic_string<ucs2>(buffer);
    }

    bool    ConsoleDisplay::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
    {
        bool consume = false;
        if (input._pressedChar) {
            if (input._pressedChar >= 0x20 && input._pressedChar != 0x7f && input._pressedChar != '~') {
                DeleteSelectedPart();
                assert(_caret <= _currentLine.size());
                if (_caret <= _currentLine.size()) {
                    _currentLine.insert(_caret++, 1, (std::basic_string<ucs2>::value_type)input._pressedChar);
                    _autoComplete.clear();
                    _selectionStart = _selectionEnd = _caret;
                    consume = true;
                }
            }
        }

        static KeyId left       = KeyId_Make("left");
        static KeyId right      = KeyId_Make("right");
        static KeyId home       = KeyId_Make("home");
        static KeyId end        = KeyId_Make("end");
        static KeyId enter      = KeyId_Make("enter");
        static KeyId escape     = KeyId_Make("escape");
        static KeyId backspace  = KeyId_Make("backspace");
        static KeyId del        = KeyId_Make("delete");
        static KeyId up         = KeyId_Make("up");
        static KeyId down       = KeyId_Make("down");
        static KeyId tab        = KeyId_Make("tab");
        static KeyId shift      = KeyId_Make("shift");
        static KeyId ctrl       = KeyId_Make("control");
        static KeyId pgdn       = KeyId_Make("page down");
        static KeyId pgup       = KeyId_Make("page up");

        auto beginI = input._activeButtons.cbegin();
        auto endI = input._activeButtons.cend();

        auto startCaret = _caret;

        if (input.IsPress(left))     { _caret = std::max(0, signed(_caret)-1); consume = true; }
        if (input.IsPress(right))    { _caret = std::min(_currentLine.size(), _caret+1); consume = true; }
        if (input.IsPress(home))     { _caret = 0; consume = true; }
        if (input.IsPress(end))      { _caret = _currentLine.size(); consume = true; }

        if (startCaret != _caret) {
            _selectionEnd = _caret;
            if (!input.IsHeld(shift)) {
                _selectionStart = _caret;
            }
        }

        if (input.IsPress(up)) {
            unsigned newHistoryCursor = (unsigned)std::min(_history.size(), size_t(_historyCursor+1));
            if (newHistoryCursor != _historyCursor) {
                _historyCursor = newHistoryCursor;
                if (_historyCursor!=0) {
                    _currentLine = _history[_history.size() - _historyCursor];
                    _caret = _currentLine.size();
                    _selectionStart = _selectionEnd = _caret;
                }
                _autoComplete.clear();
            }
            consume = true;
        }
        if (input.IsPress(down)) {
            unsigned newHistoryCursor = std::max(0, signed(_historyCursor)-1);
            if (newHistoryCursor != _historyCursor) {
                _historyCursor = newHistoryCursor;
                if (!_historyCursor) {
                    _currentLine = std::basic_string<ucs2>();
                    _caret = 0;
                } else {
                    _currentLine = _history[_history.size() - _historyCursor];
                    _caret = _currentLine.size();
                }
                _selectionStart = _selectionEnd = _caret;
                _autoComplete.clear();
            }
            consume = true;
        }

        if (input.IsPress(tab)) {
            if (!_currentLine.empty()) {
                if (_autoComplete.empty()) {
                    _autoComplete = ConsoleRig::Console::GetInstance().AutoComplete(AsUTF8(_currentLine));
                    _autoCompleteCursor = 0;
                } else {
                    _autoCompleteCursor = (_autoCompleteCursor+1) % _autoComplete.size();
                }

                if (_autoCompleteCursor < _autoComplete.size()) {
                    _currentLine = AsUTF16(_autoComplete[_autoCompleteCursor]);
                    _selectionStart = _caret;
                    _selectionEnd = _currentLine.size();
                }
            }
            consume = true;
        }

        if (input.IsPress(backspace)) {
            if (_selectionStart != _selectionEnd) {
                DeleteSelectedPart();
            } else if (_caret>0) {
                _currentLine.erase(_caret-1, 1);
                _autoComplete.clear();
                --_caret;
            }
            consume = true;
        }

        if (input.IsPress(del)) {
            if (_selectionStart != _selectionEnd) {
                DeleteSelectedPart();
            } else if (_caret < _currentLine.size()) {
                _currentLine.erase(_caret, 1);
                _autoComplete.clear();
            }
            consume = true;
        }

        if (input.IsPress(enter)) {
            if (!_currentLine.empty()) {
                _console->Execute(AsUTF8(_currentLine));
            }
            _caret = 0;
            _selectionStart = _selectionEnd = _caret;
            _history.push_back(_currentLine);
            _historyCursor = 0;
            _scrollBack = 0;        // reset scroll?
            _scrollBackFractional = 0;
            _currentLine = std::basic_string<ucs2>();
            _autoComplete.clear();
            consume = true;
        }

        if (input.IsPress(escape)) {
            _caret = 0;
            _selectionStart = _selectionEnd = _caret;
            _currentLine = std::basic_string<ucs2>();
            _autoComplete.clear();
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

        return consume;
    }

    void ConsoleDisplay::DeleteSelectedPart()
    {
        if (_selectionStart != _selectionEnd) {
            auto diff = std::abs(ptrdiff_t(_selectionEnd) - ptrdiff_t(_selectionStart));
            auto start = std::min(_selectionStart, _selectionEnd);
            _currentLine.erase(start, diff);
            if (_caret > start) {
                if (_caret <= (start+diff)) { _caret = start; }
                else { _caret -= diff; }
            }
            _selectionStart = _selectionEnd = _caret;
            _autoComplete.clear();
        }
    }

    ConsoleDisplay::ConsoleDisplay(ConsoleRig::Console& console)
    : _console(&console)
    {
        _caret = 0;
        _selectionStart = _selectionEnd = _caret;
        _renderCounter = 0;
        _autoCompleteCursor = _historyCursor = 0;
        _scrollBack = 0;
        _scrollBackFractional = 0;
    }

    ConsoleDisplay::~ConsoleDisplay()
    {
    }

}}
