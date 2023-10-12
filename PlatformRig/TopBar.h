// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderOverlays/OverlayPrimitives.h"

namespace RenderOverlays { class IOverlayContext; struct ImmediateLayout; }
namespace RenderOverlays { namespace DebuggingDisplay { class Interactables; class InterfaceState; }}

namespace PlatformRig
{
	class ITopBarManager
	{
	public:
		virtual RenderOverlays::Rect ScreenTitle(RenderOverlays::IOverlayContext&, RenderOverlays::ImmediateLayout& layout, float requestedWidth) = 0;
		virtual RenderOverlays::Rect Menu(RenderOverlays::IOverlayContext&, float requestedWidth) = 0;
		virtual RenderOverlays::Rect FrameRigDisplay(RenderOverlays::IOverlayContext&) = 0;
		virtual void RenderFrame(RenderOverlays::IOverlayContext&) = 0;
		virtual ~ITopBarManager();
	};

	std::shared_ptr<ITopBarManager> CreateTopBarManager(const RenderOverlays::Rect&);
}
