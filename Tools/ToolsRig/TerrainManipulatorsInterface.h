// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>

namespace PlatformRig { class InputSnapshot; class IInputListener; }
namespace RenderOverlays { class IOverlayContext; namespace DebuggingDisplay { class InterfaceState; struct Layout; class Interactables; class DebugScreensSystem; }; class Font; }
namespace SceneEngine { class TerrainManager; class IIntersectionScene; }
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; class DrawingApparatus; class IPipelineAcceleratorPool; }}

namespace ToolsRig
{
    class IManipulator;
    class TerrainManipulatorContext;
	class VisCameraSettings;

    class ManipulatorsInterface : public std::enable_shared_from_this<ManipulatorsInterface>
    {
    public:
        void    Render( RenderCore::IThreadContext& context, 
                        RenderCore::Techniques::ParsingContext& parserContext,
                        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators);
        void    Update();

        void SelectManipulator(signed relativeIndex);
        IManipulator* GetActiveManipulator() const { return _manipulators[_activeManipulatorIndex].get(); }

        std::shared_ptr<PlatformRig::IInputListener>   CreateInputListener();

        ManipulatorsInterface(
            const std::shared_ptr<SceneEngine::TerrainManager>& terrainManager,
            const std::shared_ptr<TerrainManipulatorContext>& terrainManipulatorContext,
            const std::shared_ptr<VisCameraSettings>& camera,
			const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& drawingApparatus);
        ~ManipulatorsInterface();
    private:
        std::vector<std::unique_ptr<IManipulator>> _manipulators;
        unsigned _activeManipulatorIndex;

        std::shared_ptr<SceneEngine::TerrainManager>                _terrainManager;
        std::shared_ptr<SceneEngine::IIntersectionScene>            _intersectionTestScene;
		std::shared_ptr<VisCameraSettings>						    _camera;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus>	_drawingApparatus;

        class InputListener;
    };

    class ManipulatorsDisplay : public RenderOverlays::DebuggingDisplay::IWidget
    {
    public:
        void    Render( RenderOverlays::IOverlayContext& context, RenderOverlays::DebuggingDisplay::Layout& layout, 
                        RenderOverlays::DebuggingDisplay::Interactables&interactables, 
                        RenderOverlays::DebuggingDisplay::InterfaceState& interfaceState);
        ProcessInputResult    ProcessInput(RenderOverlays::DebuggingDisplay::InterfaceState& interfaceState, const PlatformRig::InputSnapshot& input);

        ManipulatorsDisplay(std::shared_ptr<ManipulatorsInterface> interf);
        ~ManipulatorsDisplay();

    private:
        std::shared_ptr<ManipulatorsInterface> _manipulatorsInterface;
    };
}


