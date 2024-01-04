// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "FrameRig.h"
#include "../RenderCore/FrameBufferDesc.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/DeviceInitialization.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../OSServices/OverlappedWindow.h"
#include "../Utility/IteratorUtils.h"
#include <variant>
#include <memory>

namespace RenderCore { namespace Techniques { class ParsingContext; struct PreregisteredAttachment; class Services; class PrimaryResourcesApparatus; } }
namespace Assets { class Services; namespace ArchiveUtility { class FileCache; } }
namespace Formatters { template<typename CharType> class CommandLineFormatter; }
namespace ToolsRig { class IPreviewSceneRegistry; }
namespace EntityInterface { class IEntityMountingTree; }

namespace PlatformRig
{

	namespace Internal
	{
		template<typename V, typename std::size_t... I, typename... O>
			constexpr auto VariantCat_Helper(V&&, std::index_sequence<I...>&&, O&&...) -> 
				std::variant<std::variant_alternative_t<I, V>..., O...>;

		template<typename V, typename... O>
			using VariantCat = decltype(VariantCat_Helper(std::declval<V>(), std::make_index_sequence<std::variant_size_v<V>>{}, std::declval<O>()...));
	}

	class WindowApparatus;

	/// <summary>Manage the OS event loop in a simple but common way required by single window applications</summary>
	///
	/// Uses OSServices::Window::SingleWindowMessagePump() internally to process the event loop, and schedules render
	/// and update frame events as appropriate.
	///
	/// This class is not thread safe, its expected to only be used on a single thread (and typically the main/startup thread)
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

		struct InputEvent
		{
			OSServices::InputSnapshot _evnt;
			InputContext _content;
		};

		struct OnRenderTargetUpdate
		{
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> _preregAttachments;
			const RenderCore::FrameBufferProperties _fbProps;
			IteratorRange<const RenderCore::Format*> _systemAttachmentFormats;
		};

		using MsgVariant = Internal::VariantCat<OSServices::SystemMessageVariant, RenderFrame, UpdateFrame, InputEvent, OnRenderTargetUpdate>;
		MsgVariant Pump();

		void ShowWindow(bool newState);
		std::optional<OnRenderTargetUpdate> GetLastRenderTargets();

		MessageLoop(std::shared_ptr<WindowApparatus> apparatus);
		~MessageLoop();
		MessageLoop();
		MessageLoop(MessageLoop&&);
		MessageLoop& operator=(MessageLoop&&);
	private:
		std::shared_ptr<WindowApparatus> _apparatus;
		enum class Pending
		{
			None, BeginRenderFrame, EndRenderFrame, ShowWindow, ShowWindowBeginRenderFrame, ShowWindowEndRenderFrame
		};
		Pending _pending = Pending::None;

		std::optional<RenderCore::Techniques::ParsingContext> _activeParsingContext;
		OSServices::IdleState _lastIdleState = OSServices::IdleState::Foreground;
		FrameRig::OverlayConfiguration _lastOverlayConfiguration;
		bool _lastOverlayConfigurationGood = false;
	};

	class DebugOverlaysApparatus;
	struct DebugScreenRegistration;

	class AppRigGlobals
	{
	public:
		std::shared_ptr<RenderCore::IDevice> _renderDevice;

		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<RenderOverlays::OverlayApparatus> _overlayApparatus;
		std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> _primaryResourcesApparatus;
		std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> _frameRenderingApparatus;
		std::shared_ptr<WindowApparatus> _windowApparatus;
		std::shared_ptr<DebugOverlaysApparatus> _debugOverlaysApparatus;

		std::vector<DebugScreenRegistration> _displayRegistrations;

		AppRigGlobals();
		~AppRigGlobals();
	};

	/// <summary>Manage the early app startup process in a convenient fashion</summary>
	///
	/// Most applications require a number of XLE "services" and "apparatuses" to be constructed before the application can do anything useful.
	/// This class uses a pattern similar MessageLoop and OSServices::Window::SingleWindowMessagePump() to abstract this process by raising
	/// events whenever the application has the ability to customize configuration settings.
	///
	/// This can be convenient and extensible, because the app can choose what to react to, and what to ignore (in which case the default settings
	/// will just be used).
	///
	/// Most applications don't need a highly specialized configuration of XLE, and for them this utility just makes things a little simpler.
	/// However, it is optional: applications that require a very specific set of XLE features (or perhaps just a particular subset) can 
	/// always elect to construct the services and apparatuses themselves.
	///
	/// Most construction is done synchronously in the current thread.
	class StartupLoop
	{
	public:

		struct ConfigureGlobalServices
		{
			ConsoleRig::StartupConfig _startupCfg;
			std::string _xleResLocation = "xleres.pak";
			enum class XLEResType { XPak, OSFileSystem, EmbeddedXPak, None };
			XLEResType _xleResType = XLEResType::XPak;
			IteratorRange<const void*> _xleResEmbeddedData;
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

		AppRigGlobals _globals;

		std::shared_ptr<::Assets::ArchiveUtility::FileCache> _fileCache;

		StartupLoop(Formatters::CommandLineFormatter<char>& cmdLine);
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

	struct ConfigureDevelopmentFeatures
	{
		bool _installDefaultDebuggingDisplays = false;
		bool _useFrameRigSystemDisplay = false;
		bool _installHotKeysHandler = false;

		std::vector<std::pair<std::string, std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget>>> _additionalDebuggingDisplays;

		std::shared_ptr<IFrameRigDisplay> Apply(AppRigGlobals& globals);
	};

	void InstallDefaultDebuggingDisplays(AppRigGlobals& globals);

	void SetSystemDisplay(
		RenderOverlays::DebuggingDisplay::DebugScreensSystem& debugScreens, 
		std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> systemDisplay);
}

