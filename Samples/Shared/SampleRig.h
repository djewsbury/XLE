// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../PlatformRig/OverlaySystem.h"
#include <memory>

namespace RenderCore
{
	class IDevice;
	class IPresentationChain;
}

namespace RenderCore { namespace Techniques
{
	class TechniqueContext;
	class DrawingApparatus;
	class ImmediateDrawingApparatus;
	class PrimaryResourcesApparatus;
	class FrameRenderingApparatus;
}}

namespace PlatformRig { class MainInputHandler; class WindowApparatus; class DebugOverlaysApparatus; }

namespace Sample
{
	class SampleGlobals
	{
	public:
		std::shared_ptr<RenderCore::IDevice>				_renderDevice;

		std::shared_ptr<PlatformRig::WindowApparatus> _windowApparatus;
        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
        std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> _immediateDrawingApparatus;
        std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> _primaryResourcesApparatus;
        std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> _frameRenderingApparatus;
		std::shared_ptr<PlatformRig::DebugOverlaysApparatus> _debugOverlaysApparatus;
	};

	class ISampleOverlay : public PlatformRig::IOverlaySystem
	{
	public:
		virtual void OnUpdate(float deltaTime);
		virtual void OnStartup(const SampleGlobals& globals);
	};

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay);
}
