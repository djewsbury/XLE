// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Assets/AssetsCore.h"
#include <vector>

namespace Utility { class IHierarchicalProfiler; }
namespace Assets { class AssetHeapRecord; }

namespace PlatformRig { namespace Overlays
{
	class QuickMetricsDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		void    PushHeading0(StringSection<> str) { Push(Style::Heading0, str); }
		void    Push(StringSection<> str) { Push(Style::Normal, str); }
		
		enum class Style { Normal, Heading0 };
		void    Push(Style, StringSection<>);

		QuickMetricsDisplay();
		~QuickMetricsDisplay();
	protected:
		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState) override;
		ProcessInputResult    ProcessInput(InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input) override;
		
		std::vector<std::pair<Style, StringSection<>>> _lines;
		uint32_t _frameBarrierSignal = ~0u;

		char _internalBuffer[16384];
		char* _internalBufferIterator;

		RenderOverlays::DebuggingDisplay::ScrollBar _scrollBar;
		float _scrollOffset = 0.f;

		::Assets::PtrToMarkerPtr<RenderOverlays::Font> _headingFont;
	};
}}
