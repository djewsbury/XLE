// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace OSServices { class DisplaySettingsManager; class Window; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateDisplaySettingsDisplay(
		std::shared_ptr<OSServices::DisplaySettingsManager> dispSettings,
		std::shared_ptr<OSServices::Window> window);
}}
