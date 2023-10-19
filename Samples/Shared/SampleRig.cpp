// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleRig.h"
#include "../../PlatformRig/FrameRig.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/MainInputHandler.h"
#include "../../PlatformRig/DebugHotKeys.h"
#include "../../PlatformRig/DebugScreenRegistry.h"

#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../RenderOverlays/SimpleVisualization.h"       // for DrawBottomOfScreenErrorMsg

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
#include "../../OSServices/OverlappedWindow.h"
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

    class StartupLoop
    {
    public:

        struct ConfigureRenderDevice
        {
            unsigned _configurationIdx = 0u;
            RenderCore::DeviceFeatures _deviceFeatures;
            std::shared_ptr<RenderCore::IAPIInstance> _apiInstance;
            RenderCore::BindFlag::BitField _presentationChainBindFlags = 0;
            OSServices::Window* _window = nullptr;
        };

        struct ConfigureWindowInitialState
        {
            OSServices::Window* _window = nullptr;
        };

        struct ConfigureDevelopmentFeatures
        {
            bool _installDefaultDebuggingDisplays = false;
            bool _useFrameRigSystemDisplay = false;
            bool _installHotKeysHandler = false;

            std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> _additionalDebuggingDisplays;
        };

        struct ConfigureFrameRigDisplay
        {
            std::shared_ptr<PlatformRig::IFrameRigDisplay> _frameRigDisplay;
        };

        struct StartupFinished
        {
        };

        using MsgVariant = std::variant<ConfigureRenderDevice*, ConfigureWindowInitialState*, ConfigureDevelopmentFeatures*, ConfigureFrameRigDisplay*, StartupFinished>;
        MsgVariant Pump();

        std::shared_ptr<RenderCore::IAPIInstance> _renderCoreAPIInstance;
        std::shared_ptr<RenderCore::IDevice> _renderCoreDevice;
        ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;
        std::unique_ptr<OSServices::Window> _osWindow;
        std::unique_ptr<SampleRigApparatus> _sampleRigApparatus;
        SampleGlobals _sampleGlobals;

        StartupLoop();
        ~StartupLoop();
    private:
        enum class Phase { Initial, PostConfigureRenderDevice, PostConfigureWindowInitialState, PostConfigureDevelopmentFeatures, PostConfigureFrameRigDisplay, Finished };
        Phase _phase = Phase::Initial;

        ConfigureRenderDevice _configRenderDevice;
        ConfigureDevelopmentFeatures _configDevelopmentFeatures;
        ConfigureFrameRigDisplay _configFrameRigDisplay;
        ConfigureWindowInitialState _configWindowInitialState;
    };

    auto StartupLoop::Pump() -> MsgVariant
    {
        switch (_phase) {
        case Phase::Initial:
            {
                _renderCoreAPIInstance = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

                _assetServices = std::make_shared<::Assets::Services>();
                _osWindow = std::make_unique<OSServices::Window>();

                _phase = Phase::PostConfigureRenderDevice;
                _configRenderDevice = {
                    0, _renderCoreAPIInstance->QueryFeatureCapability(0),
                    _renderCoreAPIInstance,
                    0, _osWindow.get()
                };
                return &_configRenderDevice;
            }

        case Phase::PostConfigureRenderDevice:
            {
                _renderCoreDevice = _renderCoreAPIInstance->CreateDevice(_configRenderDevice._configurationIdx, _configRenderDevice._deviceFeatures);
                _sampleRigApparatus = std::make_unique<SampleRigApparatus>(_renderCoreDevice);

                _sampleGlobals._renderDevice = _renderCoreDevice;
                _sampleGlobals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_renderCoreDevice);
                _sampleGlobals._overlayApparatus = std::make_shared<RenderOverlays::OverlayApparatus>(_sampleGlobals._drawingApparatus);
                _sampleGlobals._primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_sampleGlobals._renderDevice);
                _sampleGlobals._frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_sampleGlobals._renderDevice);
                _sampleGlobals._windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(std::move(_osWindow), _sampleGlobals._drawingApparatus.get(), *_sampleGlobals._frameRenderingApparatus, _configRenderDevice._presentationChainBindFlags);
                _sampleGlobals._debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(_sampleGlobals._overlayApparatus);

                _phase = Phase::PostConfigureWindowInitialState;
                _configWindowInitialState = { _sampleGlobals._windowApparatus->_osWindow.get() };
                return &_configWindowInitialState;
            }

        case Phase::PostConfigureWindowInitialState:
            _phase = Phase::PostConfigureDevelopmentFeatures;
            return &_configDevelopmentFeatures;

        case Phase::PostConfigureDevelopmentFeatures:
            if (_configDevelopmentFeatures._useFrameRigSystemDisplay) {
                auto& frameRig = *_sampleGlobals._windowApparatus->_frameRig;
                auto frDisplay = frameRig.CreateDisplay(_sampleGlobals._debugOverlaysApparatus->_debugSystem, _sampleGlobals._windowApparatus->_mainLoadingContext);
                PlatformRig::SetSystemDisplay(*_sampleGlobals._debugOverlaysApparatus->_debugSystem, frDisplay);
                _phase = Phase::PostConfigureFrameRigDisplay;
                _configFrameRigDisplay = { frDisplay };
                return &_configFrameRigDisplay;
            }

            // intentional fall-through

        case Phase::PostConfigureFrameRigDisplay:
            if (_configDevelopmentFeatures._installDefaultDebuggingDisplays)
                InstallDefaultDebuggingDisplays(_sampleGlobals);

            for (const auto& dd:_configDevelopmentFeatures._additionalDebuggingDisplays)
                _sampleGlobals._displayRegistrations.emplace_back(dd.first, std::move(dd.second));
            _configDevelopmentFeatures._additionalDebuggingDisplays.clear();

            if (_configDevelopmentFeatures._installHotKeysHandler)
                _sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("rawos/hotkey.dat"));
            _sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::CreateInputListener(_sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem));

            _sampleGlobals._windowApparatus->_frameRig->UpdatePresentationChain(*_sampleGlobals._windowApparatus->_presentationChain);
            _sampleRigApparatus->_techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*_sampleGlobals._windowApparatus->_immediateContext);

            // intentional fall-through

        default:
        case Phase::Finished:
            _phase = Phase::Finished;
            return StartupFinished{};
        }
    }

    StartupLoop::StartupLoop() = default;
    StartupLoop::~StartupLoop()
    {
        ::ConsoleRig::GlobalServices::GetInstance().PrepareForDestruction();
        if (_renderCoreDevice)
            _renderCoreDevice->PrepareForDestruction();
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

        using MsgVariant = VariantCat<OSServices::SystemMessageVariant, RenderFrame, UpdateFrame, OnRenderTargetUpdate>;
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
        OSServices::IdleState _lastIdleState = OSServices::IdleState::Foreground;
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
                _apparatus->_frameRig->IntermedialSleep(*_apparatus, _lastIdleState == OSServices::IdleState::Background, frameResult);
            }
            break;       // break and continue with next event

        case Pending::None:
            break;
        }

        assert(!_activeParsingContext);
        auto msgPump = OSServices::Window::SingleWindowMessagePump(*_apparatus->_osWindow);
        PlatformRig::CommonEventHandling(*_apparatus, msgPump);
        if (std::holds_alternative<OSServices::Idle>(msgPump)) {

             // if we don't have any immediate OS events to process, it may be time to render
            auto& idle = std::get<OSServices::Idle>(msgPump);

            if (idle._state == OSServices::IdleState::Background) {
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

        } else if (std::holds_alternative<OSServices::WindowResize>(msgPump)) {

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

        StartupLoop startup;
        for (;;) {
            auto msg = startup.Pump();

            if (std::holds_alternative<StartupLoop::ConfigureRenderDevice*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureRenderDevice*>(msg);

                if (auto* vulkanInstance = query_interface_cast<RenderCore::IAPIInstanceVulkan*>(pkt._apiInstance.get())) {
                    Log(Verbose) << "-------------- vulkan instance --------------" << std::endl;
                    Log(Verbose) << vulkanInstance->LogInstance(pkt._window->GetUnderlyingHandle()) << std::endl;

                    auto count = pkt._apiInstance->GetDeviceConfigurationCount();
                    for (unsigned c=0; c<count; ++c) {
                        Log(Verbose) << "-------------- vulkan properties for device configuration (" << c << ") --------------" << std::endl;
                        Log(Verbose) << vulkanInstance->LogPhysicalDevice(c) << std::endl;
                    }
                }

                pkt._presentationChainBindFlags = config._presentationChainBindFlags;
            }

            else if (std::holds_alternative<StartupLoop::ConfigureWindowInitialState*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureWindowInitialState*>(msg);

                 if (config._initialWindowSize)
                    pkt._window->Resize((*config._initialWindowSize)[0], (*config._initialWindowSize)[1]);

                auto v = startup._renderCoreDevice->GetDesc();
                StringMeld<128> meld;
                if (!config._windowTitle.empty()) meld << config._windowTitle;
                else meld << "XLE sample";
                meld << " [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]";
                pkt._window->SetTitle(meld);
            }

            else if (std::holds_alternative<StartupLoop::ConfigureDevelopmentFeatures*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureDevelopmentFeatures*>(msg);

                pkt._installDefaultDebuggingDisplays = true;
                pkt._useFrameRigSystemDisplay = true;
                pkt._installHotKeysHandler = true;
            }

            else if (std::holds_alternative<StartupLoop::StartupFinished>(msg)) {
                break;
            }

        }

        auto& sampleGlobals = startup._sampleGlobals;
        auto& frameRig = *sampleGlobals._windowApparatus->_frameRig;

        auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));
        auto cleanup = AutoCleanup([rawosmnt]() { ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt); });

        sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::CreateInputListener(sampleOverlay));
        sampleOverlay->OnStartup(sampleGlobals);

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
                RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *sampleGlobals._overlayApparatus, e.what());
            } CATCH_END
            frameRig.ShutdownFrame(parserContext);
        }
        sampleGlobals._windowApparatus->_osWindow->Show();

            //  Finally, we execute the frame loop. 
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
                    RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *sampleGlobals._overlayApparatus, e.what());
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
            else if (std::holds_alternative<OSServices::ShutdownRequest>(msg)) {
                break;
            } 
        }

        sampleOverlay.reset();		// (ensure this gets destroyed before the engine is shutdown)
    }

	void ISampleOverlay::OnStartup(const SampleGlobals& globals) {}
	void ISampleOverlay::OnUpdate(float deltaTime) {}

    SampleGlobals::SampleGlobals() = default;
	SampleGlobals::~SampleGlobals() = default;
}

