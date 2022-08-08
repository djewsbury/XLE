// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace SceneEngine { class ModelCache; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateModelCacheDisplay(
		std::shared_ptr<SceneEngine::ModelCache> modelCache);

	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateModelCacheGeoBufferDisplay(
		std::shared_ptr<SceneEngine::ModelCache> modelCache);
}}
