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
#include "../Assets/IntermediatesStore.h"
#include "../Assets/ContinuationExecutor.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../OSServices/RawFS.h"
#include "../OSServices/FileSystemMonitor.h"
#include "../OSServices/PollingThread.h"
#include "../OSServices/AttachableLibrary.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Conversion.h"
#include "../Core/SelectConfiguration.h"
#include "thousandeyes/futures/Executor.h"
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
        ::Assets::DependencyValidation _cfgSetDepVal;
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

		DebugUtil_Shutdown();
    }

    namespace Internal
	{
        struct CachedBoxManager
        {
            Threading::Mutex _lock;
            std::vector<std::pair<uint64_t, std::unique_ptr<IBoxTable>>> _tables;

            void Clear()
            {
                ScopedLock(_lock);
                // Destroy the box tables in reverse order
                while (!_tables.empty())
                    _tables.erase(_tables.end()-1);
            }

            ~CachedBoxManager()
            {
                Clear();
            }
        };
        static ConsoleRig::WeakAttachablePtr<CachedBoxManager> s_cachedBoxTables;

		IBoxTable* GetOrRegisterBoxTable(uint64_t typeId, std::unique_ptr<IBoxTable> table)
        {
            auto man = s_cachedBoxTables.lock();
            ScopedLock(man->_lock);
            auto i = LowerBound(man->_tables, typeId);
            if (i == man->_tables.end() || i->first != typeId)
                i = man->_tables.insert(i, std::make_pair(typeId, std::move(table)));
            return i->second.get();
        }
		IBoxTable::~IBoxTable() {}
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
		std::shared_ptr<PluginSet> _pluginSet;
        std::shared_ptr<thousandeyes::futures::Executor> _continuationExecutor;

        AttachablePtr<Internal::CachedBoxManager> _cachedBoxManager;
        AttachablePtr<::Assets::IDependencyValidationSystem> _depValSys;
        std::shared_ptr<::Assets::IFileSystem> _defaultFilesystem;
        std::shared_ptr<::Assets::MountingTree> _mountingTree;
        AttachablePtr<::Assets::CompileAndAsyncManager> _compileAndAsyncManager;
        AttachablePtr<::Assets::AssetSetManager> _assetsSetsManager;
	};

    GlobalServices* GlobalServices::s_instance = nullptr;

    GlobalServices::GlobalServices(const StartupConfig& cfg)
    {
        CrossModule::GetInstance().EnsureReady();   // if we called CrossModule::GetInstance().Shutdown() previously, we can balance it with this
		_pimpl = std::make_unique<Pimpl>();
        _pimpl->_shortTaskPool = std::make_unique<ThreadPool>(cfg._shortTaskThreadPoolCount);
        _pimpl->_longTaskPool = std::make_unique<ThreadPool>(cfg._longTaskThreadPoolCount);
        _pimpl->_pollingThread = std::make_shared<OSServices::PollingThread>();
		_pimpl->_cfg = cfg;

        MainRig_Startup(cfg);

        _pimpl->_continuationExecutor = std::make_shared<::Assets::ContinuationExecutor>(
            std::chrono::microseconds(500),
			thousandeyes::futures::detail::InvokerWithNewThread{},
			::Assets::InvokerToThreadPool{*_pimpl->_shortTaskPool});

        if (!_pimpl->_depValSys)
            _pimpl->_depValSys = ::Assets::CreateDepValSys();

        _pimpl->_defaultFilesystem = ::Assets::CreateFileSystem_OS({}, _pimpl->_pollingThread, ::Assets::OSFileSystemFlags::AllowAbsolute);
        _pimpl->_mountingTree = std::make_shared<::Assets::MountingTree>(s_defaultFilenameRules);

        if (!_pimpl->_compileAndAsyncManager)
            _pimpl->_compileAndAsyncManager = std::make_shared<::Assets::CompileAndAsyncManager>(cfg._inMemoryOnlyIntermediates ? nullptr : _pimpl->_defaultFilesystem);

        if (!_pimpl->_assetsSetsManager)
            _pimpl->_assetsSetsManager = std::make_shared<::Assets::AssetSetManager>();

        if (!_pimpl->_cachedBoxManager)
            _pimpl->_cachedBoxManager = std::make_shared<Internal::CachedBoxManager>();

            // add "nsight" marker to global services when "-nsight" is on
            // the command line. This is an easy way to record a global (&cross-dll)
            // state to use the nsight configuration when the given flag is set.
        const auto* cmdLine = OSServices::GetCommandLine();
        if (cmdLine && XlFindString(cmdLine, "-nsight"))
            CrossModule::GetInstance()._services.Add(Hash64("nsight"), []() { return true; });

        _pimpl->_pluginSet = std::make_unique<PluginSet>();
    }

    GlobalServices::~GlobalServices() 
    {
        assert(s_instance == nullptr);  // (should already have been detached in the Withhold() call)
        _pimpl->_shortTaskPool->StallAndDrainQueue();
        _pimpl->_longTaskPool->StallAndDrainQueue();
        _pimpl->_cachedBoxManager = nullptr;
        _pimpl->_assetsSetsManager->Clear();
        _pimpl->_pluginSet = nullptr;
        _pimpl->_shortTaskPool = nullptr;
        _pimpl->_longTaskPool = nullptr;
        _pimpl->_logCfg = nullptr;
        _pimpl->_assetsSetsManager = nullptr;
        _pimpl->_compileAndAsyncManager = nullptr;
        _pimpl->_mountingTree = nullptr;
        _pimpl->_defaultFilesystem = nullptr;
        _pimpl->_depValSys = nullptr;
        _pimpl->_continuationExecutor = nullptr;
        CrossModule::GetInstance().Shutdown();
    }

    static std::weak_ptr<PluginSet> s_pluginSetDoDeinit;
    static void DeinitPluginSet()
    {
        auto pluginSet = s_pluginSetDoDeinit.lock();
        if (pluginSet)
            pluginSet->DeinitializePlugins();
        s_pluginSetDoDeinit.reset();
    }

	void GlobalServices::LoadDefaultPlugins()
	{
		_pimpl->_pluginSet->LoadDefaultPlugins();
        assert(!s_pluginSetDoDeinit.lock());        // if we needed multiples of these, we might need to make the static a vector
        s_pluginSetDoDeinit = _pimpl->_pluginSet;
        std::atexit(DeinitPluginSet);
	}

	void GlobalServices::UnloadDefaultPlugins()
	{
        _pimpl->_pluginSet->DeinitializePlugins();
	}

    void GlobalServices::PrepareForDestruction()
    {
        if (_pimpl->_continuationExecutor)
            _pimpl->_continuationExecutor->stop();
        _pimpl->_shortTaskPool->StallAndDrainQueue();
        _pimpl->_longTaskPool->StallAndDrainQueue();
        _pimpl->_cachedBoxManager->Clear();
        _pimpl->_assetsSetsManager->Clear();
        UnloadDefaultPlugins();
    }

    void GlobalServices::AttachCurrentModule()
    {
        assert(s_instance == nullptr);
        s_instance = this;
        ::Assets::MainFileSystem::Init(_pimpl->_mountingTree, _pimpl->_defaultFilesystem);
        MainRig_Attach();
        // We can't do this in AttachCurrentModule, because it interacts with other globals (eg, ::Assets::GetDepValSys()), which
        // may creates requirements for what modules are attached in what order
		//if (!_pimpl->_logCfg)
		//	_pimpl->_logCfg = std::make_shared<LogCentralConfiguration>(_pimpl->_cfg._logConfigFile);
    }

    void GlobalServices::DetachCurrentModule()
    {
        MainRig_Detach();
        ::Assets::MainFileSystem::Shutdown();
        assert(s_instance == this);
        s_instance = nullptr;
    }

	ThreadPool& GlobalServices::GetShortTaskThreadPool() { return *_pimpl->_shortTaskPool; }
    ThreadPool& GlobalServices::GetLongTaskThreadPool() { return *_pimpl->_longTaskPool; }
    const std::shared_ptr<OSServices::PollingThread>& GlobalServices::GetPollingThread() { return _pimpl->_pollingThread; }
    PluginSet& GlobalServices::GetPluginSet() { return *_pimpl->_pluginSet; }

    const std::shared_ptr<thousandeyes::futures::Executor>& GlobalServices::GetContinuationExecutor()
    {
        return _pimpl->_continuationExecutor;
    }

    IStep::~IStep() {}
    IProgress::~IProgress() {}


	

	StartupConfig::StartupConfig()
    {
        _applicationName = "XLEApp";
        _logConfigFile = "log.dat";
        _setWorkingDir = false;
        _redirectCout = true;
        _inMemoryOnlyIntermediates = false;
        _longTaskThreadPoolCount = 4;
        _shortTaskThreadPoolCount = 2;
    }

    StartupConfig::StartupConfig(const char applicationName[]) : StartupConfig()
    {
        _applicationName = applicationName;
    }

	OSServices::LibVersionDesc GetLibVersionDesc()
	{
		return OSServices::LibVersionDesc { ConsoleRig_VersionString, ConsoleRig_BuildDateString };
	}

////////////////////////////////////////////////////////////////////////////////////////////////////

    static std::pair<std::shared_ptr<OSServices::LogConfigurationSet>, ::Assets::DependencyValidation> LoadConfigSet(StringSection<> fn)
    {
        size_t fileSize = 0;
        ::Assets::DependentFileState fileState;
        auto file = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(fn, &fileSize, &fileState);
        auto depVal = ::Assets::GetDepValSys().Make(fileState);
        if (!file.get() || !fileSize)
            return std::make_pair(nullptr, depVal);
        
        InputStreamFormatter<char> fmtr(MakeStringSection((const char*)file.get(), (const char*)PtrAdd(file.get(), fileSize)));
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
            if (!_cfgSet || !_cfgSetDepVal || _cfgSetDepVal.GetValidationIndex() > 0) {
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
