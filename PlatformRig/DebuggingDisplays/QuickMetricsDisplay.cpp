// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "QuickMetricsDisplay.h"
#include "../../RenderCore/Techniques/SubFrameEvents.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../Assets/Marker.h"
#include "../../Utility/MemoryUtils.h"

namespace PlatformRig { namespace Overlays
{
	void    QuickMetricsDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		auto oldBetweenAllocations = layout._paddingBetweenAllocations;
		layout._paddingBetweenAllocations = 0;
		Layout textArea = layout.AllocateFullHeight(layout.GetWidthRemaining() - layout._paddingInternalBorder - 12);
		auto scrollArea = layout.AllocateFullHeight(layout.GetWidthRemaining());
		layout._paddingBetweenAllocations = oldBetweenAllocations;

		ScrollBar::Coordinates scrollCoordinates(scrollArea, 0.f, _lines.size(), textArea.GetMaximumSize().Height()/(float)lineHeight);
		_scrollOffset = _scrollBar.CalculateCurrentOffset(scrollCoordinates, _scrollOffset);
		DrawScrollBar(context, scrollCoordinates, _scrollOffset);
		interactables.Register({scrollCoordinates.InteractableRect(), _scrollBar.GetID()});

		if (unsigned(_scrollOffset) < _lines.size()) {
			auto l = _lines.begin() + unsigned(_scrollOffset);
			for (; l!=_lines.end(); ++l) {
				if (l->first == Style::Heading0) {
					auto allocation = textArea.AllocateFullWidth(30);
					if (!allocation.Height()) break;
					FillRectangle(context, allocation, titleBkground);
					allocation._topLeft[0] += 8;
					auto* font = _headingFont->TryActualize();
					if (font)
						DrawText()
							.Font(**font)
							.Color({ 191, 123, 0 })
							.Alignment(RenderOverlays::TextAlignment::Left)
							.Flags(RenderOverlays::DrawTextFlags::Shadow)
							.Draw(context, allocation, l->second);
				} else {
					auto allocation = textArea.AllocateFullWidth(lineHeight);
					if (!allocation.Height()) break;
					DrawText()
						.Color(0xffcfcfcf)
						.Draw(context, allocation, l->second);
				}
			}
		}
	}

	auto    QuickMetricsDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
	{
		if (_scrollBar.ProcessInput(interfaceState, input) == ProcessInputResult::Consumed)
			return ProcessInputResult::Consumed;

		static KeyId pgdn       = KeyId_Make("page down");
        static KeyId pgup       = KeyId_Make("page up");
		if (input.IsPress(pgdn)) _scrollOffset += 1.f;
		if (input.IsPress(pgup)) _scrollOffset = std::max(0.f, _scrollOffset-1.f);
		return ProcessInputResult::Passthrough;
	}

	void    QuickMetricsDisplay::Push(Style style, StringSection<> str)
	{
		auto i = str.begin();
		while (i != str.end()) {
			while (i != str.end() && (*i == '\n' || *i == '\r')) ++i;
			auto i2 = i;
			while (i2 != str.end() && *i2 != '\n' && *i2 != '\r') ++i2;

			auto count = i2-i;
			if (!count) break;
			if ((this->_internalBufferIterator+count) > &_internalBuffer[dimof(_internalBuffer)])
				break;
			std::move(i, i2, this->_internalBufferIterator);
			_lines.push_back({style, {this->_internalBufferIterator, this->_internalBufferIterator+count}});
			this->_internalBufferIterator += count;
			i = i2;
		}
	}

	QuickMetricsDisplay::QuickMetricsDisplay()
	{
		_scrollOffset = 0.f;
		_internalBufferIterator = _internalBuffer;
		auto scrollBarId = RenderOverlays::DebuggingDisplay::InteractableId_Make("QuickMetrics_ScrollBar");
		scrollBarId += IntegerHash64((uint64_t)this);
		_scrollBar = RenderOverlays::DebuggingDisplay::ScrollBar(scrollBarId);
		_frameBarrierSignal = RenderCore::Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Bind(
			[this]() {
				this->_lines.clear();
				this->_internalBufferIterator = this->_internalBuffer;
			});
		_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
	}

	QuickMetricsDisplay::~QuickMetricsDisplay()
	{
		RenderCore::Techniques::Services::GetSubFrameEvents()._onFrameBarrier.Unbind(_frameBarrierSignal);
	}

}}

