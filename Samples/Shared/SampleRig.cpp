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
#include "../../Assets/XPak.h"

#include "../../OSServices/Log.h"
#include "../../OSServices/OverlappedWindow.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../ConsoleRig/Console.h"

#include "../../Formatters/CommandLineFormatter.h"
#include "../../Formatters/FormatterUtils.h"

#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"

#include <functional>
#include <variant>

namespace Sample
{
    void InstallDefaultDebuggingDisplays(SampleGlobals& globals);   // DefaultDebuggingDisplays.cpp

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

        using MsgVariant = VariantCat<OSServices::SystemMessageVariant, RenderFrame, UpdateFrame, OnRenderTargetUpdate>;
        MsgVariant Pump();

        void ShowWindow(bool newState);

        MessageLoop(std::shared_ptr<PlatformRig::WindowApparatus> apparatus);
        ~MessageLoop();
        MessageLoop();
        MessageLoop(MessageLoop&&) = default;
        MessageLoop& operator=(MessageLoop&&) = default;
    private:
        std::shared_ptr<PlatformRig::WindowApparatus> _apparatus;
        enum class Pending
        {
            None, BeginRenderFrame, EndRenderFrame, ShowWindow, ShowWindowBeginRenderFrame, ShowWindowEndRenderFrame
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
        case Pending::ShowWindowBeginRenderFrame:
            _pending = (_pending == Pending::ShowWindowBeginRenderFrame) ? Pending::ShowWindowEndRenderFrame : Pending::EndRenderFrame;
            assert(!_activeParsingContext);
            _activeParsingContext = _apparatus->_frameRig->StartupFrame(*_apparatus);
            return RenderFrame { _activeParsingContext.value() };

        case Pending::EndRenderFrame:
        case Pending::ShowWindowEndRenderFrame:
            {
                auto originalPending = _pending;
                _pending = Pending::None;
                assert(_activeParsingContext);
                auto parsingContext = std::move(_activeParsingContext.value());
                _activeParsingContext = {};

                auto frameResult = _apparatus->_frameRig->ShutdownFrame(parsingContext);

                // ------- Yield some process time when appropriate ------
                if (originalPending == Pending::ShowWindowEndRenderFrame) {
                    _apparatus->_osWindow->Show();
                } else
                    _apparatus->_frameRig->IntermedialSleep(*_apparatus, _lastIdleState == OSServices::IdleState::Background, frameResult);
            }
            break;       // break and continue with next event

        case Pending::ShowWindow:
            // We force a render target update and render before showing the window to ensure that it has content
            // when it first appears
            _pending = Pending::ShowWindowBeginRenderFrame;
            _lastOverlayConfiguration = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
            return OnRenderTargetUpdate { _lastOverlayConfiguration._preregAttachments, _lastOverlayConfiguration._fbProps, _lastOverlayConfiguration._systemAttachmentFormats };
            break;

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

    void MessageLoop::ShowWindow(bool newState)
    {
        if (!newState) {
            _apparatus->_osWindow->Show(newState);
            return;
        }

        if (_pending != Pending::None)
            Throw(std::runtime_error("Cannot show window because MessageLoop is in the middle of queued render operation"));
        _pending = Pending::ShowWindow;
    }

    MessageLoop::MessageLoop(std::shared_ptr<PlatformRig::WindowApparatus> apparatus)
    : _apparatus(std::move(apparatus))
    {
        _lastOverlayConfiguration = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
    }

    MessageLoop::~MessageLoop()
    {}

    class StartupLoop
    {
    public:

        struct ConfigureGlobalServices
        {
            ConsoleRig::StartupConfig _startupCfg;
            std::string _xleResLocation = "xleres.pak";
            enum class XLEResType { XPak, OSFileSystem, None };
            XLEResType _xleResType = XLEResType::XPak;
        };

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

        struct StartupFinished
        {
        };

        using MsgVariant = std::variant<ConfigureGlobalServices*, ConfigureRenderDevice*, ConfigureWindowInitialState*, StartupFinished>;
        MsgVariant Pump();

        MessageLoop ShowWindowAndBeginMessageLoop();

        ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
        struct MountRegistrationToken;
        std::unique_ptr<MountRegistrationToken> _xleResMountID;
        ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;
        ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
        ConsoleRig::AttachablePtr<ToolsRig::IPreviewSceneRegistry> _previewSceneRegistry;
        ConsoleRig::AttachablePtr<EntityInterface::IEntityMountingTree> _entityMountingTree;

        std::shared_ptr<RenderCore::IAPIInstance> _renderAPIInstance;
        std::shared_ptr<RenderCore::IDevice> _renderDevice;
        std::unique_ptr<OSServices::Window> _osWindow;

        SampleGlobals _sampleGlobals;

        std::shared_ptr<::Assets::ArchiveUtility::FileCache> _fileCache;

        StartupLoop(Formatters::CommandLineFormatter<>& cmdLine);
        ~StartupLoop();
        StartupLoop(const StartupLoop&) = delete;
        StartupLoop& operator=(const StartupLoop&) = delete;

    private:
        enum class Phase { Initial, PostConfigureGlobalServices, PostConfigureRenderDevice, PostConfigureWindowInitialState, PostConfigureDevelopmentFeatures, PostConfigureFrameRigDisplay, Finished };
        Phase _phase = Phase::Initial;

        ConfigureGlobalServices _configGlobalServices;
        ConfigureRenderDevice _configRenderDevice;
        ConfigureWindowInitialState _configWindowInitialState;
    };

    struct CommandLineArgsDigest
    {
        StringSection<> _xleres = "xleres.pak";
        CommandLineArgsDigest(Formatters::CommandLineFormatter<>& fmttr)
        {
            StringSection<> keyname;
            for (;;) {
                if (fmttr.TryKeyedItem(keyname)) {
                    if (XlEqStringI(keyname, "xleres"))
                        _xleres = Formatters::RequireStringValue(fmttr);
                } else if (fmttr.PeekNext() == Formatters::FormatterBlob::None) {
                    break;
                } else
                    Formatters::SkipValueOrElement(fmttr);
            }
        }
    };

    struct StartupLoop::MountRegistrationToken
    {
        ~MountRegistrationToken()
        {
            if (_mountId != ~0u)
                ::Assets::MainFileSystem::GetMountingTree()->Unmount(_mountId);
        }
        ::Assets::MountingTree::MountID _mountId = ~0u;
    };

    auto StartupLoop::Pump() -> MsgVariant
    {
        switch (_phase) {
        case Phase::Initial:
            {
                _phase = Phase::PostConfigureGlobalServices;
                return &_configGlobalServices;
            }

        case Phase::PostConfigureGlobalServices:
            {
                _globalServices = std::make_shared<ConsoleRig::GlobalServices>(_configGlobalServices._startupCfg);

                _xleResMountID = std::make_unique<MountRegistrationToken>();
                if (_configGlobalServices._xleResType == ConfigureGlobalServices::XLEResType::XPak) {
                    _fileCache = ::Assets::CreateFileCache(4 * 1024 * 1024);
                    _xleResMountID->_mountId = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateXPakFileSystem(_configGlobalServices._xleResLocation, _fileCache));
                } else if (_configGlobalServices._xleResType == ConfigureGlobalServices::XLEResType::OSFileSystem)
                    _xleResMountID->_mountId = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS(_configGlobalServices._xleResLocation, _globalServices->GetPollingThread()));

                _renderAPIInstance = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

                _assetServices = std::make_shared<::Assets::Services>();
                _osWindow = std::make_unique<OSServices::Window>();

                _phase = Phase::PostConfigureRenderDevice;
                _configRenderDevice = {
                    0, _renderAPIInstance->QueryFeatureCapability(0),
                    _renderAPIInstance,
                    0, _osWindow.get()
                };
                return &_configRenderDevice;
            }

        case Phase::PostConfigureRenderDevice:
            {
                _renderDevice = _renderAPIInstance->CreateDevice(_configRenderDevice._configurationIdx, _configRenderDevice._deviceFeatures);
                _techniqueServices = std::make_shared<RenderCore::Techniques::Services>(_renderDevice);
                _previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();
                _entityMountingTree = EntityInterface::CreateMountingTree();
                ::ConsoleRig::GlobalServices::GetInstance().LoadDefaultPlugins();

                _sampleGlobals._renderDevice = _renderDevice;
                _sampleGlobals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_renderDevice);
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
            _sampleGlobals._windowApparatus->_frameRig->UpdatePresentationChain(*_sampleGlobals._windowApparatus->_presentationChain);
            _techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*_sampleGlobals._windowApparatus->_immediateContext);

            // intentional fall-through

        default:
        case Phase::Finished:
            _phase = Phase::Finished;
            return StartupFinished{};
        }
    }

    MessageLoop StartupLoop::ShowWindowAndBeginMessageLoop()
    {
        MessageLoop result { _sampleGlobals._windowApparatus };
        result.ShowWindow(true);
        return result;
    }

    StartupLoop::StartupLoop(Formatters::CommandLineFormatter<>& cmdLine)
    {
        CommandLineArgsDigest cmdLineDigest { cmdLine };
        _configGlobalServices._xleResLocation = cmdLineDigest._xleres.AsString();
        if (XlEqStringI(MakeFileNameSplitter(cmdLineDigest._xleres).Extension(), "pak")) {
            // by default, search next to the executable if we don't have a fully qualified name
            if (::Assets::MainFileSystem::TryGetDesc(_configGlobalServices._xleResLocation)._snapshot._state == ::Assets::FileSnapshot::State::DoesNotExist) {
                char buffer[MaxPath];
                OSServices::GetProcessPath(buffer, dimof(buffer));
                _configGlobalServices._xleResLocation = Concatenate(MakeFileNameSplitter(buffer).DriveAndPath(), "/", _configGlobalServices._xleResLocation);
            }
            _configGlobalServices._xleResType = ConfigureGlobalServices::XLEResType::XPak;
        } else
            _configGlobalServices._xleResType = ConfigureGlobalServices::XLEResType::OSFileSystem;
    }

    StartupLoop::~StartupLoop()
    {
        ::ConsoleRig::GlobalServices::GetInstance().PrepareForDestruction();
        if (_renderDevice)
            _renderDevice->PrepareForDestruction();
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

    struct ConfigureDevelopmentFeatures
    {
        bool _installDefaultDebuggingDisplays = false;
        bool _useFrameRigSystemDisplay = false;
        bool _installHotKeysHandler = false;

        std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> _additionalDebuggingDisplays;

        std::shared_ptr<PlatformRig::IFrameRigDisplay> Apply(SampleGlobals& sampleGlobals)
        {
            std::shared_ptr<PlatformRig::IFrameRigDisplay> result;
            if (_useFrameRigSystemDisplay) {
                auto& frameRig = *sampleGlobals._windowApparatus->_frameRig;
                result = frameRig.CreateDisplay(sampleGlobals._debugOverlaysApparatus->_debugSystem, sampleGlobals._windowApparatus->_mainLoadingContext);
                PlatformRig::SetSystemDisplay(*sampleGlobals._debugOverlaysApparatus->_debugSystem, result);
            }

            if (_installDefaultDebuggingDisplays)
                InstallDefaultDebuggingDisplays(sampleGlobals);

            for (const auto& dd:_additionalDebuggingDisplays)
                sampleGlobals._displayRegistrations.emplace_back(dd.first, std::move(dd.second));

            if (_installHotKeysHandler)
                sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::MakeHotKeysHandler("rawos/hotkey.dat"));
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::CreateInputListener(sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem));

            return result;
        }
    };

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

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay, Formatters::CommandLineFormatter<>& cmdLine)
    {
        SampleConfiguration config;
        sampleOverlay->Configure(config);

        StartupLoop startup { cmdLine };
        for (;;) {
            auto msg = startup.Pump();

            if (std::holds_alternative<StartupLoop::ConfigureGlobalServices*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureGlobalServices*>(msg);

            }

            if (std::holds_alternative<StartupLoop::ConfigureRenderDevice*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureRenderDevice*>(msg);
                
                LogRenderAPIInstanceStartup(*pkt._apiInstance, pkt._window->GetUnderlyingHandle());
                pkt._presentationChainBindFlags = config._presentationChainBindFlags;
            }

            else if (std::holds_alternative<StartupLoop::ConfigureWindowInitialState*>(msg)) {
                auto& pkt = *std::get<StartupLoop::ConfigureWindowInitialState*>(msg);

                 if (config._initialWindowSize)
                    pkt._window->Resize((*config._initialWindowSize)[0], (*config._initialWindowSize)[1]);

                auto v = startup._renderDevice->GetDesc();
                StringMeld<128> meld;
                if (!config._windowTitle.empty()) meld << config._windowTitle;
                else meld << "XLE sample";
                meld << " [RenderCore: " << v._buildVersion << ", " << v._buildDate << "]";
                pkt._window->SetTitle(meld);
            }

            else if (std::holds_alternative<StartupLoop::StartupFinished>(msg)) {
                break;
            }

        }

        auto& sampleGlobals = startup._sampleGlobals;
        auto& frameRig = *sampleGlobals._windowApparatus->_frameRig;

        auto rawosmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("rawos", ::Assets::CreateFileSystem_OS({}, ConsoleRig::GlobalServices::GetInstance().GetPollingThread()));
        auto cleanup = AutoCleanup([rawosmnt]() { ::Assets::MainFileSystem::GetMountingTree()->Unmount(rawosmnt); });

        ConfigureDevelopmentFeatures devFeatures;
        devFeatures._installDefaultDebuggingDisplays = true;
        devFeatures._useFrameRigSystemDisplay = true;
        devFeatures._installHotKeysHandler = true;
        devFeatures.Apply(sampleGlobals);

        auto sampleOverlayAsOverlay = std::dynamic_pointer_cast<PlatformRig::IOverlaySystem>(sampleOverlay);

        if (sampleOverlayAsOverlay)
            sampleGlobals._windowApparatus->_mainInputHandler->AddListener(PlatformRig::CreateInputListener(sampleOverlayAsOverlay));
        sampleOverlay->OnStartup(sampleGlobals);

            //  Finally, we execute the frame loop.
        auto msgLoop = startup.ShowWindowAndBeginMessageLoop();
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
                    if (sampleOverlayAsOverlay)
                        sampleOverlayAsOverlay->Render(parserContext);
                    sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem->Render(parserContext);
                } CATCH(const std::exception& e) {
                    RenderOverlays::DrawBottomOfScreenErrorMsg(parserContext, *sampleGlobals._overlayApparatus, e.what());
                } CATCH_END
            }

                    // ------- Render target update ---------------------------
            else if (std::holds_alternative<MessageLoop::OnRenderTargetUpdate>(msg)) {
                auto& rtu = std::get<MessageLoop::OnRenderTargetUpdate>(msg);
                OnRenderTargetUpdate(
                    sampleOverlayAsOverlay.get(), *sampleGlobals._debugOverlaysApparatus->_debugScreensOverlaySystem,
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

	void ISampleOverlay::OnStartup(const SampleGlobals&) {}
	void ISampleOverlay::OnUpdate(float) {}
    void ISampleOverlay::Configure(SampleConfiguration&) {}

    SampleGlobals::SampleGlobals() = default;
	SampleGlobals::~SampleGlobals() = default;
}

