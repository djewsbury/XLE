// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Plugins.h"
#include "AttachablePtr.h"
#include "AttachableLibrary.h"
#include "../OSServices/Log.h"
#include "../OSServices/RawFS.h"
#include "../Utility/Streams/PathUtils.h"
#include <vector>
#include <set>
#include <unordered_map>

namespace ConsoleRig
{
	class PluginSet::Pimpl
	{
	public:
		std::unordered_map<std::string, std::shared_ptr<AttachableLibrary>> _pluginLibraries;
		std::unordered_map<std::string, std::string> _failedPlugins;
		std::vector<std::shared_ptr<IStartupShutdownPlugin>> _plugins;
	};

	std::shared_ptr<AttachableLibrary> PluginSet::LoadLibrary(std::string name)
	{
		auto simplified = MakeSplitPath(name).Rebuild();

		auto i = _pimpl->_pluginLibraries.find(simplified);
		if (i != _pimpl->_pluginLibraries.end())
			return i->second;
		auto i2 = _pimpl->_failedPlugins.find(simplified);
		if (i2 != _pimpl->_failedPlugins.end())
			Throw(std::runtime_error(i2->second));

		auto library = std::make_shared<ConsoleRig::AttachableLibrary>(simplified);
		std::string errorMsg;
		if (library->TryAttach(errorMsg)) {
			_pimpl->_pluginLibraries.insert(std::make_pair(simplified, library));
			return library;
		} else {
			auto msg = "Plugin failed to attach with error msg (" + errorMsg + ")";
			Log(Error) << msg << std::endl;
			_pimpl->_failedPlugins.insert(std::make_pair(simplified, msg));
			Throw(std::runtime_error(msg));
		}
	}
	
	void PluginSet::LoadDefaultPlugins()
	{
		char processPath[MaxPath], cwd[MaxPath];
		OSServices::GetProcessPath((utf8*)processPath, dimof(processPath));
    	OSServices::GetCurrentDirectory(dimof(cwd), cwd);

		auto group0 = OSServices::FindFiles(MakeFileNameSplitter(processPath).DriveAndPath().AsString() + "/*Plugin.dll");
		auto group1 = OSServices::FindFiles(std::string(cwd) + "/*Plugin.dll");

		std::set<std::string> candidatePlugins;
		for (auto c:group0)
			candidatePlugins.insert(MakeSplitPath(c).Rebuild());
		for (auto c:group1)
			candidatePlugins.insert(MakeSplitPath(c).Rebuild());

		for (auto& c:candidatePlugins) {
			auto library = std::make_shared<ConsoleRig::AttachableLibrary>(c);
			std::string errorMsg;
			if (library->TryAttach(errorMsg)) {
				TRY {
					using PluginFn = std::shared_ptr<ConsoleRig::IStartupShutdownPlugin>();
					auto fn = library->GetFunction<PluginFn*>("GetStartupShutdownPlugin");
					if (fn) {
						auto plugin = (*fn)();
						plugin->Initialize();
						_pimpl->_plugins.emplace_back(std::move(plugin));
					}
					_pimpl->_pluginLibraries.insert(std::make_pair(c, std::move(library)));
				} CATCH(const std::exception& e) {
					Log(Error) << "Plugin (" << c << ") failed during the Initialize method with error msg (" << e.what() << ")" << std::endl;
				} CATCH_END
			} else {
				auto msg = "Plugin (" + c + ") failed to attach with error msg (" + errorMsg + ")";
				Log(Error) << msg << std::endl;
				_pimpl->_failedPlugins.insert(std::make_pair(c, msg));
			}
		}
	}

	void PluginSet::DeinitializePlugins()
	{
		// This is called either explicitly via the global services, or during an atexit() function
		// it should attempt to unload all plugins before we start running other atexit() functions
		// (as a way to try to make the destruction process feel more predictable, and avoid destroying
		// some global objects -- like GlobalServices -- from an attached dll)
		for (auto& p:_pimpl->_plugins)
			p->Deinitialize();
		_pimpl->_plugins.clear();
		for (auto&a:_pimpl->_pluginLibraries)
			a.second->Detach();
		_pimpl->_pluginLibraries.clear();
	}

	PluginSet::PluginSet()
	{
		_pimpl = std::make_unique<Pimpl>();
	}

	PluginSet::~PluginSet()
	{
		_pimpl->_plugins.clear();
		for (auto&a:_pimpl->_pluginLibraries)
			a.second->Detach();
	}
}
