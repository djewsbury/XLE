// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <memory>
#include <vector>
#include <string>

namespace Formatter { class IDynamicFormatter; }
namespace Assets { class OperationContext; }
namespace EntityInterface { using EntityId = uint64_t; }
namespace Formatters { class IDynamicFormatter; }
namespace std { template<typename T> class future; }

namespace ToolsRig
{
	class PluginConfiguration
	{
	public:
		std::vector<std::string> GetConfiguredPluginNames() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		
		using ConfiguredPlugins = std::vector<std::pair<std::string, EntityInterface::EntityId>>;
		PluginConfiguration(ConfiguredPlugins&& configuredPlugins, ::Assets::DependencyValidation depVal);
		~PluginConfiguration();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
			std::shared_ptr<::Assets::OperationContext> opContext,
			StringSection<> cfgLocation);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
			std::shared_ptr<::Assets::OperationContext> opContext,
			Formatters::IDynamicFormatter& formatter);

		PluginConfiguration(PluginConfiguration&&) = delete;
		PluginConfiguration& operator=(PluginConfiguration&&) = delete;

	private:
		::Assets::DependencyValidation _depVal;
		ConfiguredPlugins _configuredPlugins;
	};
}
