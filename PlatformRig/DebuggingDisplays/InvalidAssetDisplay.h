// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <vector>

namespace Utility { class IHierarchicalProfiler; }
namespace Assets { class AssetHeapRecord; class OperationContext; class IAssetTracking; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateInvalidAssetDisplay(std::shared_ptr<Assets::IAssetTracking> tracking);

	class OperationContextDisplay : public RenderOverlays::DebuggingDisplay::IWidget ///////////////////////////////////////////////////////////
	{
	public:
		using IOverlayContext = RenderOverlays::IOverlayContext;
		using Layout = RenderOverlays::DebuggingDisplay::Layout;
		using Interactables = RenderOverlays::DebuggingDisplay::Interactables;
		using InterfaceState = RenderOverlays::DebuggingDisplay::InterfaceState;
		using InputSnapshot = PlatformRig::InputSnapshot;

		void    Render(IOverlayContext& context, Layout& layout, Interactables&interactables, InterfaceState& interfaceState);

		OperationContextDisplay(std::shared_ptr<::Assets::OperationContext>);
		~OperationContextDisplay();
	private:
		std::shared_ptr<::Assets::OperationContext> _opContext;
	};
}}
