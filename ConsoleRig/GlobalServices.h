// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <string>
#include <memory>
#include <assert.h>

namespace Utility { class ThreadPool; }
namespace OSServices { class PollingThread; class LibVersionDesc; }
namespace thousandeyes { namespace futures { class Executor; }}

namespace ConsoleRig
{
    class StartupConfig
    {
    public:
        std::string _applicationName;
        std::string _logConfigFile;
        bool _setWorkingDir;
        bool _redirectCout;
        bool _inMemoryOnlyIntermediates;
        unsigned _longTaskThreadPoolCount;
        unsigned _shortTaskThreadPoolCount;

        StartupConfig();
        StartupConfig(const char applicationName[]);
    };

    class PluginSet;

    class GlobalServices
    {
    public:
        Utility::ThreadPool& GetShortTaskThreadPool();
        Utility::ThreadPool& GetLongTaskThreadPool();
        const std::shared_ptr<OSServices::PollingThread>& GetPollingThread();
        PluginSet& GetPluginSet();
        const std::shared_ptr<thousandeyes::futures::Executor>& GetContinuationExecutor();

        static GlobalServices& GetInstance() { assert(s_instance); return *s_instance; }

		void LoadDefaultPlugins();
		void UnloadDefaultPlugins();

        void PrepareForDestruction();

        GlobalServices(const StartupConfig& cfg = StartupConfig());
        ~GlobalServices();

        GlobalServices(const GlobalServices&) = delete;
        GlobalServices& operator=(const GlobalServices&) = delete;

        void AttachCurrentModule();
        void DetachCurrentModule();
    protected:
        static GlobalServices* s_instance;

        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
    };

	OSServices::LibVersionDesc GetLibVersionDesc();
}
