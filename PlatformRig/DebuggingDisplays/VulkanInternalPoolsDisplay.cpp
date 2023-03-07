// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VulkanInternalPoolsDisplay.h"
#include "../TopBar.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../RenderCore/Vulkan/Metal/Pools.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Assets/Marker.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/StringFormat.h"

using namespace PlatformRig::Literals;

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	static const std::string s_descriptorTypeNames[] {
		"Sampler", "CombinedImageSampler", "SampledImage", "StorageImage", "UniformTexelBuffer", "StorageTexelBuffer",
		"UniformBuffer", "StorageBuffer", "UniformBufferDynamic", "StorageBufferDynamic", "InputAttachment" };

	class VulkanInternalPoolsDisplay : public IWidget
	{
	public:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const unsigned headerLineHeight = 30;
			const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

			if (auto* topBar = context.GetService<ITopBarManager>()) {
				const char headingString[] = "Vulkan Internal Pools";
				if (auto* headingFont = _screenHeadingFont->TryActualize()) {
					auto rect = topBar->RegisterScreenTitle(context, layout, StringWidth(**headingFont, MakeStringSection(headingString)));
					if (IsGood(rect) && headingFont)
						DrawText()
							.Font(**headingFont)
							.Color(ColorB::Black)
							.Alignment(RenderOverlays::TextAlignment::Left)
							.Flags(0)
							.Draw(context, rect, headingString);
				}
			}

			layout._maximumSize._topLeft[1] -= _scrollOffset;

			auto* vulkanDevice = (RenderCore::IDeviceVulkan*)_device->QueryInterface(TypeHashCode<RenderCore::IDeviceVulkan>);
			if (!vulkanDevice) return;

			{
				auto allocation = layout.AllocateFullWidth(headerLineHeight);
				FillRectangle(context, allocation, titleBkground);
				allocation._topLeft[0] += 8;
				auto* font = _headingFont->TryActualize();
				if (font)
					DrawText()
						.Font(**font)
						.Color({ 191, 123, 0 })
						.Alignment(RenderOverlays::TextAlignment::Left)
						.Flags(RenderOverlays::DrawTextFlags::Shadow)
						.Draw(context, allocation, "Main Descriptor Set Pool");
			}

			RenderCore::Metal_Vulkan::DescriptorPoolMetrics mainDescriptorSetPoolMetrics;
			vulkanDevice->GetInternalMetrics(RenderCore::IDeviceVulkan::MainDescriptorPoolMetrics, MakeOpaqueIteratorRange(mainDescriptorSetPoolMetrics));
			RenderDescriptorPoolMetrics(context, layout, interactables, mainDescriptorSetPoolMetrics);

			{
				auto allocation = layout.AllocateFullWidth(headerLineHeight);
				FillRectangle(context, allocation, titleBkground);
				allocation._topLeft[0] += 8;
				auto* font = _headingFont->TryActualize();
				if (font)
					DrawText()
						.Font(**font)
						.Color({ 191, 123, 0 })
						.Alignment(RenderOverlays::TextAlignment::Left)
						.Flags(RenderOverlays::DrawTextFlags::Shadow)
						.Draw(context, allocation, "Long Term Descriptor Set Pool");
			}

			RenderCore::Metal_Vulkan::DescriptorPoolMetrics longTermDescriptorSetPool;
			vulkanDevice->GetInternalMetrics(RenderCore::IDeviceVulkan::LongTermDescriptorPoolMetrics, MakeOpaqueIteratorRange(longTermDescriptorSetPool));
			RenderDescriptorPoolMetrics(context, layout, interactables, longTermDescriptorSetPool);
		}

		void RenderDescriptorPoolMetrics(IOverlayContext& context, Layout& layout, Interactables& interactables, const RenderCore::Metal_Vulkan::DescriptorPoolMetrics& metrics)
		{
			const unsigned lineHeight = 20;
			char buffer[256];
			DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Descriptor sets -- allocated: " << metrics._setsAllocated << ", reserved: " << metrics._setsReserved).AsStringSection());

			{
				std::pair<std::string, unsigned> headers0[] = { std::make_pair("Descriptor Allocations (Type)", 800), std::make_pair("Allocated", 200), std::make_pair("Reserved", 200) };

				auto height = DrawTableHeaders(context, Layout{layout}.AllocateFullHeightFraction(1.f), MakeIteratorRange(headers0), &interactables);
				layout.AllocateFullWidth(height);
				
				for (unsigned c=0; c<dimof(s_descriptorTypeNames); ++c) {
					auto height = DrawTableEntry(
						context, Layout{layout}.AllocateFullHeightFraction(1.f), MakeIteratorRange(headers0),
						{	std::make_pair(headers0[0].first, s_descriptorTypeNames[c]), 
							std::make_pair(headers0[1].first, std::to_string(metrics._descriptorsAllocated[c])),
							std::make_pair(headers0[2].first, std::to_string(metrics._descriptorsReserved[c])) });
					layout.AllocateFullWidth(height + 8);
				}

				DrawTableBase(context, layout.AllocateFullWidth(height/2));
			}

			{
				std::pair<std::string, unsigned> headers1[] = { std::make_pair("Reusable Sets (Layout)", 800), std::make_pair("Allocated", 200), std::make_pair("Reserved", 200) };

				auto height = DrawTableHeaders(context, Layout{layout}.AllocateFullHeightFraction(1.f), MakeIteratorRange(headers1), &interactables);
				layout.AllocateFullWidth(height);
				
				for (const auto&g:metrics._reusableGroups) {
					auto height = DrawTableEntry(
						context, Layout{layout}.AllocateFullHeightFraction(1.f), MakeIteratorRange(headers1),
						{	std::make_pair(headers1[0].first, g._layoutName), 
							std::make_pair(headers1[1].first, std::to_string(g._allocatedCount)),
							std::make_pair(headers1[2].first, std::to_string(g._reservedCount)) });
					layout.AllocateFullWidth(height + 8);
				}

				DrawTableBase(context, layout.AllocateFullWidth(height/2));
			}
		}

		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
		{
			if (input.IsPress("page down"_key)) _scrollOffset += 100;
			else if (input.IsPress("page up"_key)) _scrollOffset = std::max(0, _scrollOffset - 100);
			else if (input.IsPress("home"_key)) _scrollOffset = 0;
			return ProcessInputResult::Passthrough;
		}

		VulkanInternalPoolsDisplay(std::shared_ptr<RenderCore::IDevice> device)
		: _device(std::move(device))
		{
			_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
			_screenHeadingFont = RenderOverlays::MakeFont("OrbitronBlack", 20);
		}

	protected:
		std::shared_ptr<RenderCore::IDevice> _device;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _screenHeadingFont;
		int _scrollOffset = 0;
	};


	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateVulkanInternalPoolsDisplay(std::shared_ptr<RenderCore::IDevice> device)
	{
		auto* vulkanDevice = (RenderCore::IDeviceVulkan*)device->QueryInterface(TypeHashCode<RenderCore::IDeviceVulkan>);
		if (!vulkanDevice) return nullptr;

		return std::make_shared<VulkanInternalPoolsDisplay>(std::move(device));
	}

}}
