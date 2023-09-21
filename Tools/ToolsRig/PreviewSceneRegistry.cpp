// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PreviewSceneRegistry.h"
#include "ConsoleRig/AttachablePtr.h"
#include "ConsoleRig/GlobalServices.h"
#include "Utility/Threading/Mutex.h"
#include "Utility/Threading/CompletionThreadPool.h"
#include <future>

namespace ToolsRig
{

	class MainPreviewSceneRegistry : public IPreviewSceneRegistry, public EntityInterface::IMutableEntityDocument, public std::enable_shared_from_this<MainPreviewSceneRegistry>
	{
	public:
		std::vector<std::string> EnumerateScenes() override
		{
			ScopedLock(_lock);
			std::vector<std::string> result;
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i) {
				auto setScenes = i->second->EnumerateScenes();
				result.insert(result.end(), setScenes.begin(), setScenes.end());
			}
			return result;
		}

		::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(StringSection<> sceneName, const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>& drawablesPool, const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators, const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators, const std::shared_ptr<::Assets::OperationContext>& loadingContext) override
		{
			ScopedLock(_lock);
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i) {
				auto s = i->second->CreateScene(sceneName, drawablesPool, pipelineAccelerators, deformAccelerators, loadingContext);
				if (s) return s;
			}
			return nullptr;
		}

		RegistrySetId Register(std::shared_ptr<IPreviewSceneRegistrySet> registrySet) override
		{
			ScopedLock(_lock);
			auto result = _nextRegistrySetId++;
			_registrySet.push_back(std::make_pair(result, std::move(registrySet)));
			return result;
		}

		void Deregister(RegistrySetId setId) override
		{
			ScopedLock(_lock);
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i)
				if (i->first == setId) {
					_registrySet.erase(i);
					break;
				}
		}

		std::shared_ptr<IConfigurablePlugin> GetConfigurablePlugin(
			StringSection<> name) override
		{
			ScopedLock(_lock);
			for (auto i=_configurablePlugins.begin(); i!=_configurablePlugins.end(); ++i)
				if (XlEqString(name, std::get<1>(*i)))
					return std::get<2>(*i);
			return nullptr;
		}

		std::shared_ptr<EntityInterface::IMutableEntityDocument> GetConfigurablePluginDocument() override
		{
			return shared_from_this();
		}

		std::vector<std::future<ApplyConfigurablePluginLog>> ApplyConfigurablePlugins(std::shared_ptr<::Assets::OperationContext> opContext) override
		{
			ScopedLock(_lock);

			std::vector<ConfigurablePluginId> pluginsPendingApply;
			for (auto& q:_configurablePluginEntities)
				pluginsPendingApply.push_back(q.second);
			std::sort(pluginsPendingApply.begin(), pluginsPendingApply.end());
			pluginsPendingApply.erase(std::unique(pluginsPendingApply.begin(), pluginsPendingApply.end()), pluginsPendingApply.end());
			_configurablePluginEntities.clear();

			// let's parallelize the plugin apply; because these can actually be expensive operations
			std::vector<std::future<ApplyConfigurablePluginLog>> result;
			for (auto c:pluginsPendingApply) {
				auto i2 = std::find_if(_configurablePlugins.begin(), _configurablePlugins.end(), [c](const auto& q) { return std::get<0>(q) == c; });
				if (i2 == _configurablePlugins.end())
					continue;

				std::promise<ApplyConfigurablePluginLog> promise;
				result.emplace_back(promise.get_future());
				ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
					[opContext, p=std::get<2>(*i2), promise=std::move(promise), pluginName = std::get<1>(*i2)]() mutable {
						TRY {
							ApplyConfigurablePluginLog log;
							log._initializationLog = p->ApplyConfiguration(std::move(opContext));
							log._pluginName = pluginName;
							promise.set_value(std::move(log));
						} CATCH (...) {
							promise.set_exception(std::current_exception());
						} CATCH_END
					});
			}

			return result;
		}

		ConfigurablePluginId Register(StringSection<> name, std::shared_ptr<IConfigurablePlugin> plugin) override
		{
			ScopedLock(_lock);
			auto result = _nextConfigurablePluginId++;
			_configurablePlugins.emplace_back(result, name.AsString(), std::move(plugin));
			return result;
		}

		void DeregisterConfigurablePlugin(ConfigurablePluginId id) override
		{
			ScopedLock(_lock);
			for (auto i=_configurablePlugins.begin(); i!=_configurablePlugins.end(); ++i)
				if (std::get<0>(*i) == id) {
					_configurablePlugins.erase(i);
					break;
				}
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////

		EntityInterface::EntityId AssignEntityId() override
		{
			ScopedLock(_lock);
			return _nextConfigurablePluginEntityId++;
		}

		bool CreateEntity(EntityInterface::StringAndHash objType, EntityInterface::EntityId id, IteratorRange<const EntityInterface::PropertyInitializer*> initializers) override
		{
			ScopedLock(_lock);
			auto i = std::find_if(_configurablePluginEntities.begin(), _configurablePluginEntities.end(), [id](const auto& q) { return q.first == id; });
			assert(i == _configurablePluginEntities.end());
			if (i != _configurablePluginEntities.end()) return false;

			for (auto& plugin:_configurablePlugins)
				if (std::get<2>(plugin)->CreateEntity(objType, id, initializers)) {
					_configurablePluginEntities.emplace_back(id, std::get<0>(plugin));
					return true;
				}

			return false;
		}

		bool DeleteEntity(EntityInterface::EntityId id) override
		{
			ScopedLock(_lock);
			auto i = std::find_if(_configurablePluginEntities.begin(), _configurablePluginEntities.end(), [id](const auto& q) { return q.first == id; });
			if (i == _configurablePluginEntities.end())
				return false;

			auto i2 = std::find_if(_configurablePlugins.begin(), _configurablePlugins.end(), [c=i->second](const auto& q) { return std::get<0>(q) == c; });
			if (i2 == _configurablePlugins.end())
				return false;

			_configurablePluginEntities.erase(i);
			return std::get<2>(*i2)->DeleteEntity(id);
		}

		bool SetProperty(EntityInterface::EntityId id, IteratorRange<const EntityInterface::PropertyInitializer*> initializers) override
		{
			ScopedLock(_lock);
			auto i = std::find_if(_configurablePluginEntities.begin(), _configurablePluginEntities.end(), [id](const auto& q) { return q.first == id; });
			if (i == _configurablePluginEntities.end())
				return false;

			auto i2 = std::find_if(_configurablePlugins.begin(), _configurablePlugins.end(), [c=i->second](const auto& q) { return std::get<0>(q) == c; });
			if (i2 == _configurablePlugins.end())
				return false;

			return std::get<2>(*i2)->SetProperty(id, initializers);
		}

		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityInterface::EntityId id, EntityInterface::StringAndHash prop, IteratorRange<void*> destinationBuffer) const override
		{
			ScopedLock(_lock);
			auto i = std::find_if(_configurablePluginEntities.begin(), _configurablePluginEntities.end(), [id](const auto& q) { return q.first == id; });
			if (i == _configurablePluginEntities.end())
				return {};

			auto i2 = std::find_if(_configurablePlugins.begin(), _configurablePlugins.end(), [c=i->second](const auto& q) { return std::get<0>(q) == c; });
			if (i2 == _configurablePlugins.end())
				return {};

			return std::get<2>(*i2)->GetProperty(id, prop, destinationBuffer);
		}

		bool SetParent(EntityInterface::EntityId child, EntityInterface::EntityId parent, EntityInterface::StringAndHash childList, int insertionPosition) override
		{
			ScopedLock(_lock);
			auto i = std::find_if(_configurablePluginEntities.begin(), _configurablePluginEntities.end(), [child](const auto& q) { return q.first == child; });
			if (i == _configurablePluginEntities.end())
				return {};

			auto i2 = std::find_if(_configurablePlugins.begin(), _configurablePlugins.end(), [c=i->second](const auto& q) { return std::get<0>(q) == c; });
			if (i2 == _configurablePlugins.end())
				return {};

			return std::get<2>(*i2)->SetParent(child, parent, childList, insertionPosition);
		}

		////////////////////////////////////////////////////////////////////////////////////////////////////

		MainPreviewSceneRegistry() {}
		~MainPreviewSceneRegistry() {}

		mutable Threading::Mutex _lock;
		std::vector<std::pair<RegistrySetId, std::shared_ptr<IPreviewSceneRegistrySet>>> _registrySet;
		std::vector<std::tuple<ConfigurablePluginId, std::string, std::shared_ptr<IConfigurablePlugin>>> _configurablePlugins;
		RegistrySetId _nextRegistrySetId = 1;
		RegistrySetId _nextConfigurablePluginId = 1;

		std::vector<std::pair<EntityInterface::EntityId, ConfigurablePluginId>> _configurablePluginEntities;
		EntityInterface::EntityId _nextConfigurablePluginEntityId = 1;
	};

	std::shared_ptr<IPreviewSceneRegistry> CreatePreviewSceneRegistry()
	{
		return std::make_shared<MainPreviewSceneRegistry>();
	}

	IPreviewSceneRegistry::~IPreviewSceneRegistry() {}

}

