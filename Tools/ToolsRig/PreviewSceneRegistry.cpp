// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PreviewSceneRegistry.h"
#include "ConsoleRig/AttachablePtr.h"

namespace ToolsRig
{

	class MainPreviewSceneRegistry : public IPreviewSceneRegistry
	{
	public:
		std::vector<std::string> EnumerateScenes()
		{
			std::vector<std::string> result;
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i) {
				auto setScenes = i->second->EnumerateScenes();
				result.insert(result.end(), setScenes.begin(), setScenes.end());
			}
			return result;
		}

		::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(StringSection<> sceneName, const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>& drawablesPool, const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAccelerators, const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>& deformAccelerators)
		{
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i) {
				auto s = i->second->CreateScene(sceneName, drawablesPool, pipelineAccelerators, deformAccelerators);
				if (s) return s;
			}
			return nullptr;
		}

		RegistrySetId Register(std::shared_ptr<IPreviewSceneRegistrySet> registrySet)
		{
			auto result = _nextRegistrySetId+1;
			_registrySet.push_back(std::make_pair(result, std::move(registrySet)));
			return result;
		}

		void Deregister(RegistrySetId setId)
		{
			for (auto i=_registrySet.begin(); i!=_registrySet.end(); ++i)
				if (i->first == setId) {
					_registrySet.erase(i);
					break;
				}
		}

		virtual std::shared_ptr<IConfigurablePlugin> GetConfigurablePlugin(
			StringSection<> name)
		{
			for (auto i=_configurablePlugins.begin(); i!=_configurablePlugins.end(); ++i)
				if (XlEqString(name, std::get<1>(*i)))
					return std::get<2>(*i);
			return nullptr;
		}

		ConfigurablePluginId Register(StringSection<> name, std::shared_ptr<IConfigurablePlugin> plugin)
		{
			auto result = _nextConfigurablePluginId+1;
			_configurablePlugins.emplace_back(result, name.AsString(), std::move(plugin));
			return result;
		}

		void DeregisterConfigurablePlugin(ConfigurablePluginId id)
		{
			for (auto i=_configurablePlugins.begin(); i!=_configurablePlugins.end(); ++i)
				if (std::get<0>(*i) == id) {
					_configurablePlugins.erase(i);
					break;
				}
		}

		MainPreviewSceneRegistry() {}
		~MainPreviewSceneRegistry() {}

		std::vector<std::pair<RegistrySetId, std::shared_ptr<IPreviewSceneRegistrySet>>> _registrySet;
		std::vector<std::tuple<ConfigurablePluginId, std::string, std::shared_ptr<IConfigurablePlugin>>> _configurablePlugins;
		RegistrySetId _nextRegistrySetId = 1;
		RegistrySetId _nextConfigurablePluginId = 1;
	};

	std::shared_ptr<IPreviewSceneRegistry> CreatePreviewSceneRegistry()
	{
		return std::make_shared<MainPreviewSceneRegistry>();
	}

	IPreviewSceneRegistry::~IPreviewSceneRegistry() {}

}

