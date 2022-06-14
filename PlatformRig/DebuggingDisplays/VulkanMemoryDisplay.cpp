// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VulkanMemoryDisplay.h"
#include "../../RenderCore/Vulkan/Metal/ObjectFactory.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../Assets/Marker.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/StringFormat.h"
#include <iostream>
#include <fstream>

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	class VulkanMemoryAllocatorDisplay : public IWidget
	{
	public:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input);

		VulkanMemoryAllocatorDisplay(std::shared_ptr<RenderCore::IDevice> device);
		~VulkanMemoryAllocatorDisplay();
	protected:
		unsigned _counter;
		std::shared_ptr<RenderCore::IDevice> _device;
		VmaTotalStatistics _stats;
		unsigned _memoryHeap = ~0;

		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	void    VulkanMemoryAllocatorDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		// get statistics out of the Vulkan Memory Allocator system
		if ((_counter++ % 64) == 0)
			vmaCalculateStatistics(RenderCore::Metal_Vulkan::GetObjectFactory(*_device).GetVmaAllocator(), &_stats);

		VmaDetailedStatistics* stats = &_stats.total;
		if (_memoryHeap < VK_MAX_MEMORY_HEAPS) stats = &_stats.memoryHeap[_memoryHeap];

		{
			auto titleRect = layout.AllocateFullWidth(30);
			RenderOverlays::DebuggingDisplay::FillRectangle(context, titleRect, titleBkground);
			titleRect._topLeft[0] += 8;
			auto* font = _headingFont->TryActualize();
			if (font) {
				auto headingDraw = DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow);
				if (_memoryHeap < VK_MAX_MEMORY_HEAPS) headingDraw.FormatAndDraw(context, titleRect, "Vulkan Memory (heap: %i)", _memoryHeap);
				else headingDraw.Draw(context, titleRect, "Vulkan Memory (overall)");
			}
		}
		
		char buffer[256];
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "VkDeviceMemory count: " << stats->statistics.blockCount).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "VmaAllocation count: " << stats->statistics.allocationCount).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "VkDeviceMemory size: " << ByteCount{stats->statistics.blockBytes}).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "VmaAllocation size: " << ByteCount{stats->statistics.allocationBytes}).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Allocator overhead: " << ByteCount{stats->statistics.blockBytes-stats->statistics.allocationBytes}).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Allocation size min/max: " << ByteCount{stats->allocationSizeMin} << " / " << ByteCount{stats->allocationSizeMax}).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Unused size min/max: " << ByteCount{stats->unusedRangeSizeMin} << " / " << ByteCount{stats->unusedRangeSizeMax}).AsStringSection());
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Unused ranges count: " << stats->unusedRangeCount).AsStringSection());

		layout.AllocateFullWidth(lineHeight);
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "Press 0-9 to select a specific heap (or ` for all).");
		DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), "Press 'q' to write out a report to vk_alloc.json");
	}

	ProcessInputResult VulkanMemoryAllocatorDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
	{
		if (input._pressedChar >= '0' && input._pressedChar <= '9') {
			_memoryHeap = input._pressedChar-'0';
			return ProcessInputResult::Consumed;
		} else if (input._pressedChar == '`') {
			_memoryHeap = ~0u;
			return ProcessInputResult::Consumed;
		} else if (input._pressedChar == 'q') {
			auto allocator = RenderCore::Metal_Vulkan::GetObjectFactory(*_device).GetVmaAllocator();
			char* statsString = nullptr;
			vmaBuildStatsString(allocator, &statsString, true);
			if (statsString) {
				std::ofstream{"vk_alloc.json"} << statsString;
				vmaFreeStatsString(allocator, statsString);
			}
		}
		return ProcessInputResult::Passthrough;
	}

	VulkanMemoryAllocatorDisplay::VulkanMemoryAllocatorDisplay(std::shared_ptr<RenderCore::IDevice> device)
	: _device(std::move(device)), _counter(0)
	{
		_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
	}

	VulkanMemoryAllocatorDisplay::~VulkanMemoryAllocatorDisplay()
	{}

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateVulkanMemoryAllocatorDisplay(std::shared_ptr<RenderCore::IDevice> device)
	{
		return std::make_shared<VulkanMemoryAllocatorDisplay>(std::move(device));
	}

}}

