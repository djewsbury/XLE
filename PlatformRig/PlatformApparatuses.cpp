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
#include "../RenderCore/Techniques/RenderPass.h"		// for ResetFrameBufferPool
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/IDevice.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Utility/Profiling/CPUProfiler.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/FunctionUtils.h"

namespace PlatformRig
{

	static auto Fn_ShowScreen = ConstHash64<'show', 'scre', 'en'>::Value;

	DebugOverlaysApparatus::DebugOverlaysApparatus(
		const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& immediateDrawingApparatus,
		PlatformRig::FrameRig& frameRig)
	{
		using DebugScreensSystem = RenderOverlays::DebuggingDisplay::DebugScreensSystem;
		_debugSystem = std::make_shared<DebugScreensSystem>();
		_debugSystem->Register(
			frameRig.CreateDisplay(_debugSystem),
			"FrameRig", DebugScreensSystem::SystemDisplay);

		_debugScreensOverlaySystem = std::make_shared<PlatformRig::OverlaySystemSet>();
		_debugScreensOverlaySystem->AddSystem(CreateDebugScreensOverlay(_debugSystem, immediateDrawingApparatus->_immediateDrawables, immediateDrawingApparatus->_fontRenderingManager));

		auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
		overlaySwitch->AddSystem(PlatformRig::KeyId_Make("~"), PlatformRig::CreateConsoleOverlaySystem(*immediateDrawingApparatus));
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
		_presentationChain = device->CreatePresentationChain(
			_osWindow->GetUnderlyingHandle(),
			desc);

		_frameRig = std::make_shared<FrameRig>(frameRenderingApparatus, drawingApparatus);

		_mainInputHandler = std::make_shared<PlatformRig::MainInputHandler>();
		auto threadId = std::this_thread::get_id();

		_osWindow->OnMessage().Bind(
			[	wfr = std::weak_ptr<FrameRig>{_frameRig}, wpc = std::weak_ptr<RenderCore::IPresentationChain>(_presentationChain),
				immediateContext=_immediateContext.get(),
				wih = std::weak_ptr<MainInputHandler>(_mainInputHandler), window=_osWindow.get(), threadId](auto&& msg) {

				assert(std::this_thread::get_id() == threadId);

				if (std::holds_alternative<WindowResize>(msg)) {
					auto frameRig = wfr.lock();
					auto presentationChain = wpc.lock();
					if (!frameRig || !presentationChain) return;

					auto resize = std::get<WindowResize>(msg);
					RenderCore::Techniques::ResetFrameBufferPool(*frameRig->GetTechniqueContext()._frameBufferPool);
					frameRig->GetTechniqueContext()._attachmentPool->ResetActualized();
					auto desc = presentationChain->GetDesc();
					desc._width = resize._newWidth;
					desc._height = resize._newHeight;
					presentationChain->ChangeConfiguration(*immediateContext, desc);
					frameRig->UpdatePresentationChain(*presentationChain);
				} else if (std::holds_alternative<InputSnapshot>(msg)) {
					auto inputHandler = wih.lock();
					if (!inputHandler) return;
					auto context = window->MakeInputContext();
					inputHandler->OnInputEvent(context, std::get<InputSnapshot>(msg));
				}
			});
	}
	
	WindowApparatus::~WindowApparatus()
	{

	}

	void ShowDebugScreen(StringSection<> screenName)
	{
		ConsoleRig::CrossModule::GetInstance()._services.Call<void>(Fn_ShowScreen, screenName);
	}

}

