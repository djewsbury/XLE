// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCacheDisplay.h"
#include "BufferUploadDisplay.h"
#include "../../RenderCore/Techniques/ModelCache.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../Assets/AssetHeap.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"

namespace PlatformRig { namespace Overlays
{
    class ModelCacheDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		ModelCacheDisplay(std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache);
		~ModelCacheDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override;
		
		std::shared_ptr<RenderCore::Techniques::ModelCache> _modelCache;

		RenderOverlays::DebuggingDisplay::ScrollBar _scrollBar;
		float _scrollOffsets[3];
		unsigned _tab = 0;
	};

	static void DrawButton(RenderOverlays::IOverlayContext& context, const char name[], const RenderOverlays::Rect&buttonRect, RenderOverlays::DebuggingDisplay::Interactables&interactables, RenderOverlays::DebuggingDisplay::InterfaceState& interfaceState)
    {
		using namespace RenderOverlays::DebuggingDisplay;
        InteractableId id = InteractableId_Make(name);
		RenderOverlays::CommonWidgets::Draw{context, interactables, interfaceState}.ButtonBasic(buttonRect, id, name);
        interactables.Register({buttonRect, id});
    }

    static const char* s_tabNames[] = { "ModelRenderers", "ModelScaffolds", "MaterialScaffolds" };

	void    ModelCacheDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		{
			Layout buttonsLayout(layout.AllocateFullWidth(2*lineHeight));
			buttonsLayout._paddingInternalBorder = 2;
            for (unsigned t=0; t<dimof(s_tabNames); ++t)
                DrawButton(context, s_tabNames[t], buttonsLayout.AllocateFullHeightFraction(1.f/float(dimof(s_tabNames))), interactables, interfaceState);
		}

    	auto records = _modelCache->LogRecords();
		{
			auto oldBetweenAllocations = layout._paddingBetweenAllocations;
			layout._paddingBetweenAllocations = 0;
			Layout tableArea = layout.AllocateFullHeight(layout.GetWidthRemaining() - layout._paddingInternalBorder - 12);
			tableArea._paddingInternalBorder = 2;
			auto scrollBarLocation = layout.AllocateFullHeight(layout.GetWidthRemaining());
			layout._paddingBetweenAllocations = oldBetweenAllocations;
			unsigned entryCount = 0, sourceEntryCount = 0;

			// fill in the background now, so it doesn't have to be interleaved with rendering the entry text elements
			context.DrawQuad(
				RenderOverlays::ProjectionMode::P2D, AsPixelCoords(tableArea.GetMaximumSize()._topLeft), AsPixelCoords(scrollBarLocation._bottomRight),
				RenderOverlays::ColorB { 0, 0, 0, 145 });
                    
			const auto headerColor = RenderOverlays::ColorB::Blue;
            std::vector<Float3> lines;
			if (_tab == 0) {
				std::pair<std::string, unsigned> headers0[] = { 
					std::make_pair("Model", 900), std::make_pair("Material", 900), std::make_pair("Decay", 50) };

				DrawTableHeaders(context, tableArea.AllocateFullWidth(28), MakeIteratorRange(headers0), headerColor, &interactables);
				std::sort(records._modelRenderers.begin(),records._modelRenderers.end(), [](const auto& lhs, const auto& rhs) { return lhs._model < rhs._model; });
				for (const auto& r:records._modelRenderers) {
					if (entryCount < _scrollOffsets[_tab]) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
					entries["Model"] = r._model;
					entries["Material"] = r._material;
					entries["Decay"] = std::to_string(r._decayFrames);
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
				sourceEntryCount = (unsigned)records._modelRenderers.size();
			} else if (_tab == 1 || _tab == 2) {
				std::pair<std::string, unsigned> headers0[] = { 
					std::make_pair("Name", 3000)
				};

				DrawTableHeaders(context, tableArea.AllocateFullWidth(28), MakeIteratorRange(headers0), headerColor, &interactables);
                auto& recordList = (_tab == 1) ? records._modelScaffolds : records._materialScaffolds;
				std::sort(recordList.begin(), recordList.end(), [](const auto& lhs, const auto& rhs) { return lhs._initializer < rhs._initializer; });
				for (const auto& r:recordList) {
					if (entryCount < _scrollOffsets[_tab]) {
						++entryCount;
						continue;
					}
					std::map<std::string, TableElement> entries;
                    entries["Name"] = r._initializer;
                    if (r._state != ::Assets::AssetState::Ready) entries["Name"]._bkColour = 0xffff3f3f;
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
				sourceEntryCount = (unsigned)recordList.size();
			}

			context.DrawLines(RenderOverlays::ProjectionMode::P2D, lines.data(), lines.size(), RenderOverlays::ColorB::White);
			auto& scrollOffset = _scrollOffsets[_tab];

			ScrollBar::Coordinates scrollCoordinates(scrollBarLocation, 0.f, sourceEntryCount, entryCount-(unsigned)scrollOffset);
			scrollOffset = _scrollBar.CalculateCurrentOffset(scrollCoordinates, scrollOffset);
			DrawScrollBar(context, scrollCoordinates, scrollOffset, interfaceState.HasMouseOver(_scrollBar.GetID()) ? RenderOverlays::ColorB(120, 120, 120) : RenderOverlays::ColorB(51, 51, 51));
			interactables.Register({scrollCoordinates.InteractableRect(), _scrollBar.GetID()});
		}
	}

	auto    ModelCacheDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
	{
		using namespace RenderOverlays::DebuggingDisplay;
		if (_scrollBar.ProcessInput(interfaceState, input) == ProcessInputResult::Consumed)
			return ProcessInputResult::Consumed;

		static KeyId pgdn       = KeyId_Make("page down");
        static KeyId pgup       = KeyId_Make("page up");
        if (input.IsPress(pgdn)) _scrollOffsets[_tab] += 1.f;
        if (input.IsPress(pgup)) _scrollOffsets[_tab] = std::max(0.f, _scrollOffsets[_tab]-1.f);

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

	ModelCacheDisplay::ModelCacheDisplay(std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache)
    : _modelCache(std::move(modelCache))
	{
        for (auto&so:_scrollOffsets) so = 0.f;
		_tab = 0;
		auto scrollBarId = RenderOverlays::DebuggingDisplay::InteractableId_Make("ModelCache_ScrollBar");
		scrollBarId += IntegerHash64((uint64_t)this);
		_scrollBar = RenderOverlays::DebuggingDisplay::ScrollBar(scrollBarId);
	}

	ModelCacheDisplay::~ModelCacheDisplay()
	{
	}

    std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateModelCacheDisplay(
        std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache)
    {
        return std::make_shared<ModelCacheDisplay>(std::move(modelCache));
    }

	class ModelCacheGeoBufferDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		void Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState) override;
		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override;
		ModelCacheGeoBufferDisplay(std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache);
	protected:
		std::shared_ptr<BatchingDisplay> _vbDisplay, _ibDisplay;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	void ModelCacheGeoBufferDisplay::Render(IOverlayContext& context, Layout& layout, Interactables& interactables, InterfaceState& interfaceState)
	{
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };
		{
			auto allocation = layout.AllocateFullWidth(30);
			FillRectangle(context, allocation, titleBkground);
			allocation._topLeft[0] += 8;
			auto* font = _headingFont->TryActualize();
			if (font)
				DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow)
					.Draw(context, allocation, "Model Cache Geobuffers");
		}

		auto leftRect = layout.AllocateFullHeightFraction(0.5f);
		auto rightRect = layout.AllocateFullHeight(layout.GetWidthRemaining());
		if (_vbDisplay) {
			Layout l{leftRect};
			_vbDisplay->Render(context, l, interactables, interfaceState);
		}
		if (_ibDisplay) {
			Layout l{rightRect};
			_ibDisplay->Render(context, l, interactables, interfaceState);
		}
	}

	ProcessInputResult ModelCacheGeoBufferDisplay::ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input)
	{
		if (_vbDisplay) {
			auto res = _vbDisplay->ProcessInput(interfaceState, input);
			if (res != ProcessInputResult::Passthrough) return res;
		}
		if (_ibDisplay) {
			auto res = _ibDisplay->ProcessInput(interfaceState, input);
			if (res != ProcessInputResult::Passthrough) return res;
		}
		return ProcessInputResult::Passthrough;
	}

	ModelCacheGeoBufferDisplay::ModelCacheGeoBufferDisplay(std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache)
	{
		auto vb = modelCache->GetVBResources();
		if (vb) _vbDisplay = std::make_shared<BatchingDisplay>(std::move(vb));

		auto ib = modelCache->GetIBResources();
		if (ib) _ibDisplay = std::make_shared<BatchingDisplay>(std::move(ib));

		_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
	}

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateModelCacheGeoBufferDisplay(
		std::shared_ptr<RenderCore::Techniques::ModelCache> modelCache)
	{
		return std::make_shared<ModelCacheGeoBufferDisplay>(std::move(modelCache));
	}

}}

