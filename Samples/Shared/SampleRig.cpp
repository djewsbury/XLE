// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/SingleWindowAppRig.h"
#include "../../PlatformRig/DebugScreenRegistry.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/DebuggingDisplays/HelpDisplay.h"
#include "../../RenderOverlays/SimpleVisualization.h"       // for DrawBottomOfScreenErrorMsg

#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"

#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"

#include "../../OSServices/Log.h"

namespace Sample
{
	static void InstallSampleDebuggingDisplays(PlatformRig::AppRigGlobals& globals);
	static void LogRenderAPIInstanceStartup(RenderCore::IAPIInstance& apiInstance, const void* underlyingWindowHandle);
	static void OnRenderTargetUpdate(
		PlatformRig::IOverlaySystem* mainOverlay,
		PlatformRig::IOverlaySystem& debuggingOverlay,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats);

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay, Formatters::CommandLineFormatter<char>& cmdLine)
	{
		SampleConfiguration config;
		sampleOverlay->Configure(config);

		PlatformRig::StartupLoop startup { cmdLine };
		for (;;) {
			auto msg = startup.Pump();

			if (std::holds_alternative<PlatformRig::StartupLoop::ConfigureGlobalServices*>(msg)) {
				auto& pkt = *std::get<PlatformRig::StartupLoop::ConfigureGlobalServices*>(msg);

			}

			if (std::holds_alternative<PlatformRig::StartupLoop::ConfigureRenderDevice*>(msg)) {
				auto& pkt = *std::get<PlatformRig::StartupLoop::ConfigureRenderDevice*>(msg);
				
				LogRenderAPIInstanceStartup(*pkt._apiInstance, pkt._window->GetUnderlyingHandle());
				pkt._presentationChainBindFlags = config._presentationChainBindFlags;
			}

			else if (std::holds_alternative<PlatformRig::StartupLoop::ConfigureWindowInitialState*>(msg)) {
				auto& pkt = *std::get<PlatformRig::StartupLoop::ConfigureWindowInitialState*>(msg);

				 if (config._initialWindowSize)
					pkt._window->Resize((*config._initialWindowSize)[0], (*config._initialWindowSize)[1]);

				auto v = startup._renderDevice->GetDesc();
				StringMeld<128> meld;
				if (!config._windowTitle.empty()) meld << config._windowTitle;
				else meld << "XLE sample";
				meld << " [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]";
				pkt._window->SetTitle(meld);
			}

			else if (std::holds_alternative<PlatformRig::StartupLoop::StartupFinished>(msg)) {
				break;
			}

		}

		auto& globals = startup._globals;

		auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));
		auto cleanup = AutoCleanup([rawosmnt]() { ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt); });

		PlatformRig::ConfigureDevelopmentFeatures devFeatures;
		devFeatures._installDefaultDebuggingDisplays = true;
		devFeatures._useFrameRigSystemDisplay = true;
		devFeatures._installHotKeysHandler = true;
		devFeatures.Apply(globals);
		InstallSampleDebuggingDisplays(globals);

		auto sampleOverlayAsOverlay = std::dynamic_pointer_cast<PlatformRig::IOverlaySystem>(sampleOverlay);

		if (sampleOverlayAsOverlay)
			globals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::CreateInputListener(sampleOverlayAsOverlay));
		sampleOverlay->OnStartup(globals);

			//  Finally, we execute the frame loop.
		auto msgLoop = startup.ShowWindowAndBeginMessageLoop();
		for (;;) {
			auto msg = msgLoop.Pump();

					// ------- Update -----------------------------------------
			if (std::holds_alternative<PlatformRig::MessageLoop::UpdateFrame>(msg)) {
				sampleOverlay->OnUpdate(std::get<PlatformRig::MessageLoop::UpdateFrame>(msg)._deltaTime);
			}

					// ------- Render -----------------------------------------
			else if (std::holds_alternative<PlatformRig::MessageLoop::RenderFrame>(msg)) {
				auto& parserContext = std::get<PlatformRig::MessageLoop::RenderFrame>(msg)._parsingContext;
				TRY {
					if (sampleOverlayAsOverlay)
						sampleOverlayAsOverlay->Render(parserContext);
					globals._debugOverlaysApparatus->_debugScreensOverlaySystem->Render(parserContext);
				} CATCH(const std::exception& e) {
					RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *globals._overlayApparatus, e.what());
				} CATCH_END
			}

					// ------- Render target update ---------------------------
			else if (std::holds_alternative<PlatformRig::MessageLoop::OnRenderTargetUpdate>(msg)) {
				auto& rtu = std::get<PlatformRig::MessageLoop::OnRenderTargetUpdate>(msg);
				OnRenderTargetUpdate(
					sampleOverlayAsOverlay.get(), *globals._debugOverlaysApparatus->_debugScreensOverlaySystem,
					rtu._preregAttachments, rtu._fbProps, rtu._systemAttachmentFormats);
			}

					// ------- Quit -------------------------------------------
			else if (std::holds_alternative<OSServices::ShutdownRequest>(msg)) {
				break;
			} 
		}

		sampleOverlayAsOverlay.reset();
		sampleOverlay.reset();		// (ensure this gets destroyed before the engine is shutdown)
	}

	static void InstallSampleDebuggingDisplays(PlatformRig::AppRigGlobals& globals)
	{
		auto helpDisplay = PlatformRig::Overlays::CreateHelpDisplay();
		helpDisplay->AddKey("Ctrl ←", "Prev Screen");
		helpDisplay->AddKey("Ctrl →", "Next Screen");
		helpDisplay->AddKey("Esc", "Back");
		helpDisplay->AddKey("~", "Console");
		helpDisplay->AddText("Bound keys can access {color:66d0a4}full-screen overlays{color:} which breakdown profiling and debugging information");
		helpDisplay->AddText("On first startup, certain compilation operations may heavily consume system resources. This may take several minutes. See the {color:74bfe3}Compile Progress{color:} screen for details.");
		globals._displayRegistrations.emplace_back(
			"[Console] Key Binding Help",
			std::move(helpDisplay));
	}

	static void OnRenderTargetUpdate(
		PlatformRig::IOverlaySystem* mainOverlay,
		PlatformRig::IOverlaySystem& debuggingOverlay,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
		const RenderCore::FrameBufferProperties& fbProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
	{
		if (mainOverlay) {
			mainOverlay->OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
			auto updatedAttachments = PlatformRig::InitializeColorLDR(preregAttachments);
			debuggingOverlay.OnRenderTargetUpdate(updatedAttachments, fbProps, systemAttachmentFormats);
		} else {
			debuggingOverlay.OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
		}
	}

	static void LogRenderAPIInstanceStartup(RenderCore::IAPIInstance& apiInstance, const void* underlyingWindowHandle)
	{
		if (auto* vulkanInstance = query_interface_cast<RenderCore::IAPIInstanceVulkan*>(&apiInstance)) {
			Log(Verbose) << "-------------- vulkan instance --------------" << std::endl;
			Log(Verbose) << vulkanInstance->LogInstance(underlyingWindowHandle) << std::endl;

			auto count = apiInstance.GetDeviceConfigurationCount();
			for (unsigned c=0; c<count; ++c) {
				Log(Verbose) << "-------------- vulkan properties for device configuration (" << c << ") --------------" << std::endl;
				Log(Verbose) << vulkanInstance->LogPhysicalDevice(c) << std::endl;
			}
		}
	}

	void ISampleOverlay::OnStartup(const PlatformRig::AppRigGlobals&) {}
	void ISampleOverlay::OnUpdate(float) {}
	void ISampleOverlay::Configure(SampleConfiguration&) {}

}

