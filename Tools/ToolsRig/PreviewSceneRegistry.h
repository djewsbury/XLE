// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../EntityInterface/EntityInterface.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/StringUtils.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace SceneEngine { class IScene; }
namespace RenderCore { namespace Techniques { class IDrawablesPool; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; }}
namespace Assets { class OperationContext; }

namespace ToolsRig
{
	class IPreviewSceneRegistrySet
	{
	public:
		virtual std::vector<std::string> EnumerateScenes() = 0;
		virtual ::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(
			StringSection<>, 
			const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>&,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>&,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>&) = 0;
		virtual ~IPreviewSceneRegistrySet() = default;
	};

	class IConfigurablePlugin : public EntityInterface::IMutableEntityDocument
	{
	public:
		virtual void ApplyConfiguration(std::shared_ptr<::Assets::OperationContext> =nullptr) = 0;
		virtual ~IConfigurablePlugin() = default;
	};

	class IPreviewSceneRegistry
	{
	public:
		virtual std::vector<std::string> EnumerateScenes() = 0;
		virtual ::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(
			StringSection<>, 
			const std::shared_ptr<RenderCore::Techniques::IDrawablesPool>&,
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>&,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>&) = 0;

		virtual std::shared_ptr<IConfigurablePlugin> GetConfigurablePlugin(
			StringSection<>) = 0;

		using RegistrySetId = uint64_t;
		virtual RegistrySetId Register(std::shared_ptr<IPreviewSceneRegistrySet>) = 0;
		virtual void Deregister(RegistrySetId) = 0;

		using ConfigurablePluginId = uint64_t;
		virtual ConfigurablePluginId Register(StringSection<>, std::shared_ptr<IConfigurablePlugin>) = 0;
		virtual void DeregisterConfigurablePlugin(ConfigurablePluginId) = 0;

		virtual ~IPreviewSceneRegistry();
	};

	std::shared_ptr<IPreviewSceneRegistry> CreatePreviewSceneRegistry();
}

