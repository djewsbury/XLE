// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleRig.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/DebugHotKeys.h"
#include "../../PlatformRig/DebugScreenRegistry.h"

#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/SubFrameEvents.h"
#include "../../RenderCore/DeviceInitialization.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Vulkan/IDeviceVulkan.h"

#include "../../Tools/ToolsRig/PreviewSceneRegistry.h"
#include "../../Tools/EntityInterface/EntityInterface.h"

#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"

#include "../../OSServices/Log.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/Console.h"

#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/StringFormat.h"

#include <functional>

namespace Sample
{
    void InstallDefaultDebuggingDisplays(SampleGlobals& globals);   // DefaultDebuggingDisplays.cpp

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay, const SampleConfiguration& config)
    {
		SampleGlobals sampleGlobals;

        Log(Verbose) << "Building primary managers" << std::endl;
        auto renderAPI = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

        auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>();
        auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));

        auto osWindow = std::make_unique<PlatformRig::Window>();
        if (config._initialWindowSize)
            osWindow->Resize((*config._initialWindowSize)[0], (*config._initialWindowSize)[1]);
        if (auto* vulkanInstance = (RenderCore::IAPIInstanceVulkan*)renderAPI->QueryInterface(typeid(RenderCore::IAPIInstanceVulkan).hash_code())) {
            Log(Verbose) << "-------------- vulkan instance --------------" << std::endl;
            Log(Verbose) << vulkanInstance->LogInstance() << std::endl;

            auto count = renderAPI->GetDeviceConfigurationCount();
            for (unsigned c=0; c<count; ++c) {
                Log(Verbose) << "-------------- vulkan properties for device configuration (" << c << ") --------------" << std::endl;
                Log(Verbose) << vulkanInstance->LogPhysicalDevice(c) << std::endl;
            }
        }

        auto capability = renderAPI->QueryFeatureCapability(0);
        sampleGlobals._renderDevice = renderAPI->CreateDevice(0, capability);

        auto techniqueServices = ConsoleRig::MakeAttachablePtr<RenderCore::Techniques::Services>(sampleGlobals._renderDevice);
        ConsoleRig::AttachablePtr<ToolsRig::IPreviewSceneRegistry> previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();
        ConsoleRig::AttachablePtr<EntityInterface::IEntityMountingTree> entityMountingTree = EntityInterface::CreateMountingTree();
        ::ConsoleRig::GlobalServices::GetInstance().LoadDefaultPlugins();

            // Many objects are initialized by via helper objects called "apparatuses". These construct and destruct
            // the objects required to do meaningful work. Often they also initialize the "services" singletons
            // as they go along
            // We separate this initialization work like this to provide some flexibility. It's only necessary to
            // construct as much as will be required for the specific use case 
        sampleGlobals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._immediateDrawingApparatus = std::make_shared<RenderCore::Techniques::ImmediateDrawingApparatus>(sampleGlobals._drawingApparatus);
        sampleGlobals._primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(std::move(osWindow), sampleGlobals._drawingApparatus.get(), *sampleGlobals._frameRenderingApparatus);
        {
            auto v = sampleGlobals._renderDevice->GetDesc();
            StringMeld<128> meld;
            if (!config._windowTitle.empty()) meld << config._windowTitle;
            else meld << "XLE sample";
            meld << " [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]";
            sampleGlobals._windowApparatus->_osWindow->SetTitle(meld);
        }

        {
                //  Create the debugging system, and add any "displays"
                //  If we have any custom displays to add, we can add them here. Often it's 
                //  useful to create a debugging display to go along with any new feature. 
                //  It just provides a convenient architecture for visualizing important information.
            Log(Verbose) << "Setup tools and debugging" << std::endl;
            auto& frameRig = *sampleGlobals._windowApparatus->_frameRig;
            sampleGlobals._debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(sampleGlobals._immediateDrawingApparatus, frameRig);
            frameRig.SetDebugScreensOverlaySystem(sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem);
            frameRig.SetMainOverlaySystem(sampleOverlay);
            techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*sampleGlobals._windowApparatus->_immediateContext);

            InstallDefaultDebuggingDisplays(sampleGlobals);

            Log(Verbose) << "Call OnStartup and start the frame loop" << std::endl;
            sampleOverlay->OnStartup(sampleGlobals);
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("rawos/hotkey.dat"));
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem->GetInputListener());
            auto sampleListener = sampleOverlay->GetInputListener();
            if (sampleListener)
                sampleGlobals._windowApparatus->_mainInputHandler->AddListener(sampleListener);

            frameRig.UpdatePresentationChain(*sampleGlobals._windowApparatus->_presentationChain);

            RenderCore::Techniques::SetThreadContext(sampleGlobals._windowApparatus->_immediateContext);
            techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*sampleGlobals._windowApparatus->_immediateContext);

            TRY {
                    // Pump a single frame to ensure we have some content when the window appears (and then show it)
                frameRig.ExecuteFrame(*sampleGlobals._windowApparatus);
                sampleGlobals._windowApparatus->_osWindow->Show();

                    //  Finally, we execute the frame loop
                for (;;) {
                    auto msgPump = PlatformRig::Window::DoMsgPump();
                    if (msgPump == PlatformRig::Window::PumpResult::Terminate) break;
                    if (msgPump == PlatformRig::Window::PumpResult::Background) {
                        // Bail if we're minimized (don't have to check this in the foreground case)
                        auto presChainDesc = sampleGlobals._windowApparatus->_presentationChain->GetDesc();
                        if (!(presChainDesc._width * presChainDesc._height)) {
                            Threading::Sleep(64);       // minimized and inactive
                            continue;
                        }
                    }

                        // ------- Render ----------------------------------------
                    auto frameResult = frameRig.ExecuteFrame(*sampleGlobals._windowApparatus);
                        // ------- Update ----------------------------------------
                    sampleOverlay->OnUpdate(frameResult._elapsedTime * Tweakable("TimeScale", 1.0f));
                    sampleGlobals._frameRenderingApparatus->_frameCPUProfiler->EndFrame();

                    if (msgPump == PlatformRig::Window::PumpResult::Background)
                        Threading::Sleep(16);       // yield some process time
                }
            } CATCH(const std::exception& e) {
                Log(Error) << "Shutting down due to exception in frame rig. Exception details follow:" << std::endl;
                Log(Error) << e.what();
            } CATCH_END

            RenderCore::Techniques::SetThreadContext(nullptr);
            sampleOverlay.reset();		// (ensure this gets destroyed before the engine is shutdown)
        }

            //  There are some manual destruction operations we need to perform...
            //  (note that currently some shutdown steps might get skipped if we get 
            //  an unhandled exception)
            //  Before we go too far, though, let's log a list of active assets.
        Log(Verbose) << "Starting shutdown" << std::endl;
        ::ConsoleRig::GlobalServices::GetInstance().PrepareForDestruction();
        sampleGlobals._renderDevice->PrepareForDestruction();
        ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt);
    }

	void ISampleOverlay::OnStartup(const SampleGlobals& globals) {}
	void ISampleOverlay::OnUpdate(float deltaTime) {}

    SampleGlobals::SampleGlobals() = default;
	SampleGlobals::~SampleGlobals() = default;
}

