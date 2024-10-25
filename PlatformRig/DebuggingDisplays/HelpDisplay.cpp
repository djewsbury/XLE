// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HelpDisplay.h"
#include "../TopBar.h"
#include "../../RenderOverlays/LayoutEngine.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderOverlays/OverlayEffects.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../Assets/Marker.h"
#include "../../Foreign/yoga/yoga/Yoga.h"

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	class HelpDisplay : public IHelpDisplay
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override
		{
			if (auto* topBar = context.GetService<ITopBarManager>()) {
				const char headingString[] = "Key Binding Help";
				if (auto* headingFont = _headingFont->TryActualize()) {
					auto rect = topBar->ScreenTitle(context, layout, StringWidth(**headingFont, MakeStringSectionNullTerm(headingString)));
					if (IsGood(rect) && headingFont)
						DrawText()
							.Font(**headingFont)
							.Color(ColorB::Black)
							.Alignment(RenderOverlays::TextAlignment::Left)
							.Flags(0)
							.Draw(context, rect, headingString);
				}
			}

			if (_layoutInvalidated)
				BuildLayout(1024);

			auto availableSpace = layout.AllocateFullWidthFraction(1.f);
			Coord2 offset {
				std::max(0, (availableSpace._topLeft[0] + availableSpace._bottomRight[0] - _layedOutWidgets._dimensions[0]) / 2),
				std::max(0, (availableSpace._topLeft[1] + availableSpace._bottomRight[1] - _layedOutWidgets._dimensions[1]) / 2),
			};

			Rect frame { offset, offset + _layedOutWidgets._dimensions };
			frame._topLeft -= Coord2(64, 64);
			frame._bottomRight += Coord2(64, 64);

			if (auto* blurryBackground = context.GetService<RenderOverlays::BlurryBackgroundEffect>()) {
				RenderOverlays::ColorAdjust colorAdjust;
				ColorAdjustAndOutlineRoundedRectangle(
					context, frame,
					blurryBackground->AsTextureCoords(frame._topLeft), blurryBackground->AsTextureCoords(frame._bottomRight),
					blurryBackground->GetResourceView(RenderOverlays::BlurryBackgroundEffect::Type::NarrowAccurateBlur),
					colorAdjust, ColorB::White,
					ColorB::White, 8.0f);
			}

			Float3x3 transform { 
				1.f, 0.f, offset[0],
				0.f, 1.f, offset[1],
				0.f, 0.f, 1.f
			};

			DrawContext draw{context, interactables, interfaceState};
			_layedOutWidgets.Draw(draw, transform);
			_lastTransform = transform;
		}

		void AddKey(StringSection<> key, StringSection<> helpText) override
		{
			_keyHelps.emplace_back(KeyHelp{key.AsString(), helpText.AsString()});
			_layoutInvalidated = true;
		}

		void AddText(StringSection<> text) override
		{
			_textBlocks.emplace_back(TextBlock{text.AsString()});
			_layoutInvalidated = true;
		}

		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) override
		{
			if (_lastTransform) {
				CommonWidgets::HoveringLayer hoveringLayer;
				PlatformRig::InputContext inputContext;
				inputContext.AttachService2(interfaceState);
				IOContext ioContext { inputContext, input };
				return _layedOutWidgets.ProcessInput(ioContext, *_lastTransform);
			}
			return ProcessInputResult::Passthrough;
		}

		void BuildLayout(unsigned maxWidth)
		{
			LayoutEngine le;
			auto rootNode = le.NewNode();
			le.PushRoot(rootNode, Rect{{0,0}, {32, 32}});
			YGNodeStyleSetMaxWidth(rootNode, (float)maxWidth);
			YGNodeStyleSetMaxHeight(rootNode, 1440);		// we need to set some maximum height to allow the dimensions returned in the layout to adapt to the children

			YGNodeStyleSetFlexDirection(rootNode, YGFlexDirectionRow);
			YGNodeStyleSetJustifyContent(rootNode, YGJustifyFlexStart);
			YGNodeStyleSetAlignItems(rootNode, YGAlignCenter);

			{
				auto keyContainer = le.NewNode();
				le.InsertChildToStackTop(keyContainer);
				le.PushNode(keyContainer);

				YGNodeStyleSetFlexDirection(keyContainer, YGFlexDirectionColumn);
				YGNodeStyleSetJustifyContent(keyContainer, YGJustifyFlexStart);
				YGNodeStyleSetAlignItems(keyContainer, YGAlignCenter);

				auto& styler = CommonWidgets::Styler::Get();
				for (auto& k:_keyHelps) {
					auto measure0 = styler.MeasureKeyIndicator(k._helpText, k._key);

					auto widget = le.NewImbuedNode(0);
					le.InsertChildToStackTop(*widget);
					YGNodeStyleSetWidth(*widget, (float)measure0._width);
					YGNodeStyleSetMinWidth(*widget, (float)measure0._minWidth);
					YGNodeStyleSetHeight(*widget, (float)measure0._height);
					YGNodeStyleSetMinHeight(*widget, (float)measure0._minHeight);
					YGNodeStyleSetFlexGrow(*widget, 0.f);
					YGNodeStyleSetFlexShrink(*widget, 1.f);
					YGNodeStyleSetMargin(*widget, YGEdgeVertical, 4);

					widget->_nodeAttachments._drawDelegate = [kd=k](DrawContext& drawContext, Rect frame, Rect content) {
						auto& styler = CommonWidgets::Styler::Get();
						auto data = styler.MeasureKeyIndicator_Precalculate(frame.Width(), frame.Height(), kd._helpText, kd._key);
						styler.KeyIndicator(drawContext, frame, data.get());
					};
				}

				le.PopNode();	// keyContainer
			}

			{
				auto textContainer = le.NewNode();
				le.InsertChildToStackTop(textContainer);
				le.PushNode(textContainer);

				YGNodeStyleSetFlexDirection(textContainer, YGFlexDirectionColumn);
				YGNodeStyleSetJustifyContent(textContainer, YGJustifyFlexStart);
				YGNodeStyleSetAlignItems(textContainer, YGAlignFlexStart);
				YGNodeStyleSetMargin(textContainer, YGEdgeHorizontal, 16.f);

				auto fnt = CommonWidgets::DefaultFontsBox::Get()._buttonFont;

				for (auto& t:_textBlocks) {
					auto widget = le.NewImbuedNode(0);
					le.InsertChildToStackTop(*widget);
					YGNodeStyleSetFlexShrink(*widget, 5.f);
					YGNodeStyleSetMargin(*widget, YGEdgeVertical, 12);

					struct DynamicData
					{
						std::string _baseString;
						unsigned _calculatedWidth = 0;
						std::string _wordWrappedString;
					};
					auto dynamicData = std::make_shared<DynamicData>();
					dynamicData->_baseString = t._text;

					widget->_nodeAttachments._drawDelegate = [fnt, dynamicData](DrawContext& drawContext, Rect frame, Rect content) {
						DrawText().Font(*fnt).Draw(drawContext.GetContext(), content, dynamicData->_wordWrappedString);
					};
					widget->_measureDelegate = [fnt, dynamicData](float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
						// hack -- the "measure" behaviour in yoga doesn't work exactly the way we need it to. The final size of the node
						// will typically go outside of it's parent area... so we're just working around it by artificially reducing the 
						// max available width
						const float graceWidth = 256;
						auto split = StringSplitByWidth(*fnt, MakeStringSection(dynamicData->_baseString), std::max(0.f, width - graceWidth), MakeStringSectionLiteral(" \t"), MakeStringSectionLiteral(""));
						dynamicData->_wordWrappedString = split.Concatenate();
						return YGSize {
							(float)split._maxLineWidth,
							split._sections.size() * fnt->GetFontProperties()._lineHeight
						};
					};
				}

				le.PopNode();	// textContainer
			}

			le.PopNode();		// rootNode

			_layedOutWidgets = le.BuildLayedOutWidgets();
			_layoutInvalidated = false;
		}

		HelpDisplay()
		{
			_headingFont = RenderOverlays::MakeFont("OrbitronBlack", 20);
		}

	private:
		struct KeyHelp
		{
			std::string _key;
			std::string _helpText;
		};
		std::vector<KeyHelp> _keyHelps;

		struct TextBlock
		{
			std::string _text;
		};
		std::vector<TextBlock> _textBlocks;

		bool _layoutInvalidated = false;
		LayedOutWidgets _layedOutWidgets;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
		std::optional<Float3x3> _lastTransform;
	};

	IHelpDisplay::~IHelpDisplay() {}

	std::shared_ptr<IHelpDisplay> CreateHelpDisplay()
	{
		return std::make_shared<HelpDisplay>();
	}
}}
