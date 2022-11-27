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
#include "DebuggingDisplays/GPUProfileDisplay.h"
#include "DebuggingDisplays/CPUProfileDisplay.h"
#include "DebuggingDisplays/InvalidAssetDisplay.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/IAnnotator.h"
#include "../RenderCore/IDevice.h"
#include "../RenderOverlays/DebuggingDisplay.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Utility/Profiling/CPUProfiler.h"
#include "../Utility/MemoryUtils.h"

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


	WindowApparatus::WindowApparatus(std::shared_ptr<OverlappedWindow> osWindow, std::shared_ptr<RenderCore::IDevice> device, RenderCore::BindFlag::BitField presentationChainBindFlags)
	{
		_device = device;
		_osWindow = std::move(osWindow);

		auto clientRect = _osWindow->GetRect();
		auto desc = RenderCore::PresentationChainDesc{unsigned(clientRect.second[0] - clientRect.first[0]), unsigned(clientRect.second[1] - clientRect.first[1])};
		desc._bindFlags |= presentationChainBindFlags;
		_presentationChain = _device->CreatePresentationChain(
			_osWindow->GetUnderlyingHandle(),
			desc);
		_windowHandler = std::make_shared<PlatformRig::ResizePresentationChain>(_presentationChain, _device->GetImmediateContext());
		_osWindow->AddWindowHandler(_windowHandler);

		_mainInputHandler = std::make_shared<PlatformRig::MainInputHandler>();
		_osWindow->AddListener(_mainInputHandler);
		
		_immediateContext = _device->GetImmediateContext();
	}
	
	WindowApparatus::~WindowApparatus()
	{

	}

	void InitProfilerDisplays(
		RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugSys, 
		RenderCore::IAnnotator* annotator,
		Utility::HierarchicalCPUProfiler& cpuProfiler)
	{
		if (annotator) {
			auto gpuProfilerDisplay = std::make_shared<PlatformRig::Overlays::GPUProfileDisplay>(*annotator);
			debugSys.Register(gpuProfilerDisplay, "[Profiler] GPU Profiler");
		}
		debugSys.Register(
			std::make_shared<PlatformRig::Overlays::HierarchicalProfilerDisplay>(&cpuProfiler),
			"[Profiler] CPU Profiler");

		/*debugSys.Register(
			std::make_shared<PlatformRig::Overlays::BufferUploadDisplay>(&RenderCore::Techniques::Services::GetBufferUploads()),
			"[Profiler] Buffer uploads");*/

		debugSys.Register(PlatformRig::Overlays::CreateInvalidAssetDisplay(::Assets::Services::GetAssetSetsPtr()), "[Console] Invalid asset display");
		debugSys.SwitchToScreen("[Console] Invalid asset display");
		// debugSys.SwitchToScreen("[Profiler] GPU Profiler");
	}

	void ShowDebugScreen(StringSection<> screenName)
	{
		ConsoleRig::CrossModule::GetInstance()._services.Call<void>(Fn_ShowScreen, screenName);
	}

}

