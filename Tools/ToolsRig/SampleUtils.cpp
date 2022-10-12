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
#include "../../Utility/Streams/FormatterUtils.h"
#include <set>

namespace ToolsRig
{

	std::vector<std::string> PluginConfiguration::GetConfiguredPluginNames() const
	{
		std::vector<std::string> result;
		result.reserve(_configuredPlugins.size());
		for (const auto& p:_configuredPlugins) result.push_back(p.first);
		return result;
	}

	PluginConfiguration::PluginConfiguration(
		ConfiguredPlugins&& configuredPlugins, ::Assets::DependencyValidation depVal)
	: _configuredPlugins(std::move(configuredPlugins)), _depVal(std::move(depVal)) {}

	static void CleanupConfiguredPlugins(const PluginConfiguration::ConfiguredPlugins& plugins)
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
		CleanupConfiguredPlugins(_configuredPlugins);
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
		Formatters::IDynamicFormatter& formatter)
	{
		ConfiguredPlugins configuredPlugins;
		TRY {
			std::set<std::shared_ptr<ToolsRig::IConfigurablePlugin>> pluginsPendingApply;

			StringSection<> keyname;
			// apply the configuration to the preview scene registry immediately, as we're loading it
			auto& previewSceneRegistry = ToolsRig::Services::GetPreviewSceneRegistry();
			while (formatter.TryKeyedItem(keyname)) {
				auto cfg = previewSceneRegistry.GetConfigurablePlugin(keyname);
				if (!cfg) {
					SkipValueOrElement(formatter);
					continue;
				}
				auto pluginName = keyname.AsString();
				auto entity = cfg->AssignEntityId();
				if (cfg->CreateEntity(EntityInterface::MakeStringAndHash("game"), entity, {})) {
					RequireBeginElement(formatter);
					while (formatter.TryKeyedItem(keyname)) {
						EntityInterface::PropertyInitializer propInit;
						propInit._prop = EntityInterface::MakeStringAndHash(keyname);
						propInit._data = RequireRawValue(formatter, propInit._type);
						cfg->SetProperty(entity, MakeIteratorRange(&propInit, &propInit+1));
					}
					RequireEndElement(formatter);
				} else {
					SkipValueOrElement(formatter);
					entity = ~0ull;
				}
				configuredPlugins.emplace_back(std::move(pluginName), entity);
				pluginsPendingApply.insert(cfg);
			}

			if (pluginsPendingApply.empty()) {
				promise.set_value(std::make_shared<PluginConfiguration>(std::move(configuredPlugins), formatter.GetDependencyValidation()));
				return;
			}

			// let's parallelize the plugin apply; because these can actually be expensive operations
			struct Helper
			{
				std::vector<std::future<void>> _pendingApplies;
				unsigned _completedIdx = 0;
				ConfiguredPlugins _configuredPlugins;
				::Assets::DependencyValidation _depVal;
			};
			auto helper = std::make_shared<Helper>();
			helper->_configuredPlugins = std::move(configuredPlugins);
			helper->_depVal = formatter.GetDependencyValidation();
			for (auto& p:pluginsPendingApply) {
				std::promise<void> promise;
				helper->_pendingApplies.emplace_back(promise.get_future());
				ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
					[opContext, p, promise=std::move(promise)]() mutable {
						TRY {
							p->ApplyConfiguration(std::move(opContext));
							promise.set_value();
						} CATCH (...) {
							promise.set_exception(std::current_exception());
						} CATCH_END
					});
			}

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
				[helper, configuredPlugins=std::move(configuredPlugins)]() {
					TRY {
						// propagate exceptions
						for (auto& f:helper->_pendingApplies) f.get();

						return std::make_shared<PluginConfiguration>(std::move(helper->_configuredPlugins), std::move(helper->_depVal));
					} CATCH (...) {
						CleanupConfiguredPlugins(helper->_configuredPlugins);
						throw;
					} CATCH_END				
				});
		} CATCH (...) {
			CleanupConfiguredPlugins(configuredPlugins);
			promise.set_exception(std::current_exception());
		} CATCH_END
	}
}
