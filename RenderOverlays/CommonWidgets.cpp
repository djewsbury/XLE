// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonWidgets.h"
#include "Font.h"
#include "ShapesRendering.h"
#include "DrawText.h"
#include "LayoutEngine.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/Continuation.h"
#include "../Assets/AssetsCore.h"

#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Tools/EntityInterface/MountedData.h"
#include "../Formatters/IDynamicFormatter.h"
#include "../Formatters/FormatterUtils.h"

using namespace PlatformRig::Literals;
using namespace Utility::Literals;

namespace RenderOverlays { namespace CommonWidgets
{
	static ColorB DeserializeColor(Formatters::IDynamicInputFormatter& fmttr)
	{
		IteratorRange<const void*> value;
		ImpliedTyping::TypeDesc typeDesc;
		if (!fmttr.TryRawValue(value, typeDesc))
			Throw(Formatters::FormatException("Expecting color value", fmttr.GetLocation()));

		if (auto intForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<unsigned>()) {
			return *intForm;
		} else if (auto tripletForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt3>()) {
			return ColorB{uint8_t((*tripletForm)[0]), uint8_t((*tripletForm)[1]), uint8_t((*tripletForm)[2])};
		} else if (auto quadForm = ImpliedTyping::VariantNonRetained{typeDesc, value}.TryCastValue<UInt4>()) {
			return ColorB{uint8_t((*quadForm)[0]), uint8_t((*quadForm)[1]), uint8_t((*quadForm)[2]), uint8_t((*quadForm)[3])};
		} else {
			Throw(Formatters::FormatException("Could not interpret value as color", fmttr.GetLocation()));
		}
	}

	struct CommonWidgetsStaticData
	{
		std::string _fallbackFont = "Petra:16";
		std::string _editBoxFont = "DosisBook:16";
		std::string _buttonFont = "DosisExtraBold:20";
		std::string _headingFont = "DosisExtraBold:20";
		std::string _sectionHeaderFont = "DosisExtraBold:16";

		unsigned _keyIndicatorBorderWeight = 4;
		ColorB _keyIndicatorHighlight = ColorB{0xff35376e};

		ColorB _checkboxCheckedColor = ColorB{191, 123, 0};
		ColorB _checkboxUncheckedColor = ColorB{38, 38, 38};
		float _checkboxRounding = 0.33f;
		float _checkboxCheckWeight = 4.f;

		float _xButtonWeight = 1.5f;
		float _xButtonSize = 3.f;

		unsigned _leftRightLabelsHorizontalMargin = 20;

		CommonWidgetsStaticData() = default;

		template<typename Formatter>
			CommonWidgetsStaticData(Formatter& fmttr)
		{
			uint64_t keyname;
			while (TryKeyedItem(fmttr, keyname)) {
				switch (keyname) {
				case "FallbackFont"_h: _fallbackFont = Formatters::RequireStringValue(fmttr).AsString(); break;
				case "EditBoxFont"_h: _editBoxFont = Formatters::RequireStringValue(fmttr).AsString(); break;
				case "ButtonFont"_h: _buttonFont = Formatters::RequireStringValue(fmttr).AsString(); break;
				case "HeadingFont"_h: _headingFont = Formatters::RequireStringValue(fmttr).AsString(); break;
				case "SectionHeaderFont"_h: _sectionHeaderFont = Formatters::RequireStringValue(fmttr).AsString(); break;

				case "KeyIndicatorBorderWeight"_h: _keyIndicatorBorderWeight = Formatters::RequireCastValue<decltype(_keyIndicatorBorderWeight)>(fmttr); break;
				case "KeyIndicatorHighlight"_h: _keyIndicatorHighlight = DeserializeColor(fmttr); break;

				case "CheckboxCheckedColor"_h: _checkboxCheckedColor = DeserializeColor(fmttr); break;
				case "CheckboxUncheckedColor"_h: _checkboxUncheckedColor = DeserializeColor(fmttr); break;
				case "CheckboxRounding"_h: _checkboxRounding = Formatters::RequireCastValue<decltype(_checkboxRounding)>(fmttr); break;
				case "CheckboxCheckWeight"_h: _checkboxCheckWeight = Formatters::RequireCastValue<decltype(_checkboxCheckWeight)>(fmttr); break;

				case "XButtonWeight"_h: _xButtonWeight = Formatters::RequireCastValue<decltype(_xButtonWeight)>(fmttr); break;
				case "XButtonSize"_h: _xButtonSize = Formatters::RequireCastValue<decltype(_xButtonSize)>(fmttr); break;

				case "LeftRightHorizontalMargin"_h: _leftRightLabelsHorizontalMargin = Formatters::RequireCastValue<decltype(_leftRightLabelsHorizontalMargin)>(fmttr); break;

				default: SkipValueOrElement(fmttr); break;
				}
			}
		}
	};

	void DefaultFontsBox::ConstructToPromise(std::promise<std::shared_ptr<DefaultFontsBox>>&& promise)
	{
		auto marker = ::Assets::GetAssetMarker<EntityInterface::MountedData<CommonWidgetsStaticData>>("cfg/displays/commonwidgets");
		::Assets::WhenAll(marker).Then(
			[promise=std::move(promise)](auto futureStaticData) mutable {
				CommonWidgetsStaticData staticData;
				::Assets::DependencyValidation depVal;
				TRY {
					auto sd = futureStaticData.get();
					staticData = sd.get();
					depVal = sd.GetDependencyValidation();
				} CATCH(...) {
				} CATCH_END

				TRY {
					::Assets::WhenAll(
						RenderOverlays::MakeFont(staticData._fallbackFont),
						RenderOverlays::MakeFont(staticData._editBoxFont),
						RenderOverlays::MakeFont(staticData._buttonFont),
						RenderOverlays::MakeFont(staticData._headingFont),
						RenderOverlays::MakeFont(staticData._sectionHeaderFont)).ThenConstructToPromise(
							std::move(promise),
							[depVal = std::move(depVal)](auto f0, auto f1, auto f2, auto f3, auto f4) mutable {
								return std::make_shared<DefaultFontsBox>(std::move(f0), std::move(f1), std::move(f2), std::move(f3), std::move(f4), std::move(depVal));
							});
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	DefaultFontsBox::DefaultFontsBox(std::shared_ptr<RenderOverlays::Font> fallbackFont, std::shared_ptr<RenderOverlays::Font> editBoxFont, std::shared_ptr<RenderOverlays::Font> buttonFont, std::shared_ptr<RenderOverlays::Font> headingFont, std::shared_ptr<RenderOverlays::Font> sectionHeaderFont, ::Assets::DependencyValidation depVal)
	: _fallbackFont(std::move(fallbackFont)), _editBoxFont(std::move(editBoxFont)), _buttonFont(std::move(buttonFont)), _headingFont(std::move(headingFont)), _sectionHeaderFont(std::move(sectionHeaderFont)), _depVal(std::move(depVal))
	{}

	DefaultFontsBox::DefaultFontsBox()
	{
		_fallbackFont = _editBoxFont = _buttonFont = _headingFont = _sectionHeaderFont = MakeDummyFont();
	}

	DefaultFontsBox& DefaultFontsBox::Get()
	{
		auto* p = ::Assets::GetAssetMarkerPtr<DefaultFontsBox>()->TryActualize();
		if (p) return *p->get();

		static DefaultFontsBox fallback;
		return fallback;
	}

	void DefaultFontsBox::StallUntilReady()
	{
		::Assets::GetAssetMarkerPtr<DefaultFontsBox>()->StallWhilePending();
	}

	void Styler::SectionHeader(DrawContext& context, Rect rectangle, StringSection<> name, bool expanded) const
	{
		using namespace DebuggingDisplay;
		ImmediateLayout layout { rectangle, ImmediateLayout::Direction::Row };
		layout._paddingInternalBorder = 0;
		auto flipperRect = layout.Allocate(14);
		auto titleRect = layout.Allocate(layout.GetSpaceRemaining());

		if (!expanded) {
			auto flipperRectCenter = (flipperRect._topLeft+flipperRect._bottomRight)/2;
			Float2 arrows[] = {
				flipperRectCenter + Coord2( 4,  0),
				flipperRectCenter + Coord2(-4, -4),
				flipperRectCenter + Coord2(-4, +4)
			};
			ColorB arrowColors[] { ColorB::White, ColorB::White, ColorB::White };
			FillTriangles(context.GetContext(), arrows, arrowColors, dimof(arrows)/3);
		} else {
			auto flipperRectCenter = (flipperRect._topLeft+flipperRect._bottomRight)/2;
			Float2 arrows[] = {
				flipperRectCenter + Coord2( 0,  4),
				flipperRectCenter + Coord2(+4, -4),
				flipperRectCenter + Coord2(-4, -4)
			};
			ColorB arrowColors[] { ColorB::White, ColorB::White, ColorB::White };
			FillTriangles(context.GetContext(), arrows, arrowColors, dimof(arrows)/3);
		}

		DrawText().Alignment(TextAlignment::Left).Font(*_fonts->_sectionHeaderFont).Draw(context.GetContext(), titleRect, name);
	}

	void Styler::XToggleButton(DrawContext& context, const Rect& xBoxRect) const
	{
		using namespace DebuggingDisplay;
		auto xBoxCenter = Float2(xBoxRect._topLeft+xBoxRect._bottomRight) / 2;
		OutlineRectangle(context.GetContext(), Rect{xBoxCenter-Coord2{6, 10}, xBoxCenter+Coord2{6, 10}}, ColorB{80, 80, 80});
		// maybe faster way to do this, since we just want a couple of lines
		auto size = _staticData->_xButtonSize;
		Float2 xBox0[] = { xBoxCenter + Float2{ -size, -size, }, xBoxCenter + Float2{  size,  size, } };
		Float2 xBox1[] = { xBoxCenter + Float2{  size, -size, }, xBoxCenter + Float2{ -size,  size, } };
		SolidLine(context.GetContext(), MakeIteratorRange(xBox0), ColorB{0x7f, 0x7f, 0x7f}, _staticData->_xButtonWeight);
		SolidLine(context.GetContext(), MakeIteratorRange(xBox1), ColorB{0x7f, 0x7f, 0x7f}, _staticData->_xButtonWeight);
	}
	
	void Styler::CheckBox(DrawContext& context, const Rect& content, bool state) const
	{
		using namespace DebuggingDisplay;
		if (state) {
			FillRaisedRoundedRectangle(context.GetContext(), content, _staticData->_checkboxCheckedColor, _staticData->_checkboxRounding);

			Float2 ptB = Float2{ (content._topLeft[0] + content._bottomRight[0]) / 2, LinearInterpolate(content._topLeft[1], content._bottomRight[1], 0.75f) };
			Float2 ptA = (content._topLeft + ptB) / 2;
			Float2 ptC = Float2{ content._bottomRight[0], content._topLeft[1] };
			Float2 lines[] = { ptA, ptB, ptC };
			SolidLine(context.GetContext(), MakeIteratorRange(lines), ColorB{38, 38, 38}, _staticData->_checkboxCheckWeight);
		} else {
			FillDepressedRoundedRectangle(context.GetContext(), content, _staticData->_checkboxUncheckedColor, _staticData->_checkboxRounding);
		}
	}

	void Styler::DisabledStateControl(DrawContext& context, const Rect& rect, StringSection<> name) const
	{
		using namespace DebuggingDisplay;
		OutlineRoundedRectangle(context.GetContext(), rect, ColorB{0x3f, 0x3f, 0x3f}, 1.f, 0.4f);
		DrawText().Color({0x5f, 0x5f, 0x5f}).Alignment(TextAlignment::Center).Draw(context.GetContext(), {rect._topLeft+Coord2{16, 0}, rect._bottomRight-Coord2{16, 0}}, name);
	}

	void Styler::RectangleContainer(DrawContext& context, const Rect& rect) const
	{
		using namespace DebuggingDisplay;
		OutlineRectangle(context.GetContext(), rect, ColorB{0x3f, 0x3f, 0x3f});
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

	void Styler::ButtonBasic(DrawContext& context, const Rect& rect, uint64_t interactable, StringSection<> label) const
	{
		if (!_fonts) return;
		using namespace DebuggingDisplay;
		auto formatting = FormatButton(context.GetInterfaceState(), interactable, s_buttonNormal, s_buttonMouseOver, s_buttonPressed);
		if (formatting._depressed)
			FillDepressedRoundedRectangle(context.GetContext(), rect, formatting._background);
		else
			FillRaisedRoundedRectangle(context.GetContext(), rect, formatting._background);
		DrawText()
			.Alignment(TextAlignment::Center)
			.Color(formatting._foreground)
			.Font(*_fonts->_buttonFont)
			.Draw(context.GetContext(), rect, label);
	}

	void Styler::KeyIndicatorLabel(DrawContext& context, const Rect& frame, const Rect& labelContent, StringSection<> label)
	{
		const Coord arrowWidth = labelContent.Height()/2;
		Coord2 A { frame._topLeft[0] + arrowWidth, frame._topLeft[1] };
		Coord2 B { frame._bottomRight[0], frame._topLeft[1] };
		Coord2 C { frame._bottomRight[0], frame._bottomRight[1] };
		Coord2 D { frame._topLeft[0] + arrowWidth, frame._bottomRight[1] };
		Coord2 E { frame._topLeft[0], (frame._topLeft[1] + frame._bottomRight[1])/2 };

		Float2 triangles[] {
			E, D, A,
			A, D, B,
			B, D, C
		};
		DebuggingDisplay::FillTriangles(context.GetContext(), triangles, _staticData->_keyIndicatorHighlight, dimof(triangles)/3);

		Float2 linePts[] {
			C, D, E, A, B
		};
		SolidLineInset(context.GetContext(), linePts, ColorB::White, _staticData->_keyIndicatorBorderWeight);

		DrawText().Color(ColorB::White).Draw(context.GetContext(), labelContent, label);
	}

	void Styler::KeyIndicatorKey(DrawContext& context, const Rect& frame, const Rect& labelContent, StringSection<> label)
	{
		const Coord arrowWidth = labelContent.Height()/2;
		Coord2 A { frame._topLeft[0] + arrowWidth, frame._topLeft[1] };
		Coord2 B { frame._bottomRight[0], frame._topLeft[1] };
		Coord2 C { frame._bottomRight[0] - arrowWidth, (frame._topLeft[1] + frame._bottomRight[1])/2 };
		Coord2 D { frame._bottomRight[0], frame._bottomRight[1] };
		Coord2 E { frame._topLeft[0] + arrowWidth, frame._bottomRight[1] };
		Coord2 F { frame._topLeft[0], (frame._topLeft[1] + frame._bottomRight[1])/2 };

		Float2 triangles[] {
			B, A, C,
			C, A, F,
			F, E, C,
			C, E, D
		};
		DebuggingDisplay::FillTriangles(context.GetContext(), triangles, ColorB::White, dimof(triangles)/3);

		DrawText().Color(ColorB::Black).Draw(context.GetContext(), labelContent, label);
	}

	struct KeyIndicatorBreakdown
	{
		Rect _labelFrame, _labelContent;
		Rect _keyFrame, _keyContent;
	};

	struct KeyIndicatorPrecalculatedData
	{
		std::string _fitLabel, _fitKey;
		Coord _keyWidth;
	};

	static KeyIndicatorBreakdown BuildKeyIndicatorBreakdown(Coord width, Coord height, Coord keyWidth, const CommonWidgetsStaticData& staticData)
	{
		const unsigned arrowWidth = height / 2;
		const unsigned hpadding = 2;
		const unsigned vpadding = 2;
		const unsigned borderWeight = staticData._keyIndicatorBorderWeight;

		KeyIndicatorBreakdown result;
		result._labelFrame =
			{
				{ 0, 0 },
				{ width - arrowWidth - keyWidth - 2*hpadding, height }
			};

		result._labelContent =
			{
				{ arrowWidth + hpadding, borderWeight + vpadding },
				{ result._labelFrame._bottomRight[0] - arrowWidth - hpadding, height - borderWeight - vpadding }
			};

		result._keyFrame = 
			{
				{ width - 2 * arrowWidth - 2 * hpadding - keyWidth, 0 },
				{ width, height },
			};

		result._keyContent =
			{
				{ result._keyFrame._topLeft[0] + arrowWidth + hpadding, borderWeight + vpadding },
				{ result._keyFrame._bottomRight[0] - arrowWidth - hpadding, height - borderWeight - vpadding }

			};

		return result;
	}

	void Styler::KeyIndicator(DrawContext& context, const Rect& frame, const void* precalculatedData)
	{
		auto& precalc = *(const KeyIndicatorPrecalculatedData*)precalculatedData;
		auto breakdown = BuildKeyIndicatorBreakdown(frame.Width(), frame.Height(), precalc._keyWidth, *_staticData);
		breakdown._labelFrame._topLeft += frame._topLeft;
		breakdown._labelFrame._bottomRight += frame._topLeft;
		breakdown._labelContent._topLeft += frame._topLeft;
		breakdown._labelContent._bottomRight += frame._topLeft;
		breakdown._keyFrame._topLeft += frame._topLeft;
		breakdown._keyFrame._bottomRight += frame._topLeft;
		breakdown._keyContent._topLeft += frame._topLeft;
		breakdown._keyContent._bottomRight += frame._topLeft;
		KeyIndicatorLabel(context, breakdown._labelFrame, breakdown._labelContent, precalc._fitLabel);
		KeyIndicatorKey(context, breakdown._keyFrame, breakdown._keyContent, precalc._fitKey);
	}

	Styler& Styler::Get()
	{
		auto* p = ::Assets::GetAssetMarkerPtr<Styler>()->TryActualize();
		if (p) return *p->get();

		static Styler fallback;		// note -- relying on compiler threadsafe protections
		return fallback;
	}

	void Styler::StallUntilReady()
	{
		// hack -- just wait for this to be completed
		::Assets::GetAssetMarkerPtr<Styler>()->StallWhilePending();
	}

	unsigned Styler::GetLeftRightLabelsHorizontalMargin() const { return _staticData->_leftRightLabelsHorizontalMargin; }

	Styler::Styler(
		std::shared_ptr<DefaultFontsBox> fonts,
		const CommonWidgetsStaticData& staticData,
		::Assets::DependencyValidation depVal)
	: _fonts(std::move(fonts)), _depVal(std::move(depVal))
	{
		_staticData = std::make_unique<CommonWidgetsStaticData>(staticData);
	}

	Styler::Styler()
	{
		_fonts = std::make_shared<DefaultFontsBox>();
		_staticData = std::make_unique<CommonWidgetsStaticData>();
	}

	std::shared_future<std::shared_ptr<Styler>> Styler::GetFuture()
	{
		return ::Assets::GetAssetMarkerPtr<Styler>()->ShareFuture();
	}

	::Assets::PtrToMarkerPtr<Styler> Styler::GetMarker()
	{
		return ::Assets::GetAssetMarkerPtr<Styler>();
	}

	std::shared_ptr<Styler> Styler::CreateSync()
	{
		auto marker = ::Assets::GetAssetMarkerPtr<Styler>();
		marker->StallWhilePending();
		return marker->Actualize();
	}

	void Styler::ConstructToPromise(std::promise<std::shared_ptr<Styler>>&& promise)
	{
		::Assets::WhenAll(
			::Assets::GetAssetMarkerPtr<DefaultFontsBox>(),
			::Assets::GetAssetMarker<EntityInterface::MountedData<CommonWidgetsStaticData>>("cfg/displays/commonwidgets")).ThenConstructToPromise(
				std::move(promise),
				[](auto&& defaultFonts, auto&& staticData) {
					::Assets::DependencyValidationMarker markers[] {
						defaultFonts->GetDependencyValidation(),
						staticData.GetDependencyValidation()
					};
					return std::make_shared<Styler>(std::move(defaultFonts), std::move(staticData), ::Assets::GetDepValSys().MakeOrReuse(markers));
				});
	}

	Styler::MeasuredRectangle Styler::MeasureKeyIndicator(StringSection<> label, StringSection<> key)
	{
		auto labelWidth = StringWidth(*_fonts->_buttonFont, label);
		auto keyWidth = StringWidth(*_fonts->_buttonFont, key);

		const unsigned hpadding = 2;
		const unsigned vpadding = 2;
		const unsigned borderWeight = _staticData->_keyIndicatorBorderWeight;
		const unsigned height = _fonts->_buttonFont->GetFontProperties()._lineHeight + 2*vpadding + 2*borderWeight;
		const unsigned arrowWidth = height / 2;

		MeasuredRectangle result;
		result._minHeight = result._height = height;
		result._minWidth = 4 * hpadding + 3 * arrowWidth + keyWidth;
		result._width = result._minWidth + labelWidth;
		return result;
	}

	std::shared_ptr<void> Styler::MeasureKeyIndicator_Precalculate(Coord width, Coord height, StringSection<> label, StringSection<> key)
	{
		auto result = std::make_shared<KeyIndicatorPrecalculatedData>();
		result->_fitKey = key.AsString();
		result->_keyWidth = StringWidth(*_fonts->_buttonFont, key);

		auto breakdown = BuildKeyIndicatorBreakdown(width, height, result->_keyWidth, *_staticData);

		VLA(char, buffer, label.Length()+4);
		StringEllipsis(buffer, label.Length()+4, *_fonts->_buttonFont, label, breakdown._labelContent.Width());
		result->_fitLabel = buffer;
		return result;
	}

	constexpr auto left       = "left"_key;
	constexpr auto right      = "right"_key;
	constexpr auto home       = "home"_key;
	constexpr auto end        = "end"_key;
	constexpr auto backspace  = "backspace"_key;
	constexpr auto del        = "delete"_key;
	constexpr auto up         = "up"_key;
	constexpr auto down       = "down"_key;
	constexpr auto tab        = "tab"_key;
	constexpr auto shift      = "shift"_key;

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
		PlatformRig::ProcessInputResult TextEntry<CharType>::ProcessInput(
			DebuggingDisplay::InterfaceState& interfaceState, const OSServices::InputSnapshot& input,
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

		return consume ? PlatformRig::ProcessInputResult::Consumed : PlatformRig::ProcessInputResult::Passthrough;
	}

	template<typename CharType>
		void TextEntry<CharType>::Reset(const std::basic_string<CharType>& currentLine)
	{
		_currentLine = currentLine;
		_caret = _selectionEnd = _currentLine.size();
		_selectionStart = 0;
	}

	template class TextEntry<char>;

	void    Render(
		IOverlayContext& context, const Rect& entryBoxArea, const std::shared_ptr<Font>& font, const TextEntry<>& textEntry,
		ColorB textColor, ColorB caretColor, ColorB selectionColor)
	{
		using namespace DebuggingDisplay;
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



