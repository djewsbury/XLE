// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderOverlays/OverlayPrimitives.h"

namespace RenderOverlays { class IOverlayContext; }
namespace RenderOverlays { namespace DebuggingDisplay { struct Layout; class Interactables; class InterfaceState; }}

namespace PlatformRig
{
	class ITopBarManager
	{
	public:
		virtual RenderOverlays::Rect RegisterScreenTitle(RenderOverlays::IOverlayContext&, RenderOverlays::DebuggingDisplay::Layout& layout, float requestedWidth) = 0;
		virtual RenderOverlays::Rect RegisterFrameRigDisplay(RenderOverlays::IOverlayContext&) = 0;
		virtual void RenderFrame(RenderOverlays::IOverlayContext&) = 0;
		virtual ~ITopBarManager();
	};

	std::shared_ptr<ITopBarManager> CreateTopBarManager(const RenderOverlays::Rect&);
}
