// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

/*
These are needed for linking against XLEBridgeUtils & RenderingInterop (though I think we can trim down the list a lot)
However, when we're not using XLEBridgeUtils or RenderingInterop, we can actually omit them all

namespace GUILayer { class NativeEngineDevice; class RenderTargetWrapper; }
namespace ToolsRig { class IManipulator; class VisCameraSettings; }
namespace SceneEngine { class LightingParserContext; class IIntersectionScene; class PlacementsEditor; }
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ProjectionDesc; class CameraDesc; class ParsingContext; class ITechniqueDelegate; } }
namespace OSServices { class InputSnapshot; }
namespace Assets { class DirectorySearchRules; }
namespace ConsoleRig { class IProgress; class GlobalServices; }

#pragma make_public(GUILayer::RenderTargetWrapper)
#pragma make_public(GUILayer::NativeEngineDevice)
#pragma make_public(ToolsRig::IManipulator)
#pragma make_public(ToolsRig::VisCameraSettings)
#pragma make_public(SceneEngine::LightingParserContext)
#pragma make_public(SceneEngine::IIntersectionScene)
#pragma make_public(SceneEngine::PlacementsEditor)
#pragma make_public(RenderCore::Techniques::ProjectionDesc)
#pragma make_public(RenderCore::Techniques::CameraDesc)
#pragma make_public(RenderCore::Techniques::ParsingContext)
#pragma make_public(RenderCore::Techniques::ITechniqueDelegate)
#pragma make_public(RenderCore::IThreadContext)
#pragma make_public(OSServices::InputSnapshot)
#pragma make_public(Assets::DirectorySearchRules)
#pragma make_public(ConsoleRig::IProgress)
#pragma make_public(ConsoleRig::GlobalServices)
*/
