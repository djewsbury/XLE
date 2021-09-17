// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineAcceleratorDisplay.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/Font.h"
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
		float _scrollOffset = 0.f;
	};

	void    PipelineAcceleratorPoolDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		Layout tableArea = layout.AllocateFullHeight(layout.GetWidthRemaining() - layout._paddingInternalBorder - 24);
		auto scrollArea = layout.AllocateFullHeight(layout.GetWidthRemaining());
        unsigned entryCount = 0, sourceEntryCount = 0;

        {
            auto[pipelineAccelerators, sequencerCfgs] = _pipelineAccelerators->LogRecords();
                    
            const auto headerColor = RenderOverlays::ColorB::Blue;
            std::pair<std::string, unsigned> headers0[] = { 
				std::make_pair("Patches", 190), std::make_pair("IA", 190), std::make_pair("States", 100), 
				std::make_pair("MatSelectors", 750),
				std::make_pair("GeoSelectors", 750) };

			std::vector<Float3> lines;
			lines.reserve(pipelineAccelerators.size()*2);

            DrawTableHeaders(context, tableArea.AllocateFullWidth(lineHeight), MakeIteratorRange(headers0), headerColor, &interactables);
            for (const auto& r:pipelineAccelerators) {
				if (entryCount < _scrollOffset) {
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
			sourceEntryCount = (unsigned)pipelineAccelerators.size();

			context.DrawLines(RenderOverlays::ProjectionMode::P2D, lines.data(), lines.size(), RenderOverlays::ColorB::White);
        }

		ScrollBar::Coordinates scrollCoordinates(scrollArea, 0.f, sourceEntryCount, entryCount-(unsigned)_scrollOffset);
		_scrollOffset = _scrollBar.CalculateCurrentOffset(scrollCoordinates, _scrollOffset);
		DrawScrollBar(context, scrollCoordinates, _scrollOffset);
		interactables.Register(Interactables::Widget(scrollCoordinates.InteractableRect(), _scrollBar.GetID()));
	}

	bool    PipelineAcceleratorPoolDisplay::ProcessInput(InterfaceState& interfaceState, const InputContext& inputContext, const InputSnapshot& input)
	{
		if (_scrollBar.ProcessInput(interfaceState, inputContext, input))
			return true;

		static KeyId pgdn       = KeyId_Make("page down");
        static KeyId pgup       = KeyId_Make("page up");
		if (input.IsPress(pgdn)) _scrollOffset += 1.f;
		if (input.IsPress(pgup)) _scrollOffset = std::max(0.f, _scrollOffset-1.f);
		return false;
	}

	PipelineAcceleratorPoolDisplay::PipelineAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators)
    : _pipelineAccelerators(std::move(pipelineAccelerators))
	{
		_scrollOffset = 0.f;
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

