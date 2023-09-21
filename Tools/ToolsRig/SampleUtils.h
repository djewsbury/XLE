// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PreviewSceneRegistry.h"
#include "../../Assets/AssetsCore.h"
#include <memory>
#include <vector>
#include <string>

namespace Formatter { class IDynamicInputFormatter; }
namespace Assets { class OperationContext; class IAsyncMarker; }
namespace EntityInterface { using EntityId = uint64_t; }
namespace Formatters { class IDynamicInputFormatter; }
namespace std { template<typename T> class future; }

namespace ToolsRig
{
	class PluginConfiguration
	{
	public:
		std::vector<std::string> GetConfigurationNames() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		std::string CreateDigest() const;
		
		using Configurations = std::vector<std::pair<std::string, EntityInterface::EntityId>>;		// name of the actual game configuration, rather than the plugin name
		PluginConfiguration(
			Configurations&& configurations,
			std::vector<IPreviewSceneRegistry::ApplyConfigurablePluginLog>&& applyLogs,
			::Assets::DependencyValidation depVal);
		~PluginConfiguration();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
			std::shared_ptr<::Assets::OperationContext> opContext,
			StringSection<> cfgLocation);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
			std::shared_ptr<::Assets::OperationContext> opContext,
			Formatters::IDynamicInputFormatter& formatter);

		PluginConfiguration(PluginConfiguration&&) = delete;
		PluginConfiguration& operator=(PluginConfiguration&&) = delete;

	private:
		::Assets::DependencyValidation _depVal;
		Configurations _configurations;
		std::vector<IPreviewSceneRegistry::ApplyConfigurablePluginLog> _applyLogs;
	};

	// Utility for exporting across to the CLR side (where futures don't work)
	std::shared_ptr<::Assets::IAsyncMarker> BeginPluginConfiguration(
		std::shared_ptr<::Assets::OperationContext>,
		std::string plugin,
		const std::vector<std::pair<std::string, std::string>>& settings);
}
