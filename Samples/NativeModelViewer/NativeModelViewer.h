// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Shared/SampleRig.h"
#include "../../PlatformRig/OverlaySystem.h"
#include <memory>
#include <vector>

namespace ToolsRig { class VisOverlayController; }
namespace PlatformRig { struct DebugScreenRegistration; }

namespace Sample
{
	class SampleLightingDelegate;

	class NativeModelViewerOverlay : public PlatformRig::OverlaySystemSet, public ISampleOverlay
	{
	public:
		virtual void OnStartup(const PlatformRig::AppRigGlobals& globals) override;
		virtual void Configure(SampleConfiguration& cfg) override;

		NativeModelViewerOverlay();
		~NativeModelViewerOverlay();
	private:
		std::shared_ptr<ToolsRig::VisOverlayController> _overlayBinder;
	};
}
