// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineAcceleratorDisplay.h"
#include "../TopBar.h"
#include "../ThemeStaticData.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/ShapesInternal.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../RenderOverlays/OverlayEffects.h"
#include "../../Assets/Marker.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StreamUtils.h"

#include "../../Formatters/IDynamicFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Tools/EntityInterface/MountedData.h"

using namespace PlatformRig::Literals;
using namespace Utility::Literals;

namespace PlatformRig { namespace Overlays
{
    class PipelineAcceleratorPoolDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		PipelineAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators);
		~PipelineAcceleratorPoolDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const OSServices::InputSnapshot& input) override;
		
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _screenHeadingFont;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _tabLabelsFont;

		RenderOverlays::DebuggingDisplay::ScrollBar _scrollBar;
		float _paScrollOffset = 0.f;
		float _cfgScrollOffset = 0.f;
		unsigned _tab = 0;
	};

	struct ComboBarStaticData
	{
		int _lineWidth = 2;
		int _bracketLength = 8;
		RenderOverlays::ColorB _selectionHighlight = 0xff828690;
		RenderOverlays::ColorB _lineColor = 0xffefe9d9;
		int _labelPadding = 6;

		ComboBarStaticData() = default;
		template<typename Formatter>
            ComboBarStaticData(Formatter& fmttr)
        {
            uint64_t keyname;
            while (fmttr.TryKeyedItem(keyname)) {
                switch (keyname) {
                case "LineWidth"_h: _lineWidth = Formatters::RequireCastValue<decltype(_lineWidth)>(fmttr); break;
                case "BracketLength"_h: _bracketLength = Formatters::RequireCastValue<decltype(_bracketLength)>(fmttr); break;
                case "LineColor"_h: _lineColor = DeserializeColor(fmttr); break;
                case "SelectionHighlight"_h: _selectionHighlight = DeserializeColor(fmttr); break;
				case "LabelPadding"_h: _labelPadding = Formatters::RequireCastValue<decltype(_labelPadding)>(fmttr); break;
                default: SkipValueOrElement(fmttr); break;
                }
            }
        }
	};

	static void ComboBar(
		IteratorRange<RenderOverlays::Rect*> result,
		RenderOverlays::IOverlayContext& context,
		RenderOverlays::Rect outerRect,
		IteratorRange<const unsigned*> buttonWidths,
		unsigned activeHighlight)
	{
		// Find the spacing required to make this work...
		assert(!buttonWidths.empty());
		assert(buttonWidths.size() == result.size());
		auto& staticData = EntityInterface::MountedData<ComboBarStaticData>::LoadOrDefault("cfg/displays/combobar");
		unsigned totalButtonWidth = 0, minimalWidth = 0;
		auto tiltWidth = outerRect.Height() / 4;
		for (auto w:buttonWidths) {
			w = std::max(w, (unsigned)staticData._bracketLength);
			totalButtonWidth += (tiltWidth + staticData._lineWidth + staticData._labelPadding) * 2 + w;
			minimalWidth += (tiltWidth + staticData._lineWidth) * 2 + staticData._bracketLength;
		}
		if (outerRect.Width() < minimalWidth) {
			for (auto& f:result) f = RenderOverlays::Rect::Invalid();
			return;		// too small to render
		}

		auto spacing = 0;
		if (buttonWidths.size() > 1 && totalButtonWidth < outerRect.Width())
			spacing = (outerRect.Width() - totalButtonWidth) / (buttonWidths.size()-1);
		
		int horzIterator = outerRect._topLeft[0];
		int lastMidLine = 0;
		for (unsigned c=0; c<buttonWidths.size(); ++c) {

			auto w = buttonWidths[c] + 2 * staticData._labelPadding;
			w = std::max(w, (unsigned)staticData._bracketLength);
			if (totalButtonWidth > outerRect.Width()) {
				// We have to shrink at least some of the buttons -- we'll do so proportionally
				auto totalResizeableWidth = totalButtonWidth - minimalWidth;
				float prop = (w - staticData._bracketLength) / float(totalResizeableWidth);
				w -= (totalButtonWidth - outerRect.Width()) * prop;
				assert(w > staticData._bracketLength);
			}

			auto leftX0 = horzIterator + staticData._lineWidth/2;
			auto leftX1 = leftX0 + tiltWidth;
			auto rightX0 = horzIterator + (tiltWidth + staticData._lineWidth) + w + staticData._lineWidth/2;
			auto rightX1 = rightX0 + tiltWidth;
			if (c == buttonWidths.size()-1) {
				// resolve errors from integer floors by aligning right edge
				rightX1 = outerRect._bottomRight[0] - staticData._lineWidth/2;
				rightX0 = rightX1 - tiltWidth;
			}

			if (activeHighlight == c) {
				Coord2 highlight[] {
					Coord2 { leftX0, outerRect._bottomRight[1] },
					Coord2 { rightX0, outerRect._bottomRight[1] },
					Coord2 { leftX1, outerRect._topLeft[1] },

					Coord2 { leftX1, outerRect._topLeft[1] },
					Coord2 { rightX0, outerRect._bottomRight[1] },
					Coord2 { rightX1, outerRect._topLeft[1] },
				};
				RenderOverlays::DebuggingDisplay::FillTriangles(context, highlight, staticData._selectionHighlight, 2);
			}

			Float2 leftButtonFrame[] {
				Float2{leftX0 + staticData._bracketLength, outerRect._bottomRight[1]},
				Float2{leftX0, outerRect._bottomRight[1]},
				Float2{leftX1, outerRect._topLeft[1]},
				Float2{leftX1 + staticData._bracketLength, outerRect._topLeft[1]}
			};
			Float2 rightButtonFrame[] {
				Float2{rightX0 - staticData._bracketLength, outerRect._bottomRight[1]},
				Float2{rightX0, outerRect._bottomRight[1]},
				Float2{rightX1, outerRect._topLeft[1]},
				Float2{rightX1 - staticData._bracketLength, outerRect._topLeft[1]}
			};
			RenderOverlays::SolidLine(context, leftButtonFrame, staticData._lineColor, staticData._lineWidth);
			RenderOverlays::SolidLine(context, rightButtonFrame, staticData._lineColor, staticData._lineWidth);

			if (c != 0) {
				Float2 midLine[] {
					Float2{lastMidLine, (outerRect._topLeft[1] + outerRect._bottomRight[1])/2},
					Float2{leftX0 + tiltWidth/2, (outerRect._topLeft[1] + outerRect._bottomRight[1])/2}
				};
				RenderOverlays::SolidLine(context, midLine, staticData._lineColor, staticData._lineWidth);
			}
			lastMidLine = rightX0 + tiltWidth/2;

			result[c] = {
				{ leftX0 + staticData._lineWidth/2 + tiltWidth + staticData._labelPadding, outerRect._topLeft[1] },
				{ rightX0 - staticData._lineWidth/2 - staticData._labelPadding, outerRect._bottomRight[1] }
			};

			horzIterator += w + 2 * (tiltWidth + staticData._lineWidth);
			horzIterator += spacing;

		}
	}

	static std::string WordWrapString(const RenderOverlays::Font& fnt, StringSection<> str, float maxWidth)
	{
		return StringSplitByWidth(fnt, str, maxWidth, MakeStringSection(" \t"), MakeStringSection("")).Concatenate();
	}

	const char* s_tabNames[] = { "1. pipeline-accelerators", "2. sequencer-configs", "3. stats" };

	void    PipelineAcceleratorPoolDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays;
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		if (auto* topBar = context.GetService<ITopBarManager>()) {
			const char headingString[] = "Pipeline Accelerators";
			if (auto* headingFont = _screenHeadingFont->TryActualize()) {
				auto rect = topBar->ScreenTitle(context, layout, StringWidth(**headingFont, MakeStringSection(headingString)));
				if (IsGood(rect) && headingFont)
					DrawText()
						.Font(**headingFont)
						.Color(ColorB::Black)
						.Alignment(RenderOverlays::TextAlignment::Left)
						.Flags(0)
						.Draw(context, rect, headingString);
			}
		}

		auto& themeStaticData = EntityInterface::MountedData<ThemeStaticData>::LoadOrDefault("cfg/displays/theme");

		if (auto* blurryBackground = context.GetService<BlurryBackgroundEffect>()) {
			ColorAdjust colAdj;
			colAdj._luminanceOffset = 0.025f; colAdj._saturationMultiplier = 0.65f;
			auto outerRect = Layout{layout}.AllocateFullWidthFraction(1.f);

			SoftShadowRectangle(
				context,
				{outerRect._topLeft + Coord2(themeStaticData._shadowOffset0, themeStaticData._shadowOffset0), outerRect._bottomRight + Coord2(themeStaticData._shadowOffset1, themeStaticData._shadowOffset1)},
				themeStaticData._shadowSoftnessRadius);

			ColorAdjustRectangle(
				context, outerRect,
				blurryBackground->AsTextureCoords(outerRect._topLeft), blurryBackground->AsTextureCoords(outerRect._bottomRight),
				blurryBackground->GetResourceView(RenderOverlays::BlurryBackgroundEffect::Type::NarrowAccurateBlur),
				colAdj, themeStaticData._semiTransparentTint);
		} else
			FillRectangle(context, Layout{layout}.AllocateFullWidthFraction(1.f), RenderOverlays::ColorB { 0, 0, 0, 145 });

		// inset a little bit
		layout = Layout{layout.AllocateFullWidthFraction(1.f)};
		layout._maximumSize._topLeft += Coord2(6,6);
		layout._maximumSize._bottomRight -= Coord2(6,6);

		if (auto* tabLabelsFont = _tabLabelsFont->TryActualize()) {
			unsigned labelWidths[dimof(s_tabNames)];
			for (unsigned c=0; c<dimof(s_tabNames); ++c)
				labelWidths[c] = (unsigned)StringWidth(**tabLabelsFont, MakeStringSection(s_tabNames[c]));
			Rect tabLabelRects[dimof(s_tabNames)];
			ComboBar(MakeIteratorRange(tabLabelRects), context, layout.AllocateFullWidth(2*lineHeight), MakeIteratorRange(labelWidths), _tab);
			for (unsigned c=0; c<dimof(s_tabNames); ++c)
				if (IsGood(tabLabelRects[c]) && tabLabelRects[c].Width() >= labelWidths[c]) {
					DrawText()
						.Font(**tabLabelsFont)
						.Color(0xffc1c9ef)
						.Alignment(RenderOverlays::TextAlignment::Center)
						.Flags(0)
						.Draw(context, tabLabelRects[c], s_tabNames[c]);

	 				interactables.Register({tabLabelRects[c], InteractableId_Make(s_tabNames[c])});
				}
		}

		auto records = _pipelineAccelerators->LogRecords();
		if (_tab == 0 || _tab == 1) {
			auto oldBetweenAllocations = layout._paddingBetweenAllocations;
			layout._paddingBetweenAllocations = 0;
			Layout tableArea = layout.AllocateFullHeight(layout.GetWidthRemaining() - layout._paddingInternalBorder);
			tableArea._paddingInternalBorder = 0;
			tableArea._paddingBetweenAllocations = 0;
			layout._paddingBetweenAllocations = oldBetweenAllocations;
			unsigned entryCount = 0, sourceEntryCount = 0;

			Rect scrollBarLocation = Rect::Invalid();

			Font* tableValuesFont = nullptr;
			if (auto* table = RenderOverlays::Internal::TryGetDefaultFontsBox())
				tableValuesFont = table->_tableValuesFont.get();
			
			auto tableValueInteractableId = InteractableId_Make("TableValue");

            std::vector<Float3> lines;
			lines.reserve(records._pipelineAccelerators.size()*2);
			if (_tab == 0) {
				unsigned matSelectorsWidth = 750, geoSelectorsWidth = 1000;
				std::pair<std::string, unsigned> headers0[] = { 
					std::make_pair("patches", 190), std::make_pair("ia", 190), std::make_pair("states", 140),
					std::make_pair("mat-selectors", matSelectorsWidth),
					std::make_pair("geo-selectors", geoSelectorsWidth) };

				auto headersHeight = DrawTableHeaders(context, tableArea.GetMaximumSize(), MakeIteratorRange(headers0));
				tableArea.AllocateFullWidth(headersHeight);
				Rect baseRect = tableArea.GetMaximumSize();
				baseRect._topLeft[1] = baseRect._bottomRight[1] - headersHeight/2;
				DrawTableBase(context, baseRect);
				scrollBarLocation = DrawEmbeddedInRightEdge(context, tableArea.GetMaximumSize());
				tableArea._maximumSize._bottomRight[1] -= headersHeight;

				for (const auto& r:records._pipelineAccelerators) {
					if (entryCount < _paScrollOffset) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
					entries["patches"] = (StringMeld<32>() << std::hex << r._shaderPatchesHash).AsString();
					entries["states"] = (StringMeld<32>() << std::hex << r._stateSetHash).AsString();
					entries["ia"] = (StringMeld<32>() << std::hex << r._inputAssemblyHash).AsString();
					if (tableValuesFont) {
						entries["mat-selectors"] = WordWrapString(*tableValuesFont, r._materialSelectors, headers0[3].second);
						entries["geo-selectors"] = WordWrapString(*tableValuesFont, r._geoSelectors, headers0[4].second);
					}
					Layout sizingLayout = tableArea;
					auto rect = sizingLayout.AllocateFullWidthFraction(1.f);
					if (rect.Height() <= 0) break;
					auto usedSpace = DrawTableEntry(context, rect, MakeIteratorRange(headers0), entries, interfaceState.TopMostId() == tableValueInteractableId+entryCount);
					auto usedArea = tableArea.AllocateFullWidth(usedSpace);
					auto lineRect = tableArea.AllocateFullWidth(16);
					usedArea._bottomRight = lineRect._bottomRight;
					lines.push_back(AsPixelCoords(Coord2(lineRect._topLeft[0]+8, lineRect._topLeft[1]+4)));
					lines.push_back(AsPixelCoords(Coord2(lineRect._bottomRight[0]-8, lineRect._topLeft[1]+4)));

					interactables.Register({usedArea, tableValueInteractableId+entryCount});

					++entryCount;
				}
				sourceEntryCount = (unsigned)records._pipelineAccelerators.size();

			} else if (_tab == 1) {
				std::pair<std::string, unsigned> headers0[] = {
					std::make_pair("name", 250), std::make_pair("fb-relevance", 190),
					std::make_pair("sequencer-selectors", 3000),
				};

				auto headersHeight = DrawTableHeaders(context, tableArea.GetMaximumSize(), MakeIteratorRange(headers0));
				tableArea.AllocateFullWidth(headersHeight);
				Rect baseRect = tableArea.GetMaximumSize();
				baseRect._topLeft[1] = baseRect._bottomRight[1] - headersHeight/2;
				DrawTableBase(context, baseRect);
				scrollBarLocation = DrawEmbeddedInRightEdge(context, tableArea.GetMaximumSize());
				tableArea._maximumSize._bottomRight[1] -= headersHeight;

				for (const auto& cfg:records._sequencerConfigs) {
					if (entryCount < _cfgScrollOffset) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
					entries["name"] = cfg._name;
					entries["fb-relevance"] = (StringMeld<32>() << std::hex << cfg._fbRelevanceValue).AsString();
					if (tableValuesFont)
						entries["sequencer-selectors"] = WordWrapString(*tableValuesFont, cfg._sequencerSelectors, headers0[2].second);
					Layout sizingLayout = tableArea;
					auto rect = sizingLayout.AllocateFullWidthFraction(1.f);
					if (rect.Height() <= 0) break;
					auto usedSpace = DrawTableEntry(context, rect, MakeIteratorRange(headers0), entries, interfaceState.TopMostId() == tableValueInteractableId+entryCount);
					auto usedArea = tableArea.AllocateFullWidth(usedSpace);
					auto lineRect = tableArea.AllocateFullWidth(16);
					usedArea._bottomRight = lineRect._bottomRight;
					lines.push_back(AsPixelCoords(Coord2(lineRect._topLeft[0]+8, lineRect._topLeft[1]+4)));
					lines.push_back(AsPixelCoords(Coord2(lineRect._bottomRight[0]-8, lineRect._topLeft[1]+4)));

					interactables.Register({usedArea, tableValueInteractableId+entryCount});

					++entryCount;
				}
				sourceEntryCount = (unsigned)records._pipelineAccelerators.size();
			}

			// context.DrawLines(RenderOverlays::ProjectionMode::P2D, lines.data(), lines.size(), RenderOverlays::ColorB::White);

			if (IsGood(scrollBarLocation)) {
				auto& scrollOffset = (_tab == 0) ? _paScrollOffset : _cfgScrollOffset;
				ScrollBar::Coordinates scrollCoordinates(scrollBarLocation, 0.f, sourceEntryCount, entryCount-(unsigned)scrollOffset);
				scrollOffset = _scrollBar.CalculateCurrentOffset(scrollCoordinates, scrollOffset);
				DrawScrollBar(context, scrollCoordinates, scrollOffset, interfaceState.HasMouseOver(_scrollBar.GetID()) ? RenderOverlays::ColorB(120, 120, 120) : RenderOverlays::ColorB(51, 51, 51));
				interactables.Register({scrollCoordinates.InteractableRect(), _scrollBar.GetID()});
			}

		} else if (_tab == 2) {
			DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Pipeline accelerator count: %u", (unsigned)records._pipelineAccelerators.size());
			DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Sequencer config count: %u", (unsigned)records._sequencerConfigs.size());
			DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Descriptor set accelerator count: %u", (unsigned)records._descriptorSetAcceleratorCount);
			DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Metal pipeline count: %u", (unsigned)records._metalPipelineCount);
			DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Pipeline layout count: %u", (unsigned)records._pipelineLayoutCount);
		}
	}

	auto    PipelineAcceleratorPoolDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
	{
		using namespace RenderOverlays::DebuggingDisplay;
		if (_scrollBar.ProcessInput(interfaceState, input) == ProcessInputResult::Consumed)
			return ProcessInputResult::Consumed;

		constexpr auto pgdn       = "page down"_key;
        constexpr auto pgup       = "page up"_key;
		if (_tab == 0) {
			if (input.IsPress(pgdn)) _paScrollOffset += 1.f;
			if (input.IsPress(pgup)) _paScrollOffset = std::max(0.f, _paScrollOffset-1.f);
		} else if (_tab == 1) {
			if (input.IsPress(pgdn)) _cfgScrollOffset += 1.f;
			if (input.IsPress(pgup)) _cfgScrollOffset = std::max(0.f, _cfgScrollOffset-1.f);
		}

		if (input._pressedChar == '1') _tab = 0;
		else if (input._pressedChar == '2') _tab = 1;
		else if (input._pressedChar == '3') _tab = 2;

		auto topMostWidget = interfaceState.TopMostId();
		if (topMostWidget)
			for (unsigned t=0; t<dimof(s_tabNames); ++t)
				if (topMostWidget == InteractableId_Make(s_tabNames[t])) {
					if (input.IsRelease_LButton())
						_tab = t;
					return ProcessInputResult::Consumed;
				}

		return ProcessInputResult::Passthrough;
	}

	PipelineAcceleratorPoolDisplay::PipelineAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators)
    : _pipelineAccelerators(std::move(pipelineAccelerators))
	{
		_paScrollOffset = _cfgScrollOffset = 0.f;
		_tab = 0;
		auto scrollBarId = RenderOverlays::DebuggingDisplay::InteractableId_Make("PipelineAccelerators_ScrollBar");
		scrollBarId += IntegerHash64((uint64_t)this);
		_scrollBar = RenderOverlays::DebuggingDisplay::ScrollBar(scrollBarId);
		_screenHeadingFont = RenderOverlays::MakeFont("OrbitronBlack", 20);
		_tabLabelsFont = RenderOverlays::MakeFont("Petra", 20);
	}

	PipelineAcceleratorPoolDisplay::~PipelineAcceleratorPoolDisplay()
	{
	}

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePipelineAcceleratorPoolDisplay(
        std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators)
    {
        return std::make_shared<PipelineAcceleratorPoolDisplay>(std::move(pipelineAccelerators));
    }

}}

