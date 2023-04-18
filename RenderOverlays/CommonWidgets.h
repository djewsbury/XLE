// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once
#include "DebuggingDisplay.h"
#include "../Utility/StringFormat.h"

namespace RenderOverlays
{
	namespace CommonWidgets { class HoveringLayer; }

	struct DrawContext
	{
		IOverlayContext& GetContext() { return *_context; }
		DebuggingDisplay::Interactables& GetInteractables() { return *_interactables; }
		DebuggingDisplay::InterfaceState& GetInterfaceState() { return *_interfaceState; }

		IOverlayContext* _context = nullptr;
		DebuggingDisplay::Interactables* _interactables = nullptr;
		DebuggingDisplay::InterfaceState* _interfaceState = nullptr;
		CommonWidgets::HoveringLayer* _hoverings = nullptr;

		DrawContext(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState)
		: _context(&context), _interactables(&interactables), _interfaceState(&interfaceState) {}

		DrawContext(IOverlayContext& context, DebuggingDisplay::Interactables& interactables, DebuggingDisplay::InterfaceState& interfaceState, CommonWidgets::HoveringLayer& hoverings)
		: _context(&context), _interactables(&interactables), _interfaceState(&interfaceState), _hoverings(&hoverings) {}
	};

	struct IOContext
	{
		PlatformRig::InputContext& GetInputContext() { return *_inputContext; }
		const OSServices::InputSnapshot& GetEvent() { return *_event; }

		PlatformRig::InputContext* _inputContext = nullptr;
		const OSServices::InputSnapshot* _event = nullptr;

		IOContext(PlatformRig::InputContext& inputContext, const OSServices::InputSnapshot& evnt)
		: _inputContext(&inputContext), _event(&evnt) {}
	};
}

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

		PlatformRig::ProcessInputResult    ProcessInput(
			DebuggingDisplay::InterfaceState& interfaceState, const OSServices::InputSnapshot& input,
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
		std::shared_ptr<RenderOverlays::Font> _headingFont;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		DefaultFontsBox(std::shared_ptr<RenderOverlays::Font> editBoxFont, std::shared_ptr<RenderOverlays::Font> buttonFont, std::shared_ptr<RenderOverlays::Font> headingFont, ::Assets::DependencyValidation depVal);
		static void ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise);
	};

	struct CommonWidgetsStaticData;
	class Styler
	{
	public:
		static constexpr unsigned baseLineHeight = 20;
		
		void SectionHeader(DrawContext&, Rect rectangle, StringSection<> name, bool expanded) const;
		template<typename Type>
			void LeftRight(DrawContext&, Rect valueBox, uint64_t interactable, StringSection<> name, Type value) const;
		template<typename Type>
			void Bounded(DrawContext&, Rect valueBox, uint64_t interactable, StringSection<> name, Type value, Type leftSideValue, Type rightSideValue) const;
		void XToggleButton(DrawContext&, const Rect& xBoxRect) const;
		void CheckBox(DrawContext&, const Rect& content, bool state) const;
		void DisabledStateControl(DrawContext&, const Rect& rect, StringSection<> name) const;
		void RectangleContainer(DrawContext&, const Rect& rect) const;
		void ButtonBasic(DrawContext&, const Rect& rect, uint64_t interactable, StringSection<> label) const;

		void KeyIndicator(DrawContext&, const Rect& frame, const void* precalculatedData);
		void KeyIndicatorLabel(DrawContext&, const Rect& frame, const Rect& labelContent, StringSection<> label);
		void KeyIndicatorKey(DrawContext&, const Rect& frame, const Rect& labelContent, StringSection<> label);

		struct MeasuredRectangle { Coord _minWidth, _width, _minHeight, _height; };
		MeasuredRectangle MeasureKeyIndicator(StringSection<> label, StringSection<> key);
		std::shared_ptr<void> MeasureKeyIndicator_Precalculate(Coord width, Coord height, StringSection<> label, StringSection<> key);

		DefaultFontsBox& GetDefaultFontsBox() { return *_fonts; }
		static DefaultFontsBox* TryGetDefaultFontsBox();
		static void StallForDefaultFonts();

		Styler();
	private:
		DefaultFontsBox* _fonts;
		const CommonWidgetsStaticData* _staticData;

		unsigned GetLeftRightLabelsHorizontalMargin() const;
	};

	template<typename Type>
		void Styler::LeftRight(DrawContext& drawContext, Rect valueBox, uint64_t interactable, StringSection<> name, Type value) const
	{
		using namespace DebuggingDisplay;
		Rect leftRect { valueBox._topLeft, Coord2{(valueBox._topLeft[0]+valueBox._bottomRight[0])/2, valueBox._bottomRight[1]} };
		Rect rightRect { Coord2{(valueBox._topLeft[0]+valueBox._bottomRight[0])/2, valueBox._topLeft[1]}, valueBox._bottomRight };

		bool leftHighlighted = drawContext.GetInterfaceState().HasMouseOver(interactable) && Contains(leftRect, drawContext.GetInterfaceState().MousePosition());
		bool rightHighlighted = drawContext.GetInterfaceState().HasMouseOver(interactable) && Contains(rightRect, drawContext.GetInterfaceState().MousePosition());
		if (leftHighlighted)
			FillRoundedRectangle(drawContext.GetContext(), leftRect, ColorB{58, 58, 58}, 0.4f, Corner::TopLeft|Corner::BottomLeft);
		if (rightHighlighted)
			FillRoundedRectangle(drawContext.GetContext(), rightRect, ColorB{58, 58, 58}, 0.4f, Corner::TopRight|Corner::BottomRight);

		// FillDepressedRoundedRectangle(drawContext.GetContext(), valueBox, ColorB{38, 38, 38}, 0.4f);
		OutlineRoundedRectangle(drawContext.GetContext(), valueBox, ColorB{0x7f, 0x7f, 0x7f}, 1.f, 0.4f);

		Float2 arrows[] = {
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
		FillTriangles(drawContext.GetContext(), arrows, arrowColors, dimof(arrows)/3);

		auto margin = GetLeftRightLabelsHorizontalMargin();
		DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Left).Draw(drawContext.GetContext(), {valueBox._topLeft+Coord2{margin, 0}, valueBox._bottomRight-Coord2{margin, 0}}, name);
		StringMeld<256> valueStr;
		valueStr << value;
		DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Right).Draw(drawContext.GetContext(), {valueBox._topLeft+Coord2{margin, 0}, valueBox._bottomRight-Coord2{margin, 0}}, valueStr.AsStringSection());
	}

	template<typename Type>
		void Styler::Bounded(DrawContext& drawContext, Rect valueBox, uint64_t interactable, StringSection<> name, Type value, Type leftSideValue, Type rightSideValue) const
	{
		using namespace DebuggingDisplay;
		assert(drawContext._hoverings);
		float alpha = (value - leftSideValue) / float(rightSideValue - leftSideValue);
		alpha = std::max(0.f, std::min(1.0f, alpha));
		ColorB filledAreaColor = (drawContext.GetInterfaceState().HasMouseOver(interactable) && !drawContext.GetInterfaceState().IsMouseButtonHeld() && drawContext._hoverings->_hoveringCtrl != interactable) ? ColorB{58, 58, 58} : ColorB{51, 51, 51};
		ColorB outlineColor = (drawContext.GetInterfaceState().HasMouseOver(interactable) && !drawContext.GetInterfaceState().IsMouseButtonHeld() && drawContext._hoverings->_hoveringCtrl != interactable) ? ColorB{0x9f, 0x9f, 0x9f} : ColorB{0x7f, 0x7f, 0x7f};
		FillRoundedRectangle(drawContext.GetContext(), Rect{{LinearInterpolate(valueBox._topLeft[0], valueBox._bottomRight[0], alpha), valueBox._topLeft[1]}, valueBox._bottomRight}, filledAreaColor, 0.4f, Corner::TopRight|Corner::BottomRight);
		OutlineRoundedRectangle(drawContext.GetContext(), valueBox, outlineColor, 1.f, 0.4f);

		auto margin = GetLeftRightLabelsHorizontalMargin();
		if (drawContext._hoverings->_hoveringCtrl == interactable) {
			auto textBoxRect = valueBox;
			textBoxRect._topLeft += Coord2(8, 2);
			textBoxRect._bottomRight -= Coord2(8, 2);
			FillAndOutlineRectangle(drawContext.GetContext(), textBoxRect, ColorB{38, 38, 38}, ColorB{192, 192, 192});
			if (_fonts)
				Render(drawContext.GetContext(), textBoxRect, _fonts->_editBoxFont, drawContext._hoverings->_textEntry);
		} else {
			DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Left).Draw(drawContext.GetContext(), {valueBox._topLeft+Coord2{margin, 0}, valueBox._bottomRight-Coord2{margin, 0}}, name);
			if (_fonts) {
				StringMeld<256> valueStr;
				valueStr << value;
				DrawText().Color({191, 123, 0}).Alignment(TextAlignment::Right).Font(*_fonts->_editBoxFont).Draw(drawContext.GetContext(), {valueBox._topLeft+Coord2{margin, 0}, valueBox._bottomRight-Coord2{margin, 0}}, valueStr.AsStringSection());
			}
		}
	}
}}

