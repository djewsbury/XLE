// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlatformApparatuses.h"
#include "FrameRig.h"
#include "PlatformRigUtil.h"
#include "OverlaySystem.h"
#include "OverlappedWindow.h"
#include "MainInputHandler.h"
#include "DebugScreensOverlay.h"
#include "DebugScreenRegistry.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/RenderPass.h"		// for IFrameBufferPool
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/IDevice.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../RenderOverlays/OverlayApparatus.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../OSServices/DisplaySettings.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Utility/Profiling/CPUProfiler.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/FunctionUtils.h"

using namespace PlatformRig::Literals;

namespace PlatformRig
{

	constexpr auto Fn_ShowScreen = ConstHash64Legacy<'show', 'scre', 'en'>::Value;

	DebugOverlaysApparatus::DebugOverlaysApparatus(
		const std::shared_ptr<RenderOverlays::OverlayApparatus>& immediateDrawingApparatus)
	{
		using DebugScreensSystem = RenderOverlays::DebuggingDisplay::DebugScreensSystem;
		_debugSystem = std::make_shared<DebugScreensSystem>();

		_debugScreensOverlaySystem = std::make_shared<PlatformRig::OverlaySystemSet>();
		_debugScreensOverlaySystem->AddSystem(CreateDebugScreensOverlay(
			_debugSystem,
			immediateDrawingApparatus->_immediateDrawables,
			immediateDrawingApparatus->_shapeRenderingDelegate,
			immediateDrawingApparatus->_fontRenderingManager));

		auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
		overlaySwitch->AddSystem("~"_key, PlatformRig::CreateConsoleOverlaySystem(*immediateDrawingApparatus));
		_debugScreensOverlaySystem->AddSystem(overlaySwitch);

		_debugScreenRegistry = CreateDebugScreenRegistry();
		_debugScreenRegistry->OnRegister.Bind(
			[debugSys = std::weak_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem>{_debugSystem}]
			(std::string name, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> widget) {
				auto l = debugSys.lock();
				if (!l) return;
				l->Register(widget, name.c_str());
			});
		_debugScreenRegistry->OnDeregister.Bind(
			[debugSys = std::weak_ptr<RenderOverlays::DebuggingDisplay::DebugScreensSystem>{_debugSystem}]
			(RenderOverlays::DebuggingDisplay::IWidget& widget) {
				auto l = debugSys.lock();
				if (!l) return;
				l->Unregister(widget);
			});

		ConsoleRig::CrossModule::GetInstance()._services.Add(
			Fn_ShowScreen,
			[weakDebugScreens = std::weak_ptr<DebugScreensSystem>{_debugSystem}](StringSection<> screenName) {
				auto l = weakDebugScreens.lock();
				if (l)
					l->SwitchToScreen(screenName);
			});
	}

	DebugOverlaysApparatus::~DebugOverlaysApparatus()
	{
		ConsoleRig::CrossModule::GetInstance()._services.Remove(Fn_ShowScreen);
	}

	void SetSystemDisplay(RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugScreens, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> systemDisplay)
	{
		debugScreens.Register(
			std::move(systemDisplay),
			"system-display", RenderOverlays::DebuggingDisplay::DebugScreensSystem::SystemDisplay);
	}

	WindowApparatus::WindowApparatus(
		std::shared_ptr<Window> osWindow,
		RenderCore::Techniques::DrawingApparatus* drawingApparatus,
		RenderCore::Techniques::FrameRenderingApparatus& frameRenderingApparatus,
		RenderCore::BindFlag::BitField presentationChainBindFlags)
	{
		_osWindow = std::move(osWindow);
		auto* device = frameRenderingApparatus._device.get();
		_immediateContext = device->GetImmediateContext();

		auto clientRect = _osWindow->GetRect();
		auto desc = RenderCore::PresentationChainDesc{unsigned(clientRect.second[0] - clientRect.first[0]), unsigned(clientRect.second[1] - clientRect.first[1])};
		desc._bindFlags |= presentationChainBindFlags;
		desc._imageCount = 3;
		_presentationChain = device->CreatePresentationChain(
			_osWindow->GetUnderlyingHandle(),
			desc);

		_frameRig = std::make_shared<FrameRig>(frameRenderingApparatus, drawingApparatus);
		_displaySettings = std::make_shared<OSServices::DisplaySettingsManager>();

		_mainInputHandler = std::make_shared<PlatformRig::MainInputHandler>();
	}
	
	WindowApparatus::~WindowApparatus()
	{}

	void ShowDebugScreen(StringSection<> screenName)
	{
		ConsoleRig::CrossModule::GetInstance()._services.Call<void>(Fn_ShowScreen, screenName);
	}

	void CommonEventHandling(PlatformRig::WindowApparatus& windowApparatus, PlatformRig::SystemMessageVariant& msgPump)
    {
        if (std::holds_alternative<PlatformRig::InputSnapshot>(msgPump)) {

            auto context = windowApparatus._osWindow->MakeInputContext();
            windowApparatus._mainInputHandler->OnInputEvent(context, std::get<PlatformRig::InputSnapshot>(msgPump));

        } else if (std::holds_alternative<PlatformRig::WindowResize>(msgPump)) {

            auto resize = std::get<PlatformRig::WindowResize>(msgPump);
            auto& frameRig = *windowApparatus._frameRig;

            frameRig.GetTechniqueContext()._frameBufferPool->Reset();
            frameRig.ReleaseDoubleBufferAttachments();
            frameRig.GetTechniqueContext()._attachmentPool->ResetActualized();
            auto desc = windowApparatus._presentationChain->GetDesc();
            desc._width = resize._newWidth;
            desc._height = resize._newHeight;
            windowApparatus._presentationChain->ChangeConfiguration(*windowApparatus._immediateContext, desc);
            frameRig.UpdatePresentationChain(*windowApparatus._presentationChain);

        }
    }

}

