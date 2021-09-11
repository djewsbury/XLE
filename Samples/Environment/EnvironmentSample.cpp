// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EnvironmentScene.h"

#include "../Shared/SampleInputHandler.h"
#include "../Shared/SampleGlobals.h"
#include "../Shared/CharactersScene.h"
#include "../Shared/PlacementsOverlaySystem.h"
#include "../Shared/TerrainOverlaySystem.h"

#include "../../PlatformRig/OverlappedWindow.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/InputTranslator.h"
#include "../../PlatformRig/DebuggingDisplays/GPUProfileDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/CPUProfileDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/DynamicImpostersDisplay.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformRigUtil.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/Screenshot.h"

#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/LightingParserStandardPlugin.h"
#include "../../SceneEngine/LightingParserContext.h"
#include "../../SceneEngine/PreparedScene.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../SceneEngine/PlacementsQuadTreeDebugger.h"
#include "../../SceneEngine/IntersectionTest.h"

#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/IAnnotator.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderOverlays/Font.h"
#include "../../PlatformRig/DebugHotKeys.h"
#include "../../RenderOverlays/Overlays/ShadowFrustumDebugger.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/MountingTree.h"
#include "../../Assets/OSFileSystem.h"
#include "../../Assets/IntermediateAssets.h"

#include "../../Tools/ToolsRig/GenerateAO.h"

#include "../../OSServices/Log.h"
#include "../../ConsoleRig/Console.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Threading/ThreadingUtils.h"

#include <functional>

unsigned FrameRenderCount = 0;

// static void RunPerformanceTest();

namespace Sample
{
///////////////////////////////////////////////////////////////////////////////////////////////////

        // "GPU profiler" doesn't have a place to live yet. We just manage it here, at 
        //  the top level
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
            _presChain = _rDevice->CreatePresentationChain(_window.GetUnderlyingHandle(), 
				RenderCore::PresentationChainDesc{unsigned(clientRect.second[0] - clientRect.first[0]), unsigned(clientRect.second[1] - clientRect.first[1])});
			RenderCore::Techniques::SetThreadContext(_rDevice->GetImmediateContext());

            _assetServices = std::make_unique<::Assets::Services>(0);
			_assetServices->AttachCurrentModule();
			ConsoleRig::GlobalServices::GetCrossModule().Publish(*_assetServices);

            _renderAssetServices = std::make_unique<RenderCore::Assets::Services>(_rDevice);
			_renderAssetServices->AttachCurrentModule();

            _window.AddWindowHandler(std::make_shared<PlatformRig::ResizePresentationChain>(_presChain));
            auto v = _rDevice->GetDesc();
            _window.SetTitle(StringMeld<128>() << "XLE sample [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]");

            _globalTechContext = std::make_shared<PlatformRig::GlobalTechniqueContext>();
        }
    };

    class UsefulFonts
    {
    public:
		std::shared_ptr<RenderOverlays::Font> _defaultFont0;
		std::shared_ptr<RenderOverlays::Font> _defaultFont1;

        UsefulFonts()
        {
            _defaultFont0 = RenderOverlays::GetX2Font("Raleway", 18);
            _defaultFont1 = RenderOverlays::GetX2Font("Vera", 18);
        }
    };

    

    static std::shared_ptr<PlatformRig::MainInputHandler> CreateInputHandler(
        EnvironmentSceneParser& mainScene, 
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionTestContext,
        std::shared_ptr<PlatformRig::IInputListener> cameraInputListener,
        std::shared_ptr<PlatformRig::IInputListener> overlaySystemInputListener)
    {
        auto mainInputHandler = std::make_shared<PlatformRig::MainInputHandler>();
        mainInputHandler->AddListener(RenderOverlays::MakeHotKeysHandler("xleres/hotkey.txt"));
        if (overlaySystemInputListener) {
            mainInputHandler->AddListener(std::move(overlaySystemInputListener));
        }

            // tie in input for player character & camera
        mainInputHandler->AddListener(std::move(cameraInputListener));
        mainInputHandler->AddListener(mainScene.GetPlayerCharacter());

            // some special input options for samples
        mainInputHandler->AddListener(
            std::make_shared<SampleInputHandler>(
                mainScene.GetPlayerCharacter(), mainScene.GetTerrainManager(), std::move(intersectionTestContext)));

        return std::move(mainInputHandler);
    }

    static void SetupCompilers(PrimaryManagers& primMan)
    {
        primMan._renderAssetServices->InitModelCompilers();

            // Add compiler for precalculated internal AO
            // (note -- requires ToolsRig library for this)
        auto& asyncMan = ::Assets::Services::GetAsyncMan();
        auto& compilers = asyncMan.GetIntermediateCompilers();
        auto aoGeoCompiler = std::make_shared<ToolsRig::AOSupplementCompiler>(primMan._rDevice->GetImmediateContext());
        compilers.AddCompiler(
            ToolsRig::AOSupplementCompiler::CompilerType,
            std::move(aoGeoCompiler));
    }

    static PlatformRig::FrameRig::RenderResult RenderFrame(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& presentationResource,
        SceneEngine::LightingParserContext& lightingParserContext, EnvironmentSceneParser* scene,
        RenderCore::IPresentationChain* presentationChain,
        PlatformRig::IOverlaySystem* overlaySys);

    void ExecuteSample(const char finalsDirectory[])
    {
        using namespace PlatformRig;
        using namespace Sample;

        // RunPerformanceTest();

		::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS("Game/xleres"));

            // We need to startup some basic objects:
            //      * OverlappedWindow (corresponds to a single basic window on Windows)
            //      * RenderDevice & presentation chain
            //      * ::Assets::Services
            //      * RenderCore::Assets::Services
        Log(Verbose) << "Building primary managers" << std::endl;
        PrimaryManagers primMan;

            // Some secondary initalisation:
        SetupCompilers(primMan);

            // main scene
        Log(Verbose) << "Creating main scene" << std::endl;
        auto mainScene = std::make_shared<EnvironmentSceneParser>(finalsDirectory);
        
        {
                //  Create the debugging system, and add any "displays"
                //  These are optional. They are for debugging and development tasks.
            Log(Verbose) << "Setup tools and debugging" << std::endl;
            FrameRig frameRig;
            
            InitDebugDisplays(*frameRig.GetDebugSystem());

            auto gpuProfilerDisplay = std::make_shared<PlatformRig::Overlays::GPUProfileDisplay>(primMan._rDevice->GetImmediateContext()->GetAnnotator());
            frameRig.GetDebugSystem()->Register(gpuProfilerDisplay, "[Profiler] GPU Profiler");

			frameRig.GetDebugSystem()->Register(
                std::make_shared<PlatformRig::Overlays::HierarchicalProfilerDisplay>(&g_cpuProfiler), 
                "[Profiler] CPU Profiler");

            frameRig.GetDebugSystem()->Register(
                std::make_shared<::Overlays::ShadowFrustumDebugger>(mainScene), 
                "[Test] Shadow frustum debugger");

            frameRig.GetDebugSystem()->Register(
                std::make_shared<SceneEngine::PlacementsQuadTreeDebugger>(mainScene->GetPlacementManager(), mainScene->GetPlacementCells()),
                "[Placements] Culling");

            frameRig.GetDebugSystem()->Register(
                std::make_shared<::PlatformRig::Overlays::DynamicImpostersDisplay>(mainScene->GetDynamicImposters()),
                "[Placements] Imposters");

            auto intersectionContext = std::make_shared<SceneEngine::IntersectionTestContext>(
                primMan._rDevice->GetImmediateContext(), mainScene, 
                primMan._presChain->GetDesc(), primMan._globalTechContext);

                //  We also create some "overlay systems" in this sample. 
                //  Again, it's optional. An overlay system will redirect all input
                //  to some alternative task. 
                //
                //  For example, when the "overlay system" for the console is opened 
                //  it will consume all input and prevent other systems from
                //  getting input events. This is convenient, because we don't want
                //  key presses to cause other things to happen while we enter text
                //  into the console.
                //
                //  But the console is just a widget. So we can also add it as a 
                //  debugging display. In this case, it won't consume all input, just
                //  the specific input directed at it.
            auto overlaySwitch = std::make_shared<PlatformRig::OverlaySystemSwitch>();
            {
                using RenderOverlays::DebuggingDisplay::KeyId_Make;
                overlaySwitch->AddSystem(KeyId_Make("~"), PlatformRig::CreateConsoleOverlaySystem());
                if (mainScene->GetPlacementManager()) {
                    overlaySwitch->AddSystem(
                        KeyId_Make("1"),
                        Sample::CreatePlacementsEditorOverlaySystem(
                            mainScene->GetPlacementManager(), 
                            mainScene->GetPlacementCells(),
                            mainScene->GetTerrainManager(), 
                            intersectionContext));
                }
                if (mainScene->GetTerrainManager()) {
                    overlaySwitch->AddSystem(
                        KeyId_Make("2"),
                        Sample::CreateTerrainEditorOverlaySystem(
                            mainScene->GetTerrainManager(), intersectionContext));
                }
            }

            frameRig.GetMainOverlaySystem()->AddSystem(overlaySwitch);

                //  We need to create input handlers, and then direct input from the 
                //  OS to that input handler.
            auto cameraInputHandler = std::make_shared<PlatformRig::Camera::CameraInputHandler>(
                mainScene->GetCameraPtr(), mainScene->GetPlayerCharacter(), CharactersScale);
            auto mainInputHandler = CreateInputHandler(
                *mainScene, intersectionContext, cameraInputHandler, frameRig.GetMainOverlaySystem()->GetInputListener());

            primMan._window.GetInputTranslator().AddListener(mainInputHandler);
            auto stdPlugin = std::make_shared<SceneEngine::LightingParserStandardPlugin>();

                //  One last object required for rendering:
                //      *   the DeviceContext provides the methods for directly 
                //          interacting with the GPU
            Log(Verbose) << "Setup frame rig and rendering context" << std::endl;
            auto context = primMan._rDevice->GetImmediateContext();

			RenderCore::Techniques::AttachmentPool namedResources;

                //  Finally, we execute the frame loop
            for (;;) {
                auto pump = OverlappedWindow::DoMsgPump();
                if (pump == OverlappedWindow::PumpResult::Terminate)
                    break;

                // When in the background, yield most of our time, and prevent rendering
                if (pump == OverlappedWindow::PumpResult::Background) {
                    Threading::Sleep(64);
                    continue;
                }

                    // ------- Render ----------------------------------------
                SceneEngine::LightingParserContext lightingParserContext(*primMan._globalTechContext, &namedResources);
                lightingParserContext._plugins.push_back(stdPlugin);

                auto& screenshot = Tweakable("Screenshot", 0);
                if (screenshot) {
                    TiledScreenshot(
                        *context, lightingParserContext,
                        *mainScene, mainScene->GetCameraDesc(),
						SceneEngine::SceneTechniqueDesc(UInt2(primMan._presChain->GetDesc()->_width, primMan._presChain->GetDesc()->_height)),
                        UInt2(screenshot, screenshot));
                    screenshot = 0;
                }

                auto frameResult = frameRig.ExecuteFrame(
                    *context.get(), primMan._presChain.get(), 
                    &g_cpuProfiler,
                    std::bind(
                        RenderFrame, std::placeholders::_1, std::placeholders::_2,
                        std::ref(lightingParserContext), mainScene.get(), 
                        primMan._presChain.get(), 
                        frameRig.GetMainOverlaySystem().get()));

                    // ------- Update ----------------------------------------
                RenderCore::Assets::Services::GetBufferUploads().Update(*context, false);
                mainScene->Update(frameResult._elapsedTime);
                cameraInputHandler->Commit(frameResult._elapsedTime);   // we need to be careful to update the camera at the right time (relative to character update)
                g_cpuProfiler.EndFrame();
                ++FrameRenderCount;
            }
        }

        Log(Verbose) << "Starting shutdown" << std::endl;
        // primMan._assetServices->GetAssetSets().LogReport();
        // RenderCore::Metal::DeviceContext::PrepareForDestruction(primMan._rDevice.get(), primMan._presChain.get());

        mainScene.reset();

        // primMan._assetServices->GetAssetSets().Clear();
        ConsoleRig::ResourceBoxes_Shutdown();
        
        primMan._renderAssetServices.reset();
        primMan._assetServices.reset();
        TerminateFileSystemMonitoring();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    PlatformRig::FrameRig::RenderResult RenderFrame(
        RenderCore::IThreadContext& context,
		const RenderCore::IResourcePtr& presentationResource,
        SceneEngine::LightingParserContext& parserContext,
        EnvironmentSceneParser* scene,
        RenderCore::IPresentationChain* presentationChain,
        PlatformRig::IOverlaySystem* overlaySys)
    {
        CPUProfileEvent pEvnt("RenderFrame", g_cpuProfiler);

		auto& namedRes = parserContext.GetNamedResources();
		auto viewContext = presentationChain->GetDesc();
		auto samples = RenderCore::TextureSamples::Create((uint8)Tweakable("SamplingCount", 1), (uint8)Tweakable("SamplingQuality", 0));
		namedRes.Bind(RenderCore::FrameBufferProperties{viewContext->_width, viewContext->_height, samples});
		namedRes.Bind(0u, presentationResource);

        using namespace SceneEngine;
        if (scene) {
			auto presChainDesc = presentationChain->GetDesc();
            SceneTechniqueDesc qualSettings(
                UInt2(presChainDesc->_width, presChainDesc->_height), 
                (Tweakable("LightingModel", 0) == 0) ? SceneTechniqueDesc::LightingModel::Deferred : SceneTechniqueDesc::LightingModel::Forward,
                Tweakable("SamplingCount", 1), Tweakable("SamplingQuality", 0));

            parserContext.GetProjectionDesc() = BuildProjectionDesc(scene->GetCameraDesc(), qualSettings._dimensions);

                //  some scene might need a "prepare" step to 
                //  build some resources before the main render occurs.
            scene->PrepareFrame(context, parserContext);

            LightingParser_ExecuteScene(context, parserContext, *scene, scene->GetCameraDesc(), qualSettings);
        }

        if (overlaySys) {
            overlaySys->RenderToScene(context, parserContext);
        }

        if (!scene->GetPlayerCharacter()->IsPresent())
            StringMeldAppend(parserContext._stringHelpers->_errorString) << "No player CharacterSpawn detected. Press Tab to switch camera types.";

        auto& usefulFonts = ConsoleRig::FindCachedBox<UsefulFonts>();
        DrawPendingResources(context, parserContext, usefulFonts._defaultFont0);

        if (Tweakable("QuickMetrics", false))
            DrawQuickMetrics(context, parserContext, usefulFonts._defaultFont1);

        if (overlaySys) {
            overlaySys->RenderWidgets(context, parserContext);
        }

        return PlatformRig::FrameRig::RenderResult(parserContext.HasPendingAssets());
    }
}

#if 0

#include "../../SceneEngine/TerrainMaterial.h"
#include "../../Utility/Streams/StreamFormatter.h"

void RunPerformanceTest()
{
    const std::basic_string<utf8> testString = (const utf8*)R"~~(~~!Format=1; Tab=4
        DiffuseDims={512u, 512u}v; NormalDims={512u, 512u}v; ParamDims={512u, 512u}v

        ~GradFlagMaterial; MaterialId=0u; 
	        Texture[0]=Game/plaintextures/grass/grassTextureNo9227
	        Texture[1]=Game/aa_terrain/canyon/tr_canyon_rock_700b_800b
	        Texture[2]=Game/aa_terrain/canyon/tr_canyon_rock3d_708a
	        Texture[3]=Game/aa_terrain/canyon/tr_canyon_rock3d_602b
	        Texture[4]=Game/plaintextures/grass/grassTextureNo9227; Mapping={1.8f, 1f, 1f, 1f, 1f}

        ~GradFlagMaterial; MaterialId=1u; Texture[0]=ProcTexture
	        Texture[1]=Game/plaintextures/gravel/stonesTextureNo8648
	        Texture[2]=Game/aa_terrain/canyon/tr_canyon_rock3d_409a
	        Texture[3]=Game/aa_terrain/canyon/tr_canyon_rock3d_409a
	        Texture[4]=Game/plaintextures/gravel/gravelTextureNo7899; Mapping={1.8f, 1f, 1f, 1f, 1f}

        ~ProcTextureSetting; Name=ProcTexture; Texture[0]=Game/plaintextures/grass/grassTextureNo7109
	        Texture[1]=Game/plaintextures/grass/grassTextureNo6354; HGrid=5f; Gain=0.5f)~~";

    for (;;) {
        MemoryMappedInputStream stream(AsPointer(testString.cbegin()), AsPointer(testString.cend()));
        InputStreamFormatter<utf8> formatter(stream);
        SceneEngine::TerrainMaterialConfig matConfig(formatter, ::Assets::DirectorySearchRules());
        (void)matConfig;
    }
}

#endif

