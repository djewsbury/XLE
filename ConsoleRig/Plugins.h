// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <string>
#include <memory>
#include <iosfwd>

namespace OSServices { class AttachableLibrary; }

namespace ConsoleRig
{
    class IStartupShutdownPlugin
	{
	public:
		virtual void Initialize() = 0;
		virtual void Deinitialize() = 0;

		virtual ~IStartupShutdownPlugin();
	};

	class PluginSet
	{
	public:
		std::shared_ptr<OSServices::AttachableLibrary> LoadLibrary(std::string name);
		void LoadDefaultPlugins();
		void DeinitializePlugins();
		void* FindPluginFunction(const char*);
		void LogStatus(std::ostream&);

		PluginSet();
		~PluginSet();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};
}

