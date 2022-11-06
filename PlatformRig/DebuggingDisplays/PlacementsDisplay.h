// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace RenderCore { namespace Techniques { class DrawingApparatus; }}
namespace SceneEngine { class PlacementsEditor; }
namespace ToolsRig { class VisCameraSettings; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreatePlacementsDisplay(
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
		std::shared_ptr<SceneEngine::PlacementsEditor> placements,
		std::shared_ptr<ToolsRig::VisCameraSettings> camera);
}}

