// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <vector>

namespace Utility { class IHierarchicalProfiler; }
namespace Assets { class AssetHeapRecord; }

namespace PlatformRig { namespace Overlays
{
	class InvalidAssetDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
	{
	public:
		using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = PlatformRig::InputSnapshot;

		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);

		InvalidAssetDisplay();
		~InvalidAssetDisplay();
	private:
		std::vector<::Assets::AssetHeapRecord> _currentRecords;
		unsigned _currentRecordsCountDown;
	};
}}
