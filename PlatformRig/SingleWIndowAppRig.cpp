// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SingleWindowAppRig.h"
#include "FrameRig.h"
#include "PlatformApparatuses.h"
#include "DebugScreenRegistry.h"
#include "OverlaySystem.h"
#include "DebugHotKeys.h"
#include "MainInputHandler.h"

#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Techniques/SubFrameEvents.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderOverlays/OverlayApparatus.h"
#include "../RenderCore/Vulkan/IDeviceVulkan.h"

#include "../Assets/IFileSystem.h"
#include "../Assets/MountingTree.h"
#include "../Assets/OSFileSystem.h"
#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Assets/XPak.h"

#include "../Tools/ToolsRig/PreviewSceneRegistry.h"
#include "../Tools/EntityInterface/EntityInterface.h"

#include "../OSServices/OverlappedWindow.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/Plugins.h"
#include "../Formatters/CommandLineFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Streams/PathUtils.h"

namespace PlatformRig
{

	namespace Internal
	{
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
	}

	auto MessageLoop::Pump() -> MsgVariant
	{
		switch (_pending) {
		case Pending::BeginRenderFrame:
		case Pending::ShowWindowBeginRenderFrame:
			{
				auto presChainDesc = _apparatus->_presentationChain->GetDesc();
				if (presChainDesc._width * presChainDesc._height) {
					_pending = (_pending == Pending::ShowWindowBeginRenderFrame) ? Pending::ShowWindowEndRenderFrame : Pending::EndRenderFrame;
					assert(!_activeParsingContext);
					_activeParsingContext = _apparatus->_frameRig->StartupFrame(*_apparatus);
					return RenderFrame { _activeParsingContext.value() };
				} else {
					// ------- zero size presentation chain -- fail to render & yield some process time when appropriate ------
					if (_pending == Pending::ShowWindowBeginRenderFrame) {
						_apparatus->_osWindow->Show();
					} else
						_apparatus->_frameRig->IntermedialSleep(*_apparatus, _lastIdleState == OSServices::IdleState::Background, {});
					break;
				}
			}

		case Pending::EndRenderFrame:
		case Pending::ShowWindowEndRenderFrame:
			{
				auto originalPending = _pending;
				_pending = Pending::None;
				assert(_activeParsingContext);
				auto parsingContext = std::move(_activeParsingContext.value());
				_activeParsingContext = {};

				auto frameResult = _apparatus->_frameRig->ShutdownFrame(parsingContext, _appLogFile.get());

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
			_lastOverlayConfigurationGood = true;
			return OnRenderTargetUpdate { _lastOverlayConfiguration._preregAttachments, _lastOverlayConfiguration._fbProps, _lastOverlayConfiguration._systemAttachmentFormats };
			break;

		case Pending::None:
			break;
		}

		assert(!_activeParsingContext);
		for (;;) {
			auto msgPump = OSServices::Window::SingleWindowMessagePump(*_apparatus->_osWindow);
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

				auto resize = std::get<OSServices::WindowResize>(msgPump);
				auto& frameRig = *_apparatus->_frameRig;

				frameRig.GetTechniqueContext()._frameBufferPool->Reset();
				frameRig.ReleaseDoubleBufferAttachments();
				frameRig.GetTechniqueContext()._attachmentPool->ResetActualized();
				auto desc = _apparatus->_presentationChain->GetDesc();
				desc._width = resize._newWidth;
				desc._height = resize._newHeight;
				_apparatus->_presentationChain->ChangeConfiguration(*_apparatus->_immediateContext, desc);
				frameRig.UpdatePresentationChain(*_apparatus->_presentationChain);

				auto newConfig = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
				if (newConfig._hash != _lastOverlayConfiguration._hash) {
					_lastOverlayConfiguration = std::move(newConfig);
					_lastOverlayConfigurationGood = true;
					return OnRenderTargetUpdate { _lastOverlayConfiguration._preregAttachments, _lastOverlayConfiguration._fbProps, _lastOverlayConfiguration._systemAttachmentFormats };
				}

			} else if (std::holds_alternative<OSServices::InputSnapshot>(msgPump)) {

				auto clientRect = _apparatus->_osWindow->GetRect();
				InputContext context;
				context._view = { {clientRect.first._x, clientRect.first._y}, {clientRect.second._x, clientRect.second._y} };
				auto evnt = std::get<OSServices::InputSnapshot>(msgPump);
				ProcessInputResult processResult = ProcessInputResult::Passthrough;
				if (_apparatus->_mainInputHandler) {
					auto oldThreadContext = RenderCore::Techniques::SetThreadContext(_apparatus->_immediateContext);
					TRY {
						processResult = _apparatus->_mainInputHandler->OnInputEvent(context, evnt);
					} CATCH(...) {
						RenderCore::Techniques::SetThreadContext(std::move(oldThreadContext));
						throw;
					} CATCH_END
					RenderCore::Techniques::SetThreadContext(std::move(oldThreadContext));
				}

				if (processResult != ProcessInputResult::Consumed)
					return InputEvent { std::move(evnt), std::move(context) };		// note that the "services" we attach to context must out-live the InputEvent we return

			} else
				return Internal::VariantCast<MsgVariant>(std::move(msgPump));
		}
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

	auto MessageLoop::GetLastRenderTargets() -> std::optional<OnRenderTargetUpdate>
	{
		if (!_lastOverlayConfigurationGood) return {};
		return OnRenderTargetUpdate {
			_lastOverlayConfiguration._preregAttachments, _lastOverlayConfiguration._fbProps, _lastOverlayConfiguration._systemAttachmentFormats
		};
	}

	MessageLoop::MessageLoop(std::shared_ptr<PlatformRig::WindowApparatus> apparatus)
	: _apparatus(std::move(apparatus))
	{
		_lastOverlayConfiguration = _apparatus->_frameRig->GetOverlayConfiguration(*_apparatus->_presentationChain);
	}

	MessageLoop::~MessageLoop() = default;
	MessageLoop::MessageLoop(MessageLoop&&) = default;
	MessageLoop& MessageLoop::operator=(MessageLoop&&) = default;


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

	static std::shared_ptr<std::fstream> InitializeAppLogFile(StringSection<> appName);
	static void LogAPIInstance(std::ostream&, RenderCore::IAPIInstance&);
	static void LogRenderDevice(std::ostream&, RenderCore::IDevice&);
	static void LogAPIInstanceAndPlatformValue(std::ostream&, RenderCore::IAPIInstance&, const void* nativePlatformValue);

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
				if (_configGlobalServices._createAppLogFile) {
					_appLogFile = InitializeAppLogFile(_configGlobalServices._startupCfg._applicationName);
					if (_appLogFile) {
						*_appLogFile << "----------------- log startup -----------------" << std::endl;
						*_appLogFile << _configGlobalServices._applLogWelcomeMsg << std::endl;
					}
				}

				_globalServices = std::make_shared<ConsoleRig::GlobalServices>(_configGlobalServices._startupCfg);
				if (_appLogFile)
					*_appLogFile << "> GlobalServices constructor done" << std::endl;

				_xleResMountID = std::make_unique<MountRegistrationToken>();
				if (_configGlobalServices._xleResType == ConfigureGlobalServices::XLEResType::XPak) {
					_fileCache = ::Assets::CreateFileCache(4 * 1024 * 1024);
					// by default, search next to the executable if we don't have a fully qualified name
					if (::Assets::MainFileSystem::TryGetDesc(_configGlobalServices._xleResLocation)._snapshot._state == ::Assets::FileSnapshot::State::DoesNotExist) {
						char buffer[MaxPath];
						OSServices::GetProcessPath(buffer, dimof(buffer));
						_configGlobalServices._xleResLocation = Concatenate(MakeFileNameSplitter(buffer).StemAndPath(), "/", _configGlobalServices._xleResLocation);
					}
					_xleResMountID->_mountId = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateXPakFileSystem(_configGlobalServices._xleResLocation, _fileCache));
				} else if (_configGlobalServices._xleResType == ConfigureGlobalServices::XLEResType::EmbeddedXPak) {
					_fileCache = ::Assets::CreateFileCache(4 * 1024 * 1024);
					_xleResMountID->_mountId = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateXPakFileSystem(_configGlobalServices._xleResEmbeddedData, OSServices::GetModuleFileTime(), _fileCache));
				} else if (_configGlobalServices._xleResType == ConfigureGlobalServices::XLEResType::OSFileSystem)
					_xleResMountID->_mountId = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS(_configGlobalServices._xleResLocation, _globalServices->GetPollingThread()));

				if (_appLogFile)
					*_appLogFile << "> Primary resources mounted" << std::endl;

				_renderAPIInstance = RenderCore::CreateAPIInstance(RenderCore::Techniques::GetTargetAPI());

				if (_appLogFile) {
					*_appLogFile << "> API Instance initialized" << std::endl;
					LogAPIInstance(*_appLogFile, *_renderAPIInstance);
				}

				_assetServices = std::make_shared<::Assets::Services>();
				_osWindow = std::make_unique<OSServices::Window>();

				if (_appLogFile) {
					*_appLogFile << "> Window created" << std::endl;
					LogAPIInstanceAndPlatformValue(*_appLogFile, *_renderAPIInstance, _osWindow->GetUnderlyingHandle());
				}

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
				if (_appLogFile) *_appLogFile << "> Creating render device" << std::endl;
				_renderDevice = _renderAPIInstance->CreateDevice(_configRenderDevice._configurationIdx, _configRenderDevice._deviceFeatures);
				if (_appLogFile) {
					*_appLogFile << "> Render device creation successful" << std::endl;
					LogRenderDevice(*_appLogFile, *_renderDevice);
				}

				_techniqueServices = std::make_shared<RenderCore::Techniques::Services>(_renderDevice);
				_previewSceneRegistry = ToolsRig::CreatePreviewSceneRegistry();
				_entityMountingTree = EntityInterface::CreateMountingTree();
				
				if (_appLogFile)
					*_appLogFile << "> Post-device services successful" << std::endl;

				::ConsoleRig::GlobalServices::GetInstance().LoadDefaultPlugins();

				if (_appLogFile) {
					*_appLogFile << "> Load default plugins successful" << std::endl;
					::ConsoleRig::GlobalServices::GetInstance().GetPluginSet().LogStatus(*_appLogFile);
				}

				_globals._renderDevice = _renderDevice;
				_globals._drawingApparatus = std::make_shared<RenderCore::Techniques::DrawingApparatus>(_renderDevice);
				_globals._overlayApparatus = std::make_shared<RenderOverlays::OverlayApparatus>(_globals._drawingApparatus);
				_globals._primaryResourcesApparatus = std::make_shared<RenderCore::Techniques::PrimaryResourcesApparatus>(_globals._renderDevice, _configRenderDevice._bufferUploadsConfiguration);
				_globals._frameRenderingApparatus = std::make_shared<RenderCore::Techniques::FrameRenderingApparatus>(_globals._renderDevice);
				_globals._windowApparatus = std::make_shared<PlatformRig::WindowApparatus>(std::move(_osWindow), _globals._drawingApparatus.get(), *_globals._frameRenderingApparatus, _configRenderDevice._presentationChainBindFlags);
				_globals._debugOverlaysApparatus = std::make_shared<PlatformRig::DebugOverlaysApparatus>(_globals._overlayApparatus);

				if (_appLogFile)
					*_appLogFile << "> Secondary services successful" << std::endl;

				_phase = Phase::PostConfigureWindowInitialState;
				_configWindowInitialState = { _globals._windowApparatus->_osWindow.get() };
				return &_configWindowInitialState;
			}

		case Phase::PostConfigureWindowInitialState:
			if (_appLogFile)
				*_appLogFile << "> Configure window initial state begin" << std::endl;

			_globals._windowApparatus->_frameRig->UpdatePresentationChain(*_globals._windowApparatus->_presentationChain);
			_techniqueServices->GetSubFrameEvents()._onCheckCompleteInitialization.Invoke(*_globals._windowApparatus->_immediateContext);

			if (_appLogFile)
				*_appLogFile << "> Configure window initial state end" << std::endl;

			// intentional fall-through

		default:
		case Phase::Finished:
			_phase = Phase::Finished;
			return StartupFinished{};
		}
	}

	MessageLoop StartupLoop::ShowWindowAndBeginMessageLoop()
	{
		MessageLoop result { _globals._windowApparatus };
		result._appLogFile = _appLogFile;
		result.ShowWindow(true);
		return result;
	}

	StartupLoop::StartupLoop(Formatters::CommandLineFormatter<>& cmdLine)
	{
		CommandLineArgsDigest cmdLineDigest { cmdLine };
		_configGlobalServices._xleResLocation = cmdLineDigest._xleres.AsString();
		if (XlEqStringI(MakeFileNameSplitter(cmdLineDigest._xleres).Extension(), "pak")) {
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


	std::shared_ptr<IFrameRigDisplay> ConfigureDevelopmentFeatures::Apply(AppRigGlobals& globals)
	{
		std::shared_ptr<IFrameRigDisplay> result;
		if (_useFrameRigSystemDisplay) {
			auto& frameRig = *globals._windowApparatus->_frameRig;
			result = frameRig.CreateDisplay(globals._debugOverlaysApparatus->_debugSystem, globals._windowApparatus->_mainLoadingContext);
			SetSystemDisplay(*globals._debugOverlaysApparatus->_debugSystem, result);
		}

		if (_installDefaultDebuggingDisplays)
			InstallDefaultDebuggingDisplays(globals);

		for (const auto& dd:_additionalDebuggingDisplays)
			globals._displayRegistrations.emplace_back(dd.first, std::move(dd.second));

		if (_installHotKeysHandler)
			globals._windowApparatus->_mainInputHandler->AddListener(MakeHotKeysHandler("rawos/hotkey.dat"));
		globals._windowApparatus->_mainInputHandler->AddListener(CreateInputListener(globals._debugOverlaysApparatus->_debugScreensOverlaySystem));

		return result;
	}

	void SetSystemDisplay(RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugScreens, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> systemDisplay)
	{
		debugScreens.Register(
			std::move(systemDisplay),
			"system-display", RenderOverlays::DebuggingDisplay::DebugScreensSystem::SystemDisplay);
	}

	static std::shared_ptr<std::fstream> InitializeAppLogFile(StringSection<> appName)
	{
		auto res = OSServices::CreateAppDataFile(appName, "app-log.txt", std::ios::out|std::ios::trunc);
		if (res.good())
			return std::make_shared<std::fstream>(std::move(res));
		return nullptr;
	}

	static const char* AsString(RenderCore::PhysicalDeviceType type)
	{
		switch (type) {
		case RenderCore::PhysicalDeviceType::Unknown: return "Unknown";
		case RenderCore::PhysicalDeviceType::DiscreteGPU: return "DiscreteGPU";
		case RenderCore::PhysicalDeviceType::IntegratedGPU: return "IntegratedGPU";
		case RenderCore::PhysicalDeviceType::VirtualGPU: return "VirtualGPU";
		case RenderCore::PhysicalDeviceType::CPU: return "CPU";
		default: return "<<error>>";
		}
	}

	static void LogAPIInstance(std::ostream& out, RenderCore::IAPIInstance& apiInstance)
	{
		auto cfgCount = apiInstance.GetDeviceConfigurationCount();
		out << "API instance with (" << cfgCount << ") configurations" << std::endl;
		for (unsigned c=0; c<cfgCount; ++c) {
			auto cfg = apiInstance.GetDeviceConfigurationProps(c);
			out << "[" << c << "]" << std::endl;
			out << "    DriverName: " << cfg._driverName << std::endl;
			out << "    Version / Vendor / Device: " << cfg._driverVersion << " / " << cfg._vendorId << " / " << cfg._deviceId << std::endl;
			out << "    Type: " << AsString(cfg._physicalDeviceType) << std::endl;

			auto features = apiInstance.QueryFeatureCapability(c);
			out << "    GeometryShaders: " << features._geometryShaders << std::endl;
			out << "    ViewInstancingRenderPasses: " << features._viewInstancingRenderPasses << std::endl;
			out << "    StreamOutput: " << features._streamOutput << std::endl;
			out << "    DepthBounds: " << features._depthBounds << std::endl;
			out << "    SampleAnsiotrophy: " << features._samplerAnisotropy << std::endl;
			out << "    WideLines: " << features._wideLines << std::endl;
			out << "    SmoothLines: " << features._smoothLines << std::endl;
			out << "    ConservativeRaster: " << features._conservativeRaster << std::endl;
			out << "    IndependentBlend: " << features._independentBlend << std::endl;
			out << "    MultiViewport: " << features._multiViewport << std::endl;
			out << "    SeparateDepthStencilLayouts: " << features._separateDepthStencilLayouts << std::endl;
			out << "    CubemapArrays: " << features._cubemapArrays << std::endl;
			out << "    QueryShaderInvocation: " << features._queryShaderInvocation << std::endl;
			out << "    QueryStreamOutput: " << features._queryStreamOutput << std::endl;
			out << "    TimelineSemaphore: " << features._timelineSemaphore << std::endl;
			out << "    ShaderImageGatherExtended: " << features._shaderImageGatherExtended << std::endl;
			out << "    PixelShaderStoresAndAtomics: " << features._pixelShaderStoresAndAtomics << std::endl;
			out << "    VertexGeoTessShaderStoresAndAtomics: " << features._vertexGeoTessellationShaderStoresAndAtomics << std::endl;
			out << "    ShaderFloat16: " << features._shaderFloat16 << std::endl;
			out << "    ETC2: " << features._textureCompressionETC2 << std::endl;
			out << "    ASTC_LDR: " << features._textureCompressionASTC_LDR << std::endl;
			out << "    ASTC_HDR: " << features._textureCompressionASTC_HDR << std::endl;
			out << "    BC: " << features._textureCompressionBC << std::endl;
			out << "    TransferQueue: " << features._dedicatedTransferQueue << std::endl;
			out << "    ComputeQueue: " << features._dedicatedComputeQueue << std::endl;
			out << "    EmulateRestrictiveLimits: " << features._emulateRestrictiveLimits << std::endl;

			if (auto* deviceVulkan = (RenderCore::IAPIInstanceVulkan*)apiInstance.QueryInterface(TypeHashCode<RenderCore::IAPIInstanceVulkan>)) {
				out << "-------------- Extended Vulkan information follows --------------" << std::endl;
				out << deviceVulkan->LogPhysicalDevice(c) << std::endl;
				out << "-----------------------------------------------------------------" << std::endl;
			}
		}
	}

	static void LogAPIInstanceAndPlatformValue(std::ostream& out, RenderCore::IAPIInstance& apiInstance, const void* nativePlatformValue)
	{
		out << "  Presentation chain compatibility: " << apiInstance.QueryPresentationChainCompatibility(0, nativePlatformValue) << std::endl;

		if (auto* deviceVulkan = (RenderCore::IAPIInstanceVulkan*)apiInstance.QueryInterface(TypeHashCode<RenderCore::IAPIInstanceVulkan>)) {
			out << "-------------- Extended Vulkan information follows --------------" << std::endl;
			out << deviceVulkan->LogInstance(nativePlatformValue) << std::endl;
			out << "-----------------------------------------------------------------" << std::endl;
		}
	}

	static void LogRenderDevice(std::ostream& out, RenderCore::IDevice& device)
	{
		auto limits = device.GetDeviceLimits();
		out << "    ConstantBufferOffsetAlignment: " << limits._constantBufferOffsetAlignment << std::endl;
		out << "    UnorderedAccessBufferOffsetAlignment: " << limits._unorderedAccessBufferOffsetAlignment << std::endl;
		out << "    TexelBufferOffsetAlignment: " << limits._texelBufferOffsetAlignment << std::endl;
		out << "    CopyBufferOffsetAlignment: " << limits._copyBufferOffsetAlignment << std::endl;
		out << "    MaxPushConstantsSize: " << limits._maxPushConstantsSize << std::endl;
	}

	AppRigGlobals::AppRigGlobals() = default;
	AppRigGlobals::~AppRigGlobals() = default;

}

