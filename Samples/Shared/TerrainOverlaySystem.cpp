// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainOverlaySystem.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../Tools/ToolsRig/TerrainManipulatorsInterface.h"
#include "../../Tools/ToolsRig/TerrainManipulators.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include <memory>

namespace Sample
{
    using RenderOverlays::DebuggingDisplay::DebugScreensSystem;

    class TerrainManipulators
        : public PlatformRig::IOverlaySystem
        , public PlatformRig::IInputListener
        , public std::enable_shared_from_this<TerrainManipulators>
    {
    public:
        TerrainManipulators(
            std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
            std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionContext)
        {
            _terrainManipulatorContext = std::make_shared<::ToolsRig::TerrainManipulatorContext>();
            _manipulatorsInterface = std::make_shared<::ToolsRig::ManipulatorsInterface>(terrainManager, _terrainManipulatorContext, intersectionContext);
            _terrainManipulators = std::make_shared<::ToolsRig::ManipulatorsDisplay>(_manipulatorsInterface);
            _manipInputListener = _manipulatorsInterface->CreateInputListener();

            _screens = std::make_shared<DebugScreensSystem>();
            _screens->Register(_terrainManipulators, "Terrain", DebugScreensSystem::SystemDisplay);
        }

        std::shared_ptr<PlatformRig::IInputListener> GetInputListener()
        {
            return shared_from_this();
        }

        bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const PlatformRig::InputSnapshot& evnt)
        {
            return  _screens->OnInputEvent(context, evnt)
                ||  _manipInputListener->OnInputEvent(context, evnt);
        }

        void RenderToScene(
            RenderCore::IThreadContext& devContext, 
            RenderCore::Techniques::ParsingContext& parserContext)
        {
            _manipulatorsInterface->Render(devContext, parserContext);
        }

        void RenderWidgets(
            RenderCore::IThreadContext& device, 
            RenderCore::Techniques::ParsingContext& parserContext)
        {
			auto overlayContext = RenderOverlays::MakeImmediateOverlayContext(device, &parserContext.GetNamedResources(), parserContext.GetProjectionDesc());
			auto viewportDims = device.GetStateDesc()._viewportDimensions;
            _screens->Render(*overlayContext, RenderOverlays::Rect{ { 0,0 },{ int(viewportDims[0]), int(viewportDims[1]) } });
        }

        void SetActivationState(bool) {}

    private:
        std::shared_ptr<::ToolsRig::ManipulatorsInterface> _manipulatorsInterface;
        std::shared_ptr<::ToolsRig::ManipulatorsDisplay> _terrainManipulators;
        std::shared_ptr<DebugScreensSystem> _screens;
        std::shared_ptr<PlatformRig::IInputListener> _manipInputListener;
        std::shared_ptr<::ToolsRig::TerrainManipulatorContext> _terrainManipulatorContext;
    };

    std::shared_ptr<PlatformRig::IOverlaySystem> CreateTerrainEditorOverlaySystem(
        std::shared_ptr<SceneEngine::TerrainManager> terrainManager,
        std::shared_ptr<SceneEngine::IntersectionTestContext> intersectionContext)
    {
        return std::make_shared<TerrainManipulators>(
            std::move(terrainManager), 
            std::move(intersectionContext));
    }
}

