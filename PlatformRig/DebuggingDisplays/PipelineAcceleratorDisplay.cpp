// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineAcceleratorDisplay.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/Font.h"
#include "../../RenderOverlays/OverlayUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"

namespace PlatformRig { namespace Overlays
{
    class PipelineAcceleratorPoolDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		PipelineAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators);
		~PipelineAcceleratorPoolDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		bool    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputContext& inputContext, const PlatformRig::InputSnapshot& input) override;
		
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;

		RenderOverlays::DebuggingDisplay::ScrollBar _scrollBar;
		float _paScrollOffset = 0.f;
		float _cfgScrollOffset = 0.f;
		unsigned _tab = 0;
	};

	static void DrawButton(RenderOverlays::IOverlayContext& context, const char name[], const RenderOverlays::DebuggingDisplay::Rect&buttonRect, RenderOverlays::DebuggingDisplay::Interactables&interactables, RenderOverlays::DebuggingDisplay::InterfaceState& interfaceState)
    {
		using namespace RenderOverlays::DebuggingDisplay;
        InteractableId id = InteractableId_Make(name);
        DrawButtonBasic(context, buttonRect, name, FormatButton(interfaceState, id));
        interactables.Register(Interactables::Widget(buttonRect, id));
    }

	const char* s_tabNames[] = { "pipeline-accelerators", "sequencer-configs", "stats" };

	void    PipelineAcceleratorPoolDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		{
			Layout buttonsLayout(layout.AllocateFullWidth(3*lineHeight));
            for (unsigned t=0; t<dimof(s_tabNames); ++t)
                DrawButton(context, s_tabNames[t], buttonsLayout.AllocateFullHeightFraction(1.f/float(dimof(s_tabNames))), interactables, interfaceState);
		}

    	auto records = _pipelineAccelerators->LogRecords();
		if (_tab == 0 || _tab == 1) {
			Layout tableArea = layout.AllocateFullHeight(layout.GetWidthRemaining() - layout._paddingInternalBorder - 24);
			auto scrollArea = layout.AllocateFullHeight(layout.GetWidthRemaining());
			unsigned entryCount = 0, sourceEntryCount = 0;
                    
			const auto headerColor = RenderOverlays::ColorB::Blue;
            std::vector<Float3> lines;
			lines.reserve(records._pipelineAccelerators.size()*2);
			if (_tab == 0) {
				std::pair<std::string, unsigned> headers0[] = { 
					std::make_pair("Patches", 190), std::make_pair("IA", 190), std::make_pair("States", 100), 
					std::make_pair("MatSelectors", 750),
					std::make_pair("GeoSelectors", 750) };

				DrawTableHeaders(context, tableArea.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), headerColor, &interactables);
				for (const auto& r:records._pipelineAccelerators) {
					if (entryCount < _paScrollOffset) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
					entries["Patches"] = (StringMeld<32>() << std::hex << r._shaderPatchesHash).AsString();
					entries["States"] = (StringMeld<32>() << std::hex << r._stateSetHash).AsString();
					entries["IA"] = (StringMeld<32>() << std::hex << r._inputAssemblyHash).AsString();
					entries["MatSelectors"] = r._materialSelectors;
					entries["GeoSelectors"] = r._geoSelectors;
					Layout sizingLayout = tableArea;
					auto rect = sizingLayout.AllocateFullWidthFraction(1.f);
					if (rect.Height() <= 0) break;
					auto usedSpace = DrawTableEntry(context, rect, MakeIteratorRange(headers0), entries);
					tableArea.AllocateFullWidth(usedSpace);
					auto lineRect = tableArea.AllocateFullWidth(8);
					lines.push_back(AsPixelCoords(Coord2(lineRect._topLeft[0]+8, lineRect._topLeft[1]+4)));
					lines.push_back(AsPixelCoords(Coord2(lineRect._bottomRight[0]-8, lineRect._topLeft[1]+4)));
					++entryCount;
				}
				sourceEntryCount = (unsigned)records._pipelineAccelerators.size();
			} else if (_tab == 1) {
				std::pair<std::string, unsigned> headers0[] = { 
					std::make_pair("Name", 250), std::make_pair("FBRelevance", 190),
					std::make_pair("SeqSelectors", 3000),
				};

				DrawTableHeaders(context, tableArea.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), headerColor, &interactables);
				for (const auto& cfg:records._sequencerConfigs) {
					if (entryCount < _cfgScrollOffset) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
					entries["Name"] = cfg._name;
					entries["FBRelevance"] = (StringMeld<32>() << std::hex << cfg._fbRelevanceValue).AsString();
					entries["SeqSelectors"] = cfg._sequencerSelectors;
					Layout sizingLayout = tableArea;
					auto rect = sizingLayout.AllocateFullWidthFraction(1.f);
					if (rect.Height() <= 0) break;
					auto usedSpace = DrawTableEntry(context, rect, MakeIteratorRange(headers0), entries);
					tableArea.AllocateFullWidth(usedSpace);
					auto lineRect = tableArea.AllocateFullWidth(8);
					lines.push_back(AsPixelCoords(Coord2(lineRect._topLeft[0]+8, lineRect._topLeft[1]+4)));
					lines.push_back(AsPixelCoords(Coord2(lineRect._bottomRight[0]-8, lineRect._topLeft[1]+4)));
					++entryCount;
				}
				sourceEntryCount = (unsigned)records._pipelineAccelerators.size();
			}

			context.DrawLines(RenderOverlays::ProjectionMode::P2D, lines.data(), lines.size(), RenderOverlays::ColorB::White);
			auto& scrollOffset = (_tab == 0) ? _paScrollOffset : _cfgScrollOffset;

			ScrollBar::Coordinates scrollCoordinates(scrollArea, 0.f, sourceEntryCount, entryCount-(unsigned)scrollOffset);
			scrollOffset = _scrollBar.CalculateCurrentOffset(scrollCoordinates, scrollOffset);
			DrawScrollBar(context, scrollCoordinates, scrollOffset, interfaceState.HasMouseOver(_scrollBar.GetID()) ? RenderOverlays::ColorB(120, 120, 120) : RenderOverlays::ColorB(51, 51, 51));
			interactables.Register(Interactables::Widget(scrollCoordinates.InteractableRect(), _scrollBar.GetID()));
		} else if (_tab == 2) {
			DrawFormatText(context, layout.AllocateFullWidth(lineHeight), nullptr, RenderOverlays::ColorB::White, "Pipeline accelerator count: %u", (unsigned)records._pipelineAccelerators.size());
			DrawFormatText(context, layout.AllocateFullWidth(lineHeight), nullptr, RenderOverlays::ColorB::White, "Sequencer config count: %u", (unsigned)records._sequencerConfigs.size());
			DrawFormatText(context, layout.AllocateFullWidth(lineHeight), nullptr, RenderOverlays::ColorB::White, "Descriptor set accelerator count: %u", (unsigned)records._descriptorSetAcceleratorCount);
			DrawFormatText(context, layout.AllocateFullWidth(lineHeight), nullptr, RenderOverlays::ColorB::White, "Metal pipeline count: %u", (unsigned)records._metalPipelineCount);
		}
	}

	bool    PipelineAcceleratorPoolDisplay::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		if (_scrollBar.ProcessInput(interfaceState, inputContext, input))
			return true;

		static KeyId pgdn       = KeyId_Make("page down");
        static KeyId pgup       = KeyId_Make("page up");
		if (_tab == 0) {
			if (input.IsPress(pgdn)) _paScrollOffset += 1.f;
			if (input.IsPress(pgup)) _paScrollOffset = std::max(0.f, _paScrollOffset-1.f);
		} else if (_tab == 1) {
			if (input.IsPress(pgdn)) _cfgScrollOffset += 1.f;
			if (input.IsPress(pgup)) _cfgScrollOffset = std::max(0.f, _cfgScrollOffset-1.f);
		}

		auto topMostWidget = interfaceState.TopMostId();
		if (topMostWidget)
			for (unsigned t=0; t<dimof(s_tabNames); ++t)
				if (topMostWidget == InteractableId_Make(s_tabNames[t])) {
					if (input.IsRelease_LButton())
						_tab = t;
					return true;
				}

		return false;
	}

	PipelineAcceleratorPoolDisplay::PipelineAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators)
    : _pipelineAccelerators(std::move(pipelineAccelerators))
	{
		_paScrollOffset = _cfgScrollOffset = 0.f;
		_tab = 0;
		auto scrollBarId = RenderOverlays::DebuggingDisplay::InteractableId_Make("PipelineAccelerators_ScrollBar");
		scrollBarId += IntegerHash64((uint64_t)this);
		_scrollBar = RenderOverlays::DebuggingDisplay::ScrollBar(scrollBarId);
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

