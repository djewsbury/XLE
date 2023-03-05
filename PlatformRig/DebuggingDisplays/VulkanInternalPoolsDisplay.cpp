// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "VulkanInternalPoolsDisplay.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"
#include "../../RenderCore/Vulkan/Metal/Pools.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../RenderOverlays/ShapesRendering.h"
#include "../../RenderOverlays/DrawText.h"
#include "../../Assets/Marker.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/StringFormat.h"

namespace PlatformRig { namespace Overlays
{
	using namespace RenderOverlays;
	using namespace RenderOverlays::DebuggingDisplay;

	static const std::pair<std::string, unsigned> s_headers0[] = { std::make_pair("Descriptor Allocations (Type)", 800), std::make_pair("Allocated", 120), std::make_pair("Reserved", 120) };
	static const std::string s_descriptorTypeNames[] {
		"Sampler", "CombinedImageSampler", "SampledImage", "StorageImage", "UniformTexelBuffer", "StorageTexelBuffer",
		"UniformBuffer", "StorageBuffer", "UniformBufferDynamic", "StorageBufferDynamic", "InputAttachment" };

	static const std::pair<std::string, unsigned> s_headers1[] = { std::make_pair("Reusable Sets (Layout)", 800), std::make_pair("Allocated", 120), std::make_pair("Reserved", 120) };

	class VulkanInternalPoolsDisplay : public IWidget
	{
	public:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
		{
			const unsigned lineHeight = 20;
			const unsigned headerLineHeight = 30;
			const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

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
			const unsigned headerLineHeight = 30;
			char buffer[256];
			DrawText().Draw(context, layout.AllocateFullWidth(lineHeight), (StringMeldInPlace(buffer) << "Descriptor sets -- allocated: " << metrics._setsAllocated << ", reserved: " << metrics._setsReserved).AsStringSection());

			{
				auto originalInternalBorder = layout._paddingInternalBorder;
				// layout._paddingInternalBorder = 0;
				DrawTableHeaders(context, layout.AllocateFullWidth(headerLineHeight), MakeIteratorRange(s_headers0), &interactables);
				
				for (unsigned c=0; c<dimof(s_descriptorTypeNames); ++c)
					DrawTableEntry(
						context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(s_headers0),
						{	std::make_pair(s_headers0[0].first, s_descriptorTypeNames[c]), 
							std::make_pair(s_headers0[1].first, std::to_string(metrics._descriptorsAllocated[c])),
							std::make_pair(s_headers0[2].first, std::to_string(metrics._descriptorsReserved[c])) });
				layout._paddingInternalBorder = originalInternalBorder;
			}

			{
				auto originalInternalBorder = layout._paddingInternalBorder;
				// layout._paddingInternalBorder = 0;
				DrawTableHeaders(context, layout.AllocateFullWidth(headerLineHeight), MakeIteratorRange(s_headers1), &interactables);
				
				for (const auto&g:metrics._reusableGroups)
					DrawTableEntry(
						context, layout.AllocateFullWidth(lineHeight), MakeIteratorRange(s_headers1),
						{	std::make_pair(s_headers1[0].first, g._layoutName), 
							std::make_pair(s_headers1[1].first, std::to_string(g._allocatedCount)),
							std::make_pair(s_headers1[2].first, std::to_string(g._reservedCount)) });
				layout._paddingInternalBorder = originalInternalBorder;
			}
		}

		ProcessInputResult ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input)
		{
			return ProcessInputResult::Passthrough;
		}

		VulkanInternalPoolsDisplay(std::shared_ptr<RenderCore::IDevice> device)
		: _device(std::move(device))
		{
			_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
		}

	protected:
		std::shared_ptr<RenderCore::IDevice> _device;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};


	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateVulkanInternalPoolsDisplay(std::shared_ptr<RenderCore::IDevice> device)
	{
		auto* vulkanDevice = (RenderCore::IDeviceVulkan*)device->QueryInterface(TypeHashCode<RenderCore::IDeviceVulkan>);
		if (!vulkanDevice) return nullptr;

		return std::make_shared<VulkanInternalPoolsDisplay>(std::move(device));
	}

}}
