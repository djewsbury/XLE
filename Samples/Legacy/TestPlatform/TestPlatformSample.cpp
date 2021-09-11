// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TestPlatformScene.h"
#include "../Shared/SampleGlobals.h"

#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/InputTranslator.h"
#include "../../PlatformRig/DebuggingDisplays/GPUProfileDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/CPUProfileDisplay.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformRigUtil.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/DebugHotKeys.h"

#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/LightingParserStandardPlugin.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/PreparedScene.h"

#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/IAnnotator.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderOverlays/Font.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Tools/ToolsRig/BasicManipulators.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/MountingTree.h"

#include "../../OSServices/Log.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Streams/FileSystemMonitor.h"

#include <functional>

unsigned FrameRenderCount = 0;

namespace Sample
{
///////////////////////////////////////////////////////////////////////////////////////////////////

    Utility::HierarchicalCPUProfiler g_cpuProfiler;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PrimaryManagers
    {
    public:
        std::shared_ptr<RenderCore::IDevice> _rDevice;
        std::shared_ptr<RenderCore::IPresentationChain> _presChain;
        PlatformRig::OverlappedWindow _window;

        std::unique_ptr<Assets::Services> _assetServices;
        std::unique_ptr<RenderCore::Assets::Services> _renderAssetServices;

        std::shared_ptr<PlatformRig::GlobalTechniqueContext> _globalTechContext;

        PrimaryManagers()
        {
            auto clientRect = _window.GetRect();

            _rDevice = RenderCore::CreateDevice(RenderCore::Techniques::GetTargetAPI());
            _presChain = _rDevice->CreatePresentationChain(
				_window.GetUnderlyingHandle(), 
                    RenderCore::PresentationChainDesc{unsigned(clientRect.second[0] - clientRect.first[0]), unsigned(clientRect.second[1] - clientRect.first[1])});

            _assetServices = std::make_unique<::Assets::Services>(0);
			_assetServices->AttachCurrentModule();
			ConsoleRig::GlobalServices::GetCrossModule().Publish(*_assetServices);
            _renderAssetServices = std::make_unique<RenderCore::Assets::Services>(_rDevice);
			_renderAssetServices->AttachCurrentModule();

            _window.AddWindowHandler(std::make_shared<PlatformRig::ResizePresentationChain>(_presChain));
            // auto v = _rDevice->GetVersionInformation();
            // _window.SetTitle(StringMeld<128>() << "XLE sample [RenderCore: " << v.first << ", " << v.second << "]");

            _globalTechContext = std::make_shared<PlatformRig::GlobalTechniqueContext>();
        }
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    static std::shared_ptr<PlatformRig::MainInputHandler> CreateInputHandler(
        std::shared_ptr<TestPlatformSceneParser> mainScene, 
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext,
        std::shared_ptr<PlatformRig::IInputListener> cameraInputListener,
        std::shared_ptr<PlatformRig::IInputListener> overlaySystemInputListener);
    static void InitProfilerDisplays(RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugSys);
    static std::shared_ptr<PlatformRig::IInputListener> CreateCameraListener(TestPlatformSceneParser& scene);

    static PlatformRig::FrameRig::RenderResult RenderFrame(
        RenderCore::IThreadContext& context,
        const RenderCore::IResourcePtr& presentationResource,
		RenderCore::Techniques::ParsingContext& parsingContext,
        TestPlatformSceneParser* scene,
        RenderCore::IPresentationChain* presentationChain,
        PlatformRig::IOverlaySystem* overlaySys);

///////////////////////////////////////////////////////////////////////////////////////////////////

    void ExecuteSample()
    {
        using namespace PlatformRig;
        using namespace Sample;

		::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS("Game/xleres"));

        Log(Verbose) << "Building primary managers";
        PrimaryManagers primMan;

            // Some secondary initalisation:
        primMan._renderAssetServices->InitModelCompilers();

            // main scene
        Log(Verbose) << "Creating main scene";
        auto mainScene = std::make_shared<TestPlatformSceneParser>();
        
        {
            Log(Verbose) << "Setup tools and debugging";
            FrameRig frameRig;
            
            InitDebugDisplays(*frameRig.GetDebugSystem());
            InitProfilerDisplays(*frameRig.GetDebugSystem());

            auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
            overlaySwitch->AddSystem(RenderOverlays::DebuggingDisplay::KeyId_Make("~"), PlatformRig::CreateConsoleOverlaySystem());
            frameRig.GetMainOverlaySystem()->AddSystem(overlaySwitch);
            auto mainInputHandler = CreateInputHandler(
                    mainScene, nullptr, CreateCameraListener(*mainScene), 
                    frameRig.GetMainOverlaySystem()->GetInputListener());
            primMan._window.GetInputTranslator().AddListener(mainInputHandler);

            auto stdPlugin = std::make_shared<SceneEngine::LightingParserStandardPlugin>();
            // primMan._assetServices->GetAssetSets().LogReport();

                //  One last object required for rendering:
                //      *   the DeviceContext provides the methods for directly 
                //          interacting with the GPU
            Log(Verbose) << "Setup frame rig and rendering context";
            auto context = primMan._rDevice->GetImmediateContext();

			RenderCore::Techniques::AttachmentPool namedResources;
			RenderCore::Techniques::FrameBufferPool frameBufferPool;

                //  Finally, we execute the frame loop
            for (;;) {
                if (OverlappedWindow::DoMsgPump() == OverlappedWindow::PumpResult::Terminate)
                    break;

                    // ------- Render ----------------------------------------
                RenderCore::Techniques::ParsingContext parserContext(*primMan._globalTechContext, &namedResources, &frameBufferPool);
                // lightingParserContext._plugins.push_back(stdPlugin);

                auto frameResult = frameRig.ExecuteFrame(
                    *context.get(), primMan._presChain.get(), 
                    &g_cpuProfiler,
                    std::bind(
                        RenderFrame, std::placeholders::_1, std::placeholders::_2,
                        std::ref(parserContext), mainScene.get(), 
                        primMan._presChain.get(), 
                        frameRig.GetMainOverlaySystem().get()));

                    // ------- Update ----------------------------------------
                RenderCore::Assets::Services::GetBufferUploads().Update(*context, false);
                mainScene->Update(frameResult._elapsedTime);
                g_cpuProfiler.EndFrame();
                ++FrameRenderCount;
            }
        }

        Log(Verbose) << "Starting shutdown";
        // primMan._assetServices->GetAssetSets().LogReport();
        RenderCore::Metal::DeviceContext::PrepareForDestruction(primMan._rDevice.get(), primMan._presChain.get());

        mainScene.reset();

        primMan._assetServices->GetAssetSets().Clear();
        ConsoleRig::ResourceBoxes_Shutdown();
        
        primMan._renderAssetServices.reset();
		ConsoleRig::GlobalServices::GetCrossModule().Withhold(*primMan._assetServices);
        primMan._assetServices.reset();
        TerminateFileSystemMonitoring();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class UsefulFonts
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _defaultFont0;
		std::shared_ptr<RenderOverlays::Font> _defaultFont1;

        UsefulFonts()
        {
            _defaultFont0 = RenderOverlays::GetX2Font("Raleway", 16);
            _defaultFont1 = RenderOverlays::GetX2Font("Vera", 16);
        }
    };

    PlatformRig::FrameRig::RenderResult RenderFrame(
        RenderCore::IThreadContext& context,
        const RenderCore::IResourcePtr& presentationResource,
		RenderCore::Techniques::ParsingContext& parsingContext,
        TestPlatformSceneParser* scene,
        RenderCore::IPresentationChain* presentationChain,
        PlatformRig::IOverlaySystem* overlaySys)
    {
        CPUProfileEvent pEvnt("RenderFrame", g_cpuProfiler);
		auto& namedRes = parsingContext.GetNamedResources();
        auto viewContext = presentationChain->GetDesc();
        auto samples = RenderCore::TextureSamples::Create((uint8)Tweakable("SamplingCount", 1), (uint8)Tweakable("SamplingQuality", 0));
        namedRes.Bind(RenderCore::FrameBufferProperties{viewContext->_width, viewContext->_height, samples});
        namedRes.Bind(0u, presentationResource);

            //  some scene might need a "prepare" step to 
            //  build some resources before the main render occurs.
        scene->PrepareFrame(context);

        using namespace SceneEngine;
        if (scene) {
            UInt2 presChainDims(presentationChain->GetDesc()->_width, presentationChain->GetDesc()->_height);
            LightingParser_ExecuteScene(
                context, parsingContext, *scene, scene->GetCameraDesc(),
                SceneTechniqueDesc{
                    presChainDims, 
                    (Tweakable("LightingModel", 0) == 0) ? SceneTechniqueDesc::LightingModel::Deferred : SceneTechniqueDesc::LightingModel::Forward,
					{},
					(uint8)Tweakable("SamplingCount", 1), (uint8)Tweakable("SamplingQuality", 0)});
        }

        if (overlaySys) {
            overlaySys->RenderToScene(context, parsingContext);
        }

        auto& usefulFonts = ConsoleRig::FindCachedBox<UsefulFonts>();
        DrawPendingResources(context, parsingContext, usefulFonts._defaultFont0);

        if (overlaySys) {
            overlaySys->RenderWidgets(context, parsingContext);
        }

        return PlatformRig::FrameRig::RenderResult(parsingContext.HasPendingAssets());
    }


    static std::shared_ptr<PlatformRig::MainInputHandler> CreateInputHandler(
        std::shared_ptr<TestPlatformSceneParser> mainScene, 
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext,
        std::shared_ptr<PlatformRig::IInputListener> cameraInputListener,
        std::shared_ptr<PlatformRig::IInputListener> overlaySystemInputListener)
    {
        auto mainInputHandler = std::make_shared<PlatformRig::MainInputHandler>();
        mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("xleres/hotkey.txt"));
        if (overlaySystemInputListener) {
            mainInputHandler->AddListener(overlaySystemInputListener);
        }

            // tie in input for player character & camera
        mainInputHandler->AddListener(std::move(cameraInputListener));

        return std::move(mainInputHandler);
    }

    static void InitProfilerDisplays(RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugSys)
    {
        // if (g_gpuProfiler) {
        //     auto gpuProfilerDisplay = std::make_shared<PlatformRig::Overlays::GPUProfileDisplay>(*g_gpuProfiler.get());
        //     debugSys.Register(gpuProfilerDisplay, "[Profiler] GPU Profiler");
        // }
        debugSys.Register(
            std::make_shared<PlatformRig::Overlays::HierarchicalProfilerDisplay>(&g_cpuProfiler), 
            "[Profiler] CPU Profiler");
    }

    static std::shared_ptr<PlatformRig::IInputListener> CreateCameraListener(TestPlatformSceneParser& scene)
    {
        auto manipulators = std::make_shared<ToolsRig::ManipulatorStack>();
        manipulators->Register(
            ToolsRig::ManipulatorStack::CameraManipulator,
            ToolsRig::CreateCameraManipulator(scene.GetCameraPtr()));
        return std::move(manipulators);
    }
}

