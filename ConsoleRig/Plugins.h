// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <string>
#include <memory>

namespace ConsoleRig
{
    class IStartupShutdownPlugin
	{
	public:
		virtual void Initialize() = 0;
		virtual void Deinitialize() = 0;

		virtual ~IStartupShutdownPlugin();
	};

	class AttachableLibrary;

	class PluginSet
	{
	public:
		std::shared_ptr<AttachableLibrary> LoadLibrary(std::string name);
		void LoadDefaultPlugins();

		PluginSet();
		~PluginSet();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};
}

