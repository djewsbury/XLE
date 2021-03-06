// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GlobalServices.h"
#include "AttachablePtr.h"
#include "../OSServices/Log.h"
#include "Console.h"
#include "ResourceBox.h"
#include "IProgress.h"
#include "Plugins.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/OSFileSystem.h"
#include "../Assets/MountingTree.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetSetManager.h"
#include "../Assets/CompileAndAsyncManager.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../OSServices/RawFS.h"
#include "../OSServices/FileSystemMonitor.h"
#include "../OSServices/PollingThread.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Conversion.h"
#include "../Core/SelectConfiguration.h"
#include <assert.h>
#include <random>
#include <typeinfo>

extern "C" const char ConsoleRig_VersionString[];
extern "C" const char ConsoleRig_BuildDateString[];

namespace ConsoleRig
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    static std::basic_string<utf8> GetAssetRoot()
    {
            //
            //      For convenience, set the working directory to be ../Working 
            //              (relative to the application path)
            //
        utf8 appPath[MaxPath];
        OSServices::GetProcessPath(appPath, dimof(appPath));
		auto splitter = MakeFileNameSplitter(appPath);
        return splitter.DriveAndPath().AsString() + "/../Working";
    }

    /// <summary>Manages configuration settings for logging</summary>
    /// Can be shared between multiple different modules.
    class LogCentralConfiguration
    {
    public:
        void Set(StringSection<>, OSServices::MessageTargetConfiguration& cfg);
        void CheckHotReload();

        static LogCentralConfiguration& GetInstance() { assert(s_instance); return *s_instance; }
        void AttachCurrentModule();
        void DetachCurrentModule();

        LogCentralConfiguration(const std::string& logCfgFile);
        ~LogCentralConfiguration();
    private:
        std::shared_ptr<OSServices::LogConfigurationSet> _cfgSet;
        std::shared_ptr<::Assets::DependencyValidation> _cfgSetDepVal;
        std::string _logCfgFile;
        std::weak_ptr<OSServices::LogCentral> _attachedLogCentral;

        static LogCentralConfiguration* s_instance;
        void Apply();
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    static auto Fn_GetConsole = ConstHash64<'getc', 'onso', 'le'>::Value;
    static auto Fn_ConsoleMainModule = ConstHash64<'cons', 'olem', 'ain'>::Value;
    static auto Fn_GetAppName = ConstHash64<'appn', 'ame'>::Value;
    static auto Fn_GuidGen = ConstHash64<'guid', 'gen'>::Value;
    static auto Fn_RedirectCout = ConstHash64<'redi', 'rect', 'cout'>::Value;
	static auto Fn_GetAssetRoot = ConstHash64<'asse', 'troo', 't'>::Value;
	static auto Fn_DefaultFileSystem = ConstHash64<'defa', 'ultf', 's'>::Value;

	void DebugUtil_Startup();
	void DebugUtil_Shutdown();

    static void MainRig_Startup(const StartupConfig& cfg)
    {
		auto& serv = CrossModule::GetInstance()._services;

        std::string appNameString = cfg._applicationName;
        bool redirectCount = cfg._redirectCout;
        serv.Add<std::string()>(Fn_GetAppName, [appNameString](){ return appNameString; });
        serv.Add<bool()>(Fn_RedirectCout, [redirectCount](){ return redirectCount; });

        srand(std::random_device().operator()());

        auto guidGen = std::make_shared<std::mt19937_64>(std::random_device().operator()());
        serv.Add<uint64()>(
            Fn_GuidGen, [guidGen](){ return (*guidGen)(); });

		auto assetRoot = GetAssetRoot();
        if (cfg._setWorkingDir)
			OSServices::ChDir(assetRoot.c_str());

		serv.Add<std::basic_string<utf8>()>(Fn_GetAssetRoot, [assetRoot](){ return assetRoot; });
    }

    static void MainRig_Attach()
    {
        auto& serv = CrossModule::GetInstance()._services;

		DebugUtil_Startup();

        if (!serv.Has<OSServices::ModuleId()>(Fn_ConsoleMainModule)) {
            auto console = std::make_shared<Console>();
            auto currentModule = OSServices::GetCurrentModuleId();
            serv.Add(
                Fn_GetConsole, 
                [console]() { return console.get(); });
            serv.Add(
                Fn_ConsoleMainModule, 
                [currentModule]() { return currentModule; });
        } else {
            Console::SetInstance(serv.Call<Console*>(Fn_GetConsole));
        }

		std::shared_ptr<::Assets::MountingTree> mountingTree;
		if (!serv.Has<std::shared_ptr<::Assets::MountingTree>()>(typeid(::Assets::MountingTree).hash_code())) {
			mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);
			serv.Add(typeid(::Assets::MountingTree).hash_code(), [mountingTree]() { return mountingTree; });
		} else {
			mountingTree = serv.Call<std::shared_ptr<::Assets::MountingTree>>(typeid(::Assets::MountingTree).hash_code());
		}

        std::shared_ptr<::Assets::IFileSystem> defaultFileSystem;
		if (!serv.Has<std::shared_ptr<::Assets::IFileSystem>()>(Fn_DefaultFileSystem)) {
			defaultFileSystem = ::Assets::CreateFileSystem_OS({}, GlobalServices::GetInstance().GetPollingThread());
			serv.Add(Fn_DefaultFileSystem, [defaultFileSystem]() { return defaultFileSystem; });
		} else {
			defaultFileSystem = serv.Call<std::shared_ptr<::Assets::IFileSystem>>(Fn_DefaultFileSystem);
		}

		::Assets::MainFileSystem::Init(mountingTree, defaultFileSystem);
    }

    static void MainRig_Detach()
    {
            // this will throw an exception if no module has successfully initialised
            // logging
        auto& serv = CrossModule::GetInstance()._services;
		OSServices::ModuleId mainModuleId = 0;
        if (serv.TryCall(Fn_ConsoleMainModule, mainModuleId) && mainModuleId == OSServices::GetCurrentModuleId()) {
            serv.Remove(Fn_GetConsole);
            serv.Remove(Fn_ConsoleMainModule);
        }

		serv.InvalidateCurrentModule();

		Console::SetInstance(nullptr);

		ResourceBoxes_Shutdown();
		DebugUtil_Shutdown();
        ::Assets::MainFileSystem::Shutdown();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	class GlobalServices::Pimpl
	{
	public:
		AttachablePtr<LogCentralConfiguration> _logCfg;
        std::unique_ptr<ThreadPool> _shortTaskPool;
        std::unique_ptr<ThreadPool> _longTaskPool;
        std::shared_ptr<OSServices::PollingThread> _pollingThread;
		StartupConfig _cfg;
		std::unique_ptr<PluginSet> _pluginSet;

        AttachablePtr<::Assets::AssetSetManager> _assetsSetsManager;
        AttachablePtr<::Assets::CompileAndAsyncManager> _compileAndAsyncManager;
	};

    GlobalServices* GlobalServices::s_instance = nullptr;

    GlobalServices::GlobalServices(const StartupConfig& cfg)
    {
		_pimpl = std::make_unique<Pimpl>();
        _pimpl->_shortTaskPool = std::make_unique<ThreadPool>(cfg._shortTaskThreadPoolCount);
        _pimpl->_longTaskPool = std::make_unique<ThreadPool>(cfg._longTaskThreadPoolCount);
        _pimpl->_pollingThread = std::make_shared<OSServices::PollingThread>();
		_pimpl->_cfg = cfg;

        MainRig_Startup(cfg);

        if (!_pimpl->_assetsSetsManager)
            _pimpl->_assetsSetsManager = std::make_shared<::Assets::AssetSetManager>();

        if (!_pimpl->_compileAndAsyncManager)
            _pimpl->_compileAndAsyncManager = std::make_shared<::Assets::CompileAndAsyncManager>();

            // add "nsight" marker to global services when "-nsight" is on
            // the command line. This is an easy way to record a global (&cross-dll)
            // state to use the nsight configuration when the given flag is set.
        const auto* cmdLine = OSServices::GetCommandLine();
        if (cmdLine && XlFindString(cmdLine, "-nsight"))
            CrossModule::GetInstance()._services.Add(Hash64("nsight"), []() { return true; });
    }

    GlobalServices::~GlobalServices() 
    {
        assert(s_instance == nullptr);  // (should already have been detached in the Withhold() call)
        _pimpl->_assetsSetsManager = nullptr;
        _pimpl->_compileAndAsyncManager = nullptr;
    }

	void GlobalServices::LoadDefaultPlugins()
	{
		if (!_pimpl->_pluginSet)
			_pimpl->_pluginSet = std::make_unique<PluginSet>();
	}

	void GlobalServices::UnloadDefaultPlugins()
	{
		_pimpl->_pluginSet.reset();
	}

    void GlobalServices::AttachCurrentModule()
    {
        assert(s_instance == nullptr);
        s_instance = this;
        MainRig_Attach();
		if (!_pimpl->_logCfg)
			_pimpl->_logCfg = std::make_shared<LogCentralConfiguration>(_pimpl->_cfg._logConfigFile);
    }

    void GlobalServices::DetachCurrentModule()
    {
        MainRig_Detach();
        assert(s_instance == this);
        s_instance = nullptr;
    }

	ThreadPool& GlobalServices::GetShortTaskThreadPool() { return *_pimpl->_shortTaskPool; }
    ThreadPool& GlobalServices::GetLongTaskThreadPool() { return *_pimpl->_longTaskPool; }
    const std::shared_ptr<OSServices::PollingThread>& GlobalServices::GetPollingThread() { return _pimpl->_pollingThread; }

    IStep::~IStep() {}
    IProgress::~IProgress() {}


	namespace Internal
	{
		std::vector<std::unique_ptr<IBoxTable>> BoxTables;
		IBoxTable::~IBoxTable() {}
	}

	void ResourceBoxes_Shutdown()
	{
		// Destroy the box tables in reverse order
		while (!Internal::BoxTables.empty())
			Internal::BoxTables.erase(Internal::BoxTables.end()-1);
		Internal::BoxTables = std::vector<std::unique_ptr<Internal::IBoxTable>>();
	}


	StartupConfig::StartupConfig()
    {
        _applicationName = "XLEApp";
        _logConfigFile = "log.dat";
        _setWorkingDir = true;
        _redirectCout = true;
        _longTaskThreadPoolCount = 4;
        _shortTaskThreadPoolCount = 2;
    }

    StartupConfig::StartupConfig(const char applicationName[]) : StartupConfig()
    {
        _applicationName = applicationName;
    }

	LibVersionDesc GetLibVersionDesc()
	{
		return LibVersionDesc { ConsoleRig_VersionString, ConsoleRig_BuildDateString };
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<std::shared_ptr<OSServices::LogConfigurationSet>, std::shared_ptr<::Assets::DependencyValidation>> LoadConfigSet(StringSection<> fn)
    {
        size_t fileSize = 0;
        auto file = ::Assets::TryLoadFileAsMemoryBlock(fn, &fileSize);
        if (!file.get() || !fileSize)
            return std::make_pair(nullptr, nullptr);
        
        InputStreamFormatter<char> fmtr(MakeStringSection((const char*)file.get(), (const char*)PtrAdd(file.get(), fileSize)));
        auto depVal = std::make_shared<::Assets::DependencyValidation>();
        ::Assets::RegisterFileDependency(depVal, fn);
        return std::make_pair(
            std::make_shared<OSServices::LogConfigurationSet>(fmtr),
            depVal);
    }

    void LogCentralConfiguration::Set(StringSection<> id, OSServices::MessageTargetConfiguration& cfg)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            _cfgSet->Set(id, cfg);

            // Reapply all configurations to the LogCentral in the local module
            auto logCentral = _attachedLogCentral.lock();
            if (logCentral)
                logCentral->SetConfiguration(_cfgSet);
            /*
            auto& central = LogCentral::GetInstance();
            auto hash = Hash64(id);
            auto i = LowerBound(central._pimpl->_activeTargets, hash);
            if (i!=central._pimpl->_activeTargets.end() && i->first == hash)
                i->second._target->SetConfiguration(cfg);
            */
        #endif
    }

    void LogCentralConfiguration::CheckHotReload()
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            if (!_cfgSet || !_cfgSetDepVal || _cfgSetDepVal->GetValidationIndex() > 0) {
                std::tie(_cfgSet, _cfgSetDepVal) = LoadConfigSet(_logCfgFile);
                auto logCentral = _attachedLogCentral.lock();
                if (logCentral)
                    logCentral->SetConfiguration(_cfgSet);
            }
        #endif
    }

    void LogCentralConfiguration::AttachCurrentModule()
    {
        assert(s_instance == nullptr);
		s_instance = this;

		auto logCentral = OSServices::LogCentral::GetInstance();
		if (logCentral)
			logCentral->SetConfiguration(_cfgSet);

		if (!_attachedLogCentral.lock() && logCentral)
			_attachedLogCentral = logCentral;
    }

    void LogCentralConfiguration::DetachCurrentModule()
    {
        assert(s_instance == this);
        s_instance = nullptr;

		auto logCentral = _attachedLogCentral.lock();
        if (logCentral)
            logCentral->SetConfiguration(nullptr);
        _attachedLogCentral.reset();
    }

    LogCentralConfiguration* LogCentralConfiguration::s_instance = nullptr;

    LogCentralConfiguration::LogCentralConfiguration(const std::string& logCfgFile)
    {
        #if defined(OSSERVICES_ENABLE_LOG)
            _logCfgFile = logCfgFile;
            std::tie(_cfgSet, _cfgSetDepVal) = LoadConfigSet(_logCfgFile);
        #endif
    }

    LogCentralConfiguration::~LogCentralConfiguration() 
    {
    }

}
