// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; }}
namespace RenderOverlays { class FontRenderingManager; class ShapesRenderingDelegate; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables;  }}

namespace PlatformRig
{
	class IOverlaySystem;

	std::shared_ptr<IOverlaySystem> CreateDebugScreensOverlay(
		std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> debugScreensSystem,
		std::shared_ptr<RenderCore::Techniques::IImmediateDrawables> immediateDrawables,
		std::shared_ptr<RenderOverlays::ShapesRenderingDelegate> sequencerConfigSet,
		std::shared_ptr<RenderOverlays::FontRenderingManager> fontRenderer);
}
