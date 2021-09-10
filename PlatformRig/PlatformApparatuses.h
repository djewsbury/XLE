// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ConsoleRig/AttachablePtr.h"
#include "../Assets/DepVal.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class IPresentationChain; class IAnnotator; }
namespace RenderCore { namespace Techniques { class ImmediateDrawingApparatus; }}
namespace RenderOverlays { class Font; }
namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; }}
namespace Assets { class DependencyValidation; }
namespace Utility { class HierarchicalCPUProfiler; }

namespace PlatformRig
{
	class OverlaySystemSet;
	class OverlappedWindow;
	class MainInputHandler;
	class ResizePresentationChain;
	class FrameRig;
	class IDebugScreenRegistry;

	class DebugOverlaysApparatus
	{
	public:
		std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> _immediateApparatus;

		std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> _debugSystem;
		std::shared_ptr<OverlaySystemSet> _debugScreensOverlaySystem;

		std::shared_ptr<RenderOverlays::Font> _debugFont0;
		std::shared_ptr<RenderOverlays::Font> _debugFont1;
		ConsoleRig::AttachablePtr<IDebugScreenRegistry> _debugScreenRegistry;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depValPtr; }
		::Assets::DependencyValidation _depValPtr;

		DebugOverlaysApparatus(
			const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
			FrameRig& frameRig);
		~DebugOverlaysApparatus();
	};

	class WindowApparatus
	{
	public:
		std::shared_ptr<OverlappedWindow> _osWindow;
		std::shared_ptr<RenderCore::IDevice> _device;
		std::shared_ptr<RenderCore::IThreadContext> _immediateContext;
		std::shared_ptr<RenderCore::IPresentationChain> _presentationChain;
		std::shared_ptr<MainInputHandler> _mainInputHandler;
		std::shared_ptr<ResizePresentationChain> _windowHandler;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depValPtr; }
		::Assets::DependencyValidation _depValPtr;

		WindowApparatus(std::shared_ptr<RenderCore::IDevice> device);
		~WindowApparatus();
	};

	void InitProfilerDisplays(
		RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugSys, 
		RenderCore::IAnnotator* annotator,
		Utility::HierarchicalCPUProfiler& cpuProfiler);

}

