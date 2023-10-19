// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../OSServices/OverlappedWindow.h"		// for SystemMessageVariant
#include "../ConsoleRig/AttachablePtr.h"
#include "../Assets/DepVal.h"
#include <memory>

namespace RenderCore { class IDevice; class IThreadContext; class IPresentationChain; class IAnnotator; }
namespace RenderCore { namespace BindFlag { using BitField = unsigned; }}
namespace RenderCore { namespace Techniques { class DrawingApparatus; class FrameRenderingApparatus; }}
namespace RenderOverlays { class Font; class OverlayApparatus; }
namespace RenderOverlays { namespace DebuggingDisplay { class DebugScreensSystem; class IWidget; }}
namespace Assets { class DependencyValidation; class OperationContext; }
namespace OSServices { class DisplaySettingsManager; class Window; }
namespace Utility { class HierarchicalCPUProfiler; }

namespace PlatformRig
{
	class OverlaySystemSet;
	class MainInputHandler;
	class FrameRig;
	class IDebugScreenRegistry;

	class DebugOverlaysApparatus
	{
	public:
		std::shared_ptr<RenderOverlays::OverlayApparatus> _immediateApparatus;

		std::shared_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem> _debugSystem;
		std::shared_ptr<OverlaySystemSet> _debugScreensOverlaySystem;

		ConsoleRig::AttachablePtr<IDebugScreenRegistry> _debugScreenRegistry;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depValPtr; }
		::Assets::DependencyValidation _depValPtr;

		DebugOverlaysApparatus(
			const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus);
		~DebugOverlaysApparatus();

	private:
		bool _popUtilityFn = false;
	};

	class WindowApparatus
	{
	public:
		std::shared_ptr<OSServices::Window> _osWindow;
		std::shared_ptr<RenderCore::IThreadContext> _immediateContext;
		std::shared_ptr<RenderCore::IPresentationChain> _presentationChain;
		std::shared_ptr<MainInputHandler> _mainInputHandler;
		std::shared_ptr<FrameRig> _frameRig;
		std::shared_ptr<OSServices::DisplaySettingsManager> _displaySettings;
		std::shared_ptr<Assets::OperationContext> _mainLoadingContext;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depValPtr; }
		::Assets::DependencyValidation _depValPtr;

		WindowApparatus(
			std::shared_ptr<OSServices::Window> osWindow,
			RenderCore::Techniques::DrawingApparatus* drawingApparatus,
			RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
			RenderCore::BindFlag::BitField presentationChainBindFlags=0);
		~WindowApparatus();
	};

	void CommonEventHandling(WindowApparatus& windowApparatus, OSServices::SystemMessageVariant& msgPump);

}

