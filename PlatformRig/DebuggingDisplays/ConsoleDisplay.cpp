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

        static void ConstructToFuture(::Assets::FuturePtr<ConsoleDisplayResources>& future)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("OrbitronBlack", 20)).ThenConstructToFuture(future);
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

        Overlays::Render(context, entryBoxArea, res->_font, _textEntry);
    }

#if 0
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
#endif

    static const KeyId left       = KeyId_Make("left");
    static const KeyId right      = KeyId_Make("right");
    static const KeyId home       = KeyId_Make("home");
    static const KeyId end        = KeyId_Make("end");
    static const KeyId enter      = KeyId_Make("enter");
    static const KeyId escape     = KeyId_Make("escape");
    static const KeyId backspace  = KeyId_Make("backspace");
    static const KeyId del        = KeyId_Make("delete");
    static const KeyId up         = KeyId_Make("up");
    static const KeyId down       = KeyId_Make("down");
    static const KeyId tab        = KeyId_Make("tab");
    static const KeyId shift      = KeyId_Make("shift");
    static const KeyId ctrl       = KeyId_Make("control");
    static const KeyId pgdn       = KeyId_Make("page down");
    static const KeyId pgup       = KeyId_Make("page up");

    template<typename CharType>
        static void DeleteSelectedPart(TextEntry<CharType>& textEntry)
    {
        if (textEntry._selectionStart != textEntry._selectionEnd) {
            auto diff = std::abs(ptrdiff_t(textEntry._selectionEnd) - ptrdiff_t(textEntry._selectionStart));
            auto start = std::min(textEntry._selectionStart, textEntry._selectionEnd);
            textEntry._currentLine.erase(start, diff);
            if (textEntry._caret > start) {
                if (textEntry._caret <= (start+diff)) { textEntry._caret = start; }
                else { textEntry._caret -= diff; }
            }
            textEntry._selectionStart = textEntry._selectionEnd = textEntry._caret;
            textEntry._autoComplete.clear();
        }
    }

    template<typename CharType>
        ProcessInputResult TextEntry<CharType>::ProcessInput(
            InterfaceState& interfaceState, const InputSnapshot& input,
            const std::function<std::vector<std::basic_string<CharType>>(const std::basic_string<CharType>&)>& autocompleteFn)
    {
        bool consume = false;
        if (input._pressedChar) {
            if (input._pressedChar >= 0x20 && input._pressedChar != 0x7f && input._pressedChar != '~') {
                DeleteSelectedPart(*this);
                assert(_caret <= _currentLine.size());
                if (_caret <= _currentLine.size()) {
                    _currentLine.insert(_caret++, 1, (CharType)input._pressedChar);
                    _autoComplete.clear();
                    _selectionStart = _selectionEnd = _caret;
                    consume = true;
                }
            }
        }

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
                    _currentLine = {};
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
                if (_autoComplete.empty() && autocompleteFn) {
                    _autoComplete = autocompleteFn(_currentLine);
                    _autoCompleteCursor = 0;
                } else {
                    _autoCompleteCursor = (_autoCompleteCursor+1) % _autoComplete.size();
                }

                if (_autoCompleteCursor < _autoComplete.size()) {
                    _currentLine = _autoComplete[_autoCompleteCursor];
                    _selectionStart = _caret;
                    _selectionEnd = _currentLine.size();
                }
            }
            consume = true;
        }

        if (input.IsPress(backspace)) {
            if (_selectionStart != _selectionEnd) {
                DeleteSelectedPart(*this);
            } else if (_caret>0) {
                _currentLine.erase(_caret-1, 1);
                _autoComplete.clear();
                --_caret;
            }
            consume = true;
        }

        if (input.IsPress(del)) {
            if (_selectionStart != _selectionEnd) {
                DeleteSelectedPart(*this);
            } else if (_caret < _currentLine.size()) {
                _currentLine.erase(_caret, 1);
                _autoComplete.clear();
            }
            consume = true;
        }

        return consume ? ProcessInputResult::Consumed : ProcessInputResult::Passthrough;
    }

    template<typename CharType>
        void TextEntry<CharType>::Reset(const std::basic_string<CharType>& currentLine)
    {
        _currentLine = currentLine;
        _caret = _selectionEnd = _currentLine.size();
        _selectionStart = 0;
    }

    template class TextEntry<char>;

    void    Render(IOverlayContext& context, const Rect& entryBoxArea, const std::shared_ptr<Font>& font, const TextEntry<>& textEntry)
    {
        const ColorB       textColor       = ColorB(0xff, 0xff, 0xff);
        const ColorB       caretColor      = ColorB(0xaf, 0xaf, 0xaf);
        const ColorB       selectionColor  = ColorB(0x7f, 0x7f, 0x7f, 0x7f);

        Coord caretOffset = 0;
        Coord selStart = 0, selEnd = 0;
        if (!textEntry._currentLine.empty()) {

            size_t firstPart = std::min(textEntry._caret, textEntry._currentLine.size());
            if (firstPart)
                caretOffset = (Coord)StringWidth(*font, MakeStringSection(textEntry._currentLine.begin(), textEntry._currentLine.begin() + firstPart));

            firstPart = std::min(textEntry._selectionStart, textEntry._currentLine.size());
            if (firstPart) {
                selStart = (Coord)StringWidth(*font, MakeStringSection(textEntry._currentLine.begin(), textEntry._currentLine.begin() + firstPart));
            }

            firstPart = std::min(textEntry._selectionEnd, textEntry._currentLine.size());
            if (firstPart)
                selEnd = (Coord)StringWidth(*font, MakeStringSection(textEntry._currentLine.begin(), textEntry._currentLine.begin() + firstPart));

            if (selStart != selEnd) {
                Rect rect(  Coord2(entryBoxArea._topLeft[0] + std::min(selStart, selEnd), entryBoxArea._topLeft[1]),
                            Coord2(entryBoxArea._topLeft[0] + std::max(selStart, selEnd), entryBoxArea._bottomRight[1]));
                FillRectangle(context, rect, selectionColor);
            }

			DrawText()
                .Font(*font)
                .Color(textColor)
                .Alignment(TextAlignment::Left)
                .Draw(context, entryBoxArea, textEntry._currentLine);

        }

        static unsigned _renderCounter = 0;
        if ((_renderCounter / 20) & 0x1) {
            Rect rect(  Coord2(entryBoxArea._topLeft[0] + caretOffset - 1, entryBoxArea._topLeft[1]),
                        Coord2(entryBoxArea._topLeft[0] + caretOffset + 2, entryBoxArea._bottomRight[1]));
            FillRectangle(context, rect, caretColor);
        }
        ++_renderCounter;
    }

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
