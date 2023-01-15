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
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPass.h"
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
#include <variant>

namespace Sample
{
    void InstallDefaultDebuggingDisplays(SampleGlobals& globals);   // DefaultDebuggingDisplays.cpp

    struct SampleRigApparatus
    {
        ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
        ConsoleRig::AttachablePtr<ToolsRig::IPreviewSceneRegistry> _previewSceneRegistry;
        ConsoleRig::AttachablePtr<EntityInterface::IEntityMountingTree> _entityMountingTree;

        SampleRigApparatus(std::shared_ptr<RenderCore::IDevice> renderDevice)
        {
            _techniqueServices = std::make_shared<RenderCore::Techniques::Services>(renderDevice);
            _previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();
            _entityMountingTree = EntityInterface::CreateMountingTree();
            ::ConsoleRig::GlobalServices::GetInstance().LoadDefaultPlugins();
        }
        ~SampleRigApparatus()
        {
            ::ConsoleRig::GlobalServices::GetInstance().UnloadDefaultPlugins();
        }
    };

    template<typename V, typename std::size_t... I, typename... O>
        constexpr auto VariantCat_Helper(V&&, std::index_sequence<I...>&&, O&&...) -> 
            std::variant<std::variant_alternative_t<I, V>..., O...>;

    template<typename V, typename... O>
        using VariantCat = decltype(VariantCat_Helper(std::declval<V>(), std::make_index_sequence<std::variant_size_v<V>>{}, std::declval<O>()...));

    template<typename O, typename V, typename T, typename... M>
        O VariantCast_(V&& input)
        {
            if (std::holds_alternative<T>(input))
                return std::move(std::get<T>(input));
            if constexpr (std::tuple_size_v<std::tuple<M...>> == 0) {
                Throw(std::runtime_error("bad variant cast"));
            } else
                return VariantCast_<O, V, M...>(std::move(input));
        }

    template<typename O, typename... T>
        O VariantCast(std::variant<T...>&& input)
        {
            return VariantCast_<O, std::variant<T...>, T...>(std::move(input));
        }

    class MessageLoop
    {
    public:
        struct RenderFrame
        {
            RenderCore::Techniques::ParsingContext& _parsingContext;
        };

        struct UpdateFrame
        {
            float _deltaTime;
        };

        struct OnRenderTargetUpdate
        {
            IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> _preregAttachments;
            const RenderCore::FrameBufferProperties _fbProps;
            IteratorRange<const RenderCore::Format*> _systemAttachmentFormats;
        };

        using MsgVariant = VariantCat<PlatformRig::SystemMessageVariant, RenderFrame, UpdateFrame, OnRenderTargetUpdate>;
        MsgVariant Pump();

        MessageLoop(std::shared_ptr<PlatformRig::WindowApparatus> apparatus);
        ~MessageLoop();
    private:
        std::shared_ptr<PlatformRig::WindowApparatus> _apparatus;
        enum class Pending
        {
            None, BeginRenderFrame, EndRenderFrame
        };
        Pending _pending = Pending::None;

        std::optional<RenderCore::Techniques::ParsingContext> _activeParsingContext;
        PlatformRig::IdleState _lastIdleState = PlatformRig::IdleState::Foreground;
        PlatformRig::FrameRig::OverlayConfiguration _lastOverlayConfiguration;
    };

    auto MessageLoop::Pump() -> MsgVariant
    {
        switch (_pending) {
        case Pending::BeginRenderFrame:
            _pending = Pending::EndRenderFrame;
            assert(!_activeParsingContext);
            _activeParsingContext = _apparatus->_frameRig->StartupFrame(*_apparatus);
            return RenderFrame { _activeParsingContext.value() };

        case Pending::EndRenderFrame:
            _pending = Pending::None;
            {
                assert(_activeParsingContext);
                auto parsingContext = std::move(_activeParsingContext.value());
                _activeParsingContext = {};

                auto frameResult = _apparatus->_frameRig->ShutdownFrame(parsingContext);

                // ------- Yield some process time when appropriate ------
                _apparatus->_frameRig->IntermedialSleep(*_apparatus, _lastIdleState == PlatformRig::IdleState::Background, frameResult);
            }
            break;       // break and continue with next event

        case Pending::None:
            break;
        }

        assert(!_activeParsingContext);
        auto msgPump = PlatformRig::Window::SingleWindowMessagePump(*_apparatus->_osWindow);
        PlatformRig::CommonEventHandling(*_apparatus, msgPump);
        if (std::holds_alternative<PlatformRig::Idle>(msgPump)) {

             // if we don't have any immediate OS events to process, it may be time to render
            auto& idle = std::get<PlatformRig::Idle>(msgPump);

            if (idle._state == PlatformRig::IdleState::Background) {
                // Bail if we're minimized (don't have to check this in the foreground case)
                auto presChainDesc = _apparatus->_presentationChain->GetDesc();
                if (!(presChainDesc._width * presChainDesc._height)) {
                    Threading::Sleep(64);       // minimized and inactive
                    return idle;
                }
            }

            _pending = Pending::BeginRenderFrame;
            _lastIdleState = idle._state;
            return UpdateFrame { _apparatus->_frameRig->GetSmoothedDeltaTime() * Tweakable("TimeScale", 1.0f) };

        } else if (std::holds_alternative<PlatformRig::WindowResize>(msgPump)) {

            // slightly awkward here -- we return PlatformRig::WindowResize only if we're not returning OnRenderTargetUpdate
            auto newConfig = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
            if (newConfig._hash != _lastOverlayConfiguration._hash) {
                _lastOverlayConfiguration = std::move(newConfig);
                return OnRenderTargetUpdate { _lastOverlayConfiguration._preregAttachments, _lastOverlayConfiguration._fbProps, _lastOverlayConfiguration._systemAttachmentFormats };
            }

        }
        
        return VariantCast<MsgVariant>(std::move(msgPump));
    }

    MessageLoop::MessageLoop(std::shared_ptr<PlatformRig::WindowApparatus> apparatus)
    : _apparatus(std::move(apparatus))
    {
        _lastOverlayConfiguration = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
    }

    MessageLoop::~MessageLoop()
    {}

    static void OnRenderTargetUpdate(
        PlatformRig::IOverlaySystem& mainOverlay,
        PlatformRig::IOverlaySystem& debuggingOverlay,
        IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
        const RenderCore::FrameBufferProperties& fbProps,
        IteratorRange<const RenderCore::Format*> systemAttachmentFormats)
    {
        mainOverlay.OnRenderTargetUpdate(preregAttachments, fbProps, systemAttachmentFormats);
        auto updatedAttachments = PlatformRig::InitializeColorLDR(preregAttachments);
        debuggingOverlay.OnRenderTargetUpdate(updatedAttachments, fbProps, systemAttachmentFormats);
    }

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay, const SampleConfiguration& config)
    {
            // XLE prefers to avoiding controlling the flow of execution
            // (in order to promote integration with other systems)
            // But one consequence of that is there isn't just a single Go() function
            //      -- we have to do a little bit of configuration work here
        Log(Verbose) << "Building primary managers" << std::endl;
        auto renderAPI = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

        auto assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>();
        auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));

        auto osWindow = std::make_unique<PlatformRig::Window>();
        if (config._initialWindowSize)
            osWindow->Resize((*config._initialWindowSize)[0], (*config._initialWindowSize)[1]);
        if (auto* vulkanInstance = query_interface_cast<RenderCore::IAPIInstanceVulkan*>(renderAPI.get())) {
            Log(Verbose) << "-------------- vulkan instance --------------" << std::endl;
            Log(Verbose) << vulkanInstance->LogInstance(osWindow->GetUnderlyingHandle()) << std::endl;

            auto count = renderAPI->GetDeviceConfigurationCount();
            for (unsigned c=0; c<count; ++c) {
                Log(Verbose) << "-------------- vulkan properties for device configuration (" << c << ") --------------" << std::endl;
                Log(Verbose) << vulkanInstance->LogPhysicalDevice(c) << std::endl;
            }
        }

        auto capability = renderAPI->QueryFeatureCapability(0);
        auto renderDevice = renderAPI->CreateDevice(0, capability);

        SampleRigApparatus sampleRigApparatus{renderDevice};

            // Many objects are initialized by via helper objects called "apparatuses". These construct and destruct
            // the objects required to do meaningful work. Often they also initialize the "services" singletons
            // as they go along
            // We separate this initialization work like this to provide some flexibility. It's only necessary to
            // construct as much as will be required for the specific use case 
        SampleGlobals sampleGlobals;
        sampleGlobals._renderDevice = std::move(renderDevice);
        sampleGlobals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._immediateDrawingApparatus = std::make_shared<RenderCore::Techniques::ImmediateDrawingApparatus>(sampleGlobals._drawingApparatus);
        sampleGlobals._primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(sampleGlobals._renderDevice);
        sampleGlobals._windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(std::move(osWindow), sampleGlobals._drawingApparatus.get(), *sampleGlobals._frameRenderingApparatus, config._presentationChainBindFlags);
        {
            auto v = sampleGlobals._renderDevice->GetDesc();
            StringMeld<128> meld;
            if (!config._windowTitle.empty()) meld << config._windowTitle;
            else meld << "XLE sample";
            meld << " [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]";
            sampleGlobals._windowApparatus->_osWindow->SetTitle(meld);
        }

            //  Create the debugging system, and add any "displays"
            //  If we have any custom displays to add, we can add them here. Often it's 
            //  useful to create a debugging display to go along with any new feature. 
            //  It just provides a convenient architecture for visualizing important information.
        Log(Verbose) << "Setup tools and debugging" << std::endl;
        auto& frameRig = *sampleGlobals._windowApparatus->_frameRig;
        sampleGlobals._debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(sampleGlobals._immediateDrawingApparatus, frameRig);
        InstallDefaultDebuggingDisplays(sampleGlobals);

            // Final startup operations
        Log(Verbose) << "Call OnStartup, prepare first frame and show window" << std::endl;
        sampleOverlay->OnStartup(sampleGlobals);
        sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("rawos/hotkey.dat"));
        sampleGlobals._windowApparatus->_mainInputHandler->AddListener(sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem->GetInputListener());
        auto sampleListener = sampleOverlay->GetInputListener();
        if (sampleListener)
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(sampleListener);

        frameRig.UpdatePresentationChain(*sampleGlobals._windowApparatus->_presentationChain);
        sampleRigApparatus._techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*sampleGlobals._windowApparatus->_immediateContext);

            // Pump a single frame to ensure we have some content when the window appears (and then show it)
        {
            auto initialConfig = frameRig.GetOverlayConfiguration(*sampleGlobals._windowApparatus->_presentationChain);
            OnRenderTargetUpdate(
                *sampleOverlay, *sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem,
                initialConfig._preregAttachments, initialConfig._fbProps, initialConfig._systemAttachmentFormats);
        }
        {
            auto parserContext = frameRig.StartupFrame(*sampleGlobals._windowApparatus);
            TRY {
                sampleOverlay->Render(parserContext);
                sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem->Render(parserContext);
            } CATCH(const std::exception& e) {
                PlatformRig::ReportError(parserContext, e.what());
            } CATCH_END
            frameRig.ShutdownFrame(parserContext);
        }
        sampleGlobals._windowApparatus->_osWindow->Show();

            //  Finally, we execute the frame loop. 
        Log(Verbose) << "Beginning the frame loop" << std::endl;
        MessageLoop msgLoop{sampleGlobals._windowApparatus};
        for (;;) {
            auto msg = msgLoop.Pump();

                    // ------- Update -----------------------------------------
            if (std::holds_alternative<MessageLoop::UpdateFrame>(msg)) {
                sampleOverlay->OnUpdate(std::get<MessageLoop::UpdateFrame>(msg)._deltaTime);
            }

                    // ------- Render -----------------------------------------
            else if (std::holds_alternative<MessageLoop::RenderFrame>(msg)) {
                auto& parserContext = std::get<MessageLoop::RenderFrame>(msg)._parsingContext;
                TRY {
                    sampleOverlay->Render(parserContext);
                    sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem->Render(parserContext);
                } CATCH(const std::exception& e) {
                    PlatformRig::ReportError(parserContext, e.what());
                } CATCH_END
            }

                    // ------- Render target update ---------------------------
            else if (std::holds_alternative<MessageLoop::OnRenderTargetUpdate>(msg)) {
                auto& rtu = std::get<MessageLoop::OnRenderTargetUpdate>(msg);
                OnRenderTargetUpdate(
                    *sampleOverlay, *sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem,
                    rtu._preregAttachments, rtu._fbProps, rtu._systemAttachmentFormats);
            }

                    // ------- Quit -------------------------------------------
            else if (std::holds_alternative<PlatformRig::ShutdownRequest>(msg)) {
                break;
            } 
        }

        Log(Verbose) << "Starting shutdown" << std::endl;
        sampleOverlay.reset();		// (ensure this gets destroyed before the engine is shutdown)
        ::ConsoleRig::GlobalServices::GetInstance().PrepareForDestruction();
        sampleGlobals._renderDevice->PrepareForDestruction();
        ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt);
    }

	void ISampleOverlay::OnStartup(const SampleGlobals& globals) {}
	void ISampleOverlay::OnUpdate(float deltaTime) {}

    SampleGlobals::SampleGlobals() = default;
	SampleGlobals::~SampleGlobals() = default;
}

