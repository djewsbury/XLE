// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/Marker.h"
#include "../Utility/StringUtils.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace SceneEngine { class IScene; }
namespace RenderCore { namespace Techniques { class IPipelineAcceleratorPool; class IDeformAcceleratorPool; }}

namespace ToolsRig
{
	class IPreviewSceneRegistrySet
	{
	public:
		virtual std::vector<std::string> EnumerateScenes() = 0;
		virtual ::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(
			StringSection<>, 
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>&,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>&) = 0;
		virtual ~IPreviewSceneRegistrySet();
	};

	class IPreviewSceneRegistry
	{
	public:
		virtual std::vector<std::string> EnumerateScenes() = 0;
		virtual ::Assets::PtrToMarkerPtr<SceneEngine::IScene> CreateScene(
			StringSection<>, 
			const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>&,
			const std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool>&) = 0;

		using RegistrySetId = uint64_t;
		virtual RegistrySetId Register(const std::shared_ptr<IPreviewSceneRegistrySet>&) = 0;
		virtual void Deregister(RegistrySetId) = 0;
		virtual ~IPreviewSceneRegistry();
	};

	std::shared_ptr<IPreviewSceneRegistry> CreatePreviewSceneRegistry();
}

