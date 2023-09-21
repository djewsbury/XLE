// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleUtils.h"
#include "ToolsRigServices.h"
#include "PreviewSceneRegistry.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../EntityInterface/FormatterAdapters.h"
#include <set>

namespace ToolsRig
{

	std::vector<std::string> PluginConfiguration::GetConfigurationNames() const
	{
		std::vector<std::string> result;
		result.reserve(_configurations.size());
		for (const auto& p:_configurations) result.push_back(p.first);
		return result;
	}

	std::string PluginConfiguration::CreateDigest() const
	{
		std::stringstream log;
		for (auto p:_configurations)
			log << "Configuration (" << p.first << ") was configured." << std::endl;
		log << std::endl;
		for (auto i:_applyLogs) {
			if (i._initializationLog.empty()) {
				log << "Plugin (" << i._pluginName << ") applied with no messages." << std::endl;
			} else {
				log << "Plugin (" << i._pluginName << ") applied with the following messages." << std::endl;
				log << i._initializationLog << std::endl;
			}
		}
		return log.str();
	}

	PluginConfiguration::PluginConfiguration(
		Configurations&& configurations,
		std::vector<IPreviewSceneRegistry::ApplyConfigurablePluginLog>&& applyLogs,
		::Assets::DependencyValidation depVal)
	: _configurations(std::move(configurations)), _applyLogs(std::move(applyLogs)), _depVal(std::move(depVal)) {}

	static void CleanupConfiguredPlugins(const PluginConfiguration::Configurations& plugins)
	{
		if (!plugins.empty()) {
			auto& previewSceneRegistry = ToolsRig::Services::GetPreviewSceneRegistry();
			for (auto& cfg:plugins) {
				auto plugin = previewSceneRegistry.GetConfigurablePlugin(cfg.first);
				if (plugin) plugin->DeleteEntity(cfg.second);
			}
		}
	}

	PluginConfiguration::~PluginConfiguration()
	{
		CleanupConfiguredPlugins(_configurations);
	}

	void PluginConfiguration::ConstructToPromise(
		std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> cfgLocation)
	{
		auto formatterFuture = ToolsRig::Services::GetEntityMountingTree().BeginFormatter("cfg/sample/Plugins");
		::Assets::WhenAll(std::move(formatterFuture)).ThenConstructToPromise(
			std::move(promise),
			[opContext=std::move(opContext)](auto&& promise, const auto& formatter) mutable {
				PluginConfiguration::ConstructToPromise(std::move(promise), std::move(opContext), *formatter);
			});
	}

	void PluginConfiguration::ConstructToPromise(
		std::promise<std::shared_ptr<PluginConfiguration>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		Formatters::IDynamicInputFormatter& formatter)
	{
		Configurations configurations;
		TRY {
			StringSection<> keyname;
			// apply the configuration to the preview scene registry immediately, as we're loading it
			auto& previewSceneRegistry = ToolsRig::Services::GetPreviewSceneRegistry();
			auto configurablePluginDoc = previewSceneRegistry.GetConfigurablePluginDocument();
			while (formatter.TryKeyedItem(keyname)) {
				auto rootEntityName = keyname.AsString();
				auto entity = configurablePluginDoc->AssignEntityId();
				if (configurablePluginDoc->CreateEntity(EntityInterface::MakeStringAndHash(keyname), entity, {})) {
					RequireBeginElement(formatter);
					while (formatter.TryKeyedItem(keyname)) {
						EntityInterface::PropertyInitializer propInit;
						propInit._prop = EntityInterface::MakeStringAndHash(keyname);
						propInit._data = RequireRawValue(formatter, propInit._type);
						configurablePluginDoc->SetProperty(entity, MakeIteratorRange(&propInit, &propInit+1));
					}
					RequireEndElement(formatter);
					configurations.emplace_back(rootEntityName, entity);
				} else {
					promise.set_exception(std::make_exception_ptr(std::runtime_error("No plugin could handle configuration for (" + keyname.AsString() + "). This could mean that the associated plugin dll failed to load.")));
					return;
				}
			}

			auto pluginsPendingApply = previewSceneRegistry.ApplyConfigurablePlugins(opContext);
			if (pluginsPendingApply.empty()) {
				promise.set_value(std::make_shared<PluginConfiguration>(std::move(configurations), std::vector<IPreviewSceneRegistry::ApplyConfigurablePluginLog>{}, formatter.GetDependencyValidation()));
				return;
			}

			// let's parallelize the plugin apply; because these can actually be expensive operations
			struct Helper
			{
				std::vector<std::future<IPreviewSceneRegistry::ApplyConfigurablePluginLog>> _pendingApplies;
				unsigned _completedIdx = 0;
				Configurations _configurations;
				::Assets::DependencyValidation _depVal;
			};
			auto helper = std::make_shared<Helper>();
			helper->_configurations = std::move(configurations);
			helper->_depVal = formatter.GetDependencyValidation();
			helper->_pendingApplies = std::move(pluginsPendingApply);

			::Assets::PollToPromise(
				std::move(promise),
				[helper](auto timeout) {
					auto timeoutTime = std::chrono::steady_clock::now() + timeout;
					for (unsigned c=helper->_completedIdx; c<helper->_pendingApplies.size(); ++c) {
						if (helper->_pendingApplies[c].wait_until(timeoutTime) == std::future_status::timeout)
							return ::Assets::PollStatus::Continue;
						++helper->_completedIdx;
					}
					return ::Assets::PollStatus::Finish;
				},
				[helper]() {
					TRY {
						// propagate exceptions
						std::vector<IPreviewSceneRegistry::ApplyConfigurablePluginLog> logs;
						logs.reserve(helper->_pendingApplies.size());
						for (auto& f:helper->_pendingApplies)
							logs.push_back(f.get());

						return std::make_shared<PluginConfiguration>(std::move(helper->_configurations), std::move(logs), std::move(helper->_depVal));
					} CATCH (...) {
						CleanupConfiguredPlugins(helper->_configurations);
						throw;
					} CATCH_END				
				});
		} CATCH (...) {
			CleanupConfiguredPlugins(configurations);
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	class ConfigurationHelper : public ::Assets::IAsyncMarker
	{
	public:
		mutable std::future<std::shared_ptr<PluginConfiguration>> _futurePluginConfiguration;

		::Assets::Blob GetActualizationLog() const override
		{
			TRY {
				auto cfg = _futurePluginConfiguration.get();
				if (cfg)
					return ::Assets::AsBlob(cfg->CreateDigest());
				return nullptr;
			} CATCH(const std::exception& e) {
				return ::Assets::AsBlob(e.what());
			} CATCH_END
		}

		::Assets::AssetState GetAssetState() const override
		{
			return (_futurePluginConfiguration.wait_for(std::chrono::seconds(0)) == std::future_status::ready) 
				? ::Assets::AssetState::Ready		// don't actually know if it's valid or invalid at this stage
				: ::Assets::AssetState::Pending;
		}

		std::optional<::Assets::AssetState>   StallWhilePending(std::chrono::microseconds timeout) const override
		{
			if (_futurePluginConfiguration.wait_for(timeout) == std::future_status::ready)
				return ::Assets::AssetState::Ready;
			return {};
		}

		ConfigurationHelper(
			std::future<std::shared_ptr<PluginConfiguration>> futurePluginConfiguration)
		: _futurePluginConfiguration(std::move(futurePluginConfiguration)) {}
	};

	std::shared_ptr<::Assets::IAsyncMarker> BeginPluginConfiguration(
		std::shared_ptr<::Assets::OperationContext> opContext,
		std::string plugin,
		const std::vector<std::pair<std::string, std::string>>& settings)
	{
		MemoryOutputStream<> strm;
		{
			Formatters::TextOutputFormatter fmttr(strm);
			auto ele = fmttr.BeginKeyedElement(plugin);
			for (const auto&s:settings)
				fmttr.WriteKeyedValue(s.first, s.second);
			fmttr.EndElement(ele);
		}
		
		std::promise<std::shared_ptr<PluginConfiguration>> promise;
		auto future = promise.get_future();
		auto dynFmttr = EntityInterface::CreateDynamicFormatter(std::move(strm), {});
		PluginConfiguration::ConstructToPromise(std::move(promise), opContext, *dynFmttr);

		return std::make_shared<ConfigurationHelper>(std::move(future));
	}
}
