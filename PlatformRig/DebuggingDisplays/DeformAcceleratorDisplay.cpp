// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformAcceleratorDisplay.h"
#include "../../RenderCore/Techniques/DeformAccelerator.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/CommonWidgets.h"
#include "../../Assets/Marker.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StreamUtils.h"

namespace PlatformRig { namespace Overlays
{
	class DeformAcceleratorPoolDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		DeformAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators);
		~DeformAcceleratorPoolDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override;
		
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> _deformAccelerators;
		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};

	void    DeformAcceleratorPoolDisplay::Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState)
	{
		using namespace RenderOverlays::DebuggingDisplay;
		const unsigned lineHeight = 20;
		const auto titleBkground = RenderOverlays::ColorB { 51, 51, 51 };

		{
			auto titleRect = layout.AllocateFullWidth(30);
			RenderOverlays::DebuggingDisplay::FillRectangle(context, titleRect, titleBkground);
			titleRect._topLeft[0] += 8;
			auto* font = _headingFont->TryActualize();
			if (font)
				DrawText()
					.Font(**font)
					.Color({ 191, 123, 0 })
					.Alignment(RenderOverlays::TextAlignment::Left)
					.Flags(RenderOverlays::DrawTextFlags::Shadow)
					.Draw(context, titleRect, "Deform Accelerators");
		}

		auto metrics = _deformAccelerators->GetMetrics();
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Accelerators readied: %u", (unsigned)metrics._acceleratorsReadied);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Deformers readied: %u", (unsigned)metrics._deformersReadied);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Instances readied: %u", (unsigned)metrics._instancesReadied);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), StringMeld<64>() << "CPU Deform Allocation: " << ByteCount{metrics._cpuDeformAllocation});
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), StringMeld<64>() << "GPU Deform Allocation: " << ByteCount{metrics._gpuDeformAllocation});
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), StringMeld<64>() << "CB Allocation: " << ByteCount{metrics._cbAllocation});
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Dispatch count: %u", (unsigned)metrics._dispatchCount);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Vertex count: %u", (unsigned)metrics._vertexCount);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Ave vertex size: %.1f bytes", metrics._vertexCount?metrics._gpuDeformAllocation/(float)metrics._vertexCount:0.f);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Ave vertices per dispatch: %u", metrics._dispatchCount?metrics._vertexCount/metrics._dispatchCount:0);
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), "Descriptor set writes count: %u", (unsigned)metrics._descriptorSetWrites);		
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), StringMeld<64>() << "Constant data size: " << ByteCount{metrics._constantDataSize});
		DrawText().FormatAndDraw(context, layout.AllocateFullWidth(lineHeight), StringMeld<64>() << "Input static data size: " << ByteCount{metrics._inputStaticDataSize});
	}

	auto    DeformAcceleratorPoolDisplay::ProcessInput(InterfaceState& interfaceState, const InputSnapshot& input) -> ProcessInputResult
	{
		return ProcessInputResult::Passthrough;
	}

	DeformAcceleratorPoolDisplay::DeformAcceleratorPoolDisplay(std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators)
	: _deformAccelerators(std::move(deformAccelerators))
	{
		_headingFont = RenderOverlays::MakeFont("DosisExtraBold", 20);
	}

	DeformAcceleratorPoolDisplay::~DeformAcceleratorPoolDisplay()
	{
	}

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateDeformAcceleratorPoolDisplay(
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators)
	{
		return std::make_shared<DeformAcceleratorPoolDisplay>(std::move(deformAccelerators));
	}

}}

