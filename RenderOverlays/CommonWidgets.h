// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "DebuggingDisplay.h"
#include "../Utility/StringFormat.h"

namespace RenderOverlays { namespace CommonWidgets
{
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

		DebuggingDisplay::ProcessInputResult    ProcessInput(
			DebuggingDisplay::InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input,
			const std::function<std::vector<std::basic_string<CharType>>(const std::basic_string<CharType>&)>& autocompleteFn = nullptr);

		void Reset(const std::basic_string<CharType>& currentLine);
	};

	void    Render(IOverlayContext& context, const Rect& entryBoxArea, const std::shared_ptr<Font>& font, const TextEntry<>& textEntry);

	class HoveringLayer
	{
	public:
		DebuggingDisplay::InteractableId _hoveringCtrl = 0;
		TextEntry<> _textEntry;
	};

	class DefaultFontsBox
	{
	public:
		std::shared_ptr<RenderOverlays::Font> _editBoxFont;
		std::shared_ptr<RenderOverlays::Font> _buttonFont;

		DefaultFontsBox(std::shared_ptr<RenderOverlays::Font> editBoxFont, std::shared_ptr<RenderOverlays::Font> buttonFont)
		: _editBoxFont(std::move(editBoxFont)), _buttonFont(std::move(buttonFont)) {}

		static void ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise);
	};

	class Draw
	{
	public:
		static constexpr unsigned baseLineHeight = 20;
		
		void SectionHeader(Rect rectangle, StringSection<> name, bool expanded) const;
		template<typename Type>
			void LeftRight(Rect valueBox, uint64_t interactable, StringSection<> name, Type value) const;
		template<typename Type>
			void Bounded(Rect valueBox, uint64_t interactable, StringSection<> name, Type value, Type leftSideValue, Type rightSideValue) const;
		void XToggleButton(const Rect& xBoxRect) const;
		void CheckBox(const Rect& content, bool state) const;
		void DisabledStateControl(const Rect& rect, StringSection<> name) const;
		void RectangleContainer(const Rect& rect) const;
		void ButtonBasic(const Rect& rect, uint64_t interactable, StringSection<> label) const;

		IOverlayContext& GetContext() { return *_context; }
		DebuggingDisplay::Interactables& GetInteractables() { return *_interactables; }
		DebuggingDisplay::InterfaceState& GetInterfaceState() { return *_interfaceState; }
		DefaultFontsBox& GetDefaultFontsBox() { return *_fonts; }

		Draw(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState, HoveringLayer& hoverings);
		Draw(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState);
	private:
		IOverlayContext* _context;
		DebuggingDisplay::Interactables* _interactables;
		DebuggingDisplay::InterfaceState* _interfaceState;
		HoveringLayer* _hoverings;
		DefaultFontsBox* _fonts;
	};

	class Input
	{
	public:
		mutable bool _madeChange = false;
		mutable bool _redoLayout = false;

		DebuggingDisplay::InterfaceState& GetInterfaceState() { return *_interfaceState; }
		const PlatformRig::InputSnapshot& GetEvent() { return *_input; }
		HoveringLayer& GetHoverings() { return *_hoverings; }

		Input(DebuggingDisplay::InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input, HoveringLayer& hoverings)
		: _interfaceState(&interfaceState), _input(&input), _hoverings(&hoverings) {}
	private:
		DebuggingDisplay::InterfaceState* _interfaceState;
		const PlatformRig::InputSnapshot* _input;
		HoveringLayer* _hoverings;
	};

	template<typename Type>
		void Draw::LeftRight(Rect valueBox, uint64_t interactable, StringSection<> name, Type value) const
	{
		using namespace DebuggingDisplay;
		Rect leftRect { valueBox._topLeft, Coord2{(valueBox._topLeft[0]+valueBox._bottomRight[0])/2, valueBox._bottomRight[1]} };
		Rect rightRect { Coord2{(valueBox._topLeft[0]+valueBox._bottomRight[0])/2, valueBox._topLeft[1]}, valueBox._bottomRight };

		bool leftHighlighted = _interfaceState->HasMouseOver(interactable) && IsInside(leftRect, _interfaceState->MousePosition());
		bool rightHighlighted = _interfaceState->HasMouseOver(interactable) && IsInside(rightRect, _interfaceState->MousePosition());
		if (leftHighlighted)
			FillRoundedRectangle(*_context, leftRect, ColorB{58, 58, 58}, 0.4f, Corner::TopLeft|Corner::BottomLeft);
		if (rightHighlighted)
			FillRoundedRectangle(*_context, rightRect, ColorB{58, 58, 58}, 0.4f, Corner::TopRight|Corner::BottomRight);

		// FillDepressedRoundedRectangle(context, valueBox, ColorB{38, 38, 38}, 0.4f);
		OutlineRoundedRectangle(*_context, valueBox, ColorB{0x7f, 0x7f, 0x7f}, 1.f, 0.4f);

		Coord2 arrows[] = {
			Coord2(valueBox._topLeft[0] + 8, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2),
			Coord2(valueBox._topLeft[0] + 14, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2+4),
			Coord2(valueBox._topLeft[0] + 14, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2-4),

			Coord2(valueBox._bottomRight[0] - 8, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2),
			Coord2(valueBox._bottomRight[0] - 14, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2-4),
			Coord2(valueBox._bottomRight[0] - 14, (valueBox._topLeft[1]+valueBox._bottomRight[1])/2+4)
		};
		ColorB leftColor = leftHighlighted ? ColorB{0xff, 0xff, 0xff} : ColorB{0x7f, 0x7f, 0x7f};
		ColorB rightColor = rightHighlighted ? ColorB{0xff, 0xff, 0xff} : ColorB{0x7f, 0x7f, 0x7f};
		ColorB arrowColors[] { 
			leftColor, leftColor, leftColor,
			rightColor, rightColor, rightColor
		};
		DrawTriangles(*_context, arrows, arrowColors, dimof(arrows)/3);

		DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Left).Draw(*_context, {valueBox._topLeft+Coord2{16, 0}, valueBox._bottomRight-Coord2{16, 0}}, name);
		StringMeld<256> valueStr;
		valueStr << value;
		DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Right).Draw(*_context, {valueBox._topLeft+Coord2{16, 0}, valueBox._bottomRight-Coord2{16, 0}}, valueStr.AsStringSection());
	}

	template<typename Type>
		void Draw::Bounded(Rect valueBox, uint64_t interactable, StringSection<> name, Type value, Type leftSideValue, Type rightSideValue) const
	{
		using namespace DebuggingDisplay;
		float alpha = (value - leftSideValue) / float(rightSideValue - leftSideValue);
		alpha = std::max(0.f, std::min(1.0f, alpha));
		ColorB filledAreaColor = (_interfaceState->HasMouseOver(interactable) && !_interfaceState->IsMouseButtonHeld() && _hoverings->_hoveringCtrl != interactable) ? ColorB{58, 58, 58} : ColorB{51, 51, 51};
		ColorB outlineColor = (_interfaceState->HasMouseOver(interactable) && !_interfaceState->IsMouseButtonHeld() && _hoverings->_hoveringCtrl != interactable) ? ColorB{0x9f, 0x9f, 0x9f} : ColorB{0x7f, 0x7f, 0x7f};
		FillRoundedRectangle(*_context, Rect{{LinearInterpolate(valueBox._topLeft[0], valueBox._bottomRight[0], alpha), valueBox._topLeft[1]}, valueBox._bottomRight}, filledAreaColor, 0.4f, Corner::TopRight|Corner::BottomRight);
		OutlineRoundedRectangle(*_context, valueBox, outlineColor, 1.f, 0.4f);

		if (_hoverings->_hoveringCtrl == interactable) {
			auto textBoxRect = valueBox;
			textBoxRect._topLeft += Coord2(8, 2);
			textBoxRect._bottomRight -= Coord2(8, 2);
			FillAndOutlineRectangle(*_context, textBoxRect, ColorB{38, 38, 38}, ColorB{192, 192, 192});
			if (_fonts)
				Render(*_context, textBoxRect, _fonts->_editBoxFont, _hoverings->_textEntry);
		} else {
			DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Left).Draw(*_context, {valueBox._topLeft+Coord2{16, 0}, valueBox._bottomRight-Coord2{16, 0}}, name);
			if (_fonts) {
				StringMeld<256> valueStr;
				valueStr << value;
				DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Right).Font(*_fonts->_editBoxFont).Draw(*_context, {valueBox._topLeft+Coord2{16, 0}, valueBox._bottomRight-Coord2{16, 0}}, valueStr.AsStringSection());
			}
		}
	}
}}

