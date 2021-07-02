// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleRig.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/PlatformRigUtil.h"
#include "../../PlatformRig/DebugHotKeys.h"

#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Init.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/IThreadContext.h"

#include "../../Tools/ToolsRig/PreviewSceneRegistry.h"

#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"

#include "../../OSServices/Log.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../ConsoleRig/Console.h"

#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/StringFormat.h"

#include "../../RenderCore/Metal/DeviceContext.h"		// (for PrepareForDestruction)

#include <functional>

namespace Sample
{
	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay)
    {
		SampleGlobals sampleGlobals;

            // We need to startup some basic objects:
            //      * OverlappedWindow (corresponds to a single basic window on Windows)
            //      * RenderDevice & presentation chain
            //      * BufferUploads
            //
            // Note that the render device should be created first, so that the window
            // object is destroyed before the device is destroyed.
        Log(Verbose) << "Building primary managers" << std::endl;
        sampleGlobals._renderDevice = RenderCore::CreateDevice(RenderCore::Techniques::GetTargetAPI());

        auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>();
        auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));
        auto rawosmnt2 = ::Assets::MainFileSystem::GetMountingTree()->Mount("", ::Assets::MainFileSystem::GetDefaultFileSystem());
        auto techniqueServices = ConsoleRig::MakeAttachablePtr<RenderCore::Techniques::Services>(sampleGlobals._renderDevice);
        ConsoleRig::AttachablePtr<ToolsRig::IPreviewSceneRegistry> previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();

		::ConsoleRig::GlobalServices::GetInstance().LoadDefaultPlugins();

            // Many objects are initialized by via helper objects called "apparatues". These construct and destruct
            // the objects required to do meaningful work. Often they also initialize the "services" singletons
            // as they go along
            // We separate this initialization work like this to provide some flexibility. It's only necessary to
            // construct as much as will be required for the specific use case 
        sampleGlobals._windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._immediateDrawingApparatus = std::make_shared<RenderCore::Techniques::ImmediateDrawingApparatus>(sampleGlobals._drawingApparatus);
        sampleGlobals._primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(sampleGlobals._renderDevice);
        auto v = sampleGlobals._renderDevice->GetDesc();
        sampleGlobals._windowApparatus->_osWindow->SetTitle(StringMeld<128>() << "XLE sample [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]");

            //  Create the debugging system, and add any "displays"
            //  If we have any custom displays to add, we can add them here. Often it's 
            //  useful to create a debugging display to go along with any new feature. 
            //  It just provides a convenient architecture for visualizing important information.
        Log(Verbose) << "Setup tools and debugging" << std::endl;
        PlatformRig::FrameRig frameRig(sampleGlobals._primaryResourcesApparatus->_subFrameEvents);
        auto debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(sampleGlobals._immediateDrawingApparatus, frameRig);
        PlatformRig::InitProfilerDisplays(*debugOverlaysApparatus->_debugSystem, &sampleGlobals._windowApparatus->_immediateContext->GetAnnotator(), *sampleGlobals._frameRenderingApparatus->_frameCPUProfiler);
        frameRig.SetDebugScreensOverlaySystem(debugOverlaysApparatus->_debugScreensOverlaySystem);
        frameRig.SetMainOverlaySystem(sampleOverlay); // (disabled temporarily)

            //  Setup input:
            //      * We create a main input handler, and tie that to the window to receive inputs
            //      * We can add secondary input handles to the main input handler as required
            //      * The order in which we add handlers determines their priority in intercepting messages
        Log(Verbose) << "Setup input" << std::endl;        
        sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("xleres/hotkey.txt"));
        auto sampleListener = sampleOverlay->GetInputListener();
        if (sampleListener)
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(sampleListener);
        sampleGlobals._windowApparatus->_mainInputHandler->AddListener(debugOverlaysApparatus->_debugScreensOverlaySystem->GetInputListener());

        Log(Verbose) << "Call OnStartup and start the frame loop" << std::endl;
        sampleOverlay->OnStartup(sampleGlobals);
        frameRig.UpdatePresentationChain(*sampleGlobals._windowApparatus->_presentationChain);
        sampleGlobals._windowApparatus->_windowHandler->_onResize.Bind(
            [fra = std::weak_ptr<RenderCore::Techniques::FrameRenderingApparatus>{sampleGlobals._frameRenderingApparatus},
             ps = std::weak_ptr<RenderCore::IPresentationChain>(sampleGlobals._windowApparatus->_presentationChain), &frameRig](unsigned, unsigned) {
                auto apparatus = fra.lock();
                if (apparatus)
                    RenderCore::Techniques::ResetFrameBufferPool(*apparatus->_frameBufferPool);
                auto presChain = ps.lock();
                if (presChain)
                    frameRig.UpdatePresentationChain(*presChain);
            });

        RenderCore::Techniques::SetThreadContext(sampleGlobals._windowApparatus->_immediateContext);

            //  Finally, we execute the frame loop
        while (PlatformRig::OverlappedWindow::DoMsgPump() != PlatformRig::OverlappedWindow::PumpResult::Terminate) {
                // ------- Render ----------------------------------------
            auto frameResult = frameRig.ExecuteFrame(*sampleGlobals._windowApparatus, *sampleGlobals._frameRenderingApparatus, sampleGlobals._drawingApparatus.get());
                // ------- Update ----------------------------------------
            sampleOverlay->OnUpdate(frameResult._elapsedTime * Tweakable("TimeScale", 1.0f));
            sampleGlobals._frameRenderingApparatus->_frameCPUProfiler->EndFrame();
        }

		RenderCore::Techniques::SetThreadContext(nullptr);
        sampleOverlay.reset();		// (ensure this gets destroyed before the engine is shutdown)

            //  There are some manual destruction operations we need to perform...
            //  (note that currently some shutdown steps might get skipped if we get 
            //  an unhandled exception)
            //  Before we go too far, though, let's log a list of active assets.
        Log(Verbose) << "Starting shutdown" << std::endl;
        RenderCore::Metal::DeviceContext::PrepareForDestruction(sampleGlobals._renderDevice.get(), sampleGlobals._windowApparatus->_presentationChain.get());
        ConsoleRig::ResourceBoxes_Shutdown();
        ::Assets::Services::GetAssetSets().Clear();
        ::ConsoleRig::GlobalServices::GetInstance().UnloadDefaultPlugins();
        ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt2);
        ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt);
    }

	void ISampleOverlay::OnStartup(const SampleGlobals& globals) {}
	void ISampleOverlay::OnUpdate(float deltaTime) {}
}

