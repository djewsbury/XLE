// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonWidgets.h"
#include "Font.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/AssetFutureContinuation.h"

namespace RenderOverlays { namespace CommonWidgets
{
	void DefaultFontsBox::ConstructToFuture(::Assets::FuturePtr<DefaultFontsBox>& future)
	{
		::Assets::WhenAll(
			RenderOverlays::MakeFont("DosisBook", 16),
			RenderOverlays::MakeFont("DosisExtraBold", 20)).ThenConstructToFuture(future);
	}

	void Draw::SectionHeader(Rect rectangle, StringSection<> name, bool expanded) const
	{
		using namespace DebuggingDisplay;
		Layout layout { rectangle };
		layout._paddingInternalBorder = 0;
		auto flipperRect = layout.AllocateFullHeight(14);
		auto titleRect = layout.AllocateFullHeight(layout.GetWidthRemaining());

		if (!expanded) {
			auto flipperRectCenter = (flipperRect._topLeft+flipperRect._bottomRight)/2;
			Coord2 arrows[] = {
				flipperRectCenter + Coord2( 4,  0),
				flipperRectCenter + Coord2(-4, -4),
				flipperRectCenter + Coord2(-4, +4)
			};
			ColorB arrowColors[] { ColorB::White, ColorB::White, ColorB::White };
			DrawTriangles(*_context, arrows, arrowColors, dimof(arrows)/3);
		} else {
			auto flipperRectCenter = (flipperRect._topLeft+flipperRect._bottomRight)/2;
			Coord2 arrows[] = {
				flipperRectCenter + Coord2( 0,  4),
				flipperRectCenter + Coord2(+4, -4),
				flipperRectCenter + Coord2(-4, -4)
			};
			ColorB arrowColors[] { ColorB::White, ColorB::White, ColorB::White };
			DrawTriangles(*_context, arrows, arrowColors, dimof(arrows)/3);
		}

		DrawText().Alignment(TextAlignment::Left).Draw(*_context, titleRect, name);
	}

	void Draw::XToggleButton(const Rect& xBoxRect) const
	{
		using namespace DebuggingDisplay;
		auto xBoxCenter = (xBoxRect._topLeft+xBoxRect._bottomRight) / 2;
		Coord2 xBox[] = {
			xBoxCenter + Coord2{ -3, -3, },
			xBoxCenter + Coord2{  3,  3, },
			xBoxCenter + Coord2{  3, -3, },
			xBoxCenter + Coord2{ -3,  3, }
		};
		ColorB xBoxColors[] { 
			ColorB{0x7f, 0x7f, 0x7f}, ColorB{0x7f, 0x7f, 0x7f}, ColorB{0x7f, 0x7f, 0x7f}, ColorB{0x7f, 0x7f, 0x7f}
		};
		OutlineRectangle(*_context, Rect{xBoxCenter-Coord2{6, 10}, xBoxCenter+Coord2{6, 10}}, ColorB{80, 80, 80});
		DrawLines(*_context, xBox, xBoxColors, dimof(xBox)/2);
	}
	
	void Draw::CheckBox(const Rect& content, bool state) const
	{
		using namespace DebuggingDisplay;
		if (state) {
			FillRaisedRoundedRectangle(*_context, content, ColorB{191, 123, 0}, 0.4f);

			Coord2 ptB = (content._topLeft + content._bottomRight) / 2;
			Coord2 ptA = (content._topLeft + ptB) / 2;
			Coord2 ptC = Coord2{content._bottomRight[0], content._topLeft[1]};
			Coord2 lines[] = {ptA, ptB, ptB, ptC};
			ColorB lineColors[] = {ColorB{38, 38, 38}, ColorB{38, 38, 38}};
			DrawLines(*_context, lines, lineColors, 2);
		} else {
			FillDepressedRoundedRectangle(*_context, content, ColorB{38, 38, 38}, 0.4f);
		}
	}

	void Draw::DisabledStateControl(const Rect& rect, StringSection<> name) const
	{
		using namespace DebuggingDisplay;
		OutlineRoundedRectangle(*_context, rect, ColorB{0x3f, 0x3f, 0x3f}, 1.f, 0.4f);
		DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(*_context, {rect._topLeft+Coord2{16, 0}, rect._bottomRight-Coord2{16, 0}}, name);
	}

	void Draw::RectangleContainer(const Rect& rect) const
	{
		using namespace DebuggingDisplay;
		OutlineRectangle(*_context, rect, ColorB{0x3f, 0x3f, 0x3f});
	}

	struct ButtonStyle
	{
		ColorB  _background;
		ColorB  _foreground;
		bool _depressed = false;
	};

	static ButtonStyle s_buttonNormal      { ColorB( 51,  51,  51), ColorB(191, 123, 0) };
	static ButtonStyle s_buttonMouseOver   { ColorB(120, 120, 120), ColorB(255, 255, 255) };
	static ButtonStyle s_buttonPressed     { ColorB(120, 120, 120), ColorB(196, 196, 196), true };

	template<typename T> inline const T& FormatButton(
		DebuggingDisplay::InterfaceState& interfaceState, DebuggingDisplay::InteractableId id, 
		const T& normalState, const T& mouseOverState, const T& pressedState)
	{
		if (interfaceState.HasMouseOver(id))
			return interfaceState.IsMouseButtonHeld(0)?pressedState:mouseOverState;
		return normalState;
	}

	void Draw::ButtonBasic(const Rect& rect, uint64_t interactable, StringSection<> label) const
	{
		using namespace DebuggingDisplay;
		auto formatting = FormatButton(*_interfaceState, interactable, s_buttonNormal, s_buttonMouseOver, s_buttonPressed);
		if (formatting._depressed)
			FillDepressedRoundedRectangle(*_context, rect, formatting._background);
		else
			FillRaisedRoundedRectangle(*_context, rect, formatting._background);
		DrawText()
			.Alignment(TextAlignment::Center)
			.Color(formatting._foreground)
			.Font(*_fonts->_buttonFont)
			.Draw(*_context, rect, label);
	}

	Draw::Draw(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState, HoveringLayer& hoverings)
	: _context(&context), _interactables(&interactables), _interfaceState(&interfaceState), _hoverings(&hoverings) 
	{
		_fonts = ConsoleRig::TryActualizeCachedBox<DefaultFontsBox>();
	}

	Draw::Draw(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState)
	: _context(&context), _interactables(&interactables), _interfaceState(&interfaceState)
	{
		_fonts = ConsoleRig::TryActualizeCachedBox<DefaultFontsBox>();
	}

	static const auto left       = PlatformRig::KeyId_Make("left");
	static const auto right      = PlatformRig::KeyId_Make("right");
	static const auto home       = PlatformRig::KeyId_Make("home");
	static const auto end        = PlatformRig::KeyId_Make("end");
	static const auto backspace  = PlatformRig::KeyId_Make("backspace");
	static const auto del        = PlatformRig::KeyId_Make("delete");
	static const auto up         = PlatformRig::KeyId_Make("up");
	static const auto down       = PlatformRig::KeyId_Make("down");
	static const auto tab        = PlatformRig::KeyId_Make("tab");
	static const auto shift      = PlatformRig::KeyId_Make("shift");

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
		DebuggingDisplay::ProcessInputResult TextEntry<CharType>::ProcessInput(
			DebuggingDisplay::InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input,
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

		return consume ? DebuggingDisplay::ProcessInputResult::Consumed : DebuggingDisplay::ProcessInputResult::Passthrough;
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
		using namespace DebuggingDisplay;
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

}}



